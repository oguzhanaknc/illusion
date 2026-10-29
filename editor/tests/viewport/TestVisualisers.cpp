#include <illusion/audio/AudioComponents.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/editor/viewport/Visualisers.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/render/DebugDraw.hpp>
#include <illusion/render/SceneComponents.hpp>

#include <doctest/doctest.h>

#include <vector>

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::editor::viewport;

// CPU tests of the DebugDraw visualisers: what each Component records, the scope, hidden and Inactive
// entities, and a full DebugDraw.

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
    ViewportVisualisers kinds{false, false, false, false, false, false};
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

    options.kinds = ViewportVisualisers{false, false, false, false, false, false};
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
