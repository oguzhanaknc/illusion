#include "Budget.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/editor/core/CoreCommands.hpp>
#include <illusion/editor/core/EditorContext.hpp>

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
