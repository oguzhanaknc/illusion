#pragma once

// The editor shell on a fixture project, run headless (HeadlessUi) at 1280x720, for the shell's UiDriver
// tests, its golden images and its benchmark.

#include "core/EditorCoreTestSupport.hpp"

#include <illusion/editor/shell/EditorShell.hpp>
#include <illusion/editor/ui/EditorData.hpp>
#include <illusion/editor/ui/FontManager.hpp>
#include <illusion/editor/ui/HeadlessUi.hpp>
#include <illusion/editor/ui/UiContext.hpp>
#include <illusion/editor/ui/UiDriver.hpp>

#include <doctest/doctest.h>

#include <imgui.h>
#include <memory>

namespace illusion::test
{

/// Size of the shell's test frames (the size of its golden images).
inline constexpr Vec2 kShellFrameSize{1280.0f, 720.0f};

/// What a ShellFixture starts with.
struct ShellSetup
{
    bool project = true;               ///< Open the fixture project (otherwise the Welcome window shows).
    bool headless = true;              ///< Answer texture requests with HeadlessUi (false: the test renders).
    bool canCapture = false;           ///< The shell can take screenshots.
    std::filesystem::path user{};      ///< User directory (empty: a fresh one in the fixture's directory).
    std::filesystem::path workspace{}; ///< Workspace of projects (empty: the fixture's directory).
};

/// The shell with everything it needs.
struct ShellFixture
{
    TempDirectory directory;
    std::filesystem::path user;
    VirtualFileSystem data;
    editor::ui::UiContext context;
    std::unique_ptr<editor::ui::FontManager> fonts;
    std::unique_ptr<editor::ui::HeadlessUi> headless;
    std::unique_ptr<editor::EditorContext> editor;
    std::unique_ptr<editor::shell::EditorShell> shell;
    f64 time = 0.0;

    explicit ShellFixture(const ShellSetup& setup = {})
        : user(setup.user.empty() ? directory.path / "user" : setup.user)
    {
        reflectEditorTestComponents();
        REQUIRE(editor::ui::mountEditorData(data));
        auto loaded = editor::ui::FontManager::load(context, data, "editor:/fonts");
        REQUIRE_MESSAGE(loaded.hasValue(), (loaded ? std::string() : loaded.error()));
        fonts = std::move(loaded).value();
        if (setup.headless)
        {
            headless = std::make_unique<editor::ui::HeadlessUi>(context);
        }
        editor::EditorContextOptions options;
        options.seed = 1;
        options.workspace = setup.workspace.empty() ? directory.path : setup.workspace;
        options.userDirectory = user;
        editor = std::make_unique<editor::EditorContext>(options);
        if (setup.project)
        {
            Expected<editor::Project, std::string> project = editor::Project::create(
                directory.path / "project", "Test", editorDataDirectory() / "templates" / "empty");
            REQUIRE_MESSAGE(project.hasValue(), (project ? std::string() : project.error()));
            const Expected<void, editor::CommandError> opened =
                editor->openProject(directory.path / "project");
            REQUIRE(opened.hasValue());
        }
        editor::shell::ShellOptions shellOptions;
        shellOptions.editorData = &data;
        shellOptions.fonts = fonts.get();
        shellOptions.userDirectory = user;
        shellOptions.canCapture = setup.canCapture;
        shellOptions.version = "0.0.0-test";
        shell = std::make_unique<editor::shell::EditorShell>(*editor, context, std::move(shellOptions));
    }

    ~ShellFixture()
    {
        shell.reset();
        editor.reset();
        headless.reset();
        fonts.reset();
    }

    ShellFixture(const ShellFixture&) = delete;
    ShellFixture& operator=(const ShellFixture&) = delete;

    /// Starts a frame: preferences and layouts, then ImGui::NewFrame() (headless) at the test size.
    void beginFrame()
    {
        context.makeCurrent();
        shell->beginFrame(1.0f);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(kShellFrameSize.x, kShellFrameSize.y);
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
    }

    /// Runs one whole frame (headless fixtures only).
    void frame()
    {
        REQUIRE(headless != nullptr);
        shell->beginFrame(1.0f);
        headless->beginFrame(kShellFrameSize);
        shell->draw(time, 1.0 / 60.0);
        headless->endFrame();
        editor->endFrame(1.0 / 60.0);
        time += 1.0 / 60.0;
    }
};

} // namespace illusion::test
