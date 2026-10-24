#include "Budget.hpp"
#include "Gallery.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/editor/ui/EditorData.hpp>
#include <illusion/editor/ui/FontManager.hpp>
#include <illusion/editor/ui/HeadlessUi.hpp>
#include <illusion/editor/ui/UiContext.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/text/Localization.hpp>

using namespace illusion;
using namespace illusion::editor;

// The CPU cost of a UI frame (ImGui::NewFrame() through ImGui::Render()) of the gallery's widget page, the
// densest page. Budget about 10x the 39 us measured on a desktop (Release, MSVC): see Budget.hpp.

ILL_BENCHMARK("ui/gallery-frame", 400'000.0)
{
    VirtualFileSystem vfs;
    ILL_CHECK(ui::mountEditorData(vfs), "editor-data is missing next to the benchmark");
    auto theme = ui::loadTheme(vfs, "dark");
    ILL_CHECK(theme.hasValue(), "cannot load the dark theme");
    ui::UiContext context;
    auto fonts = ui::FontManager::load(context, vfs, "editor:/fonts");
    ILL_CHECK(fonts.hasValue(), "cannot load the editor fonts");
    {
        ui::HeadlessUi headless(context);
        text::Localization localization;
        gallery::Gallery page(localization, theme.value(), fonts.value().get());
        page.setTheme(theme.value(), 1.0f);
        f64 now = 0.0;
        const auto frame = [&] {
            headless.beginFrame(Vec2(1600.0f, 1000.0f));
            page.draw(now);
            headless.endFrame();
            now += 1.0 / 60.0;
        };
        for (int i = 0; i < 30; ++i)
        {
            frame();
        }
        bench.run("gallery widgets frame", frame);
    }
    fonts.value().reset();
}
