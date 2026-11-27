// Sockets (ADR 0061): BoneAttachment children follow joints through clips, graph cross-fades and root
// motion; offsets in joint space; joints resolved again after a rename or a model reload; problems logged
// once; Inactive Entities skipped; kinematic bodies following; AnimationService::jointTransform(); fixed-step
// determinism and allocation.

#include "TestSupport.hpp"
#include "animation/WalkerAssets.hpp"
#include "assets/AssetTestProject.hpp"
#include "physics/PhysicsTestSupport.hpp"

#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/animation/AnimationWorld.hpp>
#include <illusion/animation/AnimationWorldService.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsWorld.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/runtime/GameSession.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace illusion;

namespace
{

using namespace test::walker;

constexpr std::string_view kTurn = "game:/walker.gltf#animation/turn";
constexpr f32 kQuarterTurn = std::numbers::pi_v<f32> / 2.0f;

void reflectAll()
{
    reflectCoreComponents();
    reflectRenderComponents();
    reflectPhysicsComponents();
    registerAnimationComponents(ComponentRegistry::instance());
}

Animator clipAnimator(std::string_view clip, f32 time = 0.0f)
{
    Animator result;
    result.skeleton = AssetRef(kSkeleton);
    result.clip = AssetRef(clip);
    result.time = time;
    return result;
}

Animator graphAnimator(std::string_view graph)
{
    Animator result;
    result.skeleton = AssetRef(kSkeleton);
    result.graph = AssetRef(graph);
    return result;
}

BoneAttachment attachment(std::string_view joint, Vec3 translation = Vec3(0.0f))
{
    BoneAttachment result;
    result.joint = joint;
    result.translation = translation;
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

// Creates a child of @p parent attached to @p socket.
Entity attach(World& world, Entity parent, const BoneAttachment& socket, Transform transform = {})
{
    const Entity child = world.create(transform, socket);
    REQUIRE(setParent(world, child, parent, ParentMode::KeepLocal).hasValue());
    return child;
}

bool near(Vec3 a, Vec3 b, f32 epsilon = 1e-4f)
{
    return glm::all(glm::lessThanEqual(glm::abs(a - b), Vec3(epsilon)));
}

bool near(const Mat4& a, const Mat4& b, f32 epsilon = 1e-4f)
{
    for (int c = 0; c < 4; ++c)
    {
        if (!glm::all(glm::lessThanEqual(glm::abs(a[c] - b[c]), Vec4(epsilon))))
        {
            return false;
        }
    }
    return true;
}

// The offset of @p socket as a matrix.
Mat4 offsetOf(const BoneAttachment& socket)
{
    return Transform{socket.translation, socket.rotation, socket.scale}.matrix();
}

// Routes the global logger into memory while alive.
struct LogCapture
{
    std::shared_ptr<MemorySink> sink = std::make_shared<MemorySink>(256);
    LogLevel previous = Logger::instance().level();

    LogCapture()
    {
        Logger::instance().addSink(sink);
        Logger::instance().setLevel(LogLevel::Info);
    }
    ~LogCapture()
    {
        Logger::instance().setLevel(previous);
        Logger::instance().removeSink(sink.get());
    }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    [[nodiscard]] usize count(std::string_view text) const
    {
        usize found = 0;
        for (const MemorySink::Entry& entry : sink->entries())
        {
            found += entry.text.find(text) != std::string::npos ? 1u : 0u;
        }
        return found;
    }
};

struct Fixture
{
    Fixture()
    {
        reflectAll();
        writeWalker(project.files);
    }

    void step(f32 seconds) { animation.update(seconds, &jobs, true); }

    // Makes Disabled Entities Inactive.
    void refreshHierarchy() { updateTransformHierarchy(world, hierarchy, nullptr); }

    // The world-space placement of @p child that its socket on @p parent asks for.
    [[nodiscard]] Mat4 socketMatrix(Entity parent, Entity child) const
    {
        const BoneAttachment& socket = world.get<BoneAttachment>(child);
        const Expected<Mat4, AnimationError> joint = service.jointTransform(parent, socket.joint);
        REQUIRE(joint.hasValue());
        return *joint * offsetOf(socket);
    }

    test::AssetProject project;
    JobSystem jobs{JobSystemDesc{2, 256}};
    AssetStreamer streamer{project.files, jobs};
    World world;
    HierarchyIndex hierarchy;
    AnimationWorld animation{world, streamer};
    AnimationWorldService service{animation};
};

// A fixed-step, started GameSession over the walker files.
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

} // namespace

TEST_CASE("BoneAttachment is reflected as a saved Component with units and its manual section")
{
    reflectAll();
    const ComponentRegistry& registry = ComponentRegistry::instance();
    const ComponentId id = registry.findByStableName("illusion.BoneAttachment");
    REQUIRE(id != kInvalidComponentId);
    const TypeReflection* type = registry.reflection(id);
    REQUIRE(type != nullptr);
    CHECK_FALSE(type->meta.transient);
    CHECK(type->meta.docsAnchor == "manual/animation.md#sockets");
    CHECK_FALSE(type->meta.icon.empty());
    REQUIRE(type->field("joint") != nullptr);
    REQUIRE(type->field("translation") != nullptr);
    CHECK(type->field("translation")->meta.unit == Unit::Meters);
    CHECK(type->field("rotation") != nullptr);
    CHECK(type->field("scale") != nullptr);
}

TEST_CASE("an attached child follows its joint through clip playback")
{
    Fixture f;
    const Entity walker = f.world.create(turned(kQuarterTurn, Vec3(1.0f, 0.0f, 0.0f)), clipAnimator(kWalk));
    const Entity child = attach(f.world, walker, attachment("hips", Vec3(0.0f, 0.5f, 0.0f)));
    f.animation.waitIdle();
    CHECK(near(f.world.get<LocalTransform>(child).position, Vec3(0.0f, 1.5f, 0.0f)));

    f.step(0.25f); // the hips are 0.5 m along +z
    CHECK(near(f.world.get<LocalTransform>(child).position, Vec3(0.0f, 1.5f, 0.5f)));
    // The parent turns +z to +x.
    CHECK(near(f.world.get<Transform>(child).position, Vec3(1.5f, 1.5f, 0.0f)));
    CHECK(near(f.world.get<Transform>(child).matrix(), f.socketMatrix(walker, child)));

    f.step(0.25f);
    CHECK(near(f.world.get<LocalTransform>(child).position, Vec3(0.0f, 1.5f, 1.0f)));
    CHECK(near(f.world.get<Transform>(child).matrix(), f.socketMatrix(walker, child)));
}

TEST_CASE("the offset is applied in the joint's space")
{
    Fixture f;
    // Half of "turn": the hips are turned 45 degrees about +y.
    const Entity walker = f.world.create(Transform{}, clipAnimator(kTurn, 0.5f));
    BoneAttachment socket = attachment("hips", Vec3(0.0f, 0.0f, 1.0f));
    socket.rotation = glm::angleAxis(kQuarterTurn, Vec3(1.0f, 0.0f, 0.0f));
    socket.scale = Vec3(2.0f);
    const Entity child = attach(f.world, walker, socket);
    f.animation.waitIdle(); // not advancing: the saved time is shown
    const LocalTransform& local = f.world.get<LocalTransform>(child);
    const f32 half = std::sqrt(0.5f);
    CHECK(near(local.position, Vec3(half, 1.0f, half)));
    CHECK(near(local.scale, Vec3(2.0f)));
    const Quat expected = glm::angleAxis(kQuarterTurn / 2.0f, Vec3(0.0f, 1.0f, 0.0f)) * socket.rotation;
    CHECK(std::abs(glm::dot(local.rotation, expected)) == doctest::Approx(1.0f).epsilon(1e-5));
    CHECK(near(f.world.get<Transform>(child).matrix(), f.socketMatrix(walker, child)));
}

TEST_CASE("an attached child follows a graph through a cross-fade")
{
    Fixture f;
    const Entity walker = f.world.create(turned(0.4f, Vec3(2.0f, 0.0f, -1.0f)), graphAnimator(kGraph));
    const Entity child = attach(f.world, walker, attachment("hips", Vec3(0.0f, 0.0f, 0.3f)));
    f.animation.waitIdle();
    REQUIRE(f.service.setFloat(walker, "speed", 1.0f).hasValue());
    f.step(0.1f);
    REQUIRE(f.service.setTrigger(walker, "go").hasValue());
    std::vector<Quat> rotations;
    for (int i = 0; i < 6; ++i) // through the 0.2 s fade to "spin" and on
    {
        f.step(0.05f);
        CHECK(near(f.world.get<Transform>(child).matrix(), f.socketMatrix(walker, child)));
        rotations.push_back(f.world.get<LocalTransform>(child).rotation);
    }
    CHECK(rotations.front() != rotations.back()); // the hips turned during the fade
}

TEST_CASE("an attached child follows root motion in a game session, with a kinematic body")
{
    Session s;
    World& world = s.world();
    const Entity walker = world.create(Transform{}, graphAnimator(kRootGraph));
    const Entity child = attach(world, walker, attachment("hips", Vec3(0.0f, 0.0f, 0.5f)));
    world.emplace<RigidBody>(child, test::body(MotionType::Kinematic));
    world.emplace<Collider>(child, test::sphere(0.1f));
    s.session->animation()->waitIdle();
    s.session->start();
    s.run(60); // one second of walking, 2 m along +z; root motion keeps the hips above the Entity
    const Vec3 walked = world.get<Transform>(walker).position;
    CHECK(walked.z == doctest::Approx(2.0f).epsilon(1e-3));
    const Vec3 socket = world.get<Transform>(child).position;
    CHECK(near(socket, walked + Vec3(0.0f, 1.0f, 0.5f)));

    // The body was moved to this update's socket, not the last one's.
    const std::optional<RaycastHit> hit =
        s.session->physics().raycast(socket + Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 0.0f, -1.0f), 2.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->entity == child);
    CHECK(hit->point.z == doctest::Approx(socket.z + 0.1f).epsilon(1e-4));
    CHECK(s.session->physics().linearVelocity(child).z == doctest::Approx(2.0f).epsilon(0.02));
}

TEST_CASE("a changed joint name and a reloaded model resolve the socket again")
{
    Fixture f;
    LogCapture log;
    const Entity walker = f.world.create(Transform{}, clipAnimator(kWalk, 0.5f));
    const Entity child = attach(f.world, walker, attachment("hips", Vec3(0.0f, 0.5f, 0.0f)));
    f.animation.waitIdle();
    CHECK(near(f.world.get<LocalTransform>(child).position, Vec3(0.0f, 1.5f, 1.0f)));

    f.world.get<BoneAttachment>(child).joint = "root";
    f.animation.update(0.0f, &f.jobs, false);
    CHECK(near(f.world.get<LocalTransform>(child).position, Vec3(0.0f, 0.5f, 0.0f)));

    // The model's "hips" becomes "pelvis": a socket on "pelvis" waits for the reload.
    f.world.get<BoneAttachment>(child).joint = "pelvis";
    f.animation.update(0.0f, &f.jobs, false);
    CHECK(log.count("no joint 'pelvis'") == 1);
    std::string renamed;
    REQUIRE(f.project.files.readText("game:/walker.gltf", renamed) == FileError::None);
    const std::string from = R"("name": "hips")";
    REQUIRE(renamed.find(from) != std::string::npos);
    renamed.replace(renamed.find(from), from.size(), R"("name": "pelvis")");
    REQUIRE(f.project.files.writeText("game:/walker.gltf", renamed) == FileError::None);
    CHECK(f.streamer.reload("game:/walker.gltf"));
    f.animation.waitIdle();
    CHECK(
        near(f.world.get<LocalTransform>(child).position, Vec3(0.0f, 1.5f, 1.0f))); // the clip moves "pelvis"
    // The rig still gives "pelvis" the bone "hips", so the old name finds it (ADR 0065).
    CHECK(f.service.jointTransform(walker, "hips").hasValue());
    CHECK(f.service.jointTransform(walker, "root2").error() == AnimationError::UnknownName);
}

TEST_CASE("a missing joint or Animator is logged once and leaves the child where it is")
{
    Fixture f;
    LogCapture log;
    const Entity walker = f.world.create(Transform{}, clipAnimator(kWalk));
    const Entity plain = f.world.create(Transform{});
    const Transform start = turned(0.3f, Vec3(4.0f, 5.0f, 6.0f));
    const Entity noJoint = attach(f.world, walker, attachment("tail"), start);
    const Entity noAnimator = attach(f.world, plain, attachment("hips"), start);
    const Entity root = f.world.create(start, attachment("hips"));
    f.animation.waitIdle();
    for (int i = 0; i < 5; ++i)
    {
        f.step(0.1f);
    }
    CHECK(log.count("no joint 'tail'") == 1);
    CHECK(log.count("has no Animator") == 2);
    for (const Entity entity : {noJoint, noAnimator, root})
    {
        CHECK(f.world.get<Transform>(entity).position == start.position);
    }
    CHECK(f.service.jointTransform(walker, "tail").error() == AnimationError::UnknownName);
}

TEST_CASE("Inactive attached children are left alone")
{
    Fixture f;
    const Entity walker = f.world.create(Transform{}, clipAnimator(kWalk));
    const Entity child = attach(f.world, walker, attachment("hips"));
    f.animation.waitIdle();
    f.step(0.25f);
    const Vec3 attached = f.world.get<LocalTransform>(child).position;
    CHECK(near(attached, Vec3(0.0f, 1.0f, 0.5f)));

    setEnabled(f.world, child, false);
    f.refreshHierarchy();
    f.step(0.25f);
    CHECK(f.world.get<LocalTransform>(child).position == attached);

    setEnabled(f.world, child, true);
    f.refreshHierarchy();
    f.step(0.0f);
    CHECK(near(f.world.get<LocalTransform>(child).position, Vec3(0.0f, 1.0f, 1.0f)));

    // A disabled parent makes both Inactive: the Animator keeps its pose and the child stays.
    setEnabled(f.world, walker, false);
    f.refreshHierarchy();
    const Vec3 kept = f.world.get<LocalTransform>(child).position;
    f.step(0.25f);
    CHECK(f.world.get<LocalTransform>(child).position == kept);
}

TEST_CASE("jointTransform() places joints in world space and returns every error")
{
    Fixture f;
    const Transform placed = turned(kQuarterTurn, Vec3(1.0f, 2.0f, 3.0f), Vec3(2.0f));
    const Entity walker = f.world.create(placed, clipAnimator(kWalk, 0.5f));
    const Entity plain = f.world.create(Transform{});
    Animator broken;
    broken.skeleton = AssetRef("game:/nothing.gltf#skin/0");
    const Entity unloaded = f.world.create(Transform{}, broken);
    const Entity early = f.world.create(Transform{}, clipAnimator(kWalk));
    CHECK(f.service.jointTransform(early, "hips").error() == AnimationError::NoGraph); // no pose yet
    f.animation.waitIdle();

    // The hips are at (0, 1, 1) in model space: scaled by 2, turned to +x, then moved.
    const Expected<Mat4, AnimationError> hips = f.service.jointTransform(walker, "hips");
    REQUIRE(hips.hasValue());
    CHECK(near(Vec3((*hips)[3]), Vec3(3.0f, 4.0f, 3.0f)));
    const std::optional<u32> slot = f.animation.findSlot(walker);
    REQUIRE(slot.has_value());
    CHECK(near(*hips, placed.matrix() * f.animation.jointMatrices(*slot)[1]));

    CHECK(f.service.jointTransform(walker, "").error() == AnimationError::UnknownName);
    CHECK(f.service.jointTransform(plain, "hips").error() == AnimationError::NoAnimator);
    CHECK(f.service.jointTransform(unloaded, "hips").error() == AnimationError::NoGraph);
    f.world.destroy(walker);
    CHECK(f.service.jointTransform(walker, "hips").error() == AnimationError::NoAnimator);
    CHECK(f.service.jointTransform(Entity{}, "hips").error() == AnimationError::NoAnimator);
}

TEST_CASE("fixed-step sessions with sockets repeat exactly")
{
    const auto run = [] {
        Session s;
        World& world = s.world();
        const Entity walker = world.create(turned(0.3f), graphAnimator(kRootGraph));
        const Entity dancer = world.create(turned(-0.2f, Vec3(3.0f, 0.0f, 0.0f)), graphAnimator(kGraph));
        const Entity a = attach(world, walker, attachment("hips", Vec3(0.1f, 0.2f, 0.3f)));
        const Entity b = attach(world, dancer, attachment("hips", Vec3(0.0f, 0.0f, 0.4f)));
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
                static_cast<void>(service.setTrigger(dancer, "go"));
            }
            s.run(1);
            for (const Entity entity : {a, b})
            {
                const Transform& transform = world.get<Transform>(entity);
                trace.insert(trace.end(), {transform.position.x, transform.position.y, transform.position.z,
                                           transform.rotation.x, transform.rotation.y, transform.rotation.w});
            }
        }
        return trace;
    };
    const std::vector<f32> first = run();
    const std::vector<f32> second = run();
    REQUIRE(first.size() == second.size());
    CHECK(std::memcmp(first.data(), second.data(), first.size() * sizeof(f32)) == 0);
}

TEST_CASE("a steady-state update with sockets allocates nothing")
{
    Fixture f;
    for (u32 i = 0; i < 24; ++i)
    {
        const Entity walker = f.world.create(turned(0.1f * static_cast<f32>(i)),
                                             i % 2 == 0 ? clipAnimator(kWalk, 0.03f * static_cast<f32>(i))
                                                        : graphAnimator(kGraph));
        attach(f.world, walker, attachment("hips", Vec3(0.0f, 0.1f, 0.0f)));
        attach(f.world, walker, attachment(i % 3 == 0 ? "missing" : "root"));
    }
    f.animation.waitIdle();
    for (int i = 0; i < 3; ++i)
    {
        f.step(1.0f / 60.0f);
    }
    usize allocations = 0;
    {
        const test::ScopedAllocationCounter counter;
        for (int i = 0; i < 60; ++i)
        {
            f.step(1.0f / 60.0f);
        }
        allocations = counter.count();
    }
    CHECK(allocations == 0);
    CHECK(f.animation.statistics().posed == 24);
}
