#include "physics/PhysicsTestSupport.hpp"

#include <illusion/ecs/SceneSerializer.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <vector>

using namespace illusion;
using namespace illusion::test;

namespace
{

using Fixture = PhysicsFixture;

constexpr std::array<Vec3, 8> kCubeCorners{
    Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, -0.5f), Vec3(-0.5f, 0.5f, -0.5f),
    Vec3(-0.5f, -0.5f, 0.5f),  Vec3(0.5f, -0.5f, 0.5f),  Vec3(0.5f, 0.5f, 0.5f),  Vec3(-0.5f, 0.5f, 0.5f)};

// A 20 x 20 m quad at y = 0 facing up.
PhysicsMeshHandle createFloorMesh(PhysicsWorld& physics)
{
    const std::array<Vec3, 4> vertices{Vec3(-10.0f, 0.0f, -10.0f), Vec3(10.0f, 0.0f, -10.0f),
                                       Vec3(10.0f, 0.0f, 10.0f), Vec3(-10.0f, 0.0f, 10.0f)};
    const std::array<u32, 6> indices{0, 3, 2, 0, 2, 1};
    return physics.createMesh(vertices, indices);
}

Collider meshCollider(ColliderShape shape, PhysicsMeshHandle mesh)
{
    Collider collider;
    collider.shape = shape;
    collider.mesh = mesh;
    return collider;
}

// A character standing on the ground at @p position after settling.
Entity createCharacter(Fixture& f, const Vec3& position)
{
    const Entity character = f.world.create(at(position), CharacterController{});
    simulate(f.physics, 0.5);
    return character;
}

// Walks @p character at @p velocity for @p seconds, setting the velocity every frame as game code does.
void walk(Fixture& f, Entity character, const Vec3& velocity, f64 seconds)
{
    const auto steps = static_cast<u32>(std::lround(seconds / kStep));
    for (u32 i = 0; i < steps; ++i)
    {
        Vec3 current = f.physics.characterVelocity(character);
        current.x = velocity.x;
        current.z = velocity.z;
        f.physics.setCharacterVelocity(character, current);
        f.physics.update(kStep);
    }
}

Joint jointOf(JointType type)
{
    Joint joint;
    joint.type = type;
    return joint;
}

f32 distance(const World& world, Entity entity, const Vec3& point)
{
    return glm::length(world.get<Transform>(entity).position - point);
}

} // namespace

TEST_CASE("convex hull colliders collide like their hull")
{
    Fixture f;
    createGround(f.world);
    const PhysicsMeshHandle hull = f.physics.createConvexHull(kCubeCorners);
    REQUIRE(hull.valid());
    CHECK(f.physics.isMeshValid(hull));
    const Entity cube = f.world.create(at(Vec3(0.0f, 2.0f, 0.0f)), body(MotionType::Dynamic),
                                       meshCollider(ColliderShape::ConvexHull, hull));
    simulate(f.physics, 3.0);
    CHECK(f.world.get<Transform>(cube).position.y == doctest::Approx(0.5).epsilon(0.02));
    CHECK(f.physics.mass(cube) == doctest::Approx(1000.0).epsilon(0.05)); // 1 m^3 at 1000 kg/m^3

    SUBCASE("destroyed geometry stays with the bodies that use it")
    {
        f.physics.destroyMesh(hull);
        CHECK_FALSE(f.physics.isMeshValid(hull));
        const Entity late = f.world.create(at(Vec3(5.0f, 2.0f, 0.0f)), body(MotionType::Dynamic),
                                           meshCollider(ColliderShape::ConvexHull, hull));
        simulate(f.physics, 0.5);
        CHECK(f.world.get<PhysicsBody>(late).id == PhysicsBody::kInvalidId);
        CHECK(f.world.get<Transform>(cube).position.y == doctest::Approx(0.5).epsilon(0.02));
        // The slot is reused with a new generation.
        const PhysicsMeshHandle next = f.physics.createConvexHull(kCubeCorners);
        CHECK(next.index == hull.index);
        CHECK(next.generation != hull.generation);
    }
}

TEST_CASE("mesh colliders are solid ground for static bodies only")
{
    Fixture f;
    const PhysicsMeshHandle floor = createFloorMesh(f.physics);
    REQUIRE(floor.valid());
    const Entity ground =
        f.world.create(at(Vec3(0.0f)), body(MotionType::Static), meshCollider(ColliderShape::Mesh, floor));
    const Entity ball = f.world.create(at(Vec3(0.0f, 3.0f, 0.0f)), body(MotionType::Dynamic), sphere(0.5f));
    simulate(f.physics, 3.0);
    CHECK(f.world.get<Transform>(ball).position.y == doctest::Approx(0.5).epsilon(0.02));
    const auto hit = f.physics.raycast(Vec3(3.0f, 5.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 10.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->entity == ground);
    CHECK(hit->normal.y == doctest::Approx(1.0));

    // A dynamic mesh body and a collider whose handle is of the other kind get no body.
    const Entity dynamicMesh = f.world.create(at(Vec3(0.0f, 5.0f, 0.0f)), body(MotionType::Dynamic),
                                              meshCollider(ColliderShape::Mesh, floor));
    const Entity wrongKind = f.world.create(at(Vec3(0.0f, 5.0f, 0.0f)), body(MotionType::Dynamic),
                                            meshCollider(ColliderShape::ConvexHull, floor));
    f.physics.update(0.0);
    CHECK(f.world.get<PhysicsBody>(dynamicMesh).id == PhysicsBody::kInvalidId);
    CHECK(f.world.get<PhysicsBody>(wrongKind).id == PhysicsBody::kInvalidId);
}

TEST_CASE("invalid collision geometry yields invalid handles")
{
    Fixture f;
    const std::array<Vec3, 3> triangle{Vec3(0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)};
    const std::array<u32, 3> outOfRange{0, 1, 3};
    const std::array<u32, 2> partial{0, 1};
    CHECK_FALSE(f.physics.createMesh({}, {}).valid());
    CHECK_FALSE(f.physics.createMesh(triangle, outOfRange).valid());
    CHECK_FALSE(f.physics.createMesh(triangle, partial).valid());
    const std::array<Vec3, 2> line{Vec3(0.0f), Vec3(1.0f)};
    CHECK_FALSE(f.physics.createConvexHull(line).valid());
    CHECK_FALSE(f.physics.isMeshValid(PhysicsMeshHandle{}));
    f.physics.destroyMesh(PhysicsMeshHandle{}); // ignored
}

TEST_CASE("characters fall, land, walk and jump")
{
    Fixture f;
    createGround(f.world);
    const Entity character = f.world.create(at(Vec3(0.0f, 2.0f, 0.0f)), CharacterController{});
    f.physics.update(0.0);
    CHECK(f.physics.characterCount() == 1);
    CHECK(f.world.has<CharacterBody>(character));
    CHECK_FALSE(f.physics.isCharacterGrounded(character));

    simulate(f.physics, 1.5);
    CHECK(f.physics.isCharacterGrounded(character));
    const CharacterBody& state = f.world.get<CharacterBody>(character);
    CHECK(state.grounded);
    CHECK(state.groundNormal.y == doctest::Approx(1.0).epsilon(0.01));
    CHECK(f.world.get<Transform>(character).position.y == doctest::Approx(0.0).epsilon(0.05));
    CHECK(f.physics.characterVelocity(character).y == doctest::Approx(0.0).epsilon(0.01));

    walk(f, character, Vec3(2.0f, 0.0f, 0.0f), 1.0);
    CHECK(f.world.get<Transform>(character).position.x == doctest::Approx(2.0).epsilon(0.05));
    CHECK(f.world.get<Transform>(character).position.y == doctest::Approx(0.0).epsilon(0.05));

    f.physics.setCharacterVelocity(character, Vec3(0.0f, 5.0f, 0.0f));
    simulate(f.physics, 0.2);
    CHECK_FALSE(f.physics.isCharacterGrounded(character));
    CHECK(f.world.get<Transform>(character).position.y > 0.5f);
    simulate(f.physics, 1.5);
    CHECK(f.physics.isCharacterGrounded(character));
    CHECK(f.world.get<Transform>(character).position.y == doctest::Approx(0.0).epsilon(0.05));
}

TEST_CASE("characters walk up steps and are stopped by walls")
{
    Fixture f;
    const Entity ground = createGround(f.world);
    // A 0.2 m step starting at x = 1.5 and a wall ending at x = -2.5.
    f.world.create(at(Vec3(6.5f, 0.1f, 0.0f)), body(MotionType::Static), box(Vec3(5.0f, 0.1f, 5.0f)));
    f.world.create(at(Vec3(-3.0f, 2.0f, 0.0f)), body(MotionType::Static), box(Vec3(0.5f, 2.0f, 5.0f)));
    const Entity character = createCharacter(f, Vec3(0.0f));
    CHECK(f.world.get<CharacterBody>(character).ground == ground);

    walk(f, character, Vec3(2.0f, 0.0f, 0.0f), 1.5);
    CHECK(f.world.get<Transform>(character).position.x > 2.0f);
    CHECK(f.world.get<Transform>(character).position.y == doctest::Approx(0.2).epsilon(0.1));
    CHECK(f.physics.isCharacterGrounded(character));

    walk(f, character, Vec3(-2.0f, 0.0f, 0.0f), 4.0);
    const f32 x = f.world.get<Transform>(character).position.x;
    CHECK(x > -2.5f);
    CHECK(x < -2.0f);
    CHECK(f.world.get<Transform>(character).position.y == doctest::Approx(0.0).epsilon(0.05));
}

TEST_CASE("characters are seen by raycasts and sensors and can be teleported")
{
    Fixture f;
    const Entity ground = createGround(f.world);
    RigidBody trigger = body(MotionType::Static);
    trigger.sensor = true;
    const Entity zone = f.world.create(at(Vec3(3.0f, 1.0f, 0.0f)), trigger, box(Vec3(1.0f)));
    const Entity character = createCharacter(f, Vec3(0.0f));

    const auto hit = f.physics.raycast(Vec3(0.0f, 5.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 10.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->entity == character);
    CHECK(hit->point.y == doctest::Approx(1.8).epsilon(0.05));
    const auto past =
        f.physics.raycast(Vec3(0.0f, 5.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 10.0f, 0xFFFFFFFFu, character);
    REQUIRE(past.has_value());
    CHECK(past->entity == ground);

    bool entered = false;
    for (int i = 0; i < 90 && !entered; ++i)
    {
        f.physics.setCharacterVelocity(character, Vec3(3.0f, 0.0f, 0.0f));
        f.physics.update(kStep);
        for (const ContactEvent& event : f.physics.contactEvents())
        {
            entered =
                entered ||
                (event.type == ContactEventType::Begin && event.sensor &&
                 ((event.a == character && event.b == zone) || (event.a == zone && event.b == character)));
        }
    }
    CHECK(entered);

    f.physics.setCharacterVelocity(character, Vec3(0.0f));
    f.world.get<Transform>(character).position = Vec3(-10.0f, 0.0f, 5.0f);
    simulate(f.physics, 0.1);
    CHECK(f.world.get<Transform>(character).position.x == doctest::Approx(-10.0).epsilon(0.01));
    CHECK(f.world.get<Transform>(character).position.z == doctest::Approx(5.0).epsilon(0.01));
}

TEST_CASE("characters are removed with their Entity and rebuilt on request")
{
    Fixture f;
    createGround(f.world);
    const Entity character = createCharacter(f, Vec3(0.0f));
    const u32 bodies = f.physics.bodyCount();
    CHECK(bodies == 2); // the ground and the character's inner body

    f.world.get<CharacterController>(character).radius = 0.5f;
    f.physics.rebuild(character);
    f.physics.update(kStep);
    CHECK(f.physics.characterCount() == 1);
    const auto hit = f.physics.raycast(Vec3(0.45f, 5.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 10.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->entity == character); // wider now

    f.world.remove<CharacterController>(character);
    f.physics.update(kStep);
    CHECK(f.physics.characterCount() == 0);
    CHECK_FALSE(f.world.has<CharacterBody>(character));
    CHECK(f.physics.bodyCount() == 1);

    // An invalid controller gets no character until it is fixed and rebuilt.
    CharacterController invalid;
    invalid.height = 0.5f; // less than 2 * radius
    const Entity broken = f.world.create(at(Vec3(0.0f)), invalid);
    f.physics.update(kStep);
    CHECK(f.world.get<CharacterBody>(broken).id == CharacterBody::kInvalidId);
    CHECK(f.physics.characterCount() == 0);
    f.world.get<CharacterController>(broken).height = 1.8f;
    f.physics.rebuild(broken);
    f.physics.update(kStep);
    CHECK(f.physics.characterCount() == 1);
}

TEST_CASE("point joints swing bodies like a pendulum")
{
    Fixture f;
    Joint joint = jointOf(JointType::Point);
    joint.anchor = Vec3(-2.0f, 0.0f, 0.0f); // the world point (0, 5, 0)
    const Entity bob =
        f.world.create(at(Vec3(2.0f, 5.0f, 0.0f)), body(MotionType::Dynamic), sphere(0.2f), joint);
    f.physics.update(0.0);
    CHECK(f.physics.jointCount() == 1);
    CHECK(f.world.get<PhysicsJoint>(bob).id != PhysicsJoint::kInvalidId);

    simulate(f.physics, 0.5);
    CHECK(f.world.get<Transform>(bob).position.y < 4.0f);
    CHECK(distance(f.world, bob, Vec3(0.0f, 5.0f, 0.0f)) == doctest::Approx(2.0).epsilon(0.02));
}

TEST_CASE("hinge joints rotate around their axis within limits and with motors")
{
    Fixture f;
    Joint hinge = jointOf(JointType::Hinge);
    hinge.anchor = Vec3(-0.5f, 0.0f, 0.0f); // the door's edge, at the world point (0, 1, 0)
    hinge.axis = Vec3(0.0f, 1.0f, 0.0f);
    hinge.motorSpeed = 1.0f;
    hinge.motorMaxForce = 10000.0f;
    const Entity door = f.world.create(at(Vec3(0.5f, 1.0f, 0.0f)), body(MotionType::Dynamic),
                                       box(Vec3(0.5f, 1.0f, 0.05f)), hinge);
    simulate(f.physics, 1.0);
    CHECK(f.physics.jointPosition(door) == doctest::Approx(1.0).epsilon(0.1));
    CHECK(distance(f.world, door, Vec3(0.0f, 1.0f, 0.0f)) == doctest::Approx(0.5).epsilon(0.02));

    // Motor speed is applied every update.
    f.world.get<Joint>(door).motorSpeed = -1.0f;
    simulate(f.physics, 1.0);
    CHECK(f.physics.jointPosition(door) == doctest::Approx(0.0).epsilon(0.1));

    SUBCASE("limits stop the motor")
    {
        Joint& joint = f.world.get<Joint>(door);
        joint.limited = true;
        joint.minLimit = -0.5f;
        joint.maxLimit = 0.5f;
        joint.motorSpeed = 2.0f;
        f.physics.rebuild(door);
        simulate(f.physics, 1.5);
        CHECK(f.physics.jointPosition(door) == doctest::Approx(0.5).epsilon(0.05));
    }
}

TEST_CASE("slider joints move along their axis")
{
    Fixture f;
    Joint slider = jointOf(JointType::Slider);
    slider.axis = Vec3(1.0f, 0.0f, 0.0f);
    slider.motorSpeed = 1.0f;
    slider.motorMaxForce = 100000.0f;
    slider.limited = true;
    slider.minLimit = -1.0f;
    slider.maxLimit = 0.5f;
    const Entity piston =
        f.world.create(at(Vec3(0.0f, 2.0f, 0.0f)), body(MotionType::Dynamic), box(Vec3(0.25f)), slider);
    simulate(f.physics, 0.25);
    CHECK(f.physics.jointPosition(piston) == doctest::Approx(0.25).epsilon(0.1));
    CHECK(f.world.get<Transform>(piston).position.y == doctest::Approx(2.0).epsilon(0.01));
    simulate(f.physics, 1.0);
    CHECK(f.physics.jointPosition(piston) == doctest::Approx(0.5).epsilon(0.05));
    CHECK(f.world.get<Transform>(piston).position.x == doctest::Approx(0.5).epsilon(0.05));
}

TEST_CASE("fixed joints glue bodies together and distance joints act as ropes")
{
    Fixture f;
    const Entity base =
        f.world.create(at(Vec3(0.0f, 10.0f, 0.0f)), body(MotionType::Dynamic), box(Vec3(0.5f)));
    Joint glue = jointOf(JointType::Fixed);
    glue.connected = base;
    const Entity attached =
        f.world.create(at(Vec3(1.0f, 10.0f, 0.0f)), body(MotionType::Dynamic), box(Vec3(0.5f)), glue);

    Joint rope = jointOf(JointType::Distance);
    rope.connectedAnchor = Vec3(10.0f, 10.0f, 0.0f); // world space: no connected body
    rope.maxLimit = 3.0f;
    const Entity ball =
        f.world.create(at(Vec3(10.0f, 9.0f, 0.0f)), body(MotionType::Dynamic), sphere(0.2f), rope);

    simulate(f.physics, 1.0);
    const Transform& a = f.world.get<Transform>(base);
    const Transform& b = f.world.get<Transform>(attached);
    CHECK(a.position.y < 7.0f);
    CHECK(b.position.x - a.position.x == doctest::Approx(1.0).epsilon(0.02));
    CHECK(b.position.y - a.position.y == doctest::Approx(0.0).epsilon(0.02));
    CHECK(distance(f.world, ball, Vec3(10.0f, 10.0f, 0.0f)) == doctest::Approx(3.0).epsilon(0.02));
    CHECK(f.physics.jointPosition(ball) == doctest::Approx(3.0).epsilon(0.02));
}

TEST_CASE("joints break when the force through them exceeds breakForce")
{
    Fixture f;
    RigidBody heavy = body(MotionType::Dynamic);
    heavy.mass = 100.0f; // about 981 N of weight
    Joint strong = jointOf(JointType::Point);
    strong.anchor = Vec3(0.0f, 1.0f, 0.0f);
    strong.breakForce = 5000.0f;
    Joint weak = strong;
    weak.breakForce = 500.0f;
    const Entity held = f.world.create(at(Vec3(0.0f, 5.0f, 0.0f)), heavy, sphere(0.3f), strong);
    const Entity dropped = f.world.create(at(Vec3(5.0f, 5.0f, 0.0f)), heavy, sphere(0.3f), weak);

    bool reported = false;
    for (int i = 0; i < 60; ++i)
    {
        f.physics.update(kStep);
        const std::span<const Entity> broken = f.physics.brokenJoints();
        reported = reported || std::find(broken.begin(), broken.end(), dropped) != broken.end();
        CHECK(std::find(broken.begin(), broken.end(), held) == broken.end());
    }
    CHECK(reported);
    CHECK(f.world.get<PhysicsJoint>(dropped).broken);
    CHECK_FALSE(f.world.get<PhysicsJoint>(held).broken);
    CHECK(f.world.get<Transform>(dropped).position.y < 3.0f);
    CHECK(f.world.get<Transform>(held).position.y == doctest::Approx(5.0).epsilon(0.01));
    CHECK(f.physics.jointCount() == 2); // broken joints stay until their Entity changes
}

TEST_CASE("joints follow the lifetime of their bodies and components")
{
    Fixture f;
    const Entity anchor =
        f.world.create(at(Vec3(0.0f, 5.0f, 0.0f)), body(MotionType::Dynamic), box(Vec3(0.5f)));
    Joint joint = jointOf(JointType::Point);
    joint.connected = anchor;
    const Entity hanging =
        f.world.create(at(Vec3(0.0f, 4.0f, 0.0f)), body(MotionType::Dynamic), box(Vec3(0.25f)), joint);
    f.physics.update(0.0);
    REQUIRE(f.physics.jointCount() == 1);

    SUBCASE("rebuilding a connected body recreates the joint")
    {
        f.physics.rebuild(anchor);
        f.physics.update(0.0);
        CHECK(f.physics.jointCount() == 1);
        CHECK(f.world.get<PhysicsJoint>(hanging).id != PhysicsJoint::kInvalidId);
    }
    SUBCASE("removing the Joint removes the joint")
    {
        f.world.remove<Joint>(hanging);
        f.physics.update(0.0);
        CHECK(f.physics.jointCount() == 0);
        CHECK_FALSE(f.world.has<PhysicsJoint>(hanging));
    }
    SUBCASE("destroying the connected Entity removes the joint")
    {
        f.world.destroy(anchor);
        f.physics.update(0.0);
        CHECK(f.physics.jointCount() == 0);
        CHECK(f.world.get<PhysicsJoint>(hanging).id == PhysicsJoint::kInvalidId); // cannot be recreated
        simulate(f.physics, 0.5);
        CHECK(f.world.get<Transform>(hanging).position.y < 3.5f); // falls freely
    }
    SUBCASE("joints without two suitable bodies are not created")
    {
        const Entity floating = f.world.create(Transform{}, joint); // no body
        Joint staticJoint = jointOf(JointType::Fixed);
        const Entity wall =
            f.world.create(at(Vec3(9.0f)), body(MotionType::Static), box(Vec3(1.0f)), staticJoint);
        f.physics.update(0.0);
        CHECK(f.world.get<PhysicsJoint>(floating).id == PhysicsJoint::kInvalidId);
        CHECK(f.world.get<PhysicsJoint>(wall).id == PhysicsJoint::kInvalidId);
        CHECK(f.physics.jointCount() == 1);
    }
}

TEST_CASE("shape casts find the first body along the path")
{
    Fixture f;
    const Entity ground = createGround(f.world);
    RigidBody upper = body(MotionType::Static);
    upper.layer = 1;
    const Entity crate = f.world.create(at(Vec3(3.0f, 0.5f, 0.0f)), upper, box(Vec3(0.5f)));
    RigidBody trigger = body(MotionType::Static);
    trigger.sensor = true;
    f.world.create(at(Vec3(0.0f, 2.0f, 0.0f)), trigger, box(Vec3(1.0f, 0.1f, 1.0f)));
    f.physics.update(0.0);
    const Collider ball = sphere(0.5f);
    const Quat identity(1.0f, 0.0f, 0.0f, 0.0f);
    const Vec3 down(0.0f, -1.0f, 0.0f);

    const auto onGround = f.physics.shapeCast(ball, Vec3(0.0f, 5.0f, 0.0f), identity, down, 10.0f);
    REQUIRE(onGround.has_value()); // the sensor is skipped
    CHECK(onGround->entity == ground);
    CHECK(onGround->distance == doctest::Approx(4.5).epsilon(0.01));
    CHECK(onGround->normal.y == doctest::Approx(1.0).epsilon(0.01));
    CHECK(onGround->point.y == doctest::Approx(0.0).epsilon(0.01));

    const auto onCrate = f.physics.shapeCast(ball, Vec3(3.0f, 5.0f, 0.0f), identity, down, 10.0f);
    REQUIRE(onCrate.has_value());
    CHECK(onCrate->entity == crate);
    CHECK(onCrate->distance == doctest::Approx(3.5).epsilon(0.01));

    const auto past =
        f.physics.shapeCast(ball, Vec3(3.0f, 5.0f, 0.0f), identity, down, 10.0f, 0xFFFFFFFFu, crate);
    REQUIRE(past.has_value());
    CHECK(past->entity == ground);
    CHECK_FALSE(
        f.physics.shapeCast(ball, Vec3(0.0f, 5.0f, 0.0f), identity, down, 10.0f, 1u << 1).has_value());
    CHECK_FALSE(f.physics.shapeCast(ball, Vec3(0.0f, 5.0f, 0.0f), identity, down, 2.0f).has_value());

    // A rotated, offset box: the collider's offset and rotation apply.
    Collider plank = box(Vec3(2.0f, 0.1f, 0.1f));
    plank.offset = Vec3(0.0f, -1.0f, 0.0f);
    const auto plankHit = f.physics.shapeCast(plank, Vec3(3.0f, 6.0f, 0.0f), identity, down, 10.0f);
    REQUIRE(plankHit.has_value());
    CHECK(plankHit->entity == crate);
    CHECK(plankHit->distance == doctest::Approx(3.9).epsilon(0.01));
}

TEST_CASE("overlap lists the bodies inside a shape")
{
    Fixture f;
    createGround(f.world);
    std::array<Entity, 3> crates;
    for (u32 i = 0; i < crates.size(); ++i)
    {
        crates[i] = f.world.create(at(Vec3(2.0f * static_cast<f32>(i), 0.5f, 0.0f)),
                                   body(MotionType::Dynamic), box(Vec3(0.5f)));
    }
    f.physics.update(0.0);

    std::vector<Entity> found;
    const Quat identity(1.0f, 0.0f, 0.0f, 0.0f);
    CHECK(f.physics.overlap(box(Vec3(1.6f, 0.4f, 1.0f)), Vec3(1.0f, 1.0f, 0.0f), identity, found) == 2);
    REQUIRE(found.size() == 2);
    CHECK(found[0] == crates[0]);
    CHECK(found[1] == crates[1]);

    CHECK(f.physics.overlap(box(Vec3(1.6f, 0.4f, 1.0f)), Vec3(1.0f, 1.0f, 0.0f), identity, found, 0xFFFFFFFFu,
                            crates[0]) == 1);
    CHECK(found[0] == crates[1]);

    const PhysicsMeshHandle hull = f.physics.createConvexHull(kCubeCorners);
    CHECK(f.physics.overlap(meshCollider(ColliderShape::ConvexHull, hull), Vec3(4.0f, 1.2f, 0.0f), identity,
                            found) == 1);
    CHECK(found[0] == crates[2]);
    CHECK(f.physics.overlap(sphere(0.2f), Vec3(0.0f, 5.0f, 0.0f), identity, found) == 0);
    CHECK(found.empty());
}

TEST_CASE("characters and joints are saved in scenes and their runtime state is not")
{
    reflectCoreComponents();
    reflectPhysicsComponents();
    Fixture f;
    const Entity anchor =
        f.world.create(at(Vec3(0.0f, 5.0f, 0.0f)), body(MotionType::Dynamic), box(Vec3(0.5f)));
    Joint joint = jointOf(JointType::Hinge);
    joint.connected = anchor;
    joint.axis = Vec3(0.0f, 0.0f, 1.0f);
    joint.limited = true;
    joint.maxLimit = 1.0f;
    joint.breakForce = 250.0f;
    f.world.create(at(Vec3(0.0f, 4.0f, 0.0f)), body(MotionType::Dynamic), box(Vec3(0.25f)), joint);
    CharacterController controller;
    controller.stepHeight = 0.5f;
    f.world.create(at(Vec3(5.0f, 0.0f, 0.0f)), controller);
    f.physics.update(0.0);

    assignMissingGuids(f.world);
    const SceneSaveResult saved = saveScene(f.world, {.order = SceneOrder::AsGiven});
    REQUIRE(saved.ok());
    CHECK(saved.skippedComponents == 4); // two PhysicsBody, a PhysicsJoint and a CharacterBody
    CHECK(saved.json.find("\"type\": \"Hinge\"") != std::string::npos);

    World loaded;
    const SceneLoadResult result = loadScene(loaded, saved.json);
    REQUIRE(result.ok());
    REQUIRE(result.entities.size() == 3);
    const Joint& copy = loaded.get<Joint>(result.entities[1]);
    CHECK(copy.type == JointType::Hinge);
    CHECK(copy.connected == result.entities[0]);
    CHECK(copy.axis == Vec3(0.0f, 0.0f, 1.0f));
    CHECK(copy.limited);
    CHECK(copy.maxLimit == 1.0f);
    CHECK(copy.breakForce == 250.0f);
    CHECK(loaded.get<CharacterController>(result.entities[2]).stepHeight == 0.5f);
}
