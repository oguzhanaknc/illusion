#include "Budget.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/editor/core/EditorContext.hpp>
#include <illusion/editor/shell/EditorShell.hpp>
#include <illusion/editor/ui/EditorData.hpp>
#include <illusion/editor/ui/FontManager.hpp>
#include <illusion/editor/ui/HeadlessUi.hpp>
#include <illusion/editor/ui/UiContext.hpp>
#include <illusion/platform/FileSystem.hpp>

using namespace illusion;
using namespace illusion::editor;

// The CPU cost of an idle frame of the editor shell at 1920x1080 (preferences check, menu bar, toolbar,
// status bar, dock space and the panels of the default layout, ImGui::NewFrame() through ImGui::Render()).
// Budget about 9x the 32 us measured on a desktop (Release, MSVC), well inside the plan's 2 ms ceiling.

ILL_BENCHMARK("editor/shell-idle-frame", 300'000.0)
{
    VirtualFileSystem data;
    ILL_CHECK(ui::mountEditorData(data), "editor-data is missing next to the benchmark");
    ui::UiContext context;
    auto fonts = ui::FontManager::load(context, data, "editor:/fonts");
    ILL_CHECK(fonts.hasValue(), "cannot load the editor fonts");
    {
        ui::HeadlessUi headless(context);
        EditorContext editor(EditorContextOptions{.seed = 1});
        shell::ShellOptions options;
        options.editorData = &data;
        options.fonts = fonts.value().get();
        shell::EditorShell shell(editor, context, std::move(options));
        f64 now = 0.0;
        const auto frame = [&] {
            shell.beginFrame(1.0f);
            headless.beginFrame(Vec2(1920.0f, 1080.0f));
            shell.draw(now, 1.0 / 60.0);
            headless.endFrame();
            now += 1.0 / 60.0;
        };
        for (int i = 0; i < 120; ++i)
        {
            frame();
        }
        bench.run("shell idle frame", frame);
    }
    fonts.value().reset();
}
