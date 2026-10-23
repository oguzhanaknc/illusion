#include "Budget.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/editor/core/CoreCommands.hpp>
#include <illusion/editor/core/EditorContext.hpp>
#include <illusion/editor/core/ReferenceIndex.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <filesystem>
#include <format>
#include <random>
#include <string>

using namespace illusion;
using namespace illusion::editor;

namespace
{

Entity createEntity(EditorContext& editor, std::string name, Entity parent)
{
    CreateEntityOptions options;
    options.name = std::move(name);
    options.parent = parent;
    static const JsonDocument components =
        std::move(JsonDocument::parse(R"({"illusion.Transform":{"position":[1,2,3]}})").value());
    options.components = components.root();
    Expected<Entity, CommandError> entity = editor.editor().create(options);
    ILL_CHECK(entity.hasValue(), "cannot create a benchmark entity");
    return *entity;
}

} // namespace

// One component.set through the typed command path: validation, capture before and after, the transaction,
// an undo step merged by its key and change records. Budget about 6x the 3.2 us measured on a desktop
// (Release, MSVC): see Budget.hpp.
ILL_BENCHMARK("editor/component-set", 20'000.0)
{
    EditorContext editor(EditorContextOptions{.seed = 1, .crashJournal = false});
    createEntity(editor, "Target", Entity{});
    ComponentSetParams params[2];
    for (int i = 0; i < 2; ++i)
    {
        params[i].entity = "name:Target";
        params[i].type = "illusion.Transform";
        params[i].value.text = i == 0 ? R"({"position":[4,5,6]})" : R"({"position":[1,2,3]})";
    }
    CommandOptions options;
    options.mergeKey = "drag";
    usize next = 0;
    bench.run("component.set (Transform, merged)", [&] {
        Expected<ComponentChangeResult, CommandError> result =
            editor.execute<ComponentChangeResult>("component.set", params[next++ & 1], options);
        ankerl::nanobench::doNotOptimizeAway(result);
    });
}

// Destroying a subtree of 100 entities in a scene of 1,000 (snapshots, reference scan, sibling records) and
// undoing it, per destroyed entity. Budget about 6x the 6.1 us measured on a desktop.
ILL_BENCHMARK("editor/undo-capture-delete", 40'000.0)
{
    EditorContext editor(EditorContextOptions{.seed = 2, .crashJournal = false});
    const Entity root = createEntity(editor, "Root", Entity{});
    for (int i = 0; i < 99; ++i)
    {
        createEntity(editor, "Child " + std::to_string(i), root);
    }
    for (int i = 0; i < 900; ++i)
    {
        createEntity(editor, "Other " + std::to_string(i), Entity{});
    }
    EntitiesParams params;
    params.entities = {"/Root"};
    bench.batch(100).unit("entity").run("entity.destroy + undo (100 of 1000)", [&] {
        Expected<EntityDestroyResult, CommandError> destroyed =
            editor.execute<EntityDestroyResult>("entity.destroy", params);
        ILL_CHECK(destroyed.hasValue() && destroyed->destroyed == 100, "destroy failed");
        Expected<u32, CommandError> undone = editor.undo(1);
        ILL_CHECK(undone.hasValue() && *undone == 1, "undo failed");
    });
}

// scene.query over 10,000 entities in a hierarchy (100 roots with 99 children each), per entity. Budget
// about 7x the 15 ns measured on a desktop.
ILL_BENCHMARK("editor/scene-query", 100.0)
{
    EditorContext editor(EditorContextOptions{.seed = 3, .crashJournal = false});
    for (int r = 0; r < 100; ++r)
    {
        const Entity root = createEntity(editor, "Root " + std::to_string(r), Entity{});
        for (int c = 0; c < 99; ++c)
        {
            createEntity(editor, "Cell " + std::to_string(c), root);
        }
    }
    SceneQueryParams params;
    params.has = std::vector<std::string>{"illusion.Transform"};
    params.name = "Cell 4*";
    params.limit = 100;
    bench.batch(10'000).unit("entity").run("scene.query (has + name, 10000 entities)", [&] {
        Expected<SceneQueryResult, CommandError> result =
            editor.execute<SceneQueryResult>("scene.query", params);
        ILL_CHECK(result.hasValue() && result->total == 1100, "query failed");
        ankerl::nanobench::doNotOptimizeAway(result);
    });
}

// transform.translate of an entity four levels deep in a hierarchy of 104 entities, merged like a gizmo drag:
// the world placement through the parent chain, LocalTransform and Transform records, the merged undo step
// and the derived Transforms of the hierarchy. Budget about 6x the measured result: see Budget.hpp.
ILL_BENCHMARK("editor/transform-translate", 60'000.0)
{
    EditorContext editor(EditorContextOptions{.seed = 4, .crashJournal = false});
    const Entity root = createEntity(editor, "Root", Entity{});
    Entity parent = root;
    for (int level = 0; level < 3; ++level)
    {
        parent = createEntity(editor, "Level " + std::to_string(level), parent);
    }
    createEntity(editor, "Target", parent);
    for (int i = 0; i < 99; ++i)
    {
        createEntity(editor, "Other " + std::to_string(i), root);
    }
    TransformTranslateParams params[2];
    for (int i = 0; i < 2; ++i)
    {
        params[i].entities = {"name:Target"};
        params[i].offset = Vec3(i == 0 ? 0.25f : -0.25f, 0.0f, 0.0f);
    }
    CommandOptions options;
    options.mergeKey = "drag";
    usize next = 0;
    bench.run("transform.translate (depth 4, merged)", [&] {
        Expected<EntitiesResult, CommandError> result =
            editor.execute<EntitiesResult>("transform.translate", params[next++ & 1], options);
        ankerl::nanobench::doNotOptimizeAway(result);
    });
}

// The reference index checking 100 unchanged scene files of 20 entities each (read and hash, no parsing), per
// file. Budget about 10x the 16 us measured on the same VM: file reads cost more on Windows.
ILL_BENCHMARK("editor/reference-refresh", 160'000.0)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
                                       std::format("illusion-bench-references-{}", std::random_device{}());
    std::filesystem::create_directories(root);
    VirtualFileSystem files;
    ILL_CHECK(files.mount("assets", root, MountAccess::ReadWrite) == FileError::None,
              "cannot mount the benchmark files");
    World world;
    GuidGenerator guids(5);
    for (int i = 0; i < 20; ++i)
    {
        const Entity entity = world.create(Transform{});
        world.emplace<Name>(entity, Name{"Entity " + std::to_string(i)});
        world.emplace<EntityGuid>(entity, guids.next());
    }
    const SceneSaveResult scene = saveScene(world);
    ILL_CHECK(scene.ok(), "cannot save the benchmark scene");
    for (int i = 0; i < 100; ++i)
    {
        ILL_CHECK(files.writeText(std::format("assets:/scenes/s{}.scene", i), scene.json) == FileError::None,
                  "cannot write a benchmark scene");
    }
    ReferenceIndex index;
    index.refresh(files);
    ILL_CHECK(index.size() == 100, "the benchmark scenes were not indexed");
    bench.batch(100).unit("file").run("ReferenceIndex::refresh (100 unchanged scenes)", [&] {
        index.refresh(files);
        ankerl::nanobench::doNotOptimizeAway(index);
    });
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
