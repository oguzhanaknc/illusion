#include "EnvironmentBaker.hpp"
#include "RenderResourcesImpl.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Memory.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/render/RenderDevice.hpp>

#include <glm/gtc/packing.hpp>
#include <nvrhi/utils.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <format>
#include <utility>

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

// Generations of the slots of one resource kind, with the slots free for reuse. Slot 0 is never used.
struct SlotTable
{
    struct Slot
    {
        u32 generation = 0;
        bool live = false;
    };

    std::vector<Slot> slots{1};
    std::vector<u32> free;
    u32 live = 0;

    // A slot for a new resource: a freed one if any, else a new one at the end.
    u32 acquire()
    {
        u32 index = 0;
        if (!free.empty())
        {
            index = free.back();
            free.pop_back();
        }
        else
        {
            index = static_cast<u32>(slots.size());
            slots.emplace_back();
        }
        slots[index].live = true;
        ++live;
        return index;
    }

    [[nodiscard]] bool valid(u32 index, u32 generation) const noexcept
    {
        return index != 0 && index < slots.size() && slots[index].live &&
               slots[index].generation == generation;
    }

    // Makes handles to @p index stale; the slot is reused after recycle().
    void retire(u32 index) noexcept
    {
        slots[index].live = false;
        ++slots[index].generation;
        --live;
    }

    void recycle(u32 index) { free.push_back(index); }
};

enum class ResourceKind : u8
{
    Mesh,
    Texture,
    Material,
    Environment,
};

struct Release
{
    ResourceKind kind = ResourceKind::Mesh;
    u32 index = 0;
};

// Resources released before `query` was set; freed once the GPU reached it.
struct ReleaseBatch
{
    nvrhi::EventQueryHandle query;
    std::vector<Release> releases;
};

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
    RangeAllocator vertexRanges;
    RangeAllocator indexRanges;
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
    SlotTable meshSlots;
    SlotTable textureSlots;
    SlotTable materialSlots;
    SlotTable environmentSlots;

    // Released resources: those of the current batch, then batches waiting for the GPU (oldest first).
    std::vector<Release> releasing;
    std::vector<ReleaseBatch> retiring;
    std::vector<nvrhi::EventQueryHandle> spareQueries;

    // Environment readbacks in flight (requestEnvironmentReadback()).
    struct EnvironmentReadback
    {
        u64 ticket = 0;
        nvrhi::StagingTextureHandle background;
        nvrhi::StagingTextureHandle specular;
        nvrhi::EventQueryHandle done;
        ShIrradiance irradiance;
    };
    std::vector<EnvironmentReadback> readbacks;
    u64 nextReadback = 1;

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

    // The slot of @p handle, or @p fallback's for an empty handle. A stale handle asserts.
    [[nodiscard]] u32 textureSlot(TextureHandle handle, TextureHandle fallback) const
    {
        if (handle.index == 0)
        {
            return fallback.index;
        }
        ILL_ASSERT(textureSlots.valid(handle.index, handle.generation),
                   "the material uses a texture handle that is invalid or released");
        return textureSlots.valid(handle.index, handle.generation) ? handle.index : fallback.index;
    }

    // Grows @p data (indexed like a SlotTable) so that slot @p index exists.
    template <class T>
    static void ensureSlot(std::vector<T>& data, u32 index)
    {
        if (data.size() <= index)
        {
            data.resize(static_cast<usize>(index) + 1);
        }
    }

    void retire(SlotTable& table, ResourceKind kind, u32 index)
    {
        table.retire(index);
        releasing.push_back(Release{kind, index});
    }

    void free(const Release& release)
    {
        switch (release.kind)
        {
        case ResourceKind::Mesh:
        {
            GpuMesh& mesh = meshes[release.index];
            vertexRanges.free(mesh.vertexOffset, mesh.vertexCount);
            indexRanges.free(mesh.indexOffset, mesh.indexCount);
            mesh = GpuMesh{};
            meshSlots.recycle(release.index);
            break;
        }
        case ResourceKind::Texture:
            if (bindless)
            {
                // Freed slots point at a live texture, so the table never holds a destroyed view.
                device->writeDescriptorTable(bindlessTextures, nvrhi::BindingSetItem::Texture_SRV(
                                                                   release.index, textures[white.index]));
            }
            textures[release.index] = nullptr;
            textureSlots.recycle(release.index);
            break;
        case ResourceKind::Material:
            materials[release.index] = GpuMaterial{};
            materialSlots.recycle(release.index);
            break;
        case ResourceKind::Environment:
            environments[release.index] = GpuEnvironment{};
            environmentSlots.recycle(release.index);
            break;
        }
    }

    EnvironmentHandle storeEnvironment(GpuEnvironment&& environment)
    {
        const bool reused = !environmentSlots.free.empty();
        const u32 index = environmentSlots.acquire();
        ensureSlot(environments, index);
        environments[index] = std::move(environment);
        if (reused)
        {
            ++generation; // renderers cache binding sets by environment slot
        }
        return EnvironmentHandle{index, environmentSlots.slots[index].generation};
    }

    u32 collect()
    {
        if (!releasing.empty())
        {
            ReleaseBatch batch;
            if (!spareQueries.empty())
            {
                batch.query = std::move(spareQueries.back());
                spareQueries.pop_back();
                device->resetEventQuery(batch.query);
            }
            else
            {
                batch.query = device->createEventQuery();
            }
            device->setEventQuery(batch.query, nvrhi::CommandQueue::Graphics);
            batch.releases.swap(releasing);
            retiring.push_back(std::move(batch));
        }
        // Batches complete in order: stop at the first one the GPU has not reached.
        u32 freed = 0;
        usize done = 0;
        while (done < retiring.size() && device->pollEventQuery(retiring[done].query))
        {
            for (const Release& release : retiring[done].releases)
            {
                free(release);
                ++freed;
            }
            spareQueries.push_back(std::move(retiring[done].query));
            ++done;
        }
        retiring.erase(retiring.begin(), retiring.begin() + static_cast<std::ptrdiff_t>(done));
        return freed;
    }

    struct MaterialParts
    {
        GpuMaterialConstants constants{};
        GpuMaterialRecord record{};
        nvrhi::BindingSetDesc set;
    };

    // The constants, table record and binding set items of @p desc (item 0, the constant buffer, is set by
    // the caller).
    MaterialParts materialParts(const MaterialDesc& desc) const
    {
        const MaterialData& p = desc.parameters;
        MaterialParts parts;
        GpuMaterialConstants& constants = parts.constants;
        constants.baseColorFactor = p.baseColorFactor;
        constants.emissiveFactor = Vec4(p.emissiveFactor, p.normalScale);
        constants.metallicFactor = p.metallicFactor;
        constants.roughnessFactor = p.roughnessFactor;
        constants.occlusionStrength = p.occlusionStrength;
        constants.alphaCutoff = p.alphaMode == AlphaMode::Mask ? p.alphaCutoff : -1.0f;

        const u32 sampler = samplerIndex(p.baseColorTexture.wrapU, p.baseColorTexture.wrapV);
        GpuMaterialRecord& record = parts.record;
        record.constants = constants;
        record.baseColorTexture = textureSlot(desc.baseColor, white);
        record.metallicRoughnessTexture = textureSlot(desc.metallicRoughness, white);
        record.normalTexture = textureSlot(desc.normal, flatNormal);
        record.occlusionTexture = textureSlot(desc.occlusion, white);
        record.emissiveTexture = textureSlot(desc.emissive, white);
        record.sampler = sampler;

        parts.set.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, nullptr),
            nvrhi::BindingSetItem::Texture_SRV(0, textures[record.baseColorTexture]),
            nvrhi::BindingSetItem::Texture_SRV(1, textures[record.metallicRoughnessTexture]),
            nvrhi::BindingSetItem::Texture_SRV(2, textures[record.normalTexture]),
            nvrhi::BindingSetItem::Texture_SRV(3, textures[record.occlusionTexture]),
            nvrhi::BindingSetItem::Texture_SRV(4, textures[record.emissiveTexture]),
            nvrhi::BindingSetItem::Sampler(0, samplers[sampler]),
        };
        return parts;
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
    impl.collect();
    const auto vertices = static_cast<u32>(mesh.vertices.size());
    const auto indices = static_cast<u32>(mesh.indices.size());

    GpuMesh gpu;
    const u32 usedVertices = impl.vertexRanges.end();
    const u32 usedIndices = impl.indexRanges.end();
    gpu.vertexOffset = impl.vertexRanges.allocate(vertices);
    gpu.indexOffset = impl.indexRanges.allocate(indices);
    gpu.vertexCount = vertices;
    gpu.indexCount = indices;
    gpu.submeshes = mesh.submeshes;

    impl.uploads->open();
    impl.growPool(impl.vertexPool, usedVertices, impl.vertexRanges.end(), sizeof(Vertex), true,
                  "Vertex pool");
    impl.growPool(impl.indexPool, usedIndices, impl.indexRanges.end(), sizeof(u32), false, "Index pool");
    impl.uploads->writeBuffer(impl.vertexPool, mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex),
                              static_cast<u64>(gpu.vertexOffset) * sizeof(Vertex));
    impl.uploads->writeBuffer(impl.indexPool, mesh.indices.data(), mesh.indices.size() * sizeof(u32),
                              static_cast<u64>(gpu.indexOffset) * sizeof(u32));
    impl.uploads->close();
    impl.device->executeCommandList(impl.uploads);

    const u32 index = impl.meshSlots.acquire();
    Impl::ensureSlot(impl.meshes, index);
    impl.meshes[index] = std::move(gpu);
    return MeshHandle{index, impl.meshSlots.slots[index].generation};
}

TextureHandle RenderResources::createTexture(const Image& image, const char* debugName)
{
    ILL_PROFILE_SCOPE();
    ILL_CHECK(image.isValid(), "createTexture: invalid image");
    Impl& impl = *m_impl;
    impl.collect();

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

    const u32 index = impl.textureSlots.acquire();
    Impl::ensureSlot(impl.textures, index);
    impl.textures[index] = texture;
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
    return TextureHandle{index, impl.textureSlots.slots[index].generation};
}

MaterialHandle RenderResources::createMaterial(const MaterialDesc& desc)
{
    Impl& impl = *m_impl;
    impl.collect();
    Impl::MaterialParts parts = impl.materialParts(desc);

    const u32 index = impl.materialSlots.acquire();
    GpuMaterial material;
    material.index = index;
    material.alphaMode = desc.parameters.alphaMode;
    material.doubleSided = desc.parameters.doubleSided;
    material.constants = impl.device->createBuffer(
        nvrhi::utils::CreateStaticConstantBufferDesc(sizeof(GpuMaterialConstants), "Material constants")
            .setInitialState(nvrhi::ResourceStates::ConstantBuffer)
            .setKeepInitialState(true));

    impl.uploads->open();
    impl.uploads->writeBuffer(material.constants, &parts.constants, sizeof(parts.constants));
    impl.storeRecord(index, parts.record);
    impl.uploads->close();
    impl.device->executeCommandList(impl.uploads);

    parts.set.bindings[0].resourceHandle = material.constants;
    material.bindingSet = impl.device->createBindingSet(parts.set, impl.materialLayout);

    Impl::ensureSlot(impl.materials, index);
    impl.materials[index] = std::move(material);
    return MaterialHandle{index, impl.materialSlots.slots[index].generation};
}

void RenderResources::updateMaterial(MaterialHandle handle, const MaterialDesc& desc)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    ILL_ASSERT(impl.materialSlots.valid(handle.index, handle.generation),
               "updateMaterial: the material handle is invalid or released");
    if (!impl.materialSlots.valid(handle.index, handle.generation))
    {
        return;
    }
    Impl::MaterialParts parts = impl.materialParts(desc);
    GpuMaterial& material = impl.materials[handle.index];
    material.alphaMode = desc.parameters.alphaMode;
    material.doubleSided = desc.parameters.doubleSided;

    impl.uploads->open();
    impl.uploads->writeBuffer(material.constants, &parts.constants, sizeof(parts.constants));
    impl.storeRecord(handle.index, parts.record);
    impl.uploads->close();
    impl.device->executeCommandList(impl.uploads);

    // The constant buffer stays the same, so only the textures and the sampler can differ.
    parts.set.bindings[0].resourceHandle = material.constants;
    const nvrhi::BindingSetDesc* bound = material.bindingSet->getDesc();
    bool sameBindings = bound != nullptr && bound->bindings.size() == parts.set.bindings.size();
    for (usize i = 0; sameBindings && i < parts.set.bindings.size(); ++i)
    {
        sameBindings = bound->bindings[i].resourceHandle == parts.set.bindings[i].resourceHandle;
    }
    if (!sameBindings)
    {
        material.bindingSet = impl.device->createBindingSet(parts.set, impl.materialLayout);
    }
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
    impl.collect();

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
    return impl.storeEnvironment(std::move(environment));
}

namespace
{

// Creates an RGBA16F cube map for an environment uploaded from a bake.
nvrhi::TextureHandle createEnvironmentCube(nvrhi::IDevice* device, u32 size, u32 mips, const char* debugName)
{
    nvrhi::TextureDesc desc;
    desc.dimension = nvrhi::TextureDimension::TextureCube;
    desc.width = size;
    desc.height = size;
    desc.arraySize = 6;
    desc.mipLevels = mips;
    desc.format = nvrhi::Format::RGBA16_FLOAT;
    desc.debugName = debugName;
    desc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    return device->createTexture(desc);
}

// Uploads `texels` (mip order, then face order, tightly packed rows) into every subresource of `cube`.
void uploadCube(nvrhi::ICommandList* commands, nvrhi::ITexture* cube, u32 size, u32 mips, const u64* texels)
{
    for (u32 mip = 0; mip < mips; ++mip)
    {
        const u32 face = std::max(size >> mip, 1u);
        for (u32 slice = 0; slice < 6; ++slice)
        {
            commands->writeTexture(cube, slice, mip, texels, static_cast<usize>(face) * sizeof(u64));
            texels += static_cast<usize>(face) * face;
        }
    }
}

// Copies every subresource of `cube` into the staging texture `staging` of the same layout.
void copyCube(nvrhi::ICommandList* commands, nvrhi::IStagingTexture* staging, nvrhi::ITexture* cube)
{
    const nvrhi::TextureDesc& desc = cube->getDesc();
    for (u32 mip = 0; mip < desc.mipLevels; ++mip)
    {
        for (u32 slice = 0; slice < 6; ++slice)
        {
            const nvrhi::TextureSlice region = nvrhi::TextureSlice().setMipLevel(mip).setArraySlice(slice);
            commands->copyTexture(staging, region, cube, region);
        }
    }
}

// Reads every subresource of `staging` (a cube map of `size` and `mips`) into `out` in upload order.
bool readCube(nvrhi::IDevice* device, nvrhi::IStagingTexture* staging, u32 size, u32 mips,
              std::vector<u64>& out)
{
    out.resize(cubeTexelCount(size, mips));
    u64* target = out.data();
    for (u32 mip = 0; mip < mips; ++mip)
    {
        const u32 face = std::max(size >> mip, 1u);
        for (u32 slice = 0; slice < 6; ++slice)
        {
            usize pitch = 0;
            const auto* source = static_cast<const std::byte*>(device->mapStagingTexture(
                staging, nvrhi::TextureSlice().setMipLevel(mip).setArraySlice(slice),
                nvrhi::CpuAccessMode::Read, &pitch));
            if (source == nullptr)
            {
                return false;
            }
            const usize row = static_cast<usize>(face) * sizeof(u64);
            for (u32 y = 0; y < face; ++y)
            {
                std::memcpy(target, source + static_cast<usize>(y) * pitch, row);
                target += face;
            }
            device->unmapStagingTexture(staging);
        }
    }
    return true;
}

} // namespace

EnvironmentHandle RenderResources::createEnvironment(const BakedEnvironment& baked, const char* debugName)
{
    ILL_PROFILE_SCOPE();
    ILL_CHECK(baked.isValid(), "createEnvironment: invalid baked environment");
    Impl& impl = *m_impl;
    impl.collect();

    GpuEnvironment environment;
    environment.background =
        createEnvironmentCube(impl.device, baked.backgroundSize, baked.backgroundMips, debugName);
    environment.specular =
        createEnvironmentCube(impl.device, baked.specularSize, baked.specularMips, debugName);
    environment.specularMips = baked.specularMips;
    environment.irradiance = baked.irradiance;
    impl.uploads->open();
    uploadCube(impl.uploads, environment.background, baked.backgroundSize, baked.backgroundMips,
               baked.background.data());
    uploadCube(impl.uploads, environment.specular, baked.specularSize, baked.specularMips,
               baked.specular.data());
    impl.uploads->close();
    impl.device->executeCommandList(impl.uploads);

    return impl.storeEnvironment(std::move(environment));
}

u64 RenderResources::requestEnvironmentReadback(EnvironmentHandle environment)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    if (environment.index == 0 || !impl.environmentSlots.valid(environment.index, environment.generation))
    {
        return 0;
    }
    const GpuEnvironment& source = impl.environments[environment.index];
    Impl::EnvironmentReadback readback;
    readback.irradiance = source.irradiance;
    readback.background =
        impl.device->createStagingTexture(source.background->getDesc(), nvrhi::CpuAccessMode::Read);
    readback.specular =
        impl.device->createStagingTexture(source.specular->getDesc(), nvrhi::CpuAccessMode::Read);
    readback.done = impl.device->createEventQuery();
    if (!readback.background || !readback.specular || !readback.done)
    {
        return 0;
    }
    readback.ticket = impl.nextReadback++;
    impl.uploads->open();
    copyCube(impl.uploads, readback.background, source.background);
    copyCube(impl.uploads, readback.specular, source.specular);
    impl.uploads->close();
    impl.device->executeCommandList(impl.uploads);
    impl.device->setEventQuery(readback.done, nvrhi::CommandQueue::Graphics);
    impl.readbacks.push_back(std::move(readback));
    return impl.readbacks.back().ticket;
}

ReadbackStatus RenderResources::readEnvironment(u64 ticket, BakedEnvironment& out, bool block)
{
    Impl& impl = *m_impl;
    const auto found = std::ranges::find(impl.readbacks, ticket, &Impl::EnvironmentReadback::ticket);
    if (ticket == 0 || found == impl.readbacks.end())
    {
        return ReadbackStatus::Invalid;
    }
    if (block)
    {
        impl.device->waitEventQuery(found->done);
    }
    else if (!impl.device->pollEventQuery(found->done))
    {
        return ReadbackStatus::Pending;
    }
    ILL_PROFILE_SCOPE_N("Environment readback");
    const nvrhi::TextureDesc& background = found->background->getDesc();
    const nvrhi::TextureDesc& specular = found->specular->getDesc();
    BakedEnvironment result;
    result.backgroundSize = background.width;
    result.backgroundMips = background.mipLevels;
    result.specularSize = specular.width;
    result.specularMips = specular.mipLevels;
    result.irradiance = found->irradiance;
    const bool ok =
        readCube(impl.device, found->background, result.backgroundSize, result.backgroundMips,
                 result.background) &&
        readCube(impl.device, found->specular, result.specularSize, result.specularMips, result.specular);
    impl.readbacks.erase(found);
    if (!ok)
    {
        return ReadbackStatus::Invalid;
    }
    out = std::move(result);
    return ReadbackStatus::Ready;
}

void RenderResources::releaseMesh(MeshHandle mesh)
{
    if (mesh.index == 0)
    {
        return;
    }
    Impl& impl = *m_impl;
    const bool valid = impl.meshSlots.valid(mesh.index, mesh.generation);
    ILL_ASSERT(valid, "releaseMesh: the mesh handle is invalid or already released");
    if (valid)
    {
        impl.retire(impl.meshSlots, ResourceKind::Mesh, mesh.index);
    }
}

void RenderResources::releaseTexture(TextureHandle texture)
{
    if (texture.index == 0)
    {
        return;
    }
    Impl& impl = *m_impl;
    const bool valid = impl.textureSlots.valid(texture.index, texture.generation);
    const bool builtin = texture.index == impl.white.index || texture.index == impl.flatNormal.index;
    ILL_ASSERT(valid, "releaseTexture: the texture handle is invalid or already released");
    ILL_ASSERT(!builtin, "releaseTexture: the default textures cannot be released");
    if (valid && !builtin)
    {
        impl.retire(impl.textureSlots, ResourceKind::Texture, texture.index);
    }
}

void RenderResources::releaseMaterial(MaterialHandle material)
{
    if (material.index == 0)
    {
        return;
    }
    Impl& impl = *m_impl;
    const bool valid = impl.materialSlots.valid(material.index, material.generation);
    const bool builtin = material.index == defaultMaterial().index;
    ILL_ASSERT(valid, "releaseMaterial: the material handle is invalid or already released");
    ILL_ASSERT(!builtin, "releaseMaterial: the default material cannot be released");
    if (valid && !builtin)
    {
        impl.retire(impl.materialSlots, ResourceKind::Material, material.index);
    }
}

void RenderResources::releaseEnvironment(EnvironmentHandle environment)
{
    if (environment.index == 0)
    {
        return;
    }
    Impl& impl = *m_impl;
    const bool valid = impl.environmentSlots.valid(environment.index, environment.generation);
    ILL_ASSERT(valid, "releaseEnvironment: the environment handle is invalid or already released");
    if (valid)
    {
        impl.retire(impl.environmentSlots, ResourceKind::Environment, environment.index);
        ++impl.generation; // renderers cache binding sets by environment slot
    }
}

void RenderResources::releaseModel(const ModelHandles& model)
{
    for (const MeshHandle mesh : model.meshes)
    {
        releaseMesh(mesh);
    }
    for (const MaterialHandle material : model.materials)
    {
        releaseMaterial(material);
    }
    for (const TextureHandle texture : model.textures)
    {
        releaseTexture(texture);
    }
}

u32 RenderResources::collect()
{
    ILL_PROFILE_SCOPE();
    return m_impl->collect();
}

bool RenderResources::isValid(MeshHandle handle) const noexcept
{
    return m_impl->meshSlots.valid(handle.index, handle.generation);
}

bool RenderResources::isValid(TextureHandle handle) const noexcept
{
    return m_impl->textureSlots.valid(handle.index, handle.generation);
}

bool RenderResources::isValid(MaterialHandle handle) const noexcept
{
    return m_impl->materialSlots.valid(handle.index, handle.generation);
}

bool RenderResources::isValid(EnvironmentHandle handle) const noexcept
{
    return m_impl->environmentSlots.valid(handle.index, handle.generation);
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
    return m_impl->meshSlots.live;
}

u32 RenderResources::textureCount() const noexcept
{
    return m_impl->textureSlots.live;
}

u32 RenderResources::materialCount() const noexcept
{
    return m_impl->materialSlots.live;
}

u32 RenderResources::environmentCount() const noexcept
{
    return m_impl->environmentSlots.live;
}

RenderResourceStatistics RenderResources::statistics() const noexcept
{
    const Impl& impl = *m_impl;
    RenderResourceStatistics statistics;
    statistics.vertices = impl.vertexRanges.allocated();
    statistics.vertexCapacity =
        impl.vertexPool ? static_cast<u32>(impl.vertexPool->getDesc().byteSize / sizeof(Vertex)) : 0;
    statistics.indices = impl.indexRanges.allocated();
    statistics.indexCapacity =
        impl.indexPool ? static_cast<u32>(impl.indexPool->getDesc().byteSize / sizeof(u32)) : 0;
    statistics.freeRanges = impl.vertexRanges.freeRanges() + impl.indexRanges.freeRanges();
    statistics.retiring = static_cast<u32>(impl.releasing.size());
    for (const ReleaseBatch& batch : impl.retiring)
    {
        statistics.retiring += static_cast<u32>(batch.releases.size());
    }
    statistics.materialCapacity = impl.materialCapacity;
    return statistics;
}

const RenderResources::GpuMesh* RenderResources::mesh(MeshHandle handle) const noexcept
{
    return m_impl->meshSlots.valid(handle.index, handle.generation) ? &m_impl->meshes[handle.index] : nullptr;
}

const RenderResources::GpuMaterial* RenderResources::material(MaterialHandle handle) const noexcept
{
    const u32 index = m_impl->materialSlots.valid(handle.index, handle.generation) ? handle.index : 1;
    return &m_impl->materials[index];
}

nvrhi::IBindingSet* RenderResources::materialBindingSet(MaterialHandle handle) const noexcept
{
    return material(handle)->bindingSet;
}

nvrhi::ITexture* RenderResources::texture(TextureHandle handle) const noexcept
{
    return m_impl->textureSlots.valid(handle.index, handle.generation) ? m_impl->textures[handle.index].Get()
                                                                       : nullptr;
}

const RenderResources::GpuEnvironment& RenderResources::environment(EnvironmentHandle handle) const noexcept
{
    return m_impl->environmentSlots.valid(handle.index, handle.generation)
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
