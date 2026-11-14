#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/Font.hpp>
#include <illusion/assets/Image.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/render/RenderResources.hpp>
#include <illusion/render/UiRenderAssets.hpp>
#include <illusion/render/UiRenderer.hpp>

#include <algorithm>
#include <bit>
#include <string_view>
#include <vector>

namespace illusion
{
namespace
{

constexpr u32 kNoEntry = ~0u;

enum class EntryState : u8
{
    Loading,
    Ready,
    Failed,
};

// Open addressing by AssetId over entry indices, sized for at least twice the entries it can hold.
class IdTable
{
public:
    explicit IdTable(u32 capacity)
        : m_slots(std::bit_ceil(std::max<usize>(usize{capacity} * 2, 2)), Slot{})
        , m_mask(m_slots.size() - 1)
    {
    }

    [[nodiscard]] u32 find(AssetId id) const noexcept
    {
        for (usize i = id.value & m_mask;; i = (i + 1) & m_mask)
        {
            const Slot& slot = m_slots[i];
            if (slot.entry == kNoEntry || slot.id == id.value)
            {
                return slot.entry;
            }
        }
    }

    void insert(AssetId id, u32 entry) noexcept
    {
        usize i = id.value & m_mask;
        while (m_slots[i].entry != kNoEntry)
        {
            i = (i + 1) & m_mask;
        }
        m_slots[i] = Slot{id.value, entry};
    }

private:
    struct Slot
    {
        u64 id = 0;
        u32 entry = kNoEntry;
    };

    std::vector<Slot> m_slots;
    usize m_mask = 0;
};

struct FontEntry
{
    AssetHandle<Font> handle;
    UiTextureId atlas{};
    EntryState state = EntryState::Loading;
};

struct ImageEntry
{
    AssetHandle<Image> handle; // dropped once the image is on the GPU
    TextureHandle texture{};
    UiTextureId id{};
    Vec2 size{0.0f};
    EntryState state = EntryState::Loading;
};

} // namespace

struct UiRenderAssets::Impl
{
    Impl(AssetStreamer& assetStreamer, RenderResources& renderResources, UiRenderer& uiRenderer,
         const UiRenderAssetsDesc& assetsDesc)
        : streamer(assetStreamer)
        , resources(renderResources)
        , renderer(uiRenderer)
        , desc(assetsDesc)
        , fontIds(assetsDesc.maxFonts)
        , imageIds(assetsDesc.maxImages)
    {
        fonts.reserve(desc.maxFonts);
        images.reserve(desc.maxImages);
    }

    ~Impl()
    {
        for (const FontEntry& entry : fonts)
        {
            if (entry.atlas.value != 0)
            {
                renderer.remove(entry.atlas);
            }
        }
        for (const ImageEntry& entry : images)
        {
            if (entry.id.value != 0)
            {
                renderer.remove(entry.id);
            }
            if (entry.texture.index != 0)
            {
                resources.releaseTexture(entry.texture);
            }
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    // Logs that @p path cannot be shown, once per asset (callers mark the entry failed).
    static void fail(std::string_view kind, AssetId id, std::string_view why)
    {
        ILL_LOG_WARN("Ui", "cannot show the {} '{}': {}", kind, AssetPathTable::instance().path(id), why);
    }

    void fontReady(FontEntry& entry, AssetId id)
    {
        if (entry.handle.state() == AssetState::Failed)
        {
            fail("font", id, entry.handle.error());
            entry.state = EntryState::Failed;
            return;
        }
        if (const Font* font = entry.handle.get())
        {
            entry.atlas = renderer.addFont(*font);
            entry.state = entry.atlas.value != 0 ? EntryState::Ready : EntryState::Failed;
            if (entry.state == EntryState::Failed)
            {
                fail("font", id,
                     font->empty() ? "it has no glyphs" : "the UI renderer has no free texture slot");
            }
        }
    }

    void imageReady(ImageEntry& entry, AssetId id)
    {
        if (entry.handle.state() == AssetState::Failed)
        {
            fail("image", id, entry.handle.error());
            entry.state = EntryState::Failed;
            entry.handle = {};
            return;
        }
        const Image* image = entry.handle.get();
        if (image == nullptr)
        {
            return;
        }
        if (!image->isValid() || image->mips.empty())
        {
            fail("image", id, "it has no pixels");
            entry.state = EntryState::Failed;
            entry.handle = {};
            return;
        }
        entry.texture = resources.createTexture(*image, "UI image");
        entry.id = entry.texture.index != 0 ? renderer.addImage(entry.texture) : UiTextureId{};
        entry.size = Vec2(static_cast<f32>(image->mips[0].width), static_cast<f32>(image->mips[0].height));
        entry.state = entry.id.value != 0 ? EntryState::Ready : EntryState::Failed;
        if (entry.state == EntryState::Failed)
        {
            fail("image", id, "it cannot be uploaded or the UI renderer has no free texture slot");
        }
        entry.handle = {};
    }

    UiFontFace font(const AssetRef& ref)
    {
        if (ref.empty())
        {
            return {};
        }
        u32 index = fontIds.find(ref.id);
        if (index == kNoEntry)
        {
            if (fonts.size() >= desc.maxFonts)
            {
                if (!fontsFull)
                {
                    fail("font", ref.id, "UiRenderAssetsDesc::maxFonts fonts are in use");
                    fontsFull = true;
                }
                return {};
            }
            index = static_cast<u32>(fonts.size());
            FontEntry& entry = fonts.emplace_back();
            fontIds.insert(ref.id, index);
            const std::string_view path = ref.path();
            if (path.empty())
            {
                fail("font", ref.id, "its path is unknown");
                entry.state = EntryState::Failed;
                return {};
            }
            entry.handle = streamer.loadFont(path);
        }
        FontEntry& entry = fonts[index];
        if (entry.state == EntryState::Loading)
        {
            fontReady(entry, ref.id);
        }
        return entry.state == EntryState::Ready ? UiFontFace{entry.handle.get(), entry.atlas} : UiFontFace{};
    }

    UiImageSource image(const AssetRef& ref)
    {
        if (ref.empty())
        {
            return {};
        }
        u32 index = imageIds.find(ref.id);
        if (index == kNoEntry)
        {
            if (images.size() >= desc.maxImages)
            {
                if (!imagesFull)
                {
                    fail("image", ref.id, "UiRenderAssetsDesc::maxImages images are in use");
                    imagesFull = true;
                }
                return {};
            }
            index = static_cast<u32>(images.size());
            ImageEntry& entry = images.emplace_back();
            imageIds.insert(ref.id, index);
            const std::string_view path = ref.path();
            if (path.empty())
            {
                fail("image", ref.id, "its path is unknown");
                entry.state = EntryState::Failed;
                return {};
            }
            entry.handle = streamer.loadImage(path);
        }
        ImageEntry& entry = images[index];
        if (entry.state == EntryState::Loading)
        {
            imageReady(entry, ref.id);
        }
        return entry.state == EntryState::Ready ? UiImageSource{entry.id, entry.size} : UiImageSource{};
    }

    AssetStreamer& streamer;
    RenderResources& resources;
    UiRenderer& renderer;
    UiRenderAssetsDesc desc;
    IdTable fontIds;
    IdTable imageIds;
    std::vector<FontEntry> fonts;
    std::vector<ImageEntry> images;
    bool fontsFull = false;
    bool imagesFull = false;
};

UiRenderAssets::UiRenderAssets(AssetStreamer& streamer, RenderResources& resources, UiRenderer& renderer,
                               const UiRenderAssetsDesc& desc)
    : m_impl(std::make_unique<Impl>(streamer, resources, renderer, desc))
{
}

UiRenderAssets::~UiRenderAssets() = default;

UiFontFace UiRenderAssets::font(const AssetRef& font)
{
    return m_impl->font(font);
}

UiImageSource UiRenderAssets::image(const AssetRef& image)
{
    return m_impl->image(image);
}

UiRenderAssetsStatistics UiRenderAssets::statistics() const noexcept
{
    UiRenderAssetsStatistics statistics;
    for (const FontEntry& entry : m_impl->fonts)
    {
        statistics.fonts += entry.state == EntryState::Ready ? 1u : 0u;
        statistics.loading += entry.state == EntryState::Loading ? 1u : 0u;
        statistics.failed += entry.state == EntryState::Failed ? 1u : 0u;
    }
    for (const ImageEntry& entry : m_impl->images)
    {
        statistics.images += entry.state == EntryState::Ready ? 1u : 0u;
        statistics.loading += entry.state == EntryState::Loading ? 1u : 0u;
        statistics.failed += entry.state == EntryState::Failed ? 1u : 0u;
    }
    return statistics;
}

} // namespace illusion
