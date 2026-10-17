#include "EnvironmentBaker.hpp"
#include "RenderResourcesImpl.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/render/RenderDevice.hpp>

#include <glm/gtc/packing.hpp>
#include <nvrhi/utils.h>

#include <algorithm>
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

    // Shared geometry pools (grown by copying), the material table and the bindless texture table.
    nvrhi::BufferHandle vertexPool;
    nvrhi::BufferHandle indexPool;
    u32 vertexCount = 0;
    u32 indexCount = 0;
    nvrhi::BufferHandle materialTable;
    u32 materialCapacity = 0;
    std::vector<GpuMaterialRecord> records{1};
    bool bindless = false;
    nvrhi::BindingLayoutHandle bindlessLayout;
    nvrhi::DescriptorTableHandle bindlessTextures;
    u32 generation = 0;

    // Slot 0 of each vector is unused so that handle index 0 means "none".
    std::vector<GpuMesh> meshes{1};
    std::vector<nvrhi::TextureHandle> textures{1};
    std::vector<GpuMaterial> materials{1};
    std::vector<GpuEnvironment> environments{1};

    EnvironmentBaker baker;
    GpuEnvironment blackEnvironment;
    nvrhi::TextureHandle brdfLut;

    TextureHandle white;
    TextureHandle flatNormal;

    static u32 samplerIndex(TextureWrap u, TextureWrap v) noexcept
    {
        return static_cast<u32>(u) * 3 + static_cast<u32>(v);
    }

    void createSamplers()
    {
        for (u32 u = 0; u < 3; ++u)
        {
            for (u32 v = 0; v < 3; ++v)
            {
                nvrhi::SamplerDesc desc;
                desc.setAllFilters(true).setMaxAnisotropy(16.0f);
                desc.addressU = addressOf(static_cast<TextureWrap>(u));
                desc.addressV = addressOf(static_cast<TextureWrap>(v));
                desc.addressW = nvrhi::SamplerAddressMode::Wrap;
                samplers[u * 3 + v] = device->createSampler(desc);
            }
        }
    }

    // Recreates `pool` with room for `required` elements of `stride` bytes, copying `used` elements.
    void growPool(nvrhi::BufferHandle& pool, u32 used, u32 required, u32 stride, bool vertex,
                  const char* name)
    {
        const u64 capacity = pool ? pool->getDesc().byteSize / stride : 0;
        if (pool && required <= capacity)
        {
            return;
        }
        nvrhi::BufferDesc desc;
        desc.byteSize =
            static_cast<u64>(std::max<u64>(required, std::max<u64>(capacity * 2, 65536))) * stride;
        desc.isVertexBuffer = vertex;
        desc.isIndexBuffer = !vertex;
        desc.debugName = name;
        desc.enableAutomaticStateTracking(vertex ? nvrhi::ResourceStates::VertexBuffer
                                                 : nvrhi::ResourceStates::IndexBuffer);
        nvrhi::BufferHandle grown = device->createBuffer(desc);
        if (pool && used > 0)
        {
            uploads->copyBuffer(grown, 0, pool, 0, static_cast<u64>(used) * stride);
        }
        pool = grown;
        ++generation;
    }

    // Writes the material record of `index`, growing the table when needed.
    void storeRecord(u32 index, const GpuMaterialRecord& record)
    {
        if (records.size() <= index)
        {
            records.resize(index + 1);
        }
        records[index] = record;
        if (index >= materialCapacity)
        {
            materialCapacity = std::max(index + 1, std::max(materialCapacity * 2, 256u));
            nvrhi::BufferDesc desc;
            desc.byteSize = static_cast<u64>(materialCapacity) * sizeof(GpuMaterialRecord);
            desc.structStride = sizeof(GpuMaterialRecord);
            desc.debugName = "Material table";
            desc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
            materialTable = device->createBuffer(desc);
            uploads->writeBuffer(materialTable, records.data(), records.size() * sizeof(GpuMaterialRecord));
            ++generation;
        }
        else
        {
            uploads->writeBuffer(materialTable, &record, sizeof(record),
                                 static_cast<u64>(index) * sizeof(GpuMaterialRecord));
        }
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
    impl.createSamplers();
    impl.bindless = renderDevice.gpu().bindless;
    if (impl.bindless)
    {
        nvrhi::BindlessLayoutDesc bindlessDesc;
        bindlessDesc.visibility = nvrhi::ShaderType::Pixel;
        bindlessDesc.firstSlot = 0;
        bindlessDesc.maxCapacity = kBindlessTextureCapacity;
        bindlessDesc.registerSpaces = {nvrhi::BindingLayoutItem::Texture_SRV(1)};
        impl.bindlessLayout = impl.device->createBindlessLayout(bindlessDesc);
        impl.bindlessTextures =
            impl.bindlessLayout ? impl.device->createDescriptorTable(impl.bindlessLayout) : nullptr;
        impl.bindless = impl.bindlessTextures != nullptr;
    }

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

    ILL_CHECK(impl.baker.initialize(impl.device), "cannot create the image-based lighting pipelines");
    impl.uploads->open();
    impl.blackEnvironment = impl.baker.makeBlack(impl.uploads);
    impl.brdfLut = impl.baker.bakeBrdfLut(impl.uploads, 128);
    impl.uploads->close();
    impl.device->executeCommandList(impl.uploads);
}

RenderResources::~RenderResources() = default;

MeshHandle RenderResources::createMesh(const MeshData& mesh, const char* debugName)
{
    ILL_PROFILE_SCOPE();
    ILL_CHECK(!mesh.vertices.empty() && !mesh.indices.empty() && !mesh.submeshes.empty(),
              "createMesh: empty mesh");
    static_cast<void>(debugName); // meshes share the pools; names would apply to the whole pool
    Impl& impl = *m_impl;
    const auto vertices = static_cast<u32>(mesh.vertices.size());
    const auto indices = static_cast<u32>(mesh.indices.size());

    impl.uploads->open();
    impl.growPool(impl.vertexPool, impl.vertexCount, impl.vertexCount + vertices, sizeof(Vertex), true,
                  "Vertex pool");
    impl.growPool(impl.indexPool, impl.indexCount, impl.indexCount + indices, sizeof(u32), false,
                  "Index pool");
    impl.uploads->writeBuffer(impl.vertexPool, mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex),
                              static_cast<u64>(impl.vertexCount) * sizeof(Vertex));
    impl.uploads->writeBuffer(impl.indexPool, mesh.indices.data(), mesh.indices.size() * sizeof(u32),
                              static_cast<u64>(impl.indexCount) * sizeof(u32));
    impl.uploads->close();
    impl.device->executeCommandList(impl.uploads);

    GpuMesh gpu;
    gpu.vertexOffset = impl.vertexCount;
    gpu.indexOffset = impl.indexCount;
    gpu.submeshes = mesh.submeshes;
    impl.vertexCount += vertices;
    impl.indexCount += indices;
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
    const auto index = static_cast<u32>(impl.textures.size() - 1);
    if (impl.bindless)
    {
        ILL_CHECK(index < kBindlessTextureCapacity, "too many textures for the bindless table");
        if (impl.bindlessTextures->getCapacity() <= index)
        {
            impl.device->resizeDescriptorTable(
                impl.bindlessTextures, std::min(std::max(index + 1, impl.bindlessTextures->getCapacity() * 2),
                                                kBindlessTextureCapacity));
        }
        impl.device->writeDescriptorTable(impl.bindlessTextures,
                                          nvrhi::BindingSetItem::Texture_SRV(index, texture));
    }
    return TextureHandle{index};
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
    material.index = static_cast<u32>(impl.materials.size());
    material.alphaMode = p.alphaMode;
    material.doubleSided = p.doubleSided;
    material.constants = impl.device->createBuffer(
        nvrhi::utils::CreateStaticConstantBufferDesc(sizeof(GpuMaterialConstants), "Material constants")
            .setInitialState(nvrhi::ResourceStates::ConstantBuffer)
            .setKeepInitialState(true));
    const TextureWrap wrapU = p.baseColorTexture.wrapU;
    const TextureWrap wrapV = p.baseColorTexture.wrapV;
    const auto textureIndex = [&](TextureHandle handle, TextureHandle fallback) {
        return handle.index != 0 && handle.index < impl.textures.size() ? handle.index : fallback.index;
    };
    GpuMaterialRecord record{};
    record.constants = constants;
    record.baseColorTexture = textureIndex(desc.baseColor, impl.white);
    record.metallicRoughnessTexture = textureIndex(desc.metallicRoughness, impl.white);
    record.normalTexture = textureIndex(desc.normal, impl.flatNormal);
    record.occlusionTexture = textureIndex(desc.occlusion, impl.white);
    record.emissiveTexture = textureIndex(desc.emissive, impl.white);
    record.sampler = Impl::samplerIndex(wrapU, wrapV);

    impl.uploads->open();
    impl.uploads->writeBuffer(material.constants, &constants, sizeof(constants));
    impl.storeRecord(material.index, record);
    impl.uploads->close();
    impl.device->executeCommandList(impl.uploads);

    nvrhi::BindingSetDesc set;
    set.bindings = {
        nvrhi::BindingSetItem::ConstantBuffer(0, material.constants),
        nvrhi::BindingSetItem::Texture_SRV(0, impl.texture(desc.baseColor, impl.white)),
        nvrhi::BindingSetItem::Texture_SRV(1, impl.texture(desc.metallicRoughness, impl.white)),
        nvrhi::BindingSetItem::Texture_SRV(2, impl.texture(desc.normal, impl.flatNormal)),
        nvrhi::BindingSetItem::Texture_SRV(3, impl.texture(desc.occlusion, impl.white)),
        nvrhi::BindingSetItem::Texture_SRV(4, impl.texture(desc.emissive, impl.white)),
        nvrhi::BindingSetItem::Sampler(0, impl.samplers[Impl::samplerIndex(wrapU, wrapV)]),
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

EnvironmentHandle RenderResources::createEnvironment(const Image& equirect, const EnvironmentDesc& desc,
                                                     const char* debugName)
{
    ILL_PROFILE_SCOPE();
    ILL_CHECK(equirect.isValid(), "createEnvironment: invalid image");
    Impl& impl = *m_impl;

    // Half floats: filterable on every GPU (32-bit float filtering is optional in Vulkan).
    const u32 width = equirect.width();
    const u32 height = equirect.height();
    std::vector<u64> halves(static_cast<usize>(width) * height);
    for (u32 y = 0; y < height; ++y)
    {
        for (u32 x = 0; x < width; ++x)
        {
            const Vec4 pixel = glm::min(readPixel(equirect, x, y), Vec4(65504.0f));
            halves[static_cast<usize>(y) * width + x] = glm::packHalf4x16(pixel);
        }
    }
    nvrhi::TextureDesc sourceDesc;
    sourceDesc.width = width;
    sourceDesc.height = height;
    sourceDesc.format = nvrhi::Format::RGBA16_FLOAT;
    sourceDesc.debugName = std::format("{} source", debugName);
    sourceDesc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    nvrhi::TextureHandle source = impl.device->createTexture(sourceDesc);

    impl.uploads->open();
    impl.uploads->writeTexture(source, 0, 0, halves.data(), static_cast<usize>(width) * sizeof(u64));
    GpuEnvironment environment = impl.baker.bake(impl.uploads, source, desc, debugName);
    impl.uploads->close();
    impl.device->executeCommandList(impl.uploads);

    environment.irradiance = computeShIrradiance(equirect);
    impl.environments.push_back(std::move(environment));
    return EnvironmentHandle{static_cast<u32>(impl.environments.size() - 1)};
}

const ShIrradiance& RenderResources::irradiance(EnvironmentHandle environment) const noexcept
{
    return this->environment(environment).irradiance;
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

u32 RenderResources::environmentCount() const noexcept
{
    return static_cast<u32>(m_impl->environments.size() - 1);
}

const RenderResources::GpuEnvironment& RenderResources::environment(EnvironmentHandle handle) const noexcept
{
    return handle.index != 0 && handle.index < m_impl->environments.size()
               ? m_impl->environments[handle.index]
               : m_impl->blackEnvironment;
}

nvrhi::ITexture* RenderResources::brdfLut() const noexcept
{
    return m_impl->brdfLut;
}

nvrhi::IBuffer* RenderResources::vertexPool() const noexcept
{
    return m_impl->vertexPool;
}

nvrhi::IBuffer* RenderResources::indexPool() const noexcept
{
    return m_impl->indexPool;
}

nvrhi::IBuffer* RenderResources::materialTable() const noexcept
{
    return m_impl->materialTable;
}

nvrhi::ISampler* RenderResources::materialSampler(u32 index) const noexcept
{
    return m_impl->samplers[index];
}

bool RenderResources::bindless() const noexcept
{
    return m_impl->bindless;
}

nvrhi::IBindingLayout* RenderResources::bindlessLayout() const noexcept
{
    return m_impl->bindlessLayout;
}

nvrhi::IDescriptorTable* RenderResources::bindlessTextures() const noexcept
{
    return m_impl->bindlessTextures;
}

u32 RenderResources::generation() const noexcept
{
    return m_impl->generation;
}

nvrhi::IBindingLayout* RenderResources::materialBindingLayout() const noexcept
{
    return m_impl->materialLayout;
}

} // namespace illusion
