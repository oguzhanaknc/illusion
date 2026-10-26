#include "TestSupport.hpp"

#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/render/DebugDraw.hpp>

#include <doctest/doctest.h>

#include <limits>

using namespace illusion;

TEST_CASE("version one cameras migrate with perspective defaults")
{
    reflectRenderComponents();
    World world;
    const auto result = loadScene(world, R"({"format":"illusion.scene","version":1,
        "types":{"illusion.Camera":1},"entities":[{"illusion.Camera":
        {"fovY":0.8,"nearPlane":0.2,"exposure":2}}]})");
    REQUIRE(result.ok());
    REQUIRE(result.entities.size() == 1);
    const Camera& camera = world.get<Camera>(result.entities[0]);
    CHECK(camera.fovY == 0.8f);
    CHECK(camera.nearPlane == 0.2f);
    CHECK(camera.exposure == 2);
    CHECK(camera.projection == CameraProjection::Perspective);
    CHECK(camera.orthoHeight == 10);
    CHECK(camera.farPlane == 1000);
    CHECK(camera.priority == 0);
}

TEST_CASE("debug drawing records bounded geometry without allocating")
{
    DebugDraw draw(37);
    usize allocations = 0;
    bool recorded = false;
    bool overflow = true;
    {
        test::ScopedAllocationCounter counter;
        recorded = draw.line(Vec3(0), Vec3(1), Vec4(1), 3, DebugDepth::OnTop) &&
                   draw.box(Vec3(-1), Vec3(1), Vec4(1)) && draw.sphere(Vec3(0), 1, Vec4(1), 8);
        overflow = draw.line(Vec3(0), Vec3(1), Vec4(1));
        allocations = counter.count();
    }
    CHECK(recorded);
    CHECK_FALSE(overflow);
    CHECK(allocations == 0);
    REQUIRE(draw.lines().size() == 37);
    CHECK(draw.lines()[0].start.w == 3);
    CHECK(draw.lines()[0].end.w == 1);
    draw.clear();
    CHECK(draw.lines().empty());
    CHECK(draw.box(Vec3(-1), Vec3(1), Vec4(1)));
}

TEST_CASE("invalid debug geometry and helpers that do not fit append nothing")
{
    DebugDraw draw(12);
    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    CHECK_FALSE(draw.line(Vec3(0), Vec3(0), Vec4(1)));
    CHECK_FALSE(draw.line(Vec3(nan), Vec3(1), Vec4(1)));
    CHECK_FALSE(draw.line(Vec3(0), Vec3(1), Vec4(1), 0));
    CHECK_FALSE(draw.line(Vec3(0), Vec3(1), Vec4(nan)));
    CHECK_FALSE(draw.box(Vec3(1), Vec3(0), Vec4(1)));
    CHECK_FALSE(draw.box(Vec3(0), Vec3(0), Vec4(1)));
    CHECK_FALSE(draw.sphere(Vec3(0), 1, Vec4(1), 8));
    CHECK(draw.lines().empty());
    REQUIRE(draw.line(Vec3(0), Vec3(1), Vec4(1)));
    CHECK_FALSE(draw.box(Vec3(0), Vec3(1), Vec4(1)));
    CHECK(draw.lines().size() == 1);
    DebugDraw empty(0);
    CHECK_FALSE(empty.line(Vec3(0), Vec3(1), Vec4(1)));
}

TEST_CASE("main camera selection uses active cameras with transforms and stable priority ties")
{
    World world;
    CHECK(findMainCamera(world).isNull());
    Camera camera;
    camera.priority = 4;
    world.create(camera); // no Transform
    const Entity first = world.create(Transform{}, camera);
    const Entity second = world.create(Transform{}, camera);
    CHECK(findMainCamera(world) == first);
    world.get<Camera>(second).priority = 5;
    CHECK(findMainCamera(world) == second);
    world.emplace<Inactive>(second);
    CHECK(findMainCamera(world) == first);
}

TEST_CASE("orthographic cameras preserve size and map finite near and far to reversed depth")
{
    Camera camera;
    camera.projection = CameraProjection::Orthographic;
    camera.orthoHeight = 6;
    camera.nearPlane = 0.5f;
    camera.farPlane = 30;
    const RenderView view = RenderView::fromCamera(Transform{}, camera, 2);
    const Vec4 nearCorner = view.projection * Vec4(6, 3, -0.5f, 1);
    const Vec4 farCorner = view.projection * Vec4(6, 3, -30, 1);
    CHECK(nearCorner.x == doctest::Approx(1));
    CHECK(nearCorner.y == doctest::Approx(1));
    CHECK(nearCorner.z == doctest::Approx(1));
    CHECK(farCorner.x == doctest::Approx(nearCorner.x));
    CHECK(farCorner.y == doctest::Approx(nearCorner.y));
    CHECK(farCorner.z == doctest::Approx(0));
}
