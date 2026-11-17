// Animator and AnimationWorld (ADR 0059): poses after loading, time, slots, mismatches, Inactive entities,
// previous matrices, bounds, GameSession determinism, saved games, live reload and allocation.

#include "TestSupport.hpp"
#include "assets/AssetTestProject.hpp"

#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/animation/AnimationWorld.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/SceneComponents.hpp>
#include <illusion/runtime/GameSession.hpp>
#include <illusion/save/SaveCapture.hpp>
#include <illusion/save/SaveComponents.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace illusion;

namespace
{

constexpr std::string_view kMesh = "game:/rigged.gltf#mesh/0";
constexpr std::string_view kSkeleton = "game:/rigged.gltf#skin/0";
constexpr std::string_view kClip = "game:/rigged.gltf#animation/linear";
constexpr f32 kLinearDuration = 1.5f; // the last key of the clip "linear" in rigged.gltf

void reflectAll()
{
    reflectCoreComponents();
    reflectRenderComponents();
    registerAnimationComponents(ComponentRegistry::instance());
    registerSaveComponents();
}

Animator animator(std::string_view clip = kClip, f32 time = 0.0f)
{
    Animator result;
    result.skeleton = AssetRef(kSkeleton);
    result.clip = clip.empty() ? AssetRef() : AssetRef(clip);
    result.time = time;
    return result;
}

MeshRenderer renderer(std::string_view mesh = kMesh)
{
    MeshRenderer result;
    result.mesh = AssetRef(mesh);
    return result;
}

Entity character(World& world, const Animator& value, Transform transform = {}, std::string_view mesh = kMesh)
{
    return world.create(transform, renderer(mesh), value);
}

struct Fixture
{
    Fixture() { reflectAll(); }

    [[nodiscard]] const AnimationPose& pose(Entity entity) const { return world.get<AnimationPose>(entity); }

    [[nodiscard]] std::vector<SkinMatrix> skin(Entity entity) const
    {
        const std::span<const SkinMatrix> matrices = animation.skinMatrices(pose(entity).slot);
        return {matrices.begin(), matrices.end()};
    }

    [[nodiscard]] std::vector<SkinMatrix> previous(Entity entity) const
    {
        const std::span<const SkinMatrix> matrices = animation.previousSkinMatrices(pose(entity).slot);
        return {matrices.begin(), matrices.end()};
    }

    test::AssetProject project;
    JobSystem jobs{JobSystemDesc{2, 256}};
    AssetStreamer streamer{project.files, jobs};
    World world;
    AnimationWorld animation{world, streamer};
};

bool equal(std::span<const SkinMatrix> a, std::span<const SkinMatrix> b)
{
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size_bytes()) == 0;
}

ModelData loadRigged()
{
    VirtualFileSystem files;
    REQUIRE(files.mount("test", utf8Path(ILLUSION_TEST_DATA_DIR)) == FileError::None);
    std::optional<ModelData> model = loadGltf(files, "test:/gltf/rigged.gltf");
    REQUIRE(model.has_value());
    return std::move(*model);
}

// The skin matrices of the clip "linear" at @p time, computed with the pose functions.
std::vector<SkinMatrix> expectedSkin(const ModelData& model, f32 time)
{
    const Skeleton& skeleton = model.skins.at(0);
    const auto clip = std::ranges::find(model.animations, std::string_view("linear"), &AnimationClip::name);
    REQUIRE(clip != model.animations.end());
    std::vector<JointPose> local(skeleton.joints.size());
    std::vector<Mat4> joints(skeleton.joints.size());
    std::vector<SkinMatrix> skin(skeleton.joints.size());
    sampleClip(*clip, bindClip(*clip, skeleton), skeleton, clipTime(*clip, time, true), local);
    localToModel(skeleton, local, joints);
    skinMatrices(skeleton, joints, skin);
    return skin;
}

Vec3 apply(const SkinMatrix& matrix, Vec3 point)
{
    const Vec4 p(point, 1.0f);
    return Vec3(glm::dot(matrix.rows[0], p), glm::dot(matrix.rows[1], p), glm::dot(matrix.rows[2], p));
}

} // namespace

TEST_CASE("Animator is reflected with asset kinds and units, and AnimationPose is runtime state")
{
    reflectAll();
    const ComponentRegistry& registry = ComponentRegistry::instance();
    const TypeReflection* type = registry.reflection(registry.findByStableName("illusion.Animator"));
    REQUIRE(type != nullptr);
    CHECK(type->fields.size() == 6);
    CHECK(type->field("skeleton")->meta.assetKind == AssetKind::Skeleton);
    CHECK(type->field("clip")->meta.assetKind == AssetKind::AnimationClip);
    CHECK(type->field("time")->meta.unit == Unit::Seconds);
    CHECK_FALSE(type->meta.transient);
    const TypeReflection* pose = registry.reflection(registry.findByStableName("illusion.AnimationPose"));
    REQUIRE(pose != nullptr);
    CHECK(pose->meta.transient);
}

TEST_CASE("an Animator gets a pose once its model has loaded")
{
    Fixture f;
    const Entity entity = character(f.world, animator());
    f.animation.update(0.0f, nullptr, false);
    REQUIRE(f.world.has<AnimationPose>(entity));
    CHECK(f.pose(entity).jointCount == 0);
    CHECK(f.animation.statistics().pending == 1);
    CHECK(f.animation.skeleton(f.pose(entity).slot) == nullptr);

    f.animation.waitIdle();
    CHECK(f.animation.statistics().pending == 0);
    CHECK(f.animation.statistics().failures == 0);
    CHECK(f.animation.statistics().poses == 1);
    CHECK(f.animation.statistics().posed == 1);
    CHECK(f.animation.statistics().joints == 5);
    CHECK(f.pose(entity).jointCount == 5);
    CHECK(f.pose(entity).frame == f.animation.frame());
    CHECK(f.pose(entity).bounds.isValid());
    REQUIRE(f.animation.skeleton(f.pose(entity).slot) != nullptr);
    CHECK(f.animation.skeleton(f.pose(entity).slot)->joints.size() == 5);
    CHECK(f.animation.jointMatrices(f.pose(entity).slot).size() == 5);
    CHECK(equal(f.skin(entity), expectedSkin(loadRigged(), 0.0f)));
}

TEST_CASE("missing skeletons and clips are reported; a missing clip shows the rest pose")
{
    Fixture f;
    const Entity noSkeleton = character(f.world, Animator{.skeleton = AssetRef("game:/rigged.gltf#skin/7")});
    const Entity noClip = character(f.world, animator("game:/rigged.gltf#animation/missing"));
    f.animation.waitIdle();
    CHECK(f.animation.statistics().failures == 2);
    CHECK(f.pose(noSkeleton).jointCount == 0);
    REQUIRE(f.pose(noClip).jointCount == 5);
    const ModelData model = loadRigged();
    std::vector<JointPose> rest(5);
    std::vector<Mat4> joints(5);
    std::vector<SkinMatrix> skin(5);
    restPose(model.skins.at(0), rest);
    localToModel(model.skins.at(0), rest, joints);
    skinMatrices(model.skins.at(0), joints, skin);
    CHECK(equal(f.skin(noClip), skin));
}

TEST_CASE("time advances with speed, wraps when looping and stops at the end otherwise")
{
    Fixture f;
    const Entity looping = character(f.world, animator());
    Animator once = animator();
    once.loop = false;
    const Entity stopping = character(f.world, once);
    f.animation.waitIdle();

    f.animation.update(1.0f, &f.jobs, true);
    CHECK(f.world.get<Animator>(looping).time == doctest::Approx(1.0f));
    CHECK(f.world.get<Animator>(stopping).time == doctest::Approx(1.0f));
    f.animation.update(1.0f, &f.jobs, true);
    CHECK(f.world.get<Animator>(looping).time == doctest::Approx(2.0f - kLinearDuration));
    CHECK(f.world.get<Animator>(looping).playing);
    CHECK(f.world.get<Animator>(stopping).time == doctest::Approx(kLinearDuration));
    CHECK_FALSE(f.world.get<Animator>(stopping).playing);
    CHECK(equal(f.skin(looping), expectedSkin(loadRigged(), f.world.get<Animator>(looping).time)));
    f.animation.update(1.0f, &f.jobs, true);
    CHECK(f.world.get<Animator>(stopping).time == doctest::Approx(kLinearDuration));
}

TEST_CASE("a negative speed plays backwards, wrapping or stopping at the start")
{
    Fixture f;
    Animator backwards = animator(kClip, 0.25f);
    backwards.speed = -1.0f;
    const Entity looping = character(f.world, backwards);
    backwards.loop = false;
    const Entity stopping = character(f.world, backwards);
    f.animation.waitIdle();
    f.animation.update(0.5f, nullptr, true);
    CHECK(f.world.get<Animator>(looping).time == doctest::Approx(kLinearDuration - 0.25f));
    CHECK(f.world.get<Animator>(looping).playing);
    CHECK(f.world.get<Animator>(stopping).time == doctest::Approx(0.0f));
    CHECK_FALSE(f.world.get<Animator>(stopping).playing);
}

TEST_CASE("without advancing, the pose shows the saved time, so editing the time scrubs")
{
    Fixture f;
    const Entity entity = character(f.world, animator(kClip, 0.75f));
    f.animation.waitIdle();
    f.animation.update(1.0f, &f.jobs, false);
    CHECK(f.world.get<Animator>(entity).time == 0.75f);
    const ModelData model = loadRigged();
    CHECK(equal(f.skin(entity), expectedSkin(model, 0.75f)));

    f.world.get<Animator>(entity).time = 0.25f;
    f.animation.update(1.0f, &f.jobs, false);
    CHECK(equal(f.skin(entity), expectedSkin(model, 0.25f)));
    Animator& paused = f.world.get<Animator>(entity);
    paused.playing = false;
    f.animation.update(1.0f, &f.jobs, true);
    CHECK(f.world.get<Animator>(entity).time == 0.25f);
}

TEST_CASE("the slot of a destroyed Animator is given to the next one")
{
    Fixture f;
    const Entity first = character(f.world, animator());
    const Entity second = character(f.world, animator());
    f.animation.waitIdle();
    const u32 slot = f.pose(first).slot;
    CHECK(f.pose(second).slot != slot);
    f.world.destroy(first);
    f.animation.update(0.0f, nullptr, false);
    CHECK(f.animation.statistics().poses == 1);

    const Entity third = character(f.world, animator());
    f.animation.update(0.0f, nullptr, false);
    CHECK(f.pose(third).slot == slot);
    CHECK(f.pose(third).jointCount == 5);
    CHECK(f.animation.statistics().poses == 2);

    // Without its Animator an Entity loses its pose too.
    f.world.remove<Animator>(second);
    f.animation.update(0.0f, nullptr, false);
    CHECK_FALSE(f.world.has<AnimationPose>(second));
    CHECK(f.animation.statistics().poses == 1);
}

TEST_CASE("many Animators share one skeleton and clip and pose in parallel")
{
    Fixture f;
    constexpr u32 kCount = 50;
    std::vector<Entity> entities;
    for (u32 i = 0; i < kCount; ++i)
    {
        entities.push_back(character(f.world, animator(kClip, 0.5f)));
    }
    f.animation.waitIdle();
    f.animation.update(0.25f, &f.jobs, true);
    CHECK(f.animation.statistics().poses == kCount);
    CHECK(f.animation.statistics().posed == kCount);
    CHECK(f.animation.statistics().joints == u64{kCount} * 5);
    const std::vector<SkinMatrix> expected = expectedSkin(loadRigged(), 0.75f);
    for (const Entity entity : entities)
    {
        CHECK(f.pose(entity).jointCount == 5);
        CHECK(equal(f.skin(entity), expected));
        CHECK(f.animation.skeleton(f.pose(entity).slot) == f.animation.skeleton(f.pose(entities[0]).slot));
    }
}

TEST_CASE("a skeleton that does not fit the mesh gives no pose")
{
    Fixture f;
    Animator other = animator();
    other.skeleton = AssetRef("game:/rigged.gltf#skin/1");
    const Entity mismatched = character(f.world, other);
    const Entity rigid = character(f.world, animator(), Transform{}, "builtin:/mesh/cube");
    const Entity meshless = f.world.create(Transform{}, animator());
    f.animation.waitIdle();
    f.animation.update(0.1f, nullptr, true);
    CHECK(f.pose(mismatched).jointCount == 0);
    CHECK(f.pose(rigid).jointCount == 0);
    CHECK(f.animation.skinMatrices(f.pose(mismatched).slot).empty());
    CHECK(f.animation.statistics().pending == 0);
    CHECK(f.animation.statistics().failures == 0);
    // Without a mesh the skeleton is posed, bounded by its joints.
    CHECK(f.pose(meshless).jointCount == 5);
    CHECK(f.pose(meshless).bounds.isValid());
}

TEST_CASE("an Inactive Animator keeps its time and its pose")
{
    Fixture f;
    const Entity entity = character(f.world, animator(kClip, 0.5f));
    HierarchyIndex hierarchy;
    f.animation.waitIdle();
    f.animation.update(0.1f, nullptr, true);
    const std::vector<SkinMatrix> before = f.skin(entity);
    setEnabled(f.world, entity, false);
    updateTransformHierarchy(f.world, hierarchy);
    REQUIRE(f.world.has<Inactive>(entity));
    f.animation.update(0.1f, nullptr, true);
    f.animation.update(0.1f, nullptr, true);
    CHECK(f.world.get<Animator>(entity).time == doctest::Approx(0.6f));
    CHECK(f.world.has<AnimationPose>(entity));
    CHECK(equal(f.skin(entity), before));
    CHECK(equal(f.previous(entity), before));
    CHECK(f.animation.statistics().poses == 1);
    CHECK(f.animation.statistics().posed == 0);

    setEnabled(f.world, entity, true);
    updateTransformHierarchy(f.world, hierarchy);
    f.animation.update(0.1f, nullptr, true);
    CHECK(f.world.get<Animator>(entity).time == doctest::Approx(0.7f));
    CHECK(equal(f.previous(entity), before));
}

TEST_CASE("previous skin matrices equal the current ones on the first pose and lag one update afterwards")
{
    Fixture f;
    const Entity entity = character(f.world, animator());
    f.animation.waitIdle();
    CHECK(equal(f.previous(entity), f.skin(entity)));
    const std::vector<SkinMatrix> first = f.skin(entity);
    f.animation.update(0.2f, &f.jobs, true);
    CHECK(equal(f.previous(entity), first));
    CHECK_FALSE(equal(f.skin(entity), first));
    const std::vector<SkinMatrix> second = f.skin(entity);
    f.animation.update(0.2f, &f.jobs, true);
    CHECK(equal(f.previous(entity), second));
}

TEST_CASE("the world bounds of a pose contain every vertex skinned on the CPU")
{
    Fixture f;
    Transform transform;
    transform.position = Vec3(3.0f, -1.0f, 2.0f);
    transform.rotation = glm::angleAxis(0.7f, glm::normalize(Vec3(1.0f, 2.0f, 0.5f)));
    transform.scale = Vec3(2.0f, 0.5f, 1.5f);
    const Entity entity = character(f.world, animator(kClip, 0.6f), transform);
    f.animation.waitIdle();
    const ModelData model = loadRigged();
    const MeshData& mesh = model.meshes.at(0);
    REQUIRE(mesh.skin.size() == mesh.vertices.size());
    const std::vector<SkinMatrix> skin = f.skin(entity);
    const Aabb& bounds = f.pose(entity).bounds;
    for (usize v = 0; v < mesh.vertices.size(); ++v)
    {
        Vec3 skinned(0.0f);
        for (u32 i = 0; i < kMaxJointInfluences; ++i)
        {
            const f32 weight = static_cast<f32>(mesh.skin[v].weights[i]) / 65535.0f;
            skinned += weight * apply(skin.at(mesh.skin[v].joints[i]), mesh.vertices[v].position);
        }
        const Vec3 world = Vec3(transform.matrix() * Vec4(skinned, 1.0f));
        CAPTURE(v);
        CHECK(glm::all(glm::greaterThanEqual(world, bounds.min - Vec3(1e-4f))));
        CHECK(glm::all(glm::lessThanEqual(world, bounds.max + Vec3(1e-4f))));
    }
}

TEST_CASE("fixed-step game sessions animate the same way on every run")
{
    reflectAll();
    const auto run = [] {
        test::AssetProject project;
        REQUIRE(project.files.mount("assets", project.directory.path / "game") == FileError::None);
        JobSystem jobs{JobSystemDesc{2, 256}};
        AssetStreamer streamer(project.files, jobs);
        GameSessionDesc desc;
        desc.jobs = &jobs;
        desc.files = &project.files;
        desc.streamer = &streamer;
        desc.audioOutput = AudioOutput::Offline;
        desc.fixedStep = true;
        auto session = GameSession::create(desc);
        REQUIRE(session.hasValue());
        GameSession& game = **session;
        Animator value = animator();
        value.speed = 1.3f;
        const Entity entity = character(game.world(), value);
        REQUIRE(game.animation() != nullptr);
        game.animation()->waitIdle();
        game.update(1.0);
        CHECK(game.world().get<Animator>(entity).time == 0.0f); // not started: the saved time stays
        game.start();
        for (int i = 0; i < 100; ++i)
        {
            game.update(0.5); // a fixed step whatever the time passed
        }
        const AnimationPose& pose = game.world().get<AnimationPose>(entity);
        const std::span<const SkinMatrix> skin = game.animation()->skinMatrices(pose.slot);
        return std::make_pair(game.world().get<Animator>(entity).time,
                              std::vector<SkinMatrix>(skin.begin(), skin.end()));
    };
    const auto first = run();
    const auto second = run();
    const f64 step = ProjectSettings{}.physics.stepSeconds;
    CHECK(first.first == doctest::Approx(std::fmod(100.0 * step * 1.3, kLinearDuration)).epsilon(1e-4));
    CHECK(first.first == second.first);
    CHECK(equal(first.second, second.second));
    CHECK(first.second.size() == 5);
}

TEST_CASE("GameSession has no AnimationWorld without a streamer")
{
    reflectAll();
    test::TempDirectory directory;
    VirtualFileSystem files;
    REQUIRE(files.mount("assets", directory.path, MountAccess::ReadWrite) == FileError::None);
    JobSystem jobs{JobSystemDesc{2, 256}};
    GameSessionDesc desc;
    desc.jobs = &jobs;
    desc.files = &files;
    desc.audioOutput = AudioOutput::Offline;
    auto session = GameSession::create(desc);
    REQUIRE(session.hasValue());
    CHECK((*session)->animation() == nullptr);
}

TEST_CASE("a saved game restores where the clip was")
{
    Fixture f;
    const Entity entity = character(f.world, animator(kClip, 0.4f));
    f.world.emplace<Saved>(entity);
    f.animation.waitIdle();
    const auto captured = captureSave(f.world);
    REQUIRE(captured.hasValue());
    f.animation.update(0.5f, nullptr, true);
    REQUIRE(f.world.get<Animator>(entity).time == doctest::Approx(0.9f));

    const auto applied = applySave(f.world, captured->world);
    REQUIRE(applied.hasValue());
    REQUIRE(applied->entities.size() == 1);
    const Entity restored = applied->entities[0];
    CHECK(f.world.get<Animator>(restored).time == doctest::Approx(0.4f));
    f.animation.update(0.0f, nullptr, false);
    CHECK(f.animation.statistics().poses == 1);
    CHECK(equal(f.skin(restored), expectedSkin(loadRigged(), 0.4f)));
}

TEST_CASE("an Animator is bound again when its model file is reloaded")
{
    Fixture f;
    const Entity entity = character(f.world, animator(kClip, 0.5f));
    f.animation.waitIdle();
    const std::vector<SkinMatrix> before = f.skin(entity);

    // Another file reloads nothing.
    CHECK(f.streamer.reload("game:/binary.glb"));
    f.animation.waitIdle();
    CHECK(equal(f.skin(entity), before));

    // Scaling the skeleton's root node changes every skin matrix.
    std::string text;
    REQUIRE(f.project.files.readText("game:/rigged.gltf", text) == FileError::None);
    const std::string from = "\"name\": \"Armature\",";
    const usize at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), from + " \"scale\": [2, 2, 2],");
    REQUIRE(f.project.files.writeText("game:/rigged.gltf", text) == FileError::None);
    CHECK(f.streamer.reload("game:/rigged.gltf"));
    f.animation.update(0.0f, nullptr, false); // starts the reload; the pose stays meanwhile
    CHECK(f.pose(entity).jointCount == 5);
    f.animation.waitIdle();
    CHECK(f.pose(entity).jointCount == 5);
    CHECK_FALSE(equal(f.skin(entity), before));
    CHECK(f.animation.statistics().failures == 0);

    // A reload that fails keeps the pose.
    const std::vector<SkinMatrix> scaled = f.skin(entity);
    REQUIRE(f.project.files.writeText("game:/rigged.gltf", "not a model") == FileError::None);
    CHECK(f.streamer.reload("game:/rigged.gltf"));
    f.animation.waitIdle();
    CHECK(f.pose(entity).jointCount == 5);
    CHECK(equal(f.skin(entity), scaled));
}

TEST_CASE("a steady-state update allocates nothing")
{
    Fixture f;
    for (u32 i = 0; i < 20; ++i)
    {
        character(f.world, animator(kClip, 0.05f * static_cast<f32>(i)));
    }
    f.world.create(Transform{}, renderer(), Animator{.skeleton = AssetRef("game:/rigged.gltf#skin/1")});
    f.animation.waitIdle();
    for (int i = 0; i < 3; ++i)
    {
        f.animation.update(1.0f / 60.0f, &f.jobs, true);
    }
    usize allocations = 0;
    {
        const test::ScopedAllocationCounter counter;
        for (int i = 0; i < 10; ++i)
        {
            f.animation.update(1.0f / 60.0f, &f.jobs, true);
        }
        allocations = counter.count();
    }
    CHECK(allocations == 0);
    CHECK(f.animation.statistics().posed == 20);
}
