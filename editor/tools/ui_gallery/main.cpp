// illusion_ui_gallery: the editor's widget catalogue, typography and icon sheet, and an editor-style
// layout, for design reviews and screenshots.
//
// Usage: illusion_ui_gallery [options]
//   --theme NAME      dark (default), light or high-contrast
//   --scale S         UI scale, 0.75 to 2 (default 1)
//   --locale TAG      en (default), en-XA (pseudo-locale) or any locale with a file in editor-data/locales
//   --page NAME       widgets (default), typography or editor
//   --headless        render offscreen without a window (needs --screenshot)
//   --screenshot PNG  write the frame to PNG after --frames frames and exit
//   --size WxH        size of the headless frame in pixels (default 1600x1000, multiplied by the scale)
//   --frames N        frames before the screenshot (default 12) or before exiting (windowed)
// Windowed: F2 cycles the theme, F3 the page, F4 the locale (en, en-XA). Exit code 2 on validation errors.

#include "Gallery.hpp"

#include <illusion/assets/Image.hpp>
#include <illusion/core/EventBus.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Time.hpp>
#include <illusion/editor/EditorStrings.hpp>
#include <illusion/editor/ui/EditorData.hpp>
#include <illusion/editor/ui/FontManager.hpp>
#include <illusion/editor/ui/OffscreenUi.hpp>
#include <illusion/editor/ui/UiContext.hpp>
#include <illusion/editor/ui/UiPlatformBackend.hpp>
#include <illusion/editor/ui/UiRenderer.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/platform/Platform.hpp>
#include <illusion/platform/Window.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/Swapchain.hpp>
#include <illusion/text/Localization.hpp>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <imgui.h>
#include <string>
#include <string_view>

using namespace illusion;
using namespace illusion::editor;

namespace
{

struct Options
{
    std::string theme = "dark";
    f32 scale = 1.0f;
    std::string locale = "en";
    gallery::GalleryPage page = gallery::GalleryPage::Widgets;
    bool headless = false;
    std::string screenshot;
    UVec2 size{1600, 1000};
    u64 frames = 0;
};

template <typename T>
bool parseNumber(std::string_view text, T& out)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
    return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

// Parses a UI scale with at most two decimals ("1.5", "0.75"). Apple's libc++ has no floating-point
// std::from_chars, so the parts are read as integers.
bool parseScale(std::string_view text, f32& out)
{
    const usize dot = text.find('.');
    u32 whole = 0;
    if (!parseNumber(text.substr(0, dot), whole))
    {
        return false;
    }
    if (dot == std::string_view::npos)
    {
        out = static_cast<f32>(whole);
        return true;
    }
    const std::string_view fraction = text.substr(dot + 1);
    u32 digits = 0;
    if (fraction.empty() || fraction.size() > 2 || !parseNumber(fraction, digits))
    {
        return false;
    }
    out = static_cast<f32>(whole) + static_cast<f32>(digits) / (fraction.size() == 1 ? 10.0f : 100.0f);
    return true;
}

bool parseOptions(int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--headless")
        {
            options.headless = true;
        }
        else if (arg == "--theme" && hasValue)
        {
            options.theme = argv[++i];
        }
        else if (arg == "--scale" && hasValue)
        {
            if (!parseScale(argv[++i], options.scale) || options.scale < 0.75f || options.scale > 2.0f)
            {
                ILL_LOG_ERROR("Gallery", "--scale takes a number from 0.75 to 2");
                return false;
            }
        }
        else if (arg == "--locale" && hasValue)
        {
            options.locale = argv[++i];
        }
        else if (arg == "--page" && hasValue)
        {
            const std::string_view page = argv[++i];
            if (page == "widgets")
            {
                options.page = gallery::GalleryPage::Widgets;
            }
            else if (page == "typography")
            {
                options.page = gallery::GalleryPage::Typography;
            }
            else if (page == "editor")
            {
                options.page = gallery::GalleryPage::Editor;
            }
            else
            {
                ILL_LOG_ERROR("Gallery", "unknown page '{}'", page);
                return false;
            }
        }
        else if (arg == "--screenshot" && hasValue)
        {
            options.screenshot = argv[++i];
        }
        else if (arg == "--size" && hasValue)
        {
            const std::string_view size = argv[++i];
            const usize x = size.find('x');
            if (x == std::string_view::npos || !parseNumber(size.substr(0, x), options.size.x) ||
                !parseNumber(size.substr(x + 1), options.size.y) || options.size.x == 0 ||
                options.size.y == 0)
            {
                ILL_LOG_ERROR("Gallery", "--size takes WIDTHxHEIGHT");
                return false;
            }
        }
        else if (arg == "--frames" && hasValue)
        {
            if (!parseNumber(argv[++i], options.frames))
            {
                ILL_LOG_ERROR("Gallery", "--frames takes a number");
                return false;
            }
        }
        else
        {
            ILL_LOG_ERROR("Gallery", "unknown option '{}' (see the comment at the top of main.cpp)", arg);
            return false;
        }
    }
    if (options.headless && options.screenshot.empty())
    {
        ILL_LOG_ERROR("Gallery", "--headless needs --screenshot");
        return false;
    }
    return true;
}

bool setLocale(text::Localization& localization, std::string_view tag)
{
    const std::optional<text::LocaleId> locale = text::LocaleId::parse(tag);
    if (!locale)
    {
        ILL_LOG_ERROR("Gallery", "'{}' is not a locale tag", tag);
        return false;
    }
    localization.setLocale(*locale);
    localization.applyPendingLocale();
    return true;
}

bool writeFile(const std::string& path, std::span<const std::byte> bytes)
{
    std::ofstream file(utf8Path(path), std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return file.good();
}

int runHeadless(const Options& options, gallery::Gallery& galleryPage, ui::UiContext& context)
{
    RenderDeviceDesc deviceDesc;
    deviceDesc.headless = true;
    auto device = RenderDevice::create(deviceDesc);
    if (!device)
    {
        return 1;
    }
    const UVec2 size(static_cast<u32>(static_cast<f32>(options.size.x) * options.scale),
                     static_cast<u32>(static_cast<f32>(options.size.y) * options.scale));
    auto offscreen = ui::OffscreenUi::create(*device, context, size);
    if (!offscreen)
    {
        return 1;
    }
    const u64 frames = std::max<u64>(options.frames, 1);
    // Screenshots show the sample toast too.
    galleryPage.showToast(0.0);
    std::optional<Image> image;
    for (u64 frame = 0; frame < frames; ++frame)
    {
        constexpr f64 kDelta = 1.0 / 60.0;
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<f32>(size.x), static_cast<f32>(size.y));
        io.DeltaTime = static_cast<f32>(kDelta);
        ImGui::NewFrame();
        galleryPage.draw(static_cast<f64>(frame) * kDelta);
        ImGui::Render();
        image = offscreen->render(ImGui::GetDrawData(), Vec4(0.0f, 0.0f, 0.0f, 1.0f));
        if (!image)
        {
            ILL_LOG_ERROR("Gallery", "cannot read the frame back");
            return 1;
        }
    }
    if (!writeFile(options.screenshot, encodePng(*image)))
    {
        ILL_LOG_ERROR("Gallery", "cannot write '{}'", options.screenshot);
        return 1;
    }
    device->waitIdle();
    const u32 errors = device->validationErrorCount();
    ILL_LOG_INFO("Gallery", "wrote {} ({}x{}), {} validation error(s)", options.screenshot, size.x, size.y,
                 errors);
    return errors == 0 ? 0 : 2;
}

int runWindowed(const Options& options, gallery::Gallery& galleryPage, ui::UiContext& context,
                ui::FontManager& fonts, const VirtualFileSystem& vfs, text::Localization& localization,
                ui::Theme& theme)
{
    EventBus events;
    auto platform = Platform::create();
    if (!platform)
    {
        return 1;
    }
    WindowDesc windowDesc;
    windowDesc.title = galleryPage.text().format(strings::kGalleryTitle);
    windowDesc.width = 1600;
    windowDesc.height = 1000;
    auto window = Window::create(*platform, windowDesc, &events);
    auto device = window ? RenderDevice::create() : nullptr;
    auto swapchain = device ? Swapchain::create(*device, *window) : nullptr;
    if (!swapchain)
    {
        return 1;
    }
    ui::UiPlatformBackend backend(context, *window, events);
    auto renderer = ui::UiRenderer::create(*device, context);
    if (!renderer)
    {
        return 1;
    }
    nvrhi::IDevice* gpu = device->device();
    nvrhi::CommandListHandle commands = gpu->createCommandList();
    nvrhi::TextureHandle uiTarget;
    nvrhi::FramebufferHandle uiFramebuffer;

    const auto applyScale = [&] {
        const f32 contentScale = backend.contentScale().x;
        fonts.setScale(options.scale, contentScale);
        galleryPage.setTheme(theme, options.scale * contentScale);
    };
    applyScale();

    usize themeIndex = 0;
    for (usize i = 0; i < ui::kThemeNames.size(); ++i)
    {
        if (ui::kThemeNames[i] == options.theme)
        {
            themeIndex = i;
        }
    }
    bool pseudo = options.locale == "en-XA";

    FrameClock clock;
    u64 frames = 0;
    while (!window->shouldClose() && (options.frames == 0 || frames < options.frames))
    {
        platform->pollEvents();
        events.dispatchQueued();
        const FrameTime& time = clock.tick(monotonicSeconds());
        if (window->input().wasPressed(Key::F2))
        {
            themeIndex = (themeIndex + 1) % ui::kThemeNames.size();
            if (auto next = ui::loadTheme(vfs, ui::kThemeNames[themeIndex]))
            {
                theme = std::move(next.value());
                applyScale();
            }
        }
        if (window->input().wasPressed(Key::F3))
        {
            galleryPage.setPage(
                static_cast<gallery::GalleryPage>((static_cast<int>(galleryPage.page()) + 1) % 3));
        }
        if (window->input().wasPressed(Key::F4))
        {
            pseudo = !pseudo;
            (void)setLocale(localization, pseudo ? "en-XA" : "en");
        }
        if (backend.consumeContentScaleChange())
        {
            applyScale();
        }

        backend.newFrame(time.deltaSeconds);
        ImGui::NewFrame();
        galleryPage.draw(time.totalSeconds);
        ImGui::Render();

        if (swapchain->beginFrame())
        {
            const UVec2 size = swapchain->size();
            if (!uiTarget || uiTarget->getDesc().width != size.x || uiTarget->getDesc().height != size.y)
            {
                nvrhi::TextureDesc desc;
                desc.width = size.x;
                desc.height = size.y;
                desc.format = nvrhi::Format::RGBA8_UNORM;
                desc.isRenderTarget = true;
                desc.isShaderResource = true;
                desc.debugName = "UI target";
                desc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
                uiTarget = gpu->createTexture(desc);
                uiFramebuffer = gpu->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(uiTarget));
            }
            commands->open();
            commands->clearTextureFloat(uiTarget, nvrhi::AllSubresources,
                                        nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
            renderer->render(commands, uiFramebuffer, ImGui::GetDrawData());
            renderer->present(commands, uiTarget, swapchain->currentFramebuffer());
            commands->close();
            gpu->executeCommandList(commands);
            swapchain->present();
        }
        ++frames;
    }
    device->waitIdle();
    const u32 errors = device->validationErrorCount();
    ILL_LOG_INFO("Gallery", "rendered {} frames, {} validation error(s)", frames, errors);
    return errors == 0 ? 0 : 2;
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!parseOptions(argc, argv, options))
    {
        return 1;
    }
    if (options.headless && options.frames == 0)
    {
        options.frames = 12;
    }

    VirtualFileSystem vfs;
    if (!ui::mountEditorData(vfs))
    {
        return 1;
    }
    auto theme = ui::loadTheme(vfs, options.theme);
    if (!theme)
    {
        ILL_LOG_ERROR("Gallery", "{}", theme.error());
        return 1;
    }

    ui::UiContext context;
    auto fonts = ui::FontManager::load(context, vfs, "editor:/fonts");
    if (!fonts)
    {
        ILL_LOG_ERROR("Gallery", "{}", fonts.error());
        return 1;
    }
    text::Localization localization;
    gallery::Gallery galleryPage(localization, theme.value(), fonts.value().get());
    (void)ui::loadEditorLocales(vfs, localization);
    if (!setLocale(localization, options.locale))
    {
        return 1;
    }
    galleryPage.setPage(options.page);

    int result = 0;
    if (options.headless)
    {
        fonts.value()->setScale(options.scale, 1.0f);
        galleryPage.setTheme(theme.value(), options.scale);
        result = runHeadless(options, galleryPage, context);
    }
    else
    {
        result = runWindowed(options, galleryPage, context, *fonts.value(), vfs, localization, theme.value());
    }
    // The renderer released its textures; the fonts go before the context.
    fonts.value().reset();
    return result;
}
