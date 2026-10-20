#pragma once

// Shared setup of the editor UI tests: editor-data mounted, a theme, fonts, a localization and the
// gallery, run headless (see HeadlessUi) or with a real renderer (GPU tests).

#include "Gallery.hpp"
#include "HeadlessUi.hpp"

#include <illusion/editor/ui/FontManager.hpp>
#include <illusion/editor/ui/Theme.hpp>
#include <illusion/editor/ui/UiContext.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/text/Localization.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <string_view>

namespace illusion::test
{

namespace gallery = editor::gallery;
namespace ui = editor::ui;

/// Default size of headless test frames.
inline constexpr Vec2 kFrameSize{1280.0f, 800.0f};

/// Mounts editor-data and loads a theme by name.
inline ui::Theme loadTestTheme(VirtualFileSystem& vfs, std::string_view name)
{
    if (!vfs.isMounted("editor"))
    {
        REQUIRE(gallery::mountEditorData(vfs));
    }
    auto theme = gallery::loadTheme(vfs, name);
    REQUIRE_MESSAGE(theme.hasValue(), (theme ? std::string() : theme.error()));
    return std::move(theme.value());
}

/// Which renderer a GalleryFixture uses.
enum class FixtureRenderer : u8
{
    Headless, ///< HeadlessUi answers texture requests; frame() runs whole frames.
    External, ///< The test attaches a renderer (UiRenderer) and runs frames itself.
};

/// The gallery with everything it needs.
struct GalleryFixture
{
    VirtualFileSystem vfs;
    ui::Theme theme;
    ui::UiContext context;
    std::unique_ptr<ui::FontManager> fonts;
    std::unique_ptr<gallery::HeadlessUi> headless;
    text::Localization localization;
    std::unique_ptr<gallery::Gallery> galleryPage;
    f64 time = 0.0;

    explicit GalleryFixture(std::string_view themeName = "dark", f32 scale = 1.0f,
                            FixtureRenderer renderer = FixtureRenderer::Headless)
        : theme(loadTestTheme(vfs, themeName))
    {
        auto loaded = ui::FontManager::load(context, vfs, "editor:/fonts");
        REQUIRE_MESSAGE(loaded.hasValue(), (loaded ? std::string() : loaded.error()));
        fonts = std::move(loaded.value());
        fonts->setScale(scale, 1.0f);
        if (renderer == FixtureRenderer::Headless)
        {
            headless = std::make_unique<gallery::HeadlessUi>(context);
        }
        galleryPage = std::make_unique<gallery::Gallery>(localization, theme, fonts.get());
        galleryPage->setTheme(theme, scale);
    }

    ~GalleryFixture()
    {
        galleryPage.reset();
        headless.reset();
        fonts.reset();
    }

    GalleryFixture(const GalleryFixture&) = delete;
    GalleryFixture& operator=(const GalleryFixture&) = delete;

    /// Runs one gallery frame (FixtureRenderer::Headless only).
    void frame()
    {
        REQUIRE(headless != nullptr);
        headless->beginFrame(kFrameSize);
        galleryPage->draw(time);
        headless->endFrame();
        time += 1.0 / 60.0;
    }

    /// Switches the language and applies it.
    void setLocale(std::string_view tag)
    {
        const auto locale = text::LocaleId::parse(tag);
        REQUIRE(locale.has_value());
        localization.setLocale(*locale);
        localization.applyPendingLocale();
    }
};

} // namespace illusion::test
