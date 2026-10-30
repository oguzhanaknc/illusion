#include "core/EditorCoreTestSupport.hpp"

#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/editor/core/Gizmo.hpp>

#include <cmath>
#include <numbers>

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;
using test::fails;
using test::ok;

namespace
{

constexpr UVec2 kImage{1280, 720};

GizmoCamera lookAt(Vec3 pivot, f32 yaw, f32 pitch, f32 distance, bool orthographic, f32 pixelScale = 1.0f)
{
    ViewportCamera camera;
    camera.pivot = pivot;
    camera.yaw = yaw;
    camera.pitch = pitch;
    camera.distance = distance;
    if (orthographic)
    {
        camera.projection = ViewportProjection::Orthographic;
        camera.orthographicHeight = distance * 0.8f;
    }
    return gizmoCamera(camera, kImage, pixelScale);
}

// Where @p point appears, through the matrices the renderer uses (an independent check of the gizmo's own
// projection).
Vec2 pixelOf(const GizmoCamera& camera, Vec3 point)
{
    const Mat4 view = glm::inverse(glm::translate(Mat4(1.0f), camera.eye) * glm::mat4_cast(camera.rotation));
    return projectToViewport(camera.projection * view, camera.size, point).pixel;
}

f32 segmentDistance(Vec2 point, Vec2 a, Vec2 b)
{
    const Vec2 ab = b - a;
    const f32 lengthSquared = glm::dot(ab, ab);
    const f32 t =
        lengthSquared > 0.0f ? std::clamp(glm::dot(point - a, ab) / lengthSquared, 0.0f, 1.0f) : 0.0f;
    return glm::length(point - (a + ab * t));
}

// Distance from @p point to the front half of ring @p axis (or every ring but @p skip).
f32 ringDistance(const GizmoShape& shape, Vec2 point, u32 axis)
{
    f32 nearest = std::numeric_limits<f32>::max();
    const GizmoRingShape& ring = shape.rings[axis];
    for (u32 k = 0; k < kGizmoRingSegments; ++k)
    {
        if (ring.segments[k] == GizmoSegment::Front)
        {
            nearest = std::min(
                nearest, segmentDistance(point, ring.points[k], ring.points[(k + 1) % kGizmoRingSegments]));
        }
    }
    return nearest;
}

f32 otherRings(const GizmoShape& shape, Vec2 point, u32 skip)
{
    f32 nearest = std::numeric_limits<f32>::max();
    for (u32 i = 0; i < 3; ++i)
    {
        if (i != skip && shape.rings[i].visibility > 0.0f)
        {
            nearest = std::min(nearest, ringDistance(shape, point, i));
        }
    }
    return nearest;
}

bool insidePlanes(const GizmoShape& shape, Vec2 point)
{
    for (const GizmoPlaneShape& plane : shape.planes)
    {
        if (!(plane.visibility > 0.0f))
        {
            continue;
        }
        bool positive = false;
        bool negative = false;
        for (usize i = 0; i < 4; ++i)
        {
            const Vec2 edge = plane.corners[(i + 1) % 4] - plane.corners[i];
            const Vec2 to = point - plane.corners[i];
            const f32 side = edge.x * to.y - edge.y * to.x;
            positive = positive || side > 0.0f;
            negative = negative || side < 0.0f;
        }
        if (!(positive && negative))
        {
            return true;
        }
    }
    return false;
}

f32 otherLines(const GizmoShape& shape, Vec2 point, u32 skip)
{
    f32 nearest = std::numeric_limits<f32>::max();
    for (u32 i = 0; i < 3; ++i)
    {
        if (i != skip && shape.lines[i].visibility > 0.0f)
        {
            nearest = std::min(nearest, segmentDistance(point, shape.lines[i].start, shape.lines[i].end));
        }
    }
    return nearest;
}

bool nearCenter(const GizmoShape& shape, Vec2 point)
{
    const Vec2 offset = glm::abs(point - shape.center);
    return shape.centerBox > 0.0f && std::max(offset.x, offset.y) <= shape.centerBox + shape.tolerance;
}

bool sameRotation(Quat a, Quat b)
{
    return std::abs(glm::dot(a, b)) > 1.0f - 1e-6f;
}

GizmoHandle handleAt(GizmoHandle first, u32 axis)
{
    return static_cast<GizmoHandle>(static_cast<u32>(first) + axis);
}

struct Pose
{
    GizmoCamera camera;
    GizmoPose pose;
};

// Many cameras: perspective and orthographic, several directions, 1x and 2x displays, near the origin and 10
// km from it, with world and rotated axes.
std::vector<Pose> manyPoses()
{
    std::vector<Pose> poses;
    for (const Vec3 origin : {Vec3(0.0f), Vec3(10000.0f, 50.0f, -10000.0f)})
    {
        for (const bool orthographic : {false, true})
        {
            for (const f32 scale : {1.0f, 2.0f})
            {
                for (const f32 yaw : {0.0f, 30.0f, 135.0f, 250.0f})
                {
                    for (const f32 pitch : {-60.0f, -20.0f, 15.0f})
                    {
                        for (const bool rotated : {false, true})
                        {
                            Pose pose;
                            pose.camera = lookAt(origin, yaw, pitch, 8.0f, orthographic, scale);
                            pose.pose.origin = origin;
                            if (rotated)
                            {
                                pose.pose.orientation =
                                    glm::normalize(quatFromEuler(glm::radians(Vec3(20.0f, 40.0f, -10.0f))));
                            }
                            poses.push_back(pose);
                        }
                    }
                }
            }
        }
    }
    return poses;
}

} // namespace

TEST_CASE("gizmos keep their size on screen and project where the renderer draws the pivot")
{
    const GizmoPose pose;
    const GizmoCamera nearCamera = lookAt(Vec3(0.0f), 30.0f, -20.0f, 5.0f, false);
    const GizmoCamera farCamera = lookAt(Vec3(0.0f), 30.0f, -20.0f, 50.0f, false);
    const GizmoShape nearShape = gizmoShape(GizmoTool::Translate, nearCamera, pose);
    const GizmoShape farShape = gizmoShape(GizmoTool::Translate, farCamera, pose);
    REQUIRE(nearShape.visible);
    REQUIRE(farShape.visible);
    CHECK(static_cast<double>(nearShape.length) == doctest::Approx(static_cast<double>(kGizmoSize)));
    CHECK(farShape.length == nearShape.length);
    CHECK(static_cast<double>(farShape.worldLength) ==
          doctest::Approx(static_cast<double>(nearShape.worldLength) * 10.0).epsilon(1e-3));
    CHECK(glm::length(nearShape.center - Vec2(640.0f, 360.0f)) < 0.01f);
    // The arrow's end appears where the renderer's matrices put it.
    const Vec2 end = pixelOf(nearCamera, Vec3(nearShape.worldLength, 0.0f, 0.0f));
    CHECK(glm::length(nearShape.lines[0].end - end) < 0.05f);
    // Twice the pixels on a 2x display; nothing behind the camera.
    const GizmoShape doubled =
        gizmoShape(GizmoTool::Translate, lookAt(Vec3(0.0f), 30.0f, -20.0f, 5.0f, false, 2.0f), pose);
    CHECK(static_cast<double>(doubled.length) == doctest::Approx(2.0 * static_cast<double>(kGizmoSize)));
    GizmoPose behind;
    behind.origin = nearCamera.eye - nearCamera.rotation * Vec3(0.0f, 0.0f, -3.0f);
    CHECK_FALSE(gizmoShape(GizmoTool::Translate, nearCamera, behind).visible);
    CHECK(gizmoHitTest(gizmoShape(GizmoTool::Translate, nearCamera, behind), Vec2(640.0f, 360.0f)) ==
          GizmoHandle::None);
    CHECK_FALSE(gizmoShape(GizmoTool::Select, nearCamera, pose).visible);
}

TEST_CASE(
    "every gizmo handle is hit at many poses, projections, display scales and distances from the origin")
{
    usize checked = 0;
    usize skipped = 0;
    for (const Pose& sample : manyPoses())
    {
        CAPTURE(sample.pose.origin);
        CAPTURE(sample.camera.eye);
        CAPTURE(sample.camera.orthographic);
        CAPTURE(sample.camera.pixelScale);
        const auto expect = [&](const GizmoShape& shape, Vec2 point, GizmoHandle handle, bool clear) {
            if (!clear)
            {
                ++skipped;
                return;
            }
            ++checked;
            CHECK(gizmoHitTest(shape, point) == handle);
        };

        const GizmoShape translate = gizmoShape(GizmoTool::Translate, sample.camera, sample.pose);
        REQUIRE(translate.visible);
        CHECK(static_cast<double>(translate.tolerance) ==
              doctest::Approx(static_cast<double>(kGizmoHitTolerance * sample.camera.pixelScale)));
        expect(translate, translate.center, GizmoHandle::TranslateScreen, true);
        for (u32 i = 0; i < 3; ++i)
        {
            const GizmoAxisShape& line = translate.lines[i];
            if (line.visibility > 0.0f)
            {
                const Vec2 point = glm::mix(line.start, line.end, 0.85f);
                expect(translate, point, handleAt(GizmoHandle::TranslateX, i),
                       !insidePlanes(translate, point) && !nearCenter(translate, point) &&
                           otherLines(translate, point, i) > translate.tolerance * 2.0f);
            }
            const GizmoPlaneShape& plane = translate.planes[i];
            if (plane.visibility > 0.0f)
            {
                const Vec2 point =
                    (plane.corners[0] + plane.corners[1] + plane.corners[2] + plane.corners[3]) * 0.25f;
                bool covered = false;
                for (u32 j = 0; j < 3; ++j)
                {
                    GizmoShape other = translate;
                    for (u32 k = 0; k < 3; ++k)
                    {
                        other.planes[k].visibility = k == j ? translate.planes[k].visibility : 0.0f;
                    }
                    covered = covered || (j != i && translate.planes[j].facing > plane.facing &&
                                          insidePlanes(other, point));
                }
                expect(translate, point, handleAt(GizmoHandle::TranslateYZ, i),
                       !nearCenter(translate, point) && !covered);
            }
        }
        expect(translate, translate.center + Vec2(translate.length * 3.0f, 0.0f), GizmoHandle::None, true);

        const GizmoShape rotate = gizmoShape(GizmoTool::Rotate, sample.camera, sample.pose);
        for (u32 i = 0; i < 3; ++i)
        {
            // The front point of the ring farthest from the other rings.
            const GizmoRingShape& ring = rotate.rings[i];
            f32 best = -1.0f;
            Vec2 point(0.0f);
            for (u32 k = 0; k < kGizmoRingSegments; ++k)
            {
                if (ring.segments[k] != GizmoSegment::Front)
                {
                    continue;
                }
                const Vec2 middle = (ring.points[k] + ring.points[(k + 1) % kGizmoRingSegments]) * 0.5f;
                const f32 clearance = otherRings(rotate, middle, i);
                if (clearance > best)
                {
                    best = clearance;
                    point = middle;
                }
            }
            expect(rotate, point, handleAt(GizmoHandle::RotateX, i), best > rotate.tolerance * 1.5f);
        }
        expect(rotate, rotate.center + Vec2(0.0f, -rotate.viewRing), GizmoHandle::RotateView, true);
        bool trackball = false;
        for (u32 k = 0; k < 16 && !trackball; ++k)
        {
            const f32 angle = static_cast<f32>(k) * std::numbers::pi_v<f32> / 8.0f;
            const Vec2 point =
                rotate.center + Vec2(std::cos(angle), std::sin(angle)) * (rotate.trackball * 0.45f);
            if (otherRings(rotate, point, 3) > rotate.tolerance * 1.5f)
            {
                trackball = true;
                expect(rotate, point, GizmoHandle::RotateTrackball, true);
            }
        }

        const GizmoShape scale = gizmoShape(GizmoTool::Scale, sample.camera, sample.pose);
        expect(scale, scale.center, GizmoHandle::ScaleUniform, true);
        for (u32 i = 0; i < 3; ++i)
        {
            const GizmoAxisShape& line = scale.lines[i];
            if (line.visibility > 0.0f)
            {
                expect(scale, line.end, handleAt(GizmoHandle::ScaleX, i),
                       !nearCenter(scale, line.end) &&
                           otherLines(scale, line.end, i) > scale.tolerance * 2.0f);
            }
        }

        const GizmoShape universal = gizmoShape(GizmoTool::Universal, sample.camera, sample.pose);
        expect(universal, universal.center, GizmoHandle::ScaleUniform, true);
        for (u32 i = 0; i < 3; ++i)
        {
            const GizmoAxisShape& line = universal.lines[i];
            if (line.visibility > 0.0f)
            {
                const Vec2 point = glm::mix(line.start, line.end, 0.95f);
                expect(universal, point, handleAt(GizmoHandle::TranslateX, i),
                       !insidePlanes(universal, point) &&
                           otherLines(universal, point, i) > universal.tolerance * 2.0f);
            }
        }
    }
    // Almost every probe is unambiguous; the others lie where two handles overlap on screen.
    CHECK(checked > skipped * 8);
}

TEST_CASE("axes that point at the camera are hidden and cannot be dragged")
{
    // Looking straight down -z: the z axis points at the camera.
    const GizmoCamera camera = lookAt(Vec3(0.0f), 0.0f, 0.0f, 10.0f, false);
    const GizmoShape shape = gizmoShape(GizmoTool::Translate, camera, GizmoPose{});
    CHECK(shape.lines[2].visibility == 0.0f);
    CHECK(static_cast<double>(shape.lines[0].visibility) == doctest::Approx(1.0));
    // The plane of x and y faces the camera; the others are edge on.
    CHECK(static_cast<double>(shape.planes[2].visibility) == doctest::Approx(1.0));
    CHECK(shape.planes[0].visibility == 0.0f);
    CHECK_FALSE(GizmoDrag::begin(GizmoTool::Translate, GizmoHandle::TranslateZ, camera, GizmoPose{},
                                 shape.center + Vec2(1.0f, 0.0f)));
    CHECK_FALSE(GizmoDrag::begin(GizmoTool::Translate, GizmoHandle::None, camera, GizmoPose{}, shape.center));
}

TEST_CASE("translate drags follow the cursor along axes, in planes and in the view plane, also far away")
{
    for (const Vec3 origin : {Vec3(0.0f), Vec3(10000.0f, 0.0f, 10000.0f)})
    {
        for (const bool orthographic : {false, true})
        {
            CAPTURE(origin);
            CAPTURE(orthographic);
            const GizmoCamera camera = lookAt(origin, 35.0f, -25.0f, 10.0f, orthographic);
            GizmoPose pose;
            pose.origin = origin;
            pose.orientation = glm::normalize(quatFromEuler(glm::radians(Vec3(0.0f, 30.0f, 0.0f))));
            const GizmoShape shape = gizmoShape(GizmoTool::Translate, camera, pose);
            const Vec3 x = shape.axes[0];
            const Vec3 grab = origin + x * (shape.worldLength * 0.6f);

            std::optional<GizmoDrag> drag = GizmoDrag::begin(GizmoTool::Translate, GizmoHandle::TranslateX,
                                                             camera, pose, pixelOf(camera, grab));
            REQUIRE(drag.has_value());
            CHECK(glm::length(drag->update(pixelOf(camera, grab), nullptr).translation) < 1e-3f);
            const GizmoDelta moved = drag->update(pixelOf(camera, grab + x * 1.7f), nullptr);
            CHECK(glm::length(moved.translation - x * 1.7f) < 2e-3f);
            // A cursor off the axis moves only along it.
            const Vec3 z = shape.axes[2];
            const GizmoDelta along = drag->update(pixelOf(camera, grab + x * 0.5f + z * 0.8f), nullptr);
            CHECK(std::abs(glm::dot(along.translation, z)) < 1e-4f);

            // In the plane of x and z.
            const Vec2 corner = (shape.planes[1].corners[0] + shape.planes[1].corners[2]) * 0.5f;
            drag = GizmoDrag::begin(GizmoTool::Translate, GizmoHandle::TranslateXZ, camera, pose, corner);
            REQUIRE(drag.has_value());
            const GizmoRay ray = gizmoRay(camera, corner);
            const Vec3 y = shape.axes[1];
            const f32 t = glm::dot(origin - ray.origin, y) / glm::dot(ray.direction, y);
            const Vec3 onPlane = ray.origin + ray.direction * t;
            const GizmoDelta planar = drag->update(pixelOf(camera, onPlane + x * 0.4f - z * 0.9f), nullptr);
            CHECK(glm::length(planar.translation - (x * 0.4f - z * 0.9f)) < 2e-3f);

            // In the view plane, the pivot stays under the cursor (within the precision of floats 10 km out).
            drag = GizmoDrag::begin(GizmoTool::Translate, GizmoHandle::TranslateScreen, camera, pose,
                                    shape.center);
            REQUIRE(drag.has_value());
            const GizmoDelta screen = drag->update(shape.center + Vec2(50.0f, -20.0f), nullptr);
            CHECK(glm::length(pixelOf(camera, origin + screen.translation) -
                              (shape.center + Vec2(50.0f, -20.0f))) < 0.15f);
        }
    }
}

TEST_CASE("snapping rounds distances, angles and scale factors to exact increments")
{
    CHECK(snapToIncrement(1.3f, 0.5f) == 1.5f);
    CHECK(snapToIncrement(-0.74f, 0.25f) == -0.75f);
    CHECK(snapToIncrement(37.0f, 15.0f) == 30.0f);
    CHECK(snapToIncrement(2.0f, 0.0f) == 2.0f);

    const GizmoCamera camera = lookAt(Vec3(0.0f), 30.0f, -30.0f, 10.0f, false);
    const GizmoPose pose;
    const GizmoShape shape = gizmoShape(GizmoTool::Translate, camera, pose);
    GizmoSnap snap;
    snap.translate = 0.5f;
    snap.rotate = 15.0f;
    snap.scale = 0.1f;
    const Vec3 grab(shape.worldLength * 0.6f, 0.0f, 0.0f);
    std::optional<GizmoDrag> drag =
        GizmoDrag::begin(GizmoTool::Translate, GizmoHandle::TranslateX, camera, pose, pixelOf(camera, grab));
    REQUIRE(drag.has_value());
    CHECK(drag->update(pixelOf(camera, grab + Vec3(1.3f, 0.0f, 0.0f)), &snap).translation ==
          Vec3(1.5f, 0.0f, 0.0f));
    CHECK(drag->update(pixelOf(camera, grab + Vec3(-0.2f, 0.0f, 0.0f)), &snap).translation == Vec3(0.0f));

    // A quarter turn about y snaps to 90 degrees; 37 degrees to 30.
    const GizmoShape rings = gizmoShape(GizmoTool::Rotate, camera, pose);
    const f32 radius = rings.worldLength;
    const auto onRing = [&](f32 degrees) {
        const f32 radians = glm::radians(degrees);
        // Ring y: angle 0 at +z, growing towards +x.
        return pixelOf(camera, Vec3(std::sin(radians), 0.0f, std::cos(radians)) * radius);
    };
    drag = GizmoDrag::begin(GizmoTool::Rotate, GizmoHandle::RotateY, camera, pose, onRing(0.0f));
    REQUIRE(drag.has_value());
    CHECK(drag->update(onRing(37.0f), &snap).angle == 30.0f);
    const GizmoDelta quarter = drag->update(onRing(88.0f), &snap);
    CHECK(quarter.angle == 90.0f);
    CHECK(glm::length(quarter.rotation * Vec3(0.0f, 0.0f, 1.0f) - Vec3(1.0f, 0.0f, 0.0f)) < 1e-5f);

    const GizmoShape boxes = gizmoShape(GizmoTool::Scale, camera, pose);
    const Vec3 end(boxes.worldLength, 0.0f, 0.0f);
    drag = GizmoDrag::begin(GizmoTool::Scale, GizmoHandle::ScaleX, camera, pose, pixelOf(camera, end));
    REQUIRE(drag.has_value());
    CHECK(drag->update(pixelOf(camera, end * 1.27f), &snap).scale == Vec3(1.0f + 3.0f * 0.1f, 1.0f, 1.0f));
}

TEST_CASE("rotate drags turn about rings, unwrap past half a turn, and work on rings seen edge on")
{
    const GizmoCamera camera = lookAt(Vec3(0.0f), 20.0f, -50.0f, 10.0f, false);
    const GizmoPose pose;
    const GizmoShape shape = gizmoShape(GizmoTool::Rotate, camera, pose);
    const f32 radius = shape.worldLength;
    const auto onRing = [&](f32 degrees) {
        const f32 radians = glm::radians(degrees);
        return pixelOf(camera, Vec3(std::sin(radians), 0.0f, std::cos(radians)) * radius);
    };
    std::optional<GizmoDrag> drag =
        GizmoDrag::begin(GizmoTool::Rotate, GizmoHandle::RotateY, camera, pose, onRing(0.0f));
    REQUIRE(drag.has_value());
    GizmoDelta delta;
    for (f32 degrees = 10.0f; degrees <= 270.0f; degrees += 10.0f)
    {
        delta = drag->update(onRing(degrees), nullptr);
    }
    CHECK(static_cast<double>(delta.angle) == doctest::Approx(270.0).epsilon(1e-3));

    // Seen almost edge on, the ring turns with the cursor along its tangent.
    const GizmoCamera grazing = lookAt(Vec3(0.0f), 0.0f, -5.0f, 10.0f, false);
    const GizmoShape flat = gizmoShape(GizmoTool::Rotate, grazing, pose);
    const Vec2 front = pixelOf(grazing, Vec3(0.0f, 0.0f, flat.worldLength));
    drag = GizmoDrag::begin(GizmoTool::Rotate, GizmoHandle::RotateY, grazing, pose, front);
    REQUIRE(drag.has_value());
    const f32 quarter = flat.length * std::numbers::pi_v<f32> * 0.5f;
    CHECK(static_cast<double>(drag->update(front + Vec2(quarter, 0.0f), nullptr).angle) ==
          doctest::Approx(90.0).epsilon(0.02));

    // The view ring: a counter-clockwise quarter turn on screen turns the camera's right into its up.
    drag = GizmoDrag::begin(GizmoTool::Rotate, GizmoHandle::RotateView, camera, pose,
                            shape.center + Vec2(shape.viewRing, 0.0f));
    REQUIRE(drag.has_value());
    const GizmoDelta view = drag->update(shape.center + Vec2(0.0f, -shape.viewRing), nullptr);
    CHECK(static_cast<double>(view.angle) == doctest::Approx(90.0));
    const Vec3 right = camera.rotation * Vec3(1.0f, 0.0f, 0.0f);
    const Vec3 up = camera.rotation * Vec3(0.0f, 1.0f, 0.0f);
    CHECK(glm::length(view.rotation * right - up) < 1e-4f);

    // The trackball: dragging right turns the front of the object to the right.
    drag = GizmoDrag::begin(GizmoTool::Rotate, GizmoHandle::RotateTrackball, camera, pose, shape.center);
    REQUIRE(drag.has_value());
    const GizmoDelta ball = drag->update(shape.center + Vec2(40.0f, 0.0f), nullptr);
    const Vec3 toward = camera.rotation * Vec3(0.0f, 0.0f, 1.0f);
    CHECK(glm::dot(ball.rotation * toward, right) > 0.3f);
    CHECK(std::abs(glm::dot(ball.rotation * toward, up)) < 1e-4f);
}

TEST_CASE("scale drags give factors along an axis or uniformly and never reach zero")
{
    const GizmoCamera camera = lookAt(Vec3(0.0f), 30.0f, -30.0f, 10.0f, true);
    GizmoPose pose;
    pose.orientation = glm::normalize(quatFromEuler(glm::radians(Vec3(0.0f, 0.0f, 30.0f))));
    const GizmoShape shape = gizmoShape(GizmoTool::Scale, camera, pose);
    const Vec3 y = shape.axes[1];
    std::optional<GizmoDrag> drag = GizmoDrag::begin(GizmoTool::Scale, GizmoHandle::ScaleY, camera, pose,
                                                     pixelOf(camera, y * shape.worldLength));
    REQUIRE(drag.has_value());
    const GizmoDelta twice = drag->update(pixelOf(camera, y * (shape.worldLength * 2.0f)), nullptr);
    CHECK(static_cast<double>(twice.scale.y) == doctest::Approx(2.0).epsilon(1e-3));
    CHECK(twice.scale.x == 1.0f);
    CHECK(drag->update(pixelOf(camera, -y * shape.worldLength), nullptr).scale.y == kGizmoMinScaleFactor);

    drag = GizmoDrag::begin(GizmoTool::Scale, GizmoHandle::ScaleUniform, camera, pose, shape.center);
    REQUIRE(drag.has_value());
    CHECK(drag->update(shape.center + Vec2(shape.length, 0.0f), nullptr).scale == Vec3(2.0f));
}

TEST_CASE("gizmo poses follow the pivot and space, and deltas move each entity or the whole selection")
{
    const GizmoCamera camera = lookAt(Vec3(0.0f), 30.0f, -30.0f, 10.0f, false);
    std::array<Transform, 2> placements{};
    placements[0].position = Vec3(-2.0f, 0.0f, 0.0f);
    placements[1].position = Vec3(4.0f, 2.0f, 0.0f);
    placements[1].rotation = glm::angleAxis(glm::radians(90.0f), Vec3(0.0f, 1.0f, 0.0f));
    CHECK_FALSE(gizmoPose({}, 0, GizmoTool::Translate, GizmoSpace::World, GizmoPivot::Median, camera));

    const GizmoPose median =
        *gizmoPose(placements, 1, GizmoTool::Translate, GizmoSpace::World, GizmoPivot::Median, camera);
    CHECK(median.origin == Vec3(1.0f, 1.0f, 0.0f));
    CHECK(median.orientation == Quat(1.0f, 0.0f, 0.0f, 0.0f));
    const GizmoPose active =
        *gizmoPose(placements, 1, GizmoTool::Rotate, GizmoSpace::Local, GizmoPivot::Active, camera);
    CHECK(active.origin == placements[1].position);
    CHECK(glm::length(active.orientation * Vec3(1.0f, 0.0f, 0.0f) - Vec3(0.0f, 0.0f, -1.0f)) < 1e-5f);
    CHECK(sameRotation(
        gizmoPose(placements, 1, GizmoTool::Translate, GizmoSpace::View, GizmoPivot::Median, camera)
            ->orientation,
        camera.rotation));
    // The scale gizmo always uses the active entity's axes.
    CHECK(
        sameRotation(gizmoPose(placements, 1, GizmoTool::Scale, GizmoSpace::World, GizmoPivot::Median, camera)
                         ->orientation,
                     placements[1].rotation));

    std::array<Transform, 2> out{};
    GizmoDelta turn;
    turn.rotation = glm::angleAxis(glm::radians(90.0f), Vec3(0.0f, 0.0f, 1.0f));
    applyGizmoDelta(placements, median, GizmoPivot::Median, turn, out);
    // (-2, 0) about (1, 1) goes to (2, -2).
    CHECK(glm::length(out[0].position - Vec3(2.0f, -2.0f, 0.0f)) < 1e-5f);
    applyGizmoDelta(placements, median, GizmoPivot::Individual, turn, out);
    CHECK(out[0].position == placements[0].position);
    CHECK(glm::length(out[1].rotation * Vec3(0.0f, 0.0f, 1.0f) -
                      turn.rotation * placements[1].rotation * Vec3(0.0f, 0.0f, 1.0f)) < 1e-5f);

    GizmoDelta grow;
    grow.scale = Vec3(2.0f, 1.0f, 1.0f);
    applyGizmoDelta(placements, median, GizmoPivot::Median, grow, out);
    CHECK(out[0].position == Vec3(-5.0f, 0.0f, 0.0f));
    CHECK(out[0].scale == Vec3(2.0f, 1.0f, 1.0f));
    GizmoDelta move;
    move.translation = Vec3(0.0f, 0.0f, 3.0f);
    applyGizmoDelta(placements, median, GizmoPivot::Individual, move, out);
    CHECK(out[1].position == Vec3(4.0f, 2.0f, 3.0f));
    CHECK(sameRotation(out[1].rotation, placements[1].rotation));
}

TEST_CASE("gizmo targets skip children of selected parents and end with the active entity")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    ok(editor, "entity.create",
       R"({"name":"Parent","components":{"illusion.Transform":{"position":[1,0,0]}}})");
    ok(editor, "entity.create",
       R"({"name":"Child","parent":"/Parent","components":{"illusion.Transform":{},"illusion.LocalTransform":{"position":[0,2,0]}}})");
    ok(editor, "entity.create",
       R"({"name":"Other","components":{"illusion.Transform":{"position":[0,0,5]}}})");
    ok(editor, "entity.create", R"({"name":"Bare"})");
    ok(editor, "selection.set", R"({"entities":["/Parent/Child","/Other","/Parent","/Bare"]})");
    std::vector<GizmoTarget> targets;
    gizmoTargets(editor, targets);
    REQUIRE(targets.size() == 2);
    CHECK(targets[0].placement.position == Vec3(0.0f, 0.0f, 5.0f));
    CHECK(targets[1].placement.position == Vec3(1.0f, 0.0f, 0.0f));
    CHECK(targets[1].guid == editor.entities().guidOf(editor.entities().find(editor.selection().items()[2])));

    ok(editor, "selection.set", R"({"entities":["/Parent/Child"]})");
    gizmoTargets(editor, targets);
    REQUIRE(targets.size() == 1);
    CHECK(targets[0].placement.position == Vec3(1.0f, 2.0f, 0.0f));
}

TEST_CASE("gizmo commands change the tool, axes, pivot and snapping and validate their params")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    const JsonDocument initial = ok(editor, "gizmo.get");
    CHECK(initial.root().find("tool").asString() == "translate");
    CHECK(initial.root().find("space").asString() == "world");
    CHECK(initial.root().find("pivot").asString() == "median");
    CHECK(initial.root().pointer("/snap/enabled").asBool() == false);

    CHECK(ok(editor, "gizmo.set_tool", R"({"tool":"rotate"})").root().find("tool").asString() == "rotate");
    CHECK(editor.gizmo().tool == GizmoTool::Rotate);
    ok(editor, "gizmo.set_space", R"({"space":"local"})");
    ok(editor, "gizmo.set_pivot", R"({"pivot":"individual"})");
    ok(editor, "gizmo.set_snap", R"({"enabled":true,"translate":0.25,"rotate":5,"scale":0.5})");
    CHECK(editor.gizmo().space == GizmoSpace::Local);
    CHECK(editor.gizmo().pivot == GizmoPivot::Individual);
    CHECK(editor.gizmo().snap.enabled);
    CHECK(editor.gizmo().snap.translate == 0.25f);
    CHECK(editor.gizmo().snap.rotate == 5.0f);
    CHECK(editor.gizmo().snap.scale == 0.5f);
    // Gizmo settings are not undo steps.
    CHECK(editor.history().steps().empty());

    ok(editor, "gizmo.set_tool", R"({"tool":"scale","dryRun":true})");
    CHECK(editor.gizmo().tool == GizmoTool::Rotate);
    CHECK(fails(editor, "gizmo.set_tool", R"({"tool":"lasso"})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "gizmo.set_tool", R"({"tool":9})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "gizmo.set_space", R"({"space":3})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "gizmo.set_pivot", R"({"pivot":7})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "gizmo.set_snap", R"({})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "gizmo.set_snap", R"({"translate":0})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "gizmo.set_snap", R"({"rotate":400})").kind == ErrorKind::ValidationFailed);
    CHECK(editor.gizmo().snap.rotate == 5.0f);
}

TEST_CASE("transform.snap_to_grid rounds world positions to the grid in one undo step")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    ok(editor, "entity.create",
       R"({"name":"Board","components":{"illusion.Transform":{"position":[1.26,0.74,-0.2]}}})");
    ok(editor, "entity.create",
       R"({"name":"Piece","parent":"/Board","components":{"illusion.Transform":{},"illusion.LocalTransform":{"position":[0.1,0,0]}}})");
    const std::string before = fixture.scene();
    const usize steps = editor.history().steps().size();
    ok(editor, "transform.snap_to_grid", R"({"entities":["/Board","/Board/Piece"],"increment":0.5})");
    const JsonDocument board = ok(editor, "transform.get", R"({"entity":"/Board"})");
    CHECK(board.root().find("position")[0].asF64() == doctest::Approx(1.5));
    CHECK(board.root().find("position")[1].asF64() == doctest::Approx(0.5));
    CHECK(board.root().find("position")[2].asF64() == doctest::Approx(0.0));
    // The child follows its parent: it keeps its local offset.
    const JsonDocument piece = ok(editor, "transform.get", R"({"entity":"/Board/Piece","space":"local"})");
    CHECK(piece.root().find("position")[0].asF64() == doctest::Approx(0.1).epsilon(1e-5));
    CHECK(editor.history().steps().size() == steps + 1);
    ok(editor, "history.undo");
    CHECK(fixture.scene() == before);

    // The default increment is the gizmo's translation snap.
    ok(editor, "gizmo.set_snap", R"({"translate":2})");
    ok(editor, "transform.snap_to_grid", R"({"entities":["/Board"]})");
    CHECK(ok(editor, "transform.get", R"({"entity":"/Board"})").root().find("position")[0].asF64() ==
          doctest::Approx(2.0));
    CHECK(fails(editor, "transform.snap_to_grid", R"({"entities":["/Board"],"increment":0})").kind ==
          ErrorKind::ValidationFailed);
}

TEST_CASE("a gizmo drag inside a transaction is one undo step and cancelling it restores the scene exactly")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    ok(editor, "entity.create",
       R"({"name":"Board","components":{"illusion.Transform":{"position":[0.1,0.2,0.3],"rotation":[0,0.3826834,0,0.9238795]}}})");
    const std::string before = fixture.scene();
    const usize steps = editor.history().steps().size();
    CommandOptions gizmo;
    gizmo.origin = "gizmo";
    const auto dragTo = [&](f32 x) {
        const std::string value = std::to_string(x);
        ok(editor, "editor.batch",
           R"({"steps":[{"method":"transform.set","params":{"entities":["/Board"],"position":[)" + value +
               R"(,0.2,0.3]}}]})",
           gizmo);
    };
    ok(editor, "transaction.begin", R"({"label":"Move"})", gizmo);
    for (int i = 1; i <= 30; ++i)
    {
        dragTo(0.1f + static_cast<f32>(i) * 0.05f);
    }
    ok(editor, "transaction.cancel", "{}", gizmo);
    CHECK(fixture.scene() == before);
    CHECK(editor.history().steps().size() == steps);

    ok(editor, "transaction.begin", R"({"label":"Move"})", gizmo);
    for (int i = 1; i <= 30; ++i)
    {
        dragTo(0.1f + static_cast<f32>(i) * 0.05f);
    }
    ok(editor, "transaction.commit", "{}", gizmo);
    REQUIRE(editor.history().steps().size() == steps + 1);
    CHECK(editor.history().steps().back().label == "Move");
    ok(editor, "history.undo");
    CHECK(fixture.scene() == before);
}
