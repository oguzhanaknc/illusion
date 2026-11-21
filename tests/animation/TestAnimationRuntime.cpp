// Animation graphs in the runtime (ADR 0060): graph Animators in AnimationWorld, AnimationService and its
// errors, events, cross-fades of Animators without a graph, root motion in GameSession (plain Transforms,
// children of rotated parents, characters on a slope), fixed-step determinism, live reload and allocation.

#include "TestSupport.hpp"
#include "assets/AssetTestProject.hpp"
#include "physics/PhysicsTestSupport.hpp"

#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/animation/AnimationWorld.hpp>
#include <illusion/animation/AnimationWorldService.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsWorld.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/SceneComponents.hpp>
#include <illusion/runtime/GameSession.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

using namespace illusion;

namespace
{

constexpr std::string_view kSkeleton = "game:/walker.gltf#skin/0";
constexpr std::string_view kIdle = "game:/walker.gltf#animation/idle";
constexpr std::string_view kWalk = "game:/walker.gltf#animation/walk";
constexpr std::string_view kGraph = "game:/walker.animgraph";
constexpr std::string_view kRootGraph = "game:/striding.animgraph";
constexpr f32 kHalfTurn = std::numbers::pi_v<f32> / 4.0f;

// Every state and transition kind the tests need: "idle" (event "tick" at 0.5) moves to "move" (a blend of
// idle and walk by "speed", event "step" at 0.25) when speed > 0.5; any state goes to "spin" on "go"; "spin"
// returns to "idle" when "armed" and "count" > 2.
constexpr std::string_view kGraphText = R"({"format": "illusion.animgraph", "version": 1,
  "parameters": [{"name": "speed", "type": "Float"}, {"name": "count", "type": "Int"},
                 {"name": "armed", "type": "Bool"}, {"name": "go", "type": "Trigger"}],
  "layers": [{"name": "base", "entry": "idle", "states": [
      {"name": "idle", "clip": "game:/walker.gltf#animation/idle", "events": [{"name": "tick", "time": 0.5}]},
      {"name": "move", "blend1d": {"parameter": "speed", "points": [
          {"value": 0, "clip": "game:/walker.gltf#animation/idle"},
          {"value": 1, "clip": "game:/walker.gltf#animation/walk"}]},
       "events": [{"name": "step", "time": 0.25}]},
      {"name": "spin", "clip": "game:/walker.gltf#animation/turn"}],
    "transitions": [
      {"from": "idle", "to": "move", "conditions": [{"parameter": "speed", "compare": "Greater", "value": 0.5}]},
      {"from": "*", "to": "spin", "duration": 0.2, "conditions": [{"parameter": "go"}]},
      {"from": "spin", "to": "idle", "conditions": [{"parameter": "armed", "compare": "Equal", "value": true},
                                                    {"parameter": "count", "compare": "Greater", "value": 2}]}]}]})";

// Root motion from "hips": "walk" (2 m/s along +z) or, after "turn", "turn" (90 degrees per second about +y).
constexpr std::string_view kRootGraphText = R"({"format": "illusion.animgraph", "version": 1,
  "parameters": [{"name": "turn", "type": "Trigger"}], "rootMotion": "hips",
  "layers": [{"name": "base", "entry": "walk", "states": [
      {"name": "walk", "clip": "game:/walker.gltf#animation/walk"},
      {"name": "turn", "clip": "game:/walker.gltf#animation/turn"}],
    "transitions": [{"from": "walk", "to": "turn", "conditions": [{"parameter": "turn"}]}]}]})";

void reflectAll()
{
    reflectCoreComponents();
    reflectRenderComponents();
    reflectPhysicsComponents();
    registerAnimationComponents(ComponentRegistry::instance());
}

// Writes "game:/walker.gltf": the joints "root" and "hips" (one metre up) and one-second clips "idle" (hips
// still), "walk" (hips 2 m along +z), "turn" (hips 90 degrees about +y) and "wave" (root about z).
void writeWalker(VirtualFileSystem& files)
{
    const std::vector<f32> data{0.0f,
                                1.0f, // times
                                0.0f,
                                1.0f,
                                0.0f,
                                0.0f,
                                1.0f,
                                2.0f, // walk
                                0.0f,
                                1.0f,
                                0.0f,
                                0.0f,
                                1.0f,
                                0.0f, // idle
                                0.0f,
                                0.0f,
                                0.0f,
                                1.0f,
                                0.0f,
                                std::sin(kHalfTurn),
                                0.0f,
                                std::cos(kHalfTurn), // turn
                                0.0f,
                                0.0f,
                                0.0f,
                                1.0f,
                                0.0f,
                                0.0f,
                                std::sin(0.25f),
                                std::cos(0.25f)}; // wave
    std::vector<std::byte> bytes(data.size() * sizeof(f32));
    std::memcpy(bytes.data(), data.data(), bytes.size());
    REQUIRE(files.writeFile("game:/walker.bin", bytes) == FileError::None);
    const auto animation = [](std::string_view name, u32 output, u32 node, std::string_view path) {
        return std::string(R"({"name": ")") + std::string(name) +
               R"(", "samplers": [{"input": 0, "output": )" + std::to_string(output) +
               R"(, "interpolation": "LINEAR"}], "channels": [{"sampler": 0, "target": )" + R"({"node": )" +
               std::to_string(node) + R"(, "path": ")" + std::string(path) + R"("}}]})";
    };
    const std::string gltf =
        R"({"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
"nodes": [{"name": "root", "children": [1]}, {"name": "hips", "translation": [0, 1, 0]}],
"skins": [{"joints": [0, 1]}], "animations": [)" +
        animation("walk", 1, 1, "translation") + ", " + animation("idle", 2, 1, "translation") + ", " +
        animation("turn", 3, 1, "rotation") + ", " + animation("wave", 4, 0, "rotation") +
        R"(], "buffers": [{"uri": "walker.bin", "byteLength": 120}],
"bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 8}, {"buffer": 0, "byteOffset": 8, "byteLength": 24},
{"buffer": 0, "byteOffset": 32, "byteLength": 24}, {"buffer": 0, "byteOffset": 56, "byteLength": 32},
{"buffer": 0, "byteOffset": 88, "byteLength": 32}],
"accessors": [{"bufferView": 0, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0], "max": [1]},
{"bufferView": 1, "componentType": 5126, "count": 2, "type": "VEC3"},
{"bufferView": 2, "componentType": 5126, "count": 2, "type": "VEC3"},
{"bufferView": 3, "componentType": 5126, "count": 2, "type": "VEC4"},
{"bufferView": 4, "componentType": 5126, "count": 2, "type": "VEC4"}]})";
    REQUIRE(files.writeText("game:/walker.gltf", gltf) == FileError::None);
    REQUIRE(files.writeText(kGraph, kGraphText) == FileError::None);
    REQUIRE(files.writeText(kRootGraph, kRootGraphText) == FileError::None);
}

Animator graphAnimator(std::string_view graph = kGraph, f32 time = 0.0f)
{
    Animator result;
    result.skeleton = AssetRef(kSkeleton);
    result.graph = AssetRef(graph);
    result.time = time;
    return result;
}

Animator clipAnimator(std::string_view clip)
{
    Animator result;
    result.skeleton = AssetRef(kSkeleton);
    result.clip = AssetRef(clip);
    return result;
}

Transform turned(f32 yaw, Vec3 position = Vec3(0.0f), Vec3 scale = Vec3(1.0f))
{
    Transform transform;
    transform.position = position;
    transform.rotation = glm::angleAxis(yaw, Vec3(0.0f, 1.0f, 0.0f));
    transform.scale = scale;
    return transform;
}

struct Fixture
{
    Fixture()
    {
        reflectAll();
        writeWalker(project.files);
    }

    [[nodiscard]] Vec3 hips(Entity entity) const
    {
        const std::optional<u32> slot = animation.findSlot(entity);
        REQUIRE(slot.has_value());
        const std::span<const Mat4> joints = animation.jointMatrices(*slot);
        REQUIRE(joints.size() == 2);
        return Vec3(joints[1][3]);
    }

    void step(f32 seconds) { animation.update(seconds, &jobs, true); }

    test::AssetProject project;
    JobSystem jobs{JobSystemDesc{2, 256}};
    AssetStreamer streamer{project.files, jobs};
    World world;
    AnimationWorld animation{world, streamer};
    AnimationWorldService service{animation};
};

std::string stateOf(const AnimationService& service, Entity entity)
{
    const Expected<std::string_view, AnimationError> state = service.currentState(entity, "base");
    REQUIRE(state.hasValue());
    return std::string(*state);
}

// A GameSession over the walker files, fixed-step, started.
struct Session
{
    Session()
    {
        reflectAll();
        writeWalker(project.files);
        REQUIRE(project.files.mount("assets", project.directory.path / "game") == FileError::None);
        GameSessionDesc desc;
        desc.jobs = &jobs;
        desc.files = &project.files;
        desc.streamer = &streamer;
        desc.audioOutput = AudioOutput::Offline;
        desc.fixedStep = true;
        auto created = GameSession::create(desc);
        REQUIRE(created.hasValue());
        session = std::move(*created);
    }

    [[nodiscard]] World& world() const { return session->world(); }

    void run(int updates) const
    {
        for (int i = 0; i < updates; ++i)
        {
            session->update(0.0);
        }
    }

    test::AssetProject project;
    JobSystem jobs{JobSystemDesc{2, 1024}};
    AssetStreamer streamer{project.files, jobs};
    std::unique_ptr<GameSession> session;
};

const f32 kStep = static_cast<f32>(ProjectSettings{}.physics.stepSeconds);

} // namespace

TEST_CASE("a graph Animator starts in its entry state at Animator::time and follows parameters")
{
    Fixture f;
    const Entity entity = f.world.create(Transform{}, graphAnimator(kGraph, 0.25f));
    CHECK(f.service.setFloat(entity, "speed", 1.0f).error() == AnimationError::NoGraph); // still loading
    f.animation.waitIdle();
    const std::optional<u32> slot = f.animation.findSlot(entity);
    REQUIRE(slot.has_value());
    REQUIRE(f.animation.graph(*slot) != nullptr);
    CHECK(f.world.get<AnimationPose>(entity).jointCount == 2);
    CHECK(stateOf(f.service, entity) == "idle");
    CHECK(f.service.normalizedTime(entity, "base").valueOr(-1.0f) == doctest::Approx(0.25f));

    // Without advancing, parameters do not move the graph: it shows the entry state at Animator::time.
    REQUIRE(f.service.setFloat(entity, "speed", 1.0f).hasValue());
    CHECK(f.service.parameter(entity, "speed").valueOr(0.0f) == 1.0f);
    f.animation.update(0.1f, nullptr, false);
    CHECK(stateOf(f.service, entity) == "idle");
    CHECK(f.service.parameter(entity, "speed").valueOr(-1.0f) == 0.0f);

    REQUIRE(f.service.setFloat(entity, "speed", 1.0f).hasValue());
    f.step(0.1f);
    CHECK(stateOf(f.service, entity) == "move");
    CHECK(f.service.normalizedTime(entity, "base").valueOr(-1.0f) == 0.0f);
    f.step(0.25f);
    CHECK(f.service.normalizedTime(entity, "base").valueOr(-1.0f) == doctest::Approx(0.25f));
    CHECK(f.hips(entity).z == doctest::Approx(0.5f));                        // walk at 0.25 s, at full weight
    CHECK(f.world.get<AnimationPose>(entity).rootTranslation == Vec3(0.0f)); // the graph has no root motion

    // Animator::speed scales the step; a paused Animator holds its graph.
    f.world.get<Animator>(entity).speed = 2.0f;
    f.step(0.125f);
    CHECK(f.service.normalizedTime(entity, "base").valueOr(-1.0f) == doctest::Approx(0.5f));
    f.world.get<Animator>(entity).playing = false;
    f.step(0.25f);
    CHECK(f.service.normalizedTime(entity, "base").valueOr(-1.0f) == doctest::Approx(0.5f));
}

TEST_CASE("triggers fire one transition and are cleared when no transition takes them")
{
    Fixture f;
    const Entity entity = f.world.create(Transform{}, graphAnimator());
    f.animation.waitIdle();
    REQUIRE(f.service.setTrigger(entity, "go").hasValue());
    f.step(0.05f);
    CHECK(stateOf(f.service, entity) == "spin");
    f.step(0.3f); // the cross-fade ends

    // "go" in "spin" takes no transition ("*" never targets the current state), so the step clears it.
    REQUIRE(f.service.setTrigger(entity, "go").hasValue());
    f.step(0.05f);
    REQUIRE(f.service.setBool(entity, "armed", true).hasValue());
    REQUIRE(f.service.setInt(entity, "count", 2).hasValue());
    f.step(0.05f);
    CHECK(stateOf(f.service, entity) == "spin"); // count is not greater than 2
    REQUIRE(f.service.setInt(entity, "count", 3).hasValue());
    CHECK(f.service.parameter(entity, "count").valueOr(0.0f) == 3.0f);
    CHECK(f.service.parameter(entity, "armed").valueOr(0.0f) == 1.0f);
    f.step(0.05f);
    CHECK(stateOf(f.service, entity) == "idle");
    f.step(0.05f);
    CHECK(stateOf(f.service, entity) == "idle");
}

TEST_CASE("every AnimationService error is returned, never asserted")
{
    Fixture f;
    const Entity graph = f.world.create(Transform{}, graphAnimator());
    const Entity clip = f.world.create(Transform{}, clipAnimator(kIdle));
    const Entity plain = f.world.create(Transform{});
    const Entity dead = f.world.create(Transform{}, graphAnimator());
    f.world.destroy(dead);
    f.animation.waitIdle();

    for (const Entity entity : {plain, dead, Entity{}})
    {
        CHECK(f.service.setFloat(entity, "speed", 1.0f).error() == AnimationError::NoAnimator);
        CHECK(f.service.setInt(entity, "count", 1).error() == AnimationError::NoAnimator);
        CHECK(f.service.setBool(entity, "armed", true).error() == AnimationError::NoAnimator);
        CHECK(f.service.setTrigger(entity, "go").error() == AnimationError::NoAnimator);
        CHECK(f.service.parameter(entity, "speed").error() == AnimationError::NoAnimator);
        CHECK(f.service.currentState(entity, "base").error() == AnimationError::NoAnimator);
        CHECK(f.service.normalizedTime(entity, "base").error() == AnimationError::NoAnimator);
        CHECK(f.service.play(entity, kWalk, 0.1f).error() == AnimationError::NoAnimator);
    }
    CHECK(f.service.setFloat(clip, "speed", 1.0f).error() == AnimationError::NoGraph);
    CHECK(f.service.currentState(clip, "base").error() == AnimationError::NoGraph);
    CHECK(f.service.play(graph, kWalk, 0.1f).error() == AnimationError::HasGraph);

    CHECK(f.service.setFloat(graph, "sped", 1.0f).error() == AnimationError::UnknownName);
    CHECK(f.service.parameter(graph, "sped").error() == AnimationError::UnknownName);
    CHECK(f.service.currentState(graph, "upper").error() == AnimationError::UnknownName);
    CHECK(f.service.normalizedTime(graph, "upper").error() == AnimationError::UnknownName);
    CHECK(f.service.play(clip, "game:/walker.gltf#skin/0", 0.1f).error() == AnimationError::UnknownName);
    CHECK(f.service.play(clip, "not a path", 0.1f).error() == AnimationError::UnknownName);

    CHECK(f.service.setFloat(graph, "count", 1.0f).error() == AnimationError::TypeMismatch);
    CHECK(f.service.setInt(graph, "speed", 1).error() == AnimationError::TypeMismatch);
    CHECK(f.service.setBool(graph, "go", true).error() == AnimationError::TypeMismatch);
    CHECK(f.service.setTrigger(graph, "armed").error() == AnimationError::TypeMismatch);
    CHECK(f.service.parameter(graph, "go").error() == AnimationError::TypeMismatch);

    CHECK(f.service.setFloat(graph, "speed", std::numeric_limits<f32>::quiet_NaN()).error() ==
          AnimationError::InvalidValue);
    CHECK(f.service.setInt(graph, "count", 1 << 25).error() == AnimationError::InvalidValue);
    CHECK(f.service.play(clip, kWalk, -1.0f).error() == AnimationError::InvalidValue);
    CHECK(f.service.play(clip, kWalk, std::numeric_limits<f32>::infinity()).error() ==
          AnimationError::InvalidValue);
    CHECK(f.service.parameter(graph, "speed").valueOr(-1.0f) == 0.0f); // nothing was written
    CHECK(f.world.get<Animator>(clip).clip == AssetRef(kIdle));

    CHECK(animationErrorName(AnimationError::NoAnimator) == "NoAnimator");
    CHECK(animationErrorName(AnimationError::NoGraph) == "NoGraph");
    CHECK(animationErrorName(AnimationError::HasGraph) == "HasGraph");
    CHECK(animationErrorName(AnimationError::UnknownName) == "UnknownName");
    CHECK(animationErrorName(AnimationError::TypeMismatch) == "TypeMismatch");
    CHECK(animationErrorName(AnimationError::InvalidValue) == "InvalidValue");
}

TEST_CASE("graph events reach pollEvents once per update, in update order")
{
    Fixture f;
    const Entity first = f.world.create(Transform{}, graphAnimator(kGraph, 0.4f));
    const Entity second = f.world.create(Transform{}, graphAnimator(kGraph, 0.45f));
    const Entity late = f.world.create(Transform{}, graphAnimator(kGraph, 0.6f));
    f.animation.waitIdle();
    f.step(0.2f);
    std::array<AnimationEvent, 4> events{};
    REQUIRE(f.service.pollEvents(events) == 2);
    CHECK(events[0].entity == first);
    CHECK(events[1].entity == second);
    CHECK(events[0].name == "tick");
    CHECK(events[0].layer == "base");
    CHECK(events[0].state == "idle");
    // Every call in an update returns the same events; a short span gets the count of all.
    std::array<AnimationEvent, 1> one{};
    CHECK(f.service.pollEvents(one) == 2);
    CHECK(one[0].entity == first);
    CHECK(f.service.pollEvents(events) == 2);
    f.step(0.01f);
    CHECK(f.service.pollEvents(events) == 0);
    CHECK(late != first);
}

TEST_CASE("play() cross-fades an Animator without a graph to another clip")
{
    Fixture f;
    const Entity entity = f.world.create(Transform{}, clipAnimator(kIdle));
    f.animation.waitIdle();
    f.step(0.1f);
    CHECK(f.hips(entity).z == doctest::Approx(0.0f));
    REQUIRE(f.service.play(entity, kWalk, 0.5f).hasValue());
    const Animator& animator = f.world.get<Animator>(entity);
    CHECK(animator.clip == AssetRef(kWalk));
    CHECK(animator.time == 0.0f);
    CHECK(animator.playing);
    f.step(0.25f); // walk at 0.25 s (0.5 m) at half weight over idle
    CHECK(f.hips(entity).z == doctest::Approx(0.25f));
    f.step(0.5f); // the fade has ended
    CHECK(f.hips(entity).z == doctest::Approx(1.5f));

    // Without a fade the clip changes at once.
    REQUIRE(f.service.play(entity, kIdle, 0.0f).hasValue());
    f.step(0.1f);
    CHECK(f.hips(entity).z == doctest::Approx(0.0f));
}

TEST_CASE("root motion is taken out of the pose and turned into world space by the Entity's Transform")
{
    Fixture f;
    const Entity walker = f.world.create(turned(std::numbers::pi_v<f32> / 2.0f, Vec3(1.0f), Vec3(2.0f)),
                                         graphAnimator(kRootGraph));
    f.animation.waitIdle();
    CHECK(f.world.get<AnimationPose>(walker).rootTranslation == Vec3(0.0f)); // not advancing
    f.step(0.1f);
    const AnimationPose& pose = f.world.get<AnimationPose>(walker);
    // 0.2 m along +z in model space, scaled by 2 and turned to +x.
    CHECK(pose.rootTranslation.x == doctest::Approx(0.4f));
    CHECK(pose.rootTranslation.y == doctest::Approx(0.0f));
    CHECK(pose.rootTranslation.z == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(pose.rootYaw == 0.0f);
    CHECK(f.hips(walker).z == doctest::Approx(0.0f)); // the joint stays at its rest position

    REQUIRE(f.service.setTrigger(walker, "turn").hasValue());
    f.step(0.1f);
    f.step(0.1f);
    CHECK(f.world.get<AnimationPose>(walker).rootYaw ==
          doctest::Approx(std::numbers::pi_v<f32> / 2.0f * 0.1f));
    CHECK(f.world.get<AnimationPose>(walker).rootTranslation == Vec3(0.0f));
}

TEST_CASE("the session adds root motion to Transforms, also of children of rotated parents")
{
    Session s;
    World& world = s.world();
    const Entity root = world.create(Transform{}, graphAnimator(kRootGraph));
    const Entity parent = world.create(turned(std::numbers::pi_v<f32> / 2.0f, Vec3(10.0f, 0.0f, 0.0f)));
    const Entity child = world.create(Transform{}, graphAnimator(kRootGraph));
    REQUIRE(setParent(world, child, parent, ParentMode::KeepLocal).hasValue());
    s.session->animation()->waitIdle();
    s.run(1);
    CHECK(world.get<Transform>(root).position == Vec3(0.0f)); // not started
    s.session->start();
    s.run(60); // one second of walking, 2 m
    CHECK(world.get<Transform>(root).position.z == doctest::Approx(2.0f).epsilon(1e-3));
    CHECK(world.get<Transform>(root).position.x == doctest::Approx(0.0f));
    // The child's +z is the world's +x.
    const Transform& moved = world.get<Transform>(child);
    CHECK(moved.position.x == doctest::Approx(12.0f).epsilon(1e-3));
    CHECK(moved.position.z == doctest::Approx(0.0f).epsilon(1e-3));
    CHECK(world.get<LocalTransform>(child).position.z == doctest::Approx(2.0f).epsilon(1e-3));

    // Turning rotates the Transform about +y.
    REQUIRE(s.session->animation() != nullptr);
    AnimationWorldService service(*s.session->animation());
    REQUIRE(service.setTrigger(root, "turn").hasValue());
    s.run(31);
    const Quat rotation = world.get<Transform>(root).rotation;
    const Vec3 forward = rotation * Vec3(0.0f, 0.0f, 1.0f);
    CHECK(std::atan2(forward.x, forward.z) == doctest::Approx(std::numbers::pi_v<f32> / 4.0f).epsilon(0.03));
}

TEST_CASE("root motion walks a character up a slope while gravity keeps it on the ground")
{
    Session s;
    World& world = s.world();
    // A 10 degree ramp rising along +z.
    Transform ramp = test::at(Vec3(0.0f, -0.5f, 0.0f));
    ramp.rotation = glm::angleAxis(-0.17453292f, Vec3(1.0f, 0.0f, 0.0f));
    world.create(ramp, test::body(MotionType::Static), test::box(Vec3(5.0f, 0.5f, 20.0f)));
    const Entity character =
        world.create(test::at(Vec3(0.0f, 0.5f, -2.0f)), CharacterController{}, graphAnimator(kRootGraph));
    s.session->animation()->waitIdle();
    s.session->start();
    s.run(30); // lands
    const Vec3 start = world.get<Transform>(character).position;
    s.run(60);
    const Vec3 end = world.get<Transform>(character).position;
    CHECK(end.z - start.z == doctest::Approx(2.0f).epsilon(0.1));
    CHECK(end.x == doctest::Approx(start.x).epsilon(0.01));
    CHECK(end.y - start.y == doctest::Approx(2.0f * std::tan(0.17453292f)).epsilon(0.25));
    CHECK(s.session->physics().isCharacterGrounded(character));
}

TEST_CASE("fixed-step sessions with graphs and root motion repeat exactly")
{
    const auto run = [] {
        Session s;
        World& world = s.world();
        const Entity walker = world.create(turned(0.3f), graphAnimator(kRootGraph));
        const Entity dancer = world.create(Transform{}, graphAnimator(kGraph));
        s.session->animation()->waitIdle();
        s.session->start();
        AnimationWorldService service(*s.session->animation());
        std::vector<f32> trace;
        for (int i = 0; i < 90; ++i)
        {
            static_cast<void>(service.setFloat(dancer, "speed", 0.01f * static_cast<f32>(i)));
            if (i == 40)
            {
                static_cast<void>(service.setTrigger(walker, "turn"));
            }
            s.run(1);
            const Transform& transform = world.get<Transform>(walker);
            trace.insert(trace.end(), {transform.position.x, transform.position.z, transform.rotation.y,
                                       service.normalizedTime(dancer, "base").valueOr(-1.0f)});
        }
        return trace;
    };
    const std::vector<f32> first = run();
    const std::vector<f32> second = run();
    REQUIRE(first.size() == second.size());
    CHECK(std::memcmp(first.data(), second.data(), first.size() * sizeof(f32)) == 0);
    CHECK(kStep > 0.0f);
}

TEST_CASE("a changed graph file restarts its Animators with the new graph")
{
    Fixture f;
    const Entity entity = f.world.create(Transform{}, graphAnimator());
    f.animation.waitIdle();
    REQUIRE(f.service.setFloat(entity, "speed", 1.0f).hasValue());
    f.step(0.1f);
    REQUIRE(stateOf(f.service, entity) == "move");

    std::string text(kGraphText);
    const std::string from = R"("entry": "idle")";
    text.replace(text.find(from), from.size(), R"("entry": "spin")");
    REQUIRE(f.project.files.writeText(kGraph, text) == FileError::None);
    CHECK(f.streamer.reload(kGraph));
    f.animation.waitIdle();
    CHECK(stateOf(f.service, entity) == "spin");
    CHECK(f.service.parameter(entity, "speed").valueOr(-1.0f) == 0.0f);

    // A broken file keeps the graph that was loaded.
    REQUIRE(f.project.files.writeText(kGraph, "{}") == FileError::None);
    CHECK(f.streamer.reload(kGraph));
    f.animation.waitIdle();
    CHECK(stateOf(f.service, entity) == "spin");
    CHECK(f.animation.graph(f.world.get<AnimationPose>(entity).slot) != nullptr);
}

TEST_CASE("a graph that cannot be bound or loaded shows the rest pose")
{
    Fixture f;
    REQUIRE(
        f.project.files.writeText("game:/broken.animgraph", R"({"format": "illusion.animgraph", "version": 1,
        "layers": [{"name": "base", "states": [{"name": "a", "clip": "game:/walker.gltf#animation/missing"}]}]})") ==
        FileError::None);
    const Entity missingClip = f.world.create(Transform{}, graphAnimator("game:/broken.animgraph"));
    const Entity missingFile = f.world.create(Transform{}, graphAnimator("game:/nothing.animgraph"));
    const Entity notAGraph = f.world.create(Transform{}, graphAnimator("game:/walker.gltf"));
    f.animation.waitIdle();
    for (const Entity entity : {missingClip, missingFile, notAGraph})
    {
        CHECK(f.world.get<AnimationPose>(entity).jointCount == 2);
        CHECK(f.hips(entity) == Vec3(0.0f, 1.0f, 0.0f));
        CHECK(f.service.setFloat(entity, "speed", 1.0f).error() == AnimationError::NoGraph);
    }
    CHECK(f.animation.statistics().failures == 4); // the missing clip, the graph without it, two files
}

TEST_CASE("a steady-state update of graph Animators allocates nothing")
{
    Fixture f;
    std::vector<Entity> entities;
    for (u32 i = 0; i < 24; ++i)
    {
        const bool root = i % 2 == 0;
        entities.push_back(
            f.world.create(turned(0.1f * static_cast<f32>(i)),
                           graphAnimator(root ? kRootGraph : kGraph, 0.03f * static_cast<f32>(i))));
    }
    f.world.create(Transform{}, clipAnimator(kWalk));
    f.animation.waitIdle();
    for (u32 i = 1; i < entities.size(); i += 2)
    {
        REQUIRE(f.service.setFloat(entities[i], "speed", 0.8f).hasValue());
    }
    std::array<AnimationEvent, 8> events{};
    for (int i = 0; i < 3; ++i)
    {
        f.step(1.0f / 60.0f);
    }
    u32 seen = 0;
    usize allocations = 0;
    {
        const test::ScopedAllocationCounter counter;
        for (int i = 0; i < 60; ++i)
        {
            static_cast<void>(f.service.setFloat(entities[1], "speed", 0.6f + 0.001f * static_cast<f32>(i)));
            f.step(1.0f / 60.0f);
            seen += f.service.pollEvents(events);
        }
        allocations = counter.count();
    }
    CHECK(allocations == 0);
    CHECK(seen > 0);
    CHECK(f.animation.statistics().posed == 25);
}
