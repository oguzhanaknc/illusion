#include "Budget.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/editor/core/EditorContext.hpp>
#include <illusion/editor/shell/EditorShell.hpp>
#include <illusion/editor/ui/EditorData.hpp>
#include <illusion/editor/ui/FontManager.hpp>
#include <illusion/editor/ui/HeadlessUi.hpp>
#include <illusion/editor/ui/UiContext.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <string>
#include <vector>

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

// An Outliner frame with 100,000 entities in the scene: the cached flat row list and ImGuiListClipper draw
// only the visible rows. Budget: 3 ms per frame (plan §M7m).
ILL_BENCHMARK("editor/outliner-frame-100k", 3'000'000.0)
{
    VirtualFileSystem data;
    ILL_CHECK(ui::mountEditorData(data), "editor-data is missing next to the benchmark");
    ui::UiContext context;
    auto fonts = ui::FontManager::load(context, data, "editor:/fonts");
    ILL_CHECK(fonts.hasValue(), "cannot load the editor fonts");
    {
        ui::HeadlessUi headless(context);
        EditorContext editor(EditorContextOptions{.seed = 1, .crashJournal = false});
        World& world = editor.world();
        GuidGenerator guids(1);
        for (u32 r = 0; r < 1000; ++r)
        {
            const Entity root = world.create(Transform{}, Name{"Root " + std::to_string(r)}, guids.next());
            for (u32 c = 0; c < 99; ++c)
            {
                const Entity child = world.create(LocalTransform{}, Name{"Child"}, guids.next());
                (void)setParent(world, child, root);
            }
        }
        editor.entities().rebuild();
        (void)editor.entities().hierarchy();

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
        for (int i = 0; i < 30; ++i)
        {
            frame();
        }
        bench.run("outliner frame (100k entities, clipped)", frame);
    }
    fonts.value().reset();
}

// Rebuilding the Outliner's flat row list over 10,000 entities in a hierarchy (100 roots with 99 children
// each), per entity. Budget: 200 ns per entity (plan §M7m).
ILL_BENCHMARK("editor/outliner-rebuild", 200.0)
{
    VirtualFileSystem data;
    ILL_CHECK(ui::mountEditorData(data), "editor-data is missing next to the benchmark");
    ui::UiContext context;
    auto fonts = ui::FontManager::load(context, data, "editor:/fonts");
    ILL_CHECK(fonts.hasValue(), "cannot load the editor fonts");
    {
        ui::HeadlessUi headless(context);
        EditorContext editor(EditorContextOptions{.seed = 2, .crashJournal = false});
        World& world = editor.world();
        GuidGenerator guids(2);
        for (u32 r = 0; r < 100; ++r)
        {
            const Entity root = world.create(Transform{}, Name{"Root " + std::to_string(r)}, guids.next());
            for (u32 c = 0; c < 99; ++c)
            {
                const Entity child = world.create(LocalTransform{}, Name{"Child"}, guids.next());
                (void)setParent(world, child, root);
            }
        }
        editor.entities().rebuild();
        (void)editor.entities().hierarchy();

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
        for (int i = 0; i < 30; ++i)
        {
            frame();
        }
        bench.batch(10'000).unit("entity").run("outliner rebuild (10000 entities)", [&] {
            world.markChanged<Name>();
            frame();
        });
    }
    fonts.value().reset();
}

// Drawing the shell with an entity selected in the Inspector (entity header, Local/World Transform card and
// Add Component button). Budget: 200 us per frame (plan §M7m).
ILL_BENCHMARK("editor/inspector-transform", 200'000.0)
{
    VirtualFileSystem data;
    ILL_CHECK(ui::mountEditorData(data), "editor-data is missing next to the benchmark");
    ui::UiContext context;
    auto fonts = ui::FontManager::load(context, data, "editor:/fonts");
    ILL_CHECK(fonts.hasValue(), "cannot load the editor fonts");
    {
        ui::HeadlessUi headless(context);
        EditorContext editor(EditorContextOptions{.seed = 3, .crashJournal = false});
        World& world = editor.world();
        const EntityGuid guid{1};
        (void)world.create(Transform{}, Name{"Target"}, guid);
        editor.entities().rebuild();
        editor.selection().set(std::vector<EntityGuid>{guid});

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
        for (int i = 0; i < 60; ++i)
        {
            frame();
        }
        bench.run("inspector transform frame", frame);
    }
    fonts.value().reset();
}
