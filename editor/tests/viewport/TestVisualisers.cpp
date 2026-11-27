#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/animation/AnimationWorld.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/audio/AudioComponents.hpp>
#include <illusion/camera/CameraComponents.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/editor/viewport/Visualisers.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/Ragdoll.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/render/DebugDraw.hpp>
#include <illusion/render/SceneComponents.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::editor::viewport;

// CPU tests of the DebugDraw visualisers: what each Component records, the scope, hidden and Inactive
// entities, a full DebugDraw, and the skeletons of Animators with IK targets, spring chains and ragdolls.

namespace
{

VisualiserOptions everything()
{
    VisualiserOptions options;
    options.all = true;
    return options;
}

ViewportVisualisers only(bool ViewportVisualisers::* kind)
{
    ViewportVisualisers kinds{false, false, false, false, false, false, false, false};
    kinds.*kind = true;
    return kinds;
}

Entity spawn(World& world, Vec3 position)
{
    const Entity entity = world.create();
    world.set(entity, Transform{position, Quat(1.0f, 0.0f, 0.0f, 0.0f), Vec3(1.0f)});
    return entity;
}

Collider shaped(ColliderShape shape, f32 radius = 0.5f)
{
    Collider collider;
    collider.shape = shape;
    collider.radius = radius;
    return collider;
}

bool near(Vec3 a, Vec3 b)
{
    return glm::length(a - b) < 1e-4f;
}

} // namespace

TEST_CASE("a box collider records its twelve edges in the entity's and the shape's space")
{
    World world;
    const Entity entity = spawn(world, Vec3(10.0f, 0.0f, 0.0f));
    Collider collider;
    collider.halfExtents = Vec3(1.0f, 2.0f, 3.0f);
    collider.offset = Vec3(0.0f, 1.0f, 0.0f);
    world.set(entity, collider);
    DebugDraw draw(64);
    VisualiserOptions options = everything();
    options.kinds = only(&ViewportVisualisers::colliders);
    const VisualiserStats stats = recordVisualisers(world, draw, options);
    CHECK(stats.objects == 1);
    CHECK_FALSE(stats.full);
    REQUIRE(draw.lines().size() == 12);
    Vec3 low(1e9f);
    Vec3 high(-1e9f);
    for (const DebugLine& line : draw.lines())
    {
        low = glm::min(low, glm::min(Vec3(line.start), Vec3(line.end)));
        high = glm::max(high, glm::max(Vec3(line.start), Vec3(line.end)));
        CHECK(line.color == options.colors.collider);
    }
    CHECK(near(low, Vec3(9.0f, -1.0f, -3.0f)));
    CHECK(near(high, Vec3(11.0f, 3.0f, 3.0f)));

    // Sensors are drawn in the sensor colour.
    RigidBody body;
    body.sensor = true;
    world.set(entity, body);
    draw.clear();
    (void)recordVisualisers(world, draw, options);
    REQUIRE_FALSE(draw.lines().empty());
    CHECK(draw.lines()[0].color == options.colors.sensor);
}

TEST_CASE("every visualiser kind records lines for its Component")
{
    World world;
    world.set(spawn(world, Vec3(0.0f)), PointLight{});
    world.set(spawn(world, Vec3(1.0f, 0.0f, 0.0f)), DirectionalLight{});
    world.set(spawn(world, Vec3(2.0f, 0.0f, 0.0f)), Camera{});
    Camera orthographic;
    orthographic.projection = CameraProjection::Orthographic;
    world.set(spawn(world, Vec3(3.0f, 0.0f, 0.0f)), orthographic);
    world.set(spawn(world, Vec3(4.0f, 0.0f, 0.0f)), shaped(ColliderShape::Sphere));
    world.set(spawn(world, Vec3(5.0f, 0.0f, 0.0f)), shaped(ColliderShape::Capsule));
    world.set(spawn(world, Vec3(6.0f, 0.0f, 0.0f)), shaped(ColliderShape::Cylinder));
    world.set(spawn(world, Vec3(7.0f, 0.0f, 0.0f)), shaped(ColliderShape::Mesh));
    world.set(spawn(world, Vec3(8.0f, 0.0f, 0.0f)), CharacterController{});
    const Entity other = spawn(world, Vec3(9.0f, 0.0f, 0.0f));
    Joint hinge;
    hinge.type = JointType::Hinge;
    hinge.connected = other;
    hinge.limited = true;
    hinge.minLimit = -0.5f;
    hinge.maxLimit = 1.0f;
    world.set(spawn(world, Vec3(10.0f, 0.0f, 0.0f)), hinge);
    Joint slider;
    slider.type = JointType::Slider;
    slider.limited = true;
    slider.minLimit = -0.2f;
    slider.maxLimit = 0.4f;
    world.set(spawn(world, Vec3(11.0f, 0.0f, 0.0f)), slider);
    Joint distance;
    distance.type = JointType::Distance;
    distance.connected = other;
    distance.minLimit = 0.5f;
    distance.maxLimit = 2.0f;
    world.set(spawn(world, Vec3(12.0f, 0.0f, 0.0f)), distance);
    AudioSource cone;
    cone.coneInnerAngle = 1.0f;
    cone.coneOuterAngle = 2.0f;
    cone.maxDistance = 20.0f;
    world.set(spawn(world, Vec3(13.0f, 0.0f, 0.0f)), cone);
    AudioSource flat;
    flat.spatial = false;
    world.set(spawn(world, Vec3(14.0f, 0.0f, 0.0f)), flat);

    DebugDraw draw(65536);
    const VisualiserStats stats = recordVisualisers(world, draw, everything());
    // Every Component above except the non-spatial sound.
    CHECK(stats.objects == 13);
    CHECK_FALSE(stats.full);

    const auto count = [&](bool ViewportVisualisers::* kind) {
        draw.clear();
        VisualiserOptions options = everything();
        options.kinds = only(kind);
        (void)recordVisualisers(world, draw, options);
        return draw.lines().size();
    };
    // A sphere is three circles of 16 segments.
    CHECK(count(&ViewportVisualisers::lights) > 48);
    // Perspective: two rectangles, their edges, the up triangle and lines from the eye; orthographic: no eye.
    CHECK(count(&ViewportVisualisers::cameras) == 18 + 14);
    CHECK(count(&ViewportVisualisers::colliders) > 48 + 12);
    CHECK(count(&ViewportVisualisers::characters) > 32);
    CHECK(count(&ViewportVisualisers::joints) > 9);
    // Two spheres and two cones.
    CHECK(count(&ViewportVisualisers::audio) == 2 * 48 + 2 * 20);
}

TEST_CASE(
    "visualisers draw the selected entities only unless asked for all, and never hidden or inactive ones")
{
    World world;
    const Entity a = spawn(world, Vec3(0.0f));
    const Entity b = spawn(world, Vec3(5.0f, 0.0f, 0.0f));
    const Entity c = spawn(world, Vec3(9.0f, 0.0f, 0.0f));
    for (const Entity entity : {a, b, c})
    {
        world.set(entity, Collider{});
    }
    world.emplace<Inactive>(c);
    const auto bits = [](std::initializer_list<Entity> entities) {
        std::vector<u64> out(1, 0);
        for (const Entity entity : entities)
        {
            out[0] |= u64{1} << entity.index;
        }
        return out;
    };
    DebugDraw draw(4096);
    VisualiserOptions options;
    CHECK(recordVisualisers(world, draw, options).objects == 0);

    const std::vector<u64> selection = bits({b, c});
    options.selection = selection;
    CHECK(recordVisualisers(world, draw, options).objects == 1);

    options.all = true;
    const std::vector<u64> hidden = bits({a});
    options.hidden = hidden;
    CHECK(recordVisualisers(world, draw, options).objects == 1);
    options.hidden = {};
    CHECK(recordVisualisers(world, draw, options).objects == 2);

    options.kinds = ViewportVisualisers{false, false, false, false, false, false, false, false};
    draw.clear();
    CHECK(recordVisualisers(world, draw, options).objects == 0);
    CHECK(draw.lines().empty());
}

TEST_CASE("visualisers stop at a full DebugDraw and say so")
{
    World world;
    for (int i = 0; i < 4; ++i)
    {
        world.set(spawn(world, Vec3(static_cast<f32>(i), 0.0f, 0.0f)), Collider{});
    }
    DebugDraw draw(20);
    const VisualiserStats stats = recordVisualisers(world, draw, everything());
    CHECK(stats.full);
    CHECK(draw.lines().size() == 20);
}

TEST_CASE("visualisers skip degenerate and non-finite geometry")
{
    World world;
    world.set(spawn(world, Vec3(0.0f)), shaped(ColliderShape::Sphere, 0.0f));
    world.set(spawn(world, Vec3(std::nanf(""), 0.0f, 0.0f)), Collider{});
    DebugDraw draw(256);
    const VisualiserStats stats = recordVisualisers(world, draw, everything());
    CHECK(stats.objects == 2);
    CHECK(draw.lines().empty());
}

namespace
{

// Ragdoll profiles given by the test, counting the lookups.
class FixedRagdollParts final : public RagdollParts
{
public:
    std::span<const RagdollPart> parts(const Ragdoll&, const Skeleton&, std::span<const Aabb>) override
    {
        ++calls;
        return list;
    }

    std::vector<RagdollPart> list;
    u32 calls = 0;
};

usize linesOf(const DebugDraw& draw, Vec4 color)
{
    return static_cast<usize>(std::count_if(draw.lines().begin(), draw.lines().end(),
                                            [&](const DebugLine& line) { return line.color == color; }));
}

} // namespace

TEST_CASE("the skeleton visualiser draws posed Animators with their IK targets, spring chains and ragdolls")
{
    registerAnimationComponents(ComponentRegistry::instance());
    VirtualFileSystem files;
    REQUIRE(files.mount("assets", utf8Path(ILLUSION_SAMPLES_DIR) / "showcase" / "assets") == FileError::None);
    JobSystem jobs;
    AssetStreamer streamer(files, jobs, AssetStreamerDesc{.cacheMount = ""});
    World world;
    AnimationWorld animation(world, streamer);

    const Entity strider = spawn(world, Vec3(2.0f, 0.0f, 0.0f));
    Animator animator;
    animator.skeleton = AssetRef("assets:/models/strider.gltf#skin/0");
    animator.clip = AssetRef("assets:/models/strider.gltf#animation/idle");
    world.set(strider, animator);
    LookAt look;
    look.joint = "head";
    look.point = Vec3(2.0f, 1.5f, 3.0f);
    world.set(strider, look);
    SpringBone spring;
    spring.root = "spine";
    spring.colliders[0].joint = "hips";
    world.set(strider, spring);
    world.set(strider, Ragdoll{});
    // A limb on a child, as the showcase's second leg.
    const Entity leg = spawn(world, Vec3(0.0f));
    REQUIRE(setParent(world, leg, strider).hasValue());
    TwoBoneIk ik;
    ik.joint = "foot_r";
    ik.point = Vec3(2.2f, 0.0f, 0.3f);
    world.set(leg, ik);
    // An Animator whose model is missing draws nothing.
    Animator missing = animator;
    missing.skeleton = AssetRef("assets:/models/none.gltf#skin/0");
    world.set(spawn(world, Vec3(0.0f)), missing);
    animation.waitIdle();
    REQUIRE(animation.findSlot(strider).has_value());

    const VisualiserColors colors;
    FixedRagdollParts ragdolls;
    ragdolls.list.push_back(RagdollPart{.joint = "hips"});
    ragdolls.list.push_back(RagdollPart{.joint = "no such joint"});
    DebugDraw draw(65536);
    VisualiserOptions options = everything();
    options.kinds = only(&ViewportVisualisers::skeleton);
    // Without poses there is nothing to draw.
    CHECK(recordVisualisers(world, draw, options).objects == 0);
    CHECK(draw.lines().empty());

    options.animation = &animation;
    CHECK(recordVisualisers(world, draw, options).objects == 1);
    // Nine joints: eight bones and a cross of three lines at every joint.
    CHECK(linesOf(draw, colors.bone) == 8);
    CHECK(linesOf(draw, colors.skeletonJoint) == 9 * 3);
    // Skeletons are inside their meshes, so they are drawn over the scene.
    CHECK(std::all_of(draw.lines().begin(), draw.lines().end(),
                      [](const DebugLine& line) { return line.end.w == 1.0f; }));
    // The chain below the spine (the head) and its collider.
    CHECK(linesOf(draw, colors.spring) > 1 + 48);
    // LookAt and TwoBoneIk: a diamond at each target and at the pole of the limb, and lines to them (the
    // foot's line is empty when the limb reaches its target).
    CHECK(linesOf(draw, colors.ikTarget) >= 3 * 12 + 2);
    CHECK(linesOf(draw, colors.ragdoll) == 0); // no profiles given
    // A bone ends at a joint's world position: the root joint, moved with the Entity.
    const Mat4 hips =
        world.get<Transform>(strider).matrix() * animation.jointMatrices(*animation.findSlot(strider))[0];
    const Vec3 root(hips[3]);
    CHECK(std::any_of(draw.lines().begin(), draw.lines().end(), [&](const DebugLine& line) {
        return line.color == colors.bone && near(Vec3(line.start), root);
    }));

    draw.clear();
    options.ragdolls = &ragdolls;
    (void)recordVisualisers(world, draw, options);
    CHECK(ragdolls.calls == 1);
    CHECK(linesOf(draw, colors.ragdoll) > 0); // the hips capsule; the part of an unknown joint is skipped

    // Only a selected Animator is drawn; selecting the leg alone draws nothing.
    draw.clear();
    std::vector<u64> selection(1, u64{1} << leg.index);
    options.all = false;
    options.selection = selection;
    CHECK(recordVisualisers(world, draw, options).objects == 0);
    CHECK(draw.lines().empty());
    selection[0] = u64{1} << strider.index;
    CHECK(recordVisualisers(world, draw, options).objects == 1);
    CHECK(linesOf(draw, colors.ikTarget) >= 3 * 12 + 2);
    world.emplace<Inactive>(strider);
    draw.clear();
    CHECK(recordVisualisers(world, draw, options).objects == 0);
    CHECK(draw.lines().empty());
}

TEST_CASE("the skeleton visualiser dims the joints that the rig map leaves without a bone")
{
    registerAnimationComponents(ComponentRegistry::instance());
    VirtualFileSystem files;
    REQUIRE(files.mount("assets", utf8Path(ILLUSION_ENGINE_TEST_DATA) / "gltf") == FileError::None);
    JobSystem jobs;
    AssetStreamer streamer(files, jobs, AssetStreamerDesc{.cacheMount = ""});
    World world;
    AnimationWorld animation(world, streamer);
    const Entity character = spawn(world, Vec3(0.0f));
    Animator animator;
    animator.skeleton = AssetRef("assets:/humanoid_b.gltf#skin/0");
    world.set(character, animator);
    animation.waitIdle();
    const std::optional<u32> slot = animation.findSlot(character);
    REQUIRE(slot.has_value());
    const Skeleton* skeleton = animation.skeleton(*slot);
    REQUIRE(skeleton != nullptr);
    REQUIRE(skeleton->rig.bones.size() == skeleton->joints.size());
    usize unmapped = 0;
    usize unmappedBones = 0;
    for (usize i = 0; i < skeleton->joints.size(); ++i)
    {
        if (skeleton->rig.bones[i].empty())
        {
            ++unmapped;
            unmappedBones += skeleton->joints[i].parent >= 0 ? 1 : 0;
        }
    }
    // The twist joints have no humanoid bone.
    REQUIRE(unmapped > 0);
    REQUIRE(unmapped < skeleton->joints.size());

    const VisualiserColors colors;
    const Vec4 dimmedJoint(Vec3(colors.skeletonJoint), colors.skeletonJoint.a * colors.unmappedAlpha);
    const Vec4 dimmedBone(Vec3(colors.bone), colors.bone.a * colors.unmappedAlpha);
    DebugDraw draw(65536);
    VisualiserOptions options = everything();
    options.kinds = only(&ViewportVisualisers::skeleton);
    options.animation = &animation;
    CHECK(recordVisualisers(world, draw, options).objects == 1);
    CHECK(linesOf(draw, dimmedJoint) == unmapped * 3);
    CHECK(linesOf(draw, colors.skeletonJoint) == (skeleton->joints.size() - unmapped) * 3);
    CHECK(linesOf(draw, dimmedBone) == unmappedBones);
}

TEST_CASE("virtual cameras draw their frusta, and the live and blending ones always, at the brain's pose")
{
    World world;
    const Entity idle = spawn(world, Vec3(0.0f));
    const Entity live = spawn(world, Vec3(10.0f, 0.0f, 0.0f));
    const Entity from = spawn(world, Vec3(20.0f, 0.0f, 0.0f));
    for (const Entity entity : {idle, live, from})
    {
        world.set(entity, VirtualCamera{});
    }
    const Entity brain = spawn(world, Vec3(30.0f, 0.0f, 0.0f));
    CameraBrainState state;
    state.live = live;
    state.from = from;
    state.blending = true;
    world.set(brain, state);
    VirtualCameraState evaluated;
    evaluated.evaluated = 1;
    evaluated.pose.position = Vec3(10.0f, 5.0f, 0.0f);
    world.set(live, evaluated);

    VisualiserOptions options;
    options.kinds = only(&ViewportVisualisers::cameras);
    const VisualiserColors& colors = options.colors;
    DebugDraw draw(1024);
    // Nothing selected: only the live and the blending camera.
    CHECK(recordVisualisers(world, draw, options).objects == 2);
    CHECK(linesOf(draw, colors.liveCamera) > 0);
    CHECK(linesOf(draw, colors.blendingCamera) > 0);
    CHECK(linesOf(draw, colors.virtualCamera) == 0);
    const bool atBrainPose = std::ranges::any_of(draw.lines(), [&](const DebugLine& line) {
        return line.color == colors.liveCamera && near(Vec3(line.start), Vec3(10.0f, 5.0f, 0.0f));
    });
    CHECK(atBrainPose);

    draw.clear();
    std::vector<u64> selection(1, u64{1} << idle.index);
    options.selection = selection;
    CHECK(recordVisualisers(world, draw, options).objects == 3);
    CHECK(linesOf(draw, colors.virtualCamera) > 0);

    // The previewed camera is drawn like a live one, at its Transform.
    draw.clear();
    options.previewCamera = idle;
    CHECK(recordVisualisers(world, draw, options).objects == 3);
    CHECK(linesOf(draw, colors.virtualCamera) == 0);

    // Hidden and Inactive cameras are not drawn.
    draw.clear();
    std::vector<u64> hidden(1, u64{1} << live.index);
    options.hidden = hidden;
    CHECK(recordVisualisers(world, draw, options).objects == 2);
    world.emplace<Inactive>(from);
    draw.clear();
    CHECK(recordVisualisers(world, draw, options).objects == 1);
}
