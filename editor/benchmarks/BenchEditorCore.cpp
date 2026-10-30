#include "Budget.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/editor/core/Arrange.hpp>
#include <illusion/editor/core/CoreCommands.hpp>
#include <illusion/editor/core/EditorContext.hpp>
#include <illusion/editor/core/Gizmo.hpp>
#include <illusion/editor/core/ReferenceIndex.hpp>
#include <illusion/editor/core/Snap.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <cmath>
#include <filesystem>
#include <format>
#include <random>
#include <string>
#include <vector>

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

// editor.batch of 100 component.set steps that name their entity through a $ reference to the first step's
// result, as one transaction and undo step, then undone, per step. Budget about 6x the 8.0 us measured on a
// desktop (Release, MSVC).
ILL_BENCHMARK("editor/batch-step", 50'000.0)
{
    EditorContext editor(EditorContextOptions{.seed = 6, .crashJournal = false});
    createEntity(editor, "Target", Entity{});
    std::string steps = R"([{"as":"target","method":"entity.get","params":{"entity":"name:Target"}})";
    for (int i = 0; i < 100; ++i)
    {
        steps += std::format(R"(,{{"method":"component.set","params":{{"entity":"$target.entity.id",)"
                             R"("type":"illusion.Transform","value":{{"position":[{},0,0]}}}}}})",
                             i);
    }
    steps += ']';
    BatchParams params;
    Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(steps);
    ILL_CHECK(parsed.hasValue(), "invalid benchmark steps");
    for (const JsonValue step : parsed->root().elements())
    {
        BatchStep batchStep;
        batchStep.as = step.find("as").asString().has_value()
                           ? std::optional<std::string>(std::string(*step.find("as").asString()))
                           : std::nullopt;
        batchStep.method = std::string(step.find("method").asString().value_or(""));
        batchStep.params = JsonText{step.find("params").dump()};
        params.steps.push_back(std::move(batchStep));
    }
    bench.batch(101).unit("step").run("editor.batch (101 steps with $ references) + undo", [&] {
        Expected<BatchResult, CommandError> result = editor.execute<BatchResult>("editor.batch", params);
        ILL_CHECK(result.hasValue() && result->steps.size() == 101, "batch failed");
        Expected<u32, CommandError> undone = editor.undo(1);
        ILL_CHECK(undone.hasValue() && *undone == 1, "undo failed");
    });
}

// Checking whether the last step of one origin can be undone past 1,000 later steps of another origin that
// changed other entities (history.undo {scope: "mine"}), per later step. Budget about 6x the 0.65 us
// measured on a desktop.
ILL_BENCHMARK("editor/undo-own-check", 4'000.0)
{
    EditorContext editor(EditorContextOptions{.seed = 7, .crashJournal = false});
    createEntity(editor, "Mine", Entity{});
    createEntity(editor, "Theirs", Entity{});
    ComponentSetParams params;
    params.entity = "name:Mine";
    params.type = "illusion.Transform";
    params.value.text = R"({"position":[4,5,6]})";
    CommandOptions mine;
    mine.origin = "rpc:me";
    ILL_CHECK(editor.execute<ComponentChangeResult>("component.set", params, mine).hasValue(), "set failed");
    params.entity = "name:Theirs";
    CommandOptions theirs;
    theirs.origin = "rpc:other";
    for (int i = 0; i < 1000; ++i)
    {
        params.value.text = std::format(R"({{"position":[{},0,0]}})", i);
        ILL_CHECK(editor.execute<ComponentChangeResult>("component.set", params, theirs).hasValue(),
                  "set failed");
    }
    bench.batch(1000).unit("step").run("EditorContext::checkUndoOwn (past 1000 steps)", [&] {
        Expected<u32, CommandError> steps = editor.checkUndoOwn(1, "rpc:me");
        ILL_CHECK(steps.hasValue() && *steps == 1, "the check failed");
    });
}

// One frame of gizmo hover: the hit test alone, then with the shape of the Universal gizmo (axes, planes and
// three rings of 64 points). The cursor is inside the rings and misses every handle, so that every handle is
// tested. Budget from the M7 plan.
ILL_BENCHMARK("gizmo/hit-test", 2'000.0)
{
    ViewportCamera view;
    view.pivot = Vec3(10000.0f, 0.0f, -10000.0f);
    view.yaw = 30.0f;
    view.pitch = -25.0f;
    const GizmoCamera camera = gizmoCamera(view, UVec2(1920, 1080), 1.0f);
    GizmoPose pose;
    pose.origin = view.pivot;
    pose.orientation = glm::normalize(quatFromEuler(Vec3(0.2f, 0.4f, 0.1f)));
    // Find a cursor between the handles, on a circle half the gizmo's size from the pivot.
    const GizmoShape probe = gizmoShape(GizmoTool::Universal, camera, pose);
    Vec2 cursor = probe.center;
    for (u32 step = 0; step < 360; ++step)
    {
        const f32 angle = glm::radians(static_cast<f32>(step));
        cursor = probe.center + 0.5f * probe.length * Vec2(std::cos(angle), std::sin(angle));
        if (gizmoHitTest(probe, cursor) == GizmoHandle::None)
        {
            break;
        }
    }
    ILL_ASSERT(gizmoHitTest(probe, cursor) == GizmoHandle::None, "the cursor must miss every handle");
    bench.run("universal hit test", [&] {
        ankerl::nanobench::doNotOptimizeAway(gizmoHitTest(probe, cursor));
        cursor.x += 0.001f;
    });
    bench.run("universal shape and hit test", [&] {
        const GizmoShape shape = gizmoShape(GizmoTool::Universal, camera, pose);
        ankerl::nanobench::doNotOptimizeAway(gizmoHitTest(shape, cursor));
        cursor.x += 0.001f;
    });
}

// One frame of a modal transform: a constrained move that follows the cursor, a free rotation and a scale,
// each with snapping (80 ns measured).
ILL_BENCHMARK("gizmo/modal-update", 500.0)
{
    ViewportCamera view;
    view.pivot = Vec3(10000.0f, 0.0f, -10000.0f);
    view.yaw = 30.0f;
    view.pitch = -25.0f;
    const GizmoCamera camera = gizmoCamera(view, UVec2(1920, 1080), 1.0f);
    const Quat local = glm::normalize(quatFromEuler(Vec3(0.2f, 0.4f, 0.1f)));
    const Vec2 start(1100.0f, 500.0f);
    std::optional<GizmoModal> move =
        GizmoModal::begin(GizmoModalOperation::Translate, camera, view.pivot, local, start);
    std::optional<GizmoModal> turn =
        GizmoModal::begin(GizmoModalOperation::Rotate, camera, view.pivot, local, start);
    std::optional<GizmoModal> scale =
        GizmoModal::begin(GizmoModalOperation::Scale, camera, view.pivot, local, start);
    ILL_ASSERT(move && turn && scale, "the pivot must be in view");
    move->constrain(0, false);
    const GizmoSnap snap{.enabled = true};
    const GizmoTypedInput typed;
    Vec2 cursor = start;
    bench.run("move, rotate and scale update", [&] {
        cursor += Vec2(0.01f, 0.005f);
        ankerl::nanobench::doNotOptimizeAway(move->update(cursor, &snap, typed));
        ankerl::nanobench::doNotOptimizeAway(turn->update(cursor, &snap, typed));
        ankerl::nanobench::doNotOptimizeAway(scale->update(cursor, &snap, typed));
    });
}

// Dropping one entity onto 1,000 surfaces (the boxes of a large level; 1,091 ns measured).
ILL_BENCHMARK("arrange/drop-to-ground", 10'000.0)
{
    std::mt19937 random(7);
    std::uniform_real_distribution<f32> spread(-50.0f, 50.0f);
    std::vector<Aabb> surfaces;
    surfaces.reserve(1000);
    for (u32 i = 0; i < 1000; ++i)
    {
        const Vec3 at(spread(random), spread(random) * 0.1f, spread(random));
        surfaces.push_back(Aabb{at - Vec3(2.0f, 0.5f, 2.0f), at + Vec3(2.0f, 0.5f, 2.0f)});
    }
    Aabb falling{Vec3(-0.5f, 20.0f, -0.5f), Vec3(0.5f, 21.0f, 0.5f)};
    bench.run("1,000 surfaces", [&] {
        falling.min.x += 1e-6f;
        ankerl::nanobench::doNotOptimizeAway(restingHeight(falling, surfaces, 0.0f));
    });
}

// The vertex snap search of one frame: the cursor over a 100,000-vertex mesh (a noisy sphere of radius 5, far
// from the origin, turned and scaled) seen in a 1080p perspective view, run once per mesh under the cursor
// per frame (11,278 ns measured).
ILL_BENCHMARK("snap/vertex-search", 80'000.0)
{
    std::mt19937 random(11);
    std::normal_distribution<f32> gauss(0.0f, 1.0f);
    std::vector<Vec3> positions;
    positions.reserve(100'000);
    for (u32 i = 0; i < 100'000; ++i)
    {
        const Vec3 direction =
            glm::normalize(Vec3(gauss(random), gauss(random), gauss(random)) + Vec3(1e-6f));
        positions.push_back(direction * (5.0f + 0.05f * gauss(random)));
    }
    const VertexSnapGrid grid(positions);
    const Vec3 center(10000.0f, 0.0f, -10000.0f);
    const Mat4 toWorld = glm::translate(Mat4(1.0f), center) *
                         glm::mat4_cast(glm::normalize(quatFromEuler(Vec3(0.2f, 0.4f, 0.1f)))) *
                         glm::scale(Mat4(1.0f), Vec3(1.0f, 1.5f, 0.75f));
    ViewportCamera view;
    view.pivot = center;
    view.yaw = 30.0f;
    view.pitch = -25.0f;
    view.distance = 20.0f;
    const GizmoCamera camera = gizmoCamera(view, UVec2(1920, 1080), 1.0f);
    Vec2 cursor(1000.0f, 500.0f);
    ILL_ASSERT(grid.nearest(vertexSnapQuery(camera, cursor, kVertexSnapTolerance), toWorld).has_value(),
               "the cursor must be over the mesh");
    bench.run("100,000 vertices", [&] {
        cursor.x += 0.001f;
        ankerl::nanobench::doNotOptimizeAway(
            grid.nearest(vertexSnapQuery(camera, cursor, kVertexSnapTolerance), toWorld));
    });
}
