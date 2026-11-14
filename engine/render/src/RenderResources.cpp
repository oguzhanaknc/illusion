#include "EnvironmentBaker.hpp"
#include "RenderResourcesImpl.hpp"

#include <illusion/assets/TextureCompression.hpp>
#include <illusion/core/Assert.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Memory.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/render/RenderDevice.hpp>

#include <glm/gtc/packing.hpp>
#include <nvrhi/utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <optional>
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
    case ImageFormat::BC7:
        return nvrhi::Format::BC7_UNORM;
    case ImageFormat::BC7Srgb:
        return nvrhi::Format::BC7_UNORM_SRGB;
    case ImageFormat::BC5:
        return nvrhi::Format::BC5_UNORM;
    case ImageFormat::BC4:
        return nvrhi::Format::BC4_UNORM;
    case ImageFormat::BC6H:
        return nvrhi::Format::BC6H_UFLOAT;
    }
    return nvrhi::Format::RGBA8_UNORM;
}

// The format a texture of @p format would be uploaded as without block compression.
ImageFormat uncompressedFormat(ImageFormat format) noexcept
{
    switch (format)
    {
    case ImageFormat::RGBA32F:
    case ImageFormat::BC6H:
        return ImageFormat::RGBA32F;
    case ImageFormat::RGBA8Srgb:
    case ImageFormat::BC7Srgb:
        return ImageFormat::RGBA8Srgb;
    default:
        return ImageFormat::RGBA8;
    }
}

// Row pitch and size in bytes of a square level @p width pixels wide in @p format (rows of blocks for BC).
struct LevelLayout
{
    usize rowPitch = 0;
    usize rows = 0;
};

LevelLayout levelLayout(nvrhi::Format format, u32 width) noexcept
{
    const nvrhi::FormatInfo& info = nvrhi::getFormatInfo(format);
    const usize blocks = (static_cast<usize>(width) + info.blockSize - 1) / info.blockSize;
    return LevelLayout{blocks * info.bytesPerBlock, blocks};
}

// Bytes of every level and array slice of the square texture @p desc.
u64 cubeBytes(const nvrhi::TextureDesc& desc) noexcept
{
    u64 bytes = 0;
    for (u32 mip = 0; mip < desc.mipLevels; ++mip)
    {
        const LevelLayout layout = levelLayout(desc.format, std::max(desc.width >> mip, 1u));
        bytes += static_cast<u64>(layout.rowPitch) * layout.rows * desc.arraySize;
    }
    return bytes;
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
    ReplacedTexture, // a texture a slot no longer binds (or never will), freed after the GPU used it
    TextureSwap,     // a texture with new resident levels, bound to its slot once the GPU wrote it
};

constexpr u32 kNoSwap = ~0u;

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
    struct TextureBytes
    {
        u64 gpu = 0;
        u64 uncompressed = 0;
        u64 saved = 0; // of the levels left out
    };
    std::vector<TextureBytes> textureSizes{1}; // indexed like `textures`
    // The image a texture was created from and its resident levels [firstMip, mipCount).
    struct TextureInfo
    {
        ImageFormat format = ImageFormat::RGBA8; // as given to createTexture()
        ImageFormat gpuFormat = ImageFormat::RGBA8;
        u32 width = 0;
        u32 height = 0;
        u32 mipCount = 0;
        u32 firstMip = 0;

        [[nodiscard]] bool matches(const Image& image) const noexcept
        {
            return image.format == format && image.width() == width && image.height() == height &&
                   image.mips.size() == mipCount;
        }
    };
    std::vector<TextureInfo> textureInfos{1}; // indexed like `textures`
    struct ReplacedTexture
    {
        nvrhi::TextureHandle texture;
        TextureBytes bytes;
    };
    std::vector<ReplacedTexture> replacedTextures;
    std::vector<u32> freeReplaced;
    // setTextureMips() binds its texture when the GPU finished writing it: the swap of the slot's generation
    // `generation`, the texture, and the record of the texture bound until then (retired at the swap).
    struct TextureSwap
    {
        u32 slot = 0;
        u32 generation = 0;
        nvrhi::TextureHandle texture;
        u32 retire = 0; // index in `replacedTextures`
    };
    std::vector<TextureSwap> textureSwaps;
    std::vector<u32> freeSwaps;
    std::vector<u32> pendingSwaps{kNoSwap}; // indexed like `textures`: the latest swap, or kNoSwap
    std::vector<GpuMaterial> materials{1};
    std::vector<GpuEnvironment> environments{1};
    SlotTable meshSlots;
    SlotTable textureSlots;
    SlotTable materialSlots;
    SlotTable environmentSlots;

    // Released resources: those of the current batch, then batches waiting for the GPU. Completed batches are
    // reused with their query and storage, so releasing and collecting do not allocate once warm.
    std::vector<Release> releasing;
    std::vector<ReleaseBatch> batches;
    std::vector<u32> retiring;     // indices in `batches`, oldest first
    std::vector<u32> spareBatches; // indices in `batches`

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

    bool compressedTextures = false; // the device samples BC formats
    bool warnedDecode = false;
    u64 textureBytes = 0;
    u64 textureBytesUncompressed = 0;
    u64 streamingSavedBytes = 0;
    u64 environmentBytes = 0;
    u64 environmentBytesUncompressed = 0;

    void warnDecode()
    {
        if (!std::exchange(warnedDecode, true))
        {
            ILL_LOG_WARN("Render",
                         "the GPU cannot sample BC textures: compressed images are decoded on the CPU "
                         "and take 4 to 8 times the memory");
        }
    }

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
            textureBytes -= textureSizes[release.index].gpu;
            textureBytesUncompressed -= textureSizes[release.index].uncompressed;
            streamingSavedBytes -= textureSizes[release.index].saved;
            textureSizes[release.index] = TextureBytes{};
            textureInfos[release.index] = TextureInfo{};
            pendingSwaps[release.index] = kNoSwap;
            textureSlots.recycle(release.index);
            break;
        case ResourceKind::TextureSwap:
        {
            // Superseded and cancelled swaps (and swaps of released slots) only drop their texture. Swaps
            // retire the texture bound until now through `releasing`, which collect() does not iterate.
            TextureSwap& swap = textureSwaps[release.index];
            if (textureSlots.valid(swap.slot, swap.generation) && pendingSwaps[swap.slot] == release.index)
            {
                const nvrhi::TextureHandle previous = std::exchange(textures[swap.slot], swap.texture);
                bindTexture(swap.slot, swap.texture);
                rebindMaterials(swap.slot, previous, swap.texture);
                pendingSwaps[swap.slot] = kNoSwap;
                releasing.push_back(Release{ResourceKind::ReplacedTexture, swap.retire});
            }
            swap = TextureSwap{};
            freeSwaps.push_back(release.index);
            break;
        }
        case ResourceKind::ReplacedTexture:
        {
            ReplacedTexture& replaced = replacedTextures[release.index];
            textureBytes -= replaced.bytes.gpu;
            textureBytesUncompressed -= replaced.bytes.uncompressed;
            replaced = ReplacedTexture{};
            freeReplaced.push_back(release.index);
            break;
        }
        case ResourceKind::Material:
            materials[release.index] = GpuMaterial{};
            materialSlots.recycle(release.index);
            break;
        case ResourceKind::Environment:
            environmentBytes -= environments[release.index].bytes;
            environmentBytesUncompressed -= environments[release.index].uncompressedBytes;
            environments[release.index] = GpuEnvironment{};
            environmentSlots.recycle(release.index);
            break;
        }
    }

    EnvironmentHandle storeEnvironment(GpuEnvironment&& environment)
    {
        const nvrhi::TextureDesc& background = environment.background->getDesc();
        const nvrhi::TextureDesc& specular = environment.specular->getDesc();
        environment.bytes = cubeBytes(background) + cubeBytes(specular);
        environment.uncompressedBytes = (cubeTexelCount(background.width, background.mipLevels) +
                                         cubeTexelCount(specular.width, specular.mipLevels)) *
                                        sizeof(u64);
        environmentBytes += environment.bytes;
        environmentBytesUncompressed += environment.uncompressedBytes;
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
            u32 index = 0;
            if (!spareBatches.empty())
            {
                index = spareBatches.back();
                spareBatches.pop_back();
                device->resetEventQuery(batches[index].query);
            }
            else
            {
                index = static_cast<u32>(batches.size());
                batches.emplace_back().query = device->createEventQuery();
            }
            ReleaseBatch& batch = batches[index];
            device->setEventQuery(batch.query, nvrhi::CommandQueue::Graphics);
            batch.releases.swap(releasing); // `releasing` keeps the storage of a completed batch
            retiring.push_back(index);
        }
        // Batches complete in order: stop at the first one the GPU has not reached.
        u32 freed = 0;
        usize done = 0;
        while (done < retiring.size() && device->pollEventQuery(batches[retiring[done]].query))
        {
            ReleaseBatch& batch = batches[retiring[done]];
            for (const Release& release : batch.releases)
            {
                free(release);
                ++freed;
            }
            batch.releases.clear();
            spareBatches.push_back(retiring[done]);
            ++done;
        }
        retiring.erase(retiring.begin(), retiring.begin() + static_cast<std::ptrdiff_t>(done));
        return freed;
    }

    // Keeps @p texture and its @p bytes counted until the record is released; returns the record.
    u32 storeReplaced(nvrhi::ITexture* texture, const TextureBytes& bytes)
    {
        u32 index = 0;
        if (!freeReplaced.empty())
        {
            index = freeReplaced.back();
            freeReplaced.pop_back();
        }
        else
        {
            index = static_cast<u32>(replacedTextures.size());
            replacedTextures.emplace_back();
        }
        replacedTextures[index] = ReplacedTexture{texture, TextureBytes{bytes.gpu, bytes.uncompressed, 0}};
        return index;
    }

    u32 storeSwap(const TextureSwap& swap)
    {
        u32 index = 0;
        if (!freeSwaps.empty())
        {
            index = freeSwaps.back();
            freeSwaps.pop_back();
        }
        else
        {
            index = static_cast<u32>(textureSwaps.size());
            textureSwaps.emplace_back();
        }
        textureSwaps[index] = swap;
        return index;
    }

    // Bytes of the levels of @p info resident from @p firstMip on, and of those left out.
    static TextureBytes textureBytesOf(const TextureInfo& info, u32 firstMip) noexcept
    {
        TextureBytes bytes;
        const ImageFormat uncompressed = uncompressedFormat(info.format);
        for (u32 level = 0; level < info.mipCount; ++level)
        {
            const u32 width = std::max(info.width >> level, 1u);
            const u32 height = std::max(info.height >> level, 1u);
            const u64 gpu = imageLevelSize(info.gpuFormat, width, height);
            if (level < firstMip)
            {
                bytes.saved += gpu;
                continue;
            }
            bytes.gpu += gpu;
            bytes.uncompressed += imageLevelSize(uncompressed, width, height);
        }
        return bytes;
    }

    // A texture for the levels [firstMip, mipCount) of @p info, without data.
    nvrhi::TextureHandle makeTexture(const TextureInfo& info, u32 firstMip, const char* debugName) const
    {
        nvrhi::TextureDesc desc;
        desc.width = std::max(info.width >> firstMip, 1u);
        desc.height = std::max(info.height >> firstMip, 1u);
        desc.mipLevels = info.mipCount - firstMip;
        desc.format = formatOf(info.gpuFormat);
        desc.debugName = debugName;
        desc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
        return device->createTexture(desc);
    }

    // Records the upload of levels [begin, end) of @p pixels into @p texture, whose first level is @p
    // firstMip.
    void writeLevels(nvrhi::ITexture* texture, const Image& pixels, u32 firstMip, u32 begin, u32 end)
    {
        for (u32 level = begin; level < end; ++level)
        {
            const ImageMip& mip = pixels.mips[level];
            uploads->writeTexture(texture, 0, level - firstMip, pixels.data.data() + mip.offset,
                                  imageRowPitch(pixels.format, mip.width));
        }
    }

    // Points the bindless slot @p index at @p texture, growing the table when needed.
    void bindTexture(u32 index, nvrhi::ITexture* texture)
    {
        if (!bindless)
        {
            return;
        }
        ILL_CHECK(index < kBindlessTextureCapacity, "too many textures for the bindless table");
        if (bindlessTextures->getCapacity() <= index)
        {
            device->resizeDescriptorTable(
                bindlessTextures,
                std::min(std::max(index + 1, bindlessTextures->getCapacity() * 2), kBindlessTextureCapacity));
        }
        device->writeDescriptorTable(bindlessTextures, nvrhi::BindingSetItem::Texture_SRV(index, texture));
    }

    // Makes the binding sets of the live materials that use texture slot @p index bind @p texture instead of
    // @p previous (the bindless path reads the slot from the material table, which does not change).
    void rebindMaterials(u32 index, nvrhi::ITexture* previous, nvrhi::ITexture* texture)
    {
        for (u32 m = 1; m < materials.size() && m < records.size(); ++m)
        {
            const GpuMaterialRecord& record = records[m];
            const bool uses = record.baseColorTexture == index || record.metallicRoughnessTexture == index ||
                              record.normalTexture == index || record.occlusionTexture == index ||
                              record.emissiveTexture == index;
            GpuMaterial& material = materials[m];
            if (!uses || !materialSlots.slots[m].live || material.bindingSet == nullptr)
            {
                continue;
            }
            nvrhi::BindingSetDesc set = *material.bindingSet->getDesc();
            for (nvrhi::BindingSetItem& item : set.bindings)
            {
                if (item.resourceHandle == previous)
                {
                    item.resourceHandle = texture;
                }
            }
            material.bindingSet = device->createBindingSet(set, materialLayout);
        }
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
        constants.wind = Vec4(std::max(p.windSway, 0.0f), std::max(p.windFlutter, 0.0f), 0.0f, 0.0f);
        // Material files are edited by hand: keep the transmission in [0, 1] and treat NaN as 0.
        const auto unit = [](f32 value) {
            return std::isnan(value) ? 0.0f : std::clamp(value, 0.0f, 1.0f);
        };
        const f32 transmission = unit(p.diffuseTransmission);
        const Vec3& tint = p.diffuseTransmissionColor;
        constants.transmission =
            Vec4(Vec3(unit(tint.x), unit(tint.y), unit(tint.z)) * transmission, transmission);

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
    impl.compressedTextures = renderDevice.gpu().textureCompressionBC;
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
    // Vertex shaders read the wind response from the constants.
    layout.visibility = nvrhi::ShaderType::All;
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
    return createTexture(image, u32{0}, debugName);
}

TextureHandle RenderResources::createTexture(const Image& image, u32 firstMip, const char* debugName)
{
    ILL_PROFILE_SCOPE();
    ILL_CHECK(image.isValid(), "createTexture: invalid image");
    Impl& impl = *m_impl;
    std::optional<Image> decoded;
    const Image* pixels = &image;
    if (isBlockCompressed(image.format) && !impl.compressedTextures)
    {
        impl.warnDecode();
        decoded = decompressImage(image);
        ILL_CHECK(decoded.has_value(), "createTexture: invalid compressed image");
        pixels = &*decoded;
    }
    impl.collect();

    Impl::TextureInfo info;
    info.format = image.format;
    info.gpuFormat = pixels->format;
    info.width = image.width();
    info.height = image.height();
    info.mipCount = static_cast<u32>(image.mips.size());
    info.firstMip = std::min(firstMip, info.mipCount - 1);
    nvrhi::TextureHandle texture = impl.makeTexture(info, info.firstMip, debugName);
    impl.uploads->open();
    impl.writeLevels(texture, *pixels, info.firstMip, info.firstMip, info.mipCount);
    impl.uploads->close();
    impl.device->executeCommandList(impl.uploads);

    const u32 index = impl.textureSlots.acquire();
    Impl::ensureSlot(impl.textures, index);
    Impl::ensureSlot(impl.textureSizes, index);
    Impl::ensureSlot(impl.textureInfos, index);
    Impl::ensureSlot(impl.pendingSwaps, index);
    impl.pendingSwaps[index] = kNoSwap;
    impl.textures[index] = texture;
    impl.textureInfos[index] = info;
    impl.textureSizes[index] = Impl::textureBytesOf(info, info.firstMip);
    impl.textureBytes += impl.textureSizes[index].gpu;
    impl.textureBytesUncompressed += impl.textureSizes[index].uncompressed;
    impl.streamingSavedBytes += impl.textureSizes[index].saved;
    impl.bindTexture(index, texture);
    return TextureHandle{index, impl.textureSlots.slots[index].generation};
}

bool RenderResources::setTextureMips(TextureHandle handle, u32 firstMip, const Image* image)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    if (!impl.textureSlots.valid(handle.index, handle.generation) || handle.index == impl.white.index ||
        handle.index == impl.flatNormal.index)
    {
        return false;
    }
    Impl::TextureInfo& info = impl.textureInfos[handle.index];
    if (image != nullptr && !info.matches(*image))
    {
        return false;
    }
    firstMip = std::min(firstMip, info.mipCount - 1);
    if (firstMip == info.firstMip)
    {
        return true;
    }
    const bool raising = firstMip < info.firstMip;
    if (raising && image == nullptr)
    {
        return false;
    }
    std::optional<Image> decoded;
    const Image* pixels = image;
    if (raising && info.gpuFormat != info.format)
    {
        decoded = decompressImage(*image);
        if (!decoded)
        {
            return false;
        }
        pixels = &*decoded;
    }

    // The newest texture of the slot (a pending one, else the bound one) holds the current range: its writes
    // were submitted before the copies recorded here.
    const u32 pending = impl.pendingSwaps[handle.index];
    const nvrhi::TextureHandle source =
        pending != kNoSwap ? impl.textureSwaps[pending].texture : impl.textures[handle.index];
    const nvrhi::TextureHandle texture =
        impl.makeTexture(info, firstMip, source->getDesc().debugName.c_str());
    impl.uploads->open();
    for (u32 level = std::max(firstMip, info.firstMip); level < info.mipCount; ++level)
    {
        impl.uploads->copyTexture(texture, nvrhi::TextureSlice().setMipLevel(level - firstMip), source,
                                  nvrhi::TextureSlice().setMipLevel(level - info.firstMip));
    }
    if (raising)
    {
        impl.writeLevels(texture, *pixels, firstMip, firstMip, info.firstMip);
    }
    impl.uploads->close();
    impl.device->executeCommandList(impl.uploads);

    // The source keeps counting until it is freed. A superseded pending texture was never bound, so it goes
    // with the work submitted so far; the bound one goes after the swap, with the frames that still use it.
    Impl::TextureBytes& bytes = impl.textureSizes[handle.index];
    const u32 replaced = impl.storeReplaced(source, bytes);
    u32 retire = replaced;
    if (pending != kNoSwap)
    {
        impl.releasing.push_back(Release{ResourceKind::ReplacedTexture, replaced});
        retire = impl.textureSwaps[pending].retire;
    }
    impl.streamingSavedBytes -= bytes.saved;
    bytes = Impl::textureBytesOf(info, firstMip);
    impl.textureBytes += bytes.gpu;
    impl.textureBytesUncompressed += bytes.uncompressed;
    impl.streamingSavedBytes += bytes.saved;
    info.firstMip = firstMip;

    const u32 swap = impl.storeSwap(Impl::TextureSwap{handle.index, handle.generation, texture, retire});
    impl.pendingSwaps[handle.index] = swap;
    impl.releasing.push_back(Release{ResourceKind::TextureSwap, swap});
    return true;
}

u32 RenderResources::textureFirstMip(TextureHandle texture) const noexcept
{
    return isValid(texture) ? m_impl->textureInfos[texture.index].firstMip : 0;
}

u32 RenderResources::textureMipCount(TextureHandle texture) const noexcept
{
    return isValid(texture) ? m_impl->textureInfos[texture.index].mipCount : 0;
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
    material.wind = parts.constants.wind.x > 0.0f || parts.constants.wind.y > 0.0f;
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
    material.wind = parts.constants.wind.x > 0.0f || parts.constants.wind.y > 0.0f;

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
    if (isBlockCompressed(equirect.format))
    {
        const std::optional<Image> pixels = decompressImage(equirect);
        ILL_CHECK(pixels.has_value(), "createEnvironment: invalid compressed image");
        return createEnvironment(*pixels, desc, debugName);
    }
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

GpuAtmosphere packAtmosphere(const AtmosphereDesc& desc) noexcept
{
    const auto unit = [](Vec3 v, Vec3 fallback) {
        const f32 length = glm::length(v);
        return length > 1e-6f && std::isfinite(length) ? v / length : fallback;
    };
    GpuAtmosphere gpu;
    gpu.sun = Vec4(unit(desc.sunDirection, Vec3(0.0f, 1.0f, 0.0f)), std::max(desc.sunRadiance, 0.0f));
    gpu.moon = Vec4(unit(desc.moonDirection, Vec3(0.0f, -1.0f, 0.0f)), std::max(desc.moonRadiance, 0.0f));
    gpu.params = Vec4(std::clamp(desc.turbidity, 1.0f, 10.0f), 0.0f, 0.0f, 0.0f);
    gpu.ground = Vec4(glm::max(desc.groundAlbedo, Vec3(0.0f)), 0.0f);
    return gpu;
}

EnvironmentHandle RenderResources::createAtmosphereEnvironment(u32 cubeSize, u32 specularSize,
                                                               const char* debugName)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    impl.collect();
    return impl.storeEnvironment(impl.baker.createAtmosphere(cubeSize, specularSize, debugName));
}

void RenderResources::updateAtmosphereEnvironment(nvrhi::ICommandList* commands,
                                                  EnvironmentHandle environment,
                                                  const AtmosphereDesc& atmosphere,
                                                  const ShIrradiance& irradiance, u32 sampleCount)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    if (!impl.environmentSlots.valid(environment.index, environment.generation))
    {
        return;
    }
    GpuEnvironment& gpu = impl.environments[environment.index];
    if (gpu.atmosphereSets.empty())
    {
        return;
    }
    impl.baker.redrawAtmosphere(commands, gpu, packAtmosphere(atmosphere), sampleCount);
    gpu.irradiance = irradiance;
}

namespace
{

// Creates an RGBA16F or BC6H cube map for an environment uploaded from a bake.
nvrhi::TextureHandle createEnvironmentCube(nvrhi::IDevice* device, u32 size, u32 mips, nvrhi::Format format,
                                           const char* debugName)
{
    nvrhi::TextureDesc desc;
    desc.dimension = nvrhi::TextureDimension::TextureCube;
    desc.width = size;
    desc.height = size;
    desc.arraySize = 6;
    desc.mipLevels = mips;
    desc.format = format;
    desc.debugName = debugName;
    desc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    return device->createTexture(desc);
}

// Uploads @p data (mip order, then face order, tightly packed rows of texels or blocks) into every
// subresource of @p cube.
void uploadCube(nvrhi::ICommandList* commands, nvrhi::ITexture* cube, const std::byte* data)
{
    const nvrhi::TextureDesc& desc = cube->getDesc();
    for (u32 mip = 0; mip < desc.mipLevels; ++mip)
    {
        const LevelLayout layout = levelLayout(desc.format, std::max(desc.width >> mip, 1u));
        for (u32 slice = 0; slice < 6; ++slice)
        {
            commands->writeTexture(cube, slice, mip, data, layout.rowPitch);
            data += layout.rowPitch * layout.rows;
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

// Reads every subresource of the cube map @p staging into @p target in upload order (see uploadCube()).
bool readCube(nvrhi::IDevice* device, nvrhi::IStagingTexture* staging, std::byte* target)
{
    const nvrhi::TextureDesc& desc = staging->getDesc();
    for (u32 mip = 0; mip < desc.mipLevels; ++mip)
    {
        const LevelLayout layout = levelLayout(desc.format, std::max(desc.width >> mip, 1u));
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
            for (usize row = 0; row < layout.rows; ++row)
            {
                std::memcpy(target, source + row * pitch, layout.rowPitch);
                target += layout.rowPitch;
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
    if (baked.compressed && !impl.compressedTextures)
    {
        impl.warnDecode();
        const std::optional<BakedEnvironment> texels = decompressEnvironment(baked);
        ILL_CHECK(texels.has_value(), "createEnvironment: invalid compressed environment");
        return createEnvironment(*texels, debugName);
    }
    impl.collect();

    const nvrhi::Format format = baked.compressed ? nvrhi::Format::BC6H_UFLOAT : nvrhi::Format::RGBA16_FLOAT;
    GpuEnvironment environment;
    environment.background =
        createEnvironmentCube(impl.device, baked.backgroundSize, baked.backgroundMips, format, debugName);
    environment.specular =
        createEnvironmentCube(impl.device, baked.specularSize, baked.specularMips, format, debugName);
    environment.specularMips = baked.specularMips;
    environment.irradiance = baked.irradiance;
    const auto* background = baked.compressed ? baked.backgroundBlocks.data()
                                              : reinterpret_cast<const std::byte*>(baked.background.data());
    const auto* specular = baked.compressed ? baked.specularBlocks.data()
                                            : reinterpret_cast<const std::byte*>(baked.specular.data());
    impl.uploads->open();
    uploadCube(impl.uploads, environment.background, background);
    uploadCube(impl.uploads, environment.specular, specular);
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
    if (source.background->getDesc().format != nvrhi::Format::RGBA16_FLOAT)
    {
        return 0; // BC6H: the caller already had the blocks
    }
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
    result.background.resize(cubeTexelCount(result.backgroundSize, result.backgroundMips));
    result.specular.resize(cubeTexelCount(result.specularSize, result.specularMips));
    const bool ok =
        readCube(impl.device, found->background, reinterpret_cast<std::byte*>(result.background.data())) &&
        readCube(impl.device, found->specular, reinterpret_cast<std::byte*>(result.specular.data()));
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
        // A pending swap is cancelled: its texture goes with the slot, the bound one with its record.
        if (const u32 pending = impl.pendingSwaps[texture.index]; pending != kNoSwap)
        {
            impl.releasing.push_back(
                Release{ResourceKind::ReplacedTexture, impl.textureSwaps[pending].retire});
            impl.pendingSwaps[texture.index] = kNoSwap;
        }
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
    for (const u32 batch : impl.retiring)
    {
        statistics.retiring += static_cast<u32>(impl.batches[batch].releases.size());
    }
    statistics.materialCapacity = impl.materialCapacity;
    statistics.textures = impl.textureSlots.live;
    statistics.textureBytes = impl.textureBytes;
    statistics.textureBytesUncompressed = impl.textureBytesUncompressed;
    statistics.streamingSavedBytes = impl.streamingSavedBytes;
    statistics.environmentBytes = impl.environmentBytes;
    statistics.environmentBytesUncompressed = impl.environmentBytesUncompressed;
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

bool RenderResources::compressedTextures() const noexcept
{
    return m_impl->compressedTextures;
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
