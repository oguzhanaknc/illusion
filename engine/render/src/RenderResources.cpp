#include "RenderResourcesImpl.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/render/RenderDevice.hpp>

#include <nvrhi/utils.h>

#include <array>
#include <format>

namespace illusion
{
namespace
{

nvrhi::Format formatOf(ImageFormat format) noexcept
{
    switch (format)
    {
    case ImageFormat::RGBA8:
        return nvrhi::Format::RGBA8_UNORM;
    case ImageFormat::RGBA8Srgb:
        return nvrhi::Format::SRGBA8_UNORM;
    case ImageFormat::RGBA32F:
        return nvrhi::Format::RGBA32_FLOAT;
    }
    return nvrhi::Format::RGBA8_UNORM;
}

nvrhi::SamplerAddressMode addressOf(TextureWrap wrap) noexcept
{
    switch (wrap)
    {
    case TextureWrap::ClampToEdge:
        return nvrhi::SamplerAddressMode::Clamp;
    case TextureWrap::MirrorRepeat:
        return nvrhi::SamplerAddressMode::Mirror;
    case TextureWrap::Repeat:
        break;
    }
    return nvrhi::SamplerAddressMode::Wrap;
}

} // namespace

struct RenderResources::Impl
{
    nvrhi::IDevice* device = nullptr;
    nvrhi::CommandListHandle uploads;
    nvrhi::BindingLayoutHandle materialLayout;
    std::array<nvrhi::SamplerHandle, 9> samplers{}; // [wrapU * 3 + wrapV]

    // Slot 0 of each vector is unused so that handle index 0 means "none".
    std::vector<GpuMesh> meshes{1};
    std::vector<nvrhi::TextureHandle> textures{1};
    std::vector<GpuMaterial> materials{1};

    TextureHandle white;
    TextureHandle flatNormal;

    nvrhi::ISampler* sampler(TextureWrap u, TextureWrap v)
    {
        nvrhi::SamplerHandle& slot = samplers[static_cast<usize>(u) * 3 + static_cast<usize>(v)];
        if (!slot)
        {
            nvrhi::SamplerDesc desc;
            desc.setAllFilters(true).setMaxAnisotropy(16.0f);
            desc.addressU = addressOf(u);
            desc.addressV = addressOf(v);
            desc.addressW = nvrhi::SamplerAddressMode::Wrap;
            slot = device->createSampler(desc);
        }
        return slot;
    }

    nvrhi::ITexture* texture(TextureHandle handle, TextureHandle fallback) const
    {
        const TextureHandle h = handle.index != 0 && handle.index < textures.size() ? handle : fallback;
        return textures[h.index];
    }
};

RenderResources::RenderResources(RenderDevice& renderDevice)
    : m_impl(std::make_unique<Impl>())
{
    Impl& impl = *m_impl;
    impl.device = renderDevice.device();
    impl.uploads = impl.device->createCommandList();

    nvrhi::BindingLayoutDesc layout;
    layout.visibility = nvrhi::ShaderType::Pixel;
    layout.registerSpace = 1;
    layout.registerSpaceIsDescriptorSet = true;
    layout.bindings = {
        nvrhi::BindingLayoutItem::ConstantBuffer(0), nvrhi::BindingLayoutItem::Texture_SRV(0),
        nvrhi::BindingLayoutItem::Texture_SRV(1),    nvrhi::BindingLayoutItem::Texture_SRV(2),
        nvrhi::BindingLayoutItem::Texture_SRV(3),    nvrhi::BindingLayoutItem::Texture_SRV(4),
        nvrhi::BindingLayoutItem::Sampler(0),
    };
    impl.materialLayout = impl.device->createBindingLayout(layout);

    impl.white = createTexture(makeSolidImage(1, 1, Vec4(1.0f), ImageFormat::RGBA8), "Default white");
    impl.flatNormal = createTexture(makeSolidImage(1, 1, Vec4(0.5f, 0.5f, 1.0f, 1.0f), ImageFormat::RGBA8),
                                    "Default normal");
    const MaterialHandle defaultMaterial = createMaterial(MaterialDesc{});
    ILL_CHECK(defaultMaterial.index == 1, "default material must be the first material");
}

RenderResources::~RenderResources() = default;

MeshHandle RenderResources::createMesh(const MeshData& mesh, const char* debugName)
{
    ILL_PROFILE_SCOPE();
    ILL_CHECK(!mesh.vertices.empty() && !mesh.indices.empty() && !mesh.submeshes.empty(),
              "createMesh: empty mesh");
    Impl& impl = *m_impl;

    nvrhi::BufferDesc vertexDesc;
    vertexDesc.byteSize = mesh.vertices.size() * sizeof(Vertex);
    vertexDesc.isVertexBuffer = true;
    vertexDesc.debugName = std::format("{} vertices", debugName);
    vertexDesc.enableAutomaticStateTracking(nvrhi::ResourceStates::VertexBuffer);
    nvrhi::BufferDesc indexDesc;
    indexDesc.byteSize = mesh.indices.size() * sizeof(u32);
    indexDesc.isIndexBuffer = true;
    indexDesc.debugName = std::format("{} indices", debugName);
    indexDesc.enableAutomaticStateTracking(nvrhi::ResourceStates::IndexBuffer);

    GpuMesh gpu;
    gpu.vertexBuffer = impl.device->createBuffer(vertexDesc);
    gpu.indexBuffer = impl.device->createBuffer(indexDesc);
    gpu.submeshes = mesh.submeshes;

    impl.uploads->open();
    impl.uploads->writeBuffer(gpu.vertexBuffer, mesh.vertices.data(), vertexDesc.byteSize);
    impl.uploads->writeBuffer(gpu.indexBuffer, mesh.indices.data(), indexDesc.byteSize);
    impl.uploads->close();
    impl.device->executeCommandList(impl.uploads);

    impl.meshes.push_back(std::move(gpu));
    return MeshHandle{static_cast<u32>(impl.meshes.size() - 1)};
}

TextureHandle RenderResources::createTexture(const Image& image, const char* debugName)
{
    ILL_PROFILE_SCOPE();
    ILL_CHECK(image.isValid(), "createTexture: invalid image");
    Impl& impl = *m_impl;

    nvrhi::TextureDesc desc;
    desc.width = image.width();
    desc.height = image.height();
    desc.mipLevels = static_cast<u32>(image.mips.size());
    desc.format = formatOf(image.format);
    desc.debugName = debugName;
    desc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    nvrhi::TextureHandle texture = impl.device->createTexture(desc);

    impl.uploads->open();
    const usize pixelSize = bytesPerPixel(image.format);
    for (u32 level = 0; level < image.mips.size(); ++level)
    {
        const ImageMip& mip = image.mips[level];
        impl.uploads->writeTexture(texture, 0, level, image.data.data() + mip.offset, mip.width * pixelSize);
    }
    impl.uploads->close();
    impl.device->executeCommandList(impl.uploads);

    impl.textures.push_back(texture);
    return TextureHandle{static_cast<u32>(impl.textures.size() - 1)};
}

MaterialHandle RenderResources::createMaterial(const MaterialDesc& desc)
{
    Impl& impl = *m_impl;
    const MaterialData& p = desc.parameters;

    GpuMaterialConstants constants{};
    constants.baseColorFactor = p.baseColorFactor;
    constants.emissiveFactor = Vec4(p.emissiveFactor, p.normalScale);
    constants.metallicFactor = p.metallicFactor;
    constants.roughnessFactor = p.roughnessFactor;
    constants.occlusionStrength = p.occlusionStrength;
    constants.alphaCutoff = p.alphaMode == AlphaMode::Mask ? p.alphaCutoff : -1.0f;

    GpuMaterial material;
    material.alphaMode = p.alphaMode;
    material.doubleSided = p.doubleSided;
    material.constants = impl.device->createBuffer(
        nvrhi::utils::CreateStaticConstantBufferDesc(sizeof(GpuMaterialConstants), "Material constants")
            .setInitialState(nvrhi::ResourceStates::ConstantBuffer)
            .setKeepInitialState(true));
    impl.uploads->open();
    impl.uploads->writeBuffer(material.constants, &constants, sizeof(constants));
    impl.uploads->close();
    impl.device->executeCommandList(impl.uploads);

    const TextureWrap wrapU = p.baseColorTexture.wrapU;
    const TextureWrap wrapV = p.baseColorTexture.wrapV;
    nvrhi::BindingSetDesc set;
    set.bindings = {
        nvrhi::BindingSetItem::ConstantBuffer(0, material.constants),
        nvrhi::BindingSetItem::Texture_SRV(0, impl.texture(desc.baseColor, impl.white)),
        nvrhi::BindingSetItem::Texture_SRV(1, impl.texture(desc.metallicRoughness, impl.white)),
        nvrhi::BindingSetItem::Texture_SRV(2, impl.texture(desc.normal, impl.flatNormal)),
        nvrhi::BindingSetItem::Texture_SRV(3, impl.texture(desc.occlusion, impl.white)),
        nvrhi::BindingSetItem::Texture_SRV(4, impl.texture(desc.emissive, impl.white)),
        nvrhi::BindingSetItem::Sampler(0, impl.sampler(wrapU, wrapV)),
    };
    material.bindingSet = impl.device->createBindingSet(set, impl.materialLayout);

    impl.materials.push_back(std::move(material));
    return MaterialHandle{static_cast<u32>(impl.materials.size() - 1)};
}

ModelHandles RenderResources::uploadModel(const ModelData& model)
{
    ILL_PROFILE_SCOPE();
    ModelHandles handles;
    handles.textures.resize(model.images.size());
    for (usize i = 0; i < model.images.size(); ++i)
    {
        if (model.images[i].isValid())
        {
            handles.textures[i] = createTexture(model.images[i], "Model texture");
        }
    }
    const auto textureFor = [&](const TextureRef& ref) {
        return ref.isSet() && static_cast<usize>(ref.image) < handles.textures.size()
                   ? handles.textures[static_cast<usize>(ref.image)]
                   : TextureHandle{};
    };
    for (const MaterialData& material : model.materials)
    {
        MaterialDesc desc;
        desc.parameters = material;
        desc.baseColor = textureFor(material.baseColorTexture);
        desc.metallicRoughness = textureFor(material.metallicRoughnessTexture);
        desc.normal = textureFor(material.normalTexture);
        desc.occlusion = textureFor(material.occlusionTexture);
        desc.emissive = textureFor(material.emissiveTexture);
        handles.materials.push_back(createMaterial(desc));
    }
    for (const MeshData& mesh : model.meshes)
    {
        handles.meshes.push_back(mesh.vertices.empty() || mesh.indices.empty() || mesh.submeshes.empty()
                                     ? MeshHandle{}
                                     : createMesh(mesh, "Model mesh"));
    }
    return handles;
}

u32 RenderResources::submeshCount(MeshHandle mesh) const noexcept
{
    const GpuMesh* gpu = this->mesh(mesh);
    return gpu != nullptr ? static_cast<u32>(gpu->submeshes.size()) : 0;
}

const Aabb& RenderResources::submeshBounds(MeshHandle mesh, u32 submesh) const noexcept
{
    static const Aabb kEmpty{};
    const GpuMesh* gpu = this->mesh(mesh);
    return gpu != nullptr && submesh < gpu->submeshes.size() ? gpu->submeshes[submesh].bounds : kEmpty;
}

u32 RenderResources::meshCount() const noexcept
{
    return static_cast<u32>(m_impl->meshes.size() - 1);
}

u32 RenderResources::textureCount() const noexcept
{
    return static_cast<u32>(m_impl->textures.size() - 1);
}

u32 RenderResources::materialCount() const noexcept
{
    return static_cast<u32>(m_impl->materials.size() - 1);
}

const RenderResources::GpuMesh* RenderResources::mesh(MeshHandle handle) const noexcept
{
    return handle.index != 0 && handle.index < m_impl->meshes.size() ? &m_impl->meshes[handle.index]
                                                                     : nullptr;
}

const RenderResources::GpuMaterial* RenderResources::material(MaterialHandle handle) const noexcept
{
    const u32 index = handle.index != 0 && handle.index < m_impl->materials.size() ? handle.index : 1;
    return &m_impl->materials[index];
}

nvrhi::IBindingLayout* RenderResources::materialBindingLayout() const noexcept
{
    return m_impl->materialLayout;
}

} // namespace illusion
