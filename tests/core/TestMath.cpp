#include <illusion/core/Math.hpp>
#include <illusion/ecs/Transform.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace illusion;

namespace
{

f32 depthOf(const Mat4& projection, f32 viewZ)
{
    const Vec4 clip = projection * Vec4(0.0f, 0.0f, viewZ, 1.0f);
    return clip.z / clip.w;
}

} // namespace

TEST_CASE("reversed-Z projection maps near to 1 and infinity towards 0")
{
    const Mat4 projection = perspectiveReversedZ(std::numbers::pi_v<f32> / 2.0f, 16.0f / 9.0f, 0.1f);
    CHECK(depthOf(projection, -0.1f) == doctest::Approx(1.0));
    CHECK(depthOf(projection, -1.0f) == doctest::Approx(0.1));
    CHECK(depthOf(projection, -1000.0f) == doctest::Approx(0.0001));
    CHECK(depthOf(projection, -10.0f) > depthOf(projection, -20.0f)); // nearer is larger

    // y up: a point above the view axis has positive clip y.
    const Vec4 above = projection * Vec4(0.0f, 1.0f, -5.0f, 1.0f);
    CHECK(above.y / above.w > 0.0f);
}

TEST_CASE("Aabb expands, transforms and converts to a sphere")
{
    Aabb box;
    CHECK_FALSE(box.isValid());
    box.expand(Vec3(-1.0f, 0.0f, 0.0f));
    box.expand(Vec3(1.0f, 2.0f, 0.0f));
    CHECK(box.isValid());
    CHECK(box.center() == Vec3(0.0f, 1.0f, 0.0f));

    const Aabb moved = box.transformed(glm::translate(Mat4(1.0f), Vec3(10.0f, 0.0f, 0.0f)));
    CHECK(moved.min.x == doctest::Approx(9.0));
    const Aabb rotated =
        box.transformed(glm::rotate(Mat4(1.0f), std::numbers::pi_v<f32> / 2.0f, Vec3(0, 0, 1)));
    CHECK(rotated.extents().x == doctest::Approx(1.0));
    CHECK(rotated.extents().y == doctest::Approx(1.0));
    CHECK_FALSE(Aabb{}.transformed(Mat4(1.0f)).isValid());

    const Sphere sphere = Sphere::fromAabb(box);
    CHECK(sphere.radius == doctest::Approx(std::sqrt(2.0)));
    const Sphere scaled = sphere.transformed(glm::scale(Mat4(1.0f), Vec3(1.0f, 3.0f, 1.0f)));
    CHECK(scaled.radius == doctest::Approx(3.0 * std::sqrt(2.0)));

    // Off-centre spheres move with the full transform: scale, then rotate, then translate.
    const Mat4 matrix = glm::translate(Mat4(1.0f), Vec3(5.0f, 0.0f, 0.0f)) *
                        glm::rotate(Mat4(1.0f), std::numbers::pi_v<f32> / 2.0f, Vec3(0, 0, 1)) *
                        glm::scale(Mat4(1.0f), Vec3(2.0f));
    const Sphere offCentre = Sphere{Vec3(1.0f, 0.0f, 0.0f), 0.5f}.transformed(matrix);
    CHECK(offCentre.center.x == doctest::Approx(5.0));
    CHECK(offCentre.center.y == doctest::Approx(2.0));
    CHECK(offCentre.center.z == doctest::Approx(0.0).epsilon(1e-6));
    CHECK(offCentre.radius == doctest::Approx(1.0));
}

TEST_CASE("frustum culling keeps visible objects and rejects hidden ones")
{
    const Mat4 projection = perspectiveReversedZ(std::numbers::pi_v<f32> / 2.0f, 1.0f, 0.1f);
    const Mat4 view = glm::lookAt(Vec3(0.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 1.0f, 0.0f));
    const Frustum frustum = Frustum::fromViewProjection(projection * view);

    CHECK(frustum.intersects(Sphere{Vec3(0.0f, 0.0f, -10.0f), 1.0f}));
    CHECK(frustum.intersects(Sphere{Vec3(0.0f, 0.0f, -100000.0f), 1.0f}));    // infinite far plane
    CHECK_FALSE(frustum.intersects(Sphere{Vec3(0.0f, 0.0f, 10.0f), 1.0f}));   // behind
    CHECK_FALSE(frustum.intersects(Sphere{Vec3(50.0f, 0.0f, -10.0f), 1.0f})); // right of a 90 degree fov
    CHECK(frustum.intersects(Sphere{Vec3(10.5f, 0.0f, -10.0f), 1.0f}));       // straddles the edge

    CHECK(frustum.intersects(Aabb{Vec3(-1.0f, -1.0f, -11.0f), Vec3(1.0f, 1.0f, -9.0f)}));
    CHECK_FALSE(frustum.intersects(Aabb{Vec3(-1.0f, -1.0f, 1.0f), Vec3(1.0f, 1.0f, 3.0f)}));
}

TEST_CASE("Transform builds matrices and decomposes them")
{
    Transform transform;
    transform.position = Vec3(1.0f, 2.0f, 3.0f);
    transform.rotation = glm::angleAxis(0.7f, glm::normalize(Vec3(1.0f, 2.0f, 0.5f)));
    transform.scale = Vec3(2.0f, 3.0f, 4.0f);
    const Mat4 m = transform.matrix();
    const Transform back = Transform::fromMatrix(m);
    CHECK(back.position.y == doctest::Approx(2.0));
    CHECK(back.scale.z == doctest::Approx(4.0));
    const Mat4 rebuilt = back.matrix();
    for (int c = 0; c < 4; ++c)
    {
        for (int r = 0; r < 4; ++r)
        {
            CHECK(rebuilt[c][r] == doctest::Approx(m[c][r]).epsilon(1e-4));
        }
    }

    CHECK(Transform{}.forward() == Vec3(0.0f, 0.0f, -1.0f));
    const Transform mirrored = Transform::fromMatrix(glm::scale(Mat4(1.0f), Vec3(-1.0f, 1.0f, 1.0f)));
    CHECK(mirrored.scale.x == doctest::Approx(-1.0));
}

namespace
{

bool near(Vec3 a, Vec3 b, f32 epsilon = 1e-4f)
{
    return glm::length(a - b) <= epsilon;
}

// Angle between two rotations in radians.
f32 angleBetween(const Quat& a, const Quat& b)
{
    return 2.0f * std::acos(std::min(1.0f, std::abs(glm::dot(glm::normalize(a), glm::normalize(b)))));
}

} // namespace

TEST_CASE("orthographic reversed-Z maps near to 1 and far to 0 with y up")
{
    const Mat4 projection = orthographicReversedZ(-4.0f, 4.0f, -2.0f, 2.0f, 0.5f, 100.0f);
    CHECK(depthOf(projection, -0.5f) == doctest::Approx(1.0));
    CHECK(depthOf(projection, -100.0f) == doctest::Approx(0.0).epsilon(1e-6));
    CHECK(depthOf(projection, -50.25f) == doctest::Approx(0.5));
    const Vec4 corner = projection * Vec4(4.0f, 2.0f, -10.0f, 1.0f);
    CHECK(corner.x == doctest::Approx(1.0));
    CHECK(corner.y == doctest::Approx(1.0));
    CHECK(corner.w == doctest::Approx(1.0));
}

TEST_CASE("rays hit boxes from outside and from inside and miss parallel faces")
{
    const Aabb box{Vec3(-1.0f), Vec3(1.0f)};
    CHECK(intersect(Ray{Vec3(0.0f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, -1.0f)}, box) == doctest::Approx(4.0));
    CHECK(intersect(Ray{Vec3(0.0f), Vec3(1.0f, 0.0f, 0.0f)}, box) == doctest::Approx(0.0)); // origin inside
    CHECK_FALSE(intersect(Ray{Vec3(0.0f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, 1.0f)}, box));       // behind
    // Parallel to the x faces: hits only between their planes.
    CHECK(intersect(Ray{Vec3(0.5f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, -1.0f)}, box));
    CHECK_FALSE(intersect(Ray{Vec3(1.5f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, -1.0f)}, box));
    // Along a face: the surface counts as inside.
    CHECK(intersect(Ray{Vec3(1.0f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, -1.0f)}, box) == doctest::Approx(4.0));
    CHECK_FALSE(intersect(Ray{Vec3(0.0f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, -1.0f)}, Aabb{}));
    // A non-unit direction gives parameters in multiples of its length.
    CHECK(intersect(Ray{Vec3(0.0f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, -2.0f)}, box) == doctest::Approx(2.0));
}

TEST_CASE("rays hit spheres and planes")
{
    const Sphere sphere{Vec3(0.0f, 0.0f, -10.0f), 2.0f};
    CHECK(intersect(Ray{Vec3(0.0f), Vec3(0.0f, 0.0f, -1.0f)}, sphere) == doctest::Approx(8.0));
    CHECK(intersect(Ray{Vec3(0.0f, 0.0f, -10.0f), Vec3(0.0f, 1.0f, 0.0f)}, sphere) == doctest::Approx(0.0));
    CHECK_FALSE(intersect(Ray{Vec3(0.0f), Vec3(0.0f, 0.0f, 1.0f)}, sphere));
    CHECK_FALSE(intersect(Ray{Vec3(3.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)}, sphere));

    const Plane ground = Plane::fromPointNormal(Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    CHECK(ground.signedDistance(Vec3(0.0f, 3.0f, 0.0f)) == doctest::Approx(2.0));
    CHECK(intersect(Ray{Vec3(0.0f, 5.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)}, ground) == doctest::Approx(4.0));
    CHECK(intersect(Ray{Vec3(0.0f, -5.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)}, ground) == doctest::Approx(6.0));
    CHECK_FALSE(intersect(Ray{Vec3(0.0f, 5.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f)}, ground)); // parallel
    CHECK_FALSE(intersect(Ray{Vec3(0.0f, 5.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)}, ground)); // behind
}

TEST_CASE("rays hit triangles on both sides and on their edges")
{
    const Vec3 a(0.0f, 0.0f, 0.0f);
    const Vec3 b(1.0f, 0.0f, 0.0f);
    const Vec3 c(0.0f, 1.0f, 0.0f);
    const auto front = intersect(Ray{Vec3(0.25f, 0.25f, 2.0f), Vec3(0.0f, 0.0f, -1.0f)}, a, b, c);
    REQUIRE(front);
    CHECK(front->t == doctest::Approx(2.0));
    CHECK(front->u == doctest::Approx(0.25));
    CHECK(front->v == doctest::Approx(0.25));
    CHECK(intersect(Ray{Vec3(0.25f, 0.25f, -2.0f), Vec3(0.0f, 0.0f, 1.0f)}, a, b, c)); // back side
    // Grazing an edge and a corner counts; just outside does not.
    CHECK(intersect(Ray{Vec3(0.5f, 0.0f, 1.0f), Vec3(0.0f, 0.0f, -1.0f)}, a, b, c));
    CHECK(intersect(Ray{Vec3(0.5f, 0.5f, 1.0f), Vec3(0.0f, 0.0f, -1.0f)}, a, b, c));
    CHECK(intersect(Ray{Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 0.0f, -1.0f)}, a, b, c));
    CHECK_FALSE(intersect(Ray{Vec3(0.6f, 0.6f, 1.0f), Vec3(0.0f, 0.0f, -1.0f)}, a, b, c));
    CHECK_FALSE(intersect(Ray{Vec3(-0.01f, 0.5f, 1.0f), Vec3(0.0f, 0.0f, -1.0f)}, a, b, c));
    // In the triangle's plane, behind it, and degenerate triangles miss.
    CHECK_FALSE(intersect(Ray{Vec3(-1.0f, 0.25f, 0.0f), Vec3(1.0f, 0.0f, 0.0f)}, a, b, c));
    CHECK_FALSE(intersect(Ray{Vec3(0.25f, 0.25f, 2.0f), Vec3(0.0f, 0.0f, 1.0f)}, a, b, c));
    CHECK_FALSE(intersect(Ray{Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 0.0f, -1.0f)}, a, b, b));
}

TEST_CASE("rays hit oriented boxes in the box's frame")
{
    const Obb box{Vec3(0.0f, 0.0f, -10.0f), Vec3(1.0f, 1.0f, 3.0f),
                  glm::angleAxis(std::numbers::pi_v<f32> / 2.0f, Vec3(0.0f, 1.0f, 0.0f))};
    // Turned 90 degrees about y, the long z axis lies along x.
    CHECK(intersect(Ray{Vec3(10.0f, 0.0f, -10.0f), Vec3(-1.0f, 0.0f, 0.0f)}, box) == doctest::Approx(7.0));
    CHECK(intersect(Ray{Vec3(0.0f), Vec3(0.0f, 0.0f, -1.0f)}, box) == doctest::Approx(9.0));
    CHECK_FALSE(
        intersect(Ray{Vec3(2.5f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)}, Obb{box.center, box.halfExtents}));
    CHECK(intersect(Ray{Vec3(2.5f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)}, box));
}

TEST_CASE("closest points between a ray and a segment")
{
    // A ray along z crossing above a segment along x.
    const RaySegmentClosest crossing = closestPoints(Ray{Vec3(0.5f, 1.0f, 5.0f), Vec3(0.0f, 0.0f, -1.0f)},
                                                     Vec3(0.0f), Vec3(1.0f, 0.0f, 0.0f));
    CHECK(crossing.rayT == doctest::Approx(5.0));
    CHECK(crossing.segmentT == doctest::Approx(0.5));
    CHECK(crossing.distance == doctest::Approx(1.0));
    // Beyond the segment's end the end is closest; behind the ray's origin the origin is.
    const RaySegmentClosest clamped = closestPoints(Ray{Vec3(3.0f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, 1.0f)},
                                                    Vec3(0.0f), Vec3(1.0f, 0.0f, 0.0f));
    CHECK(clamped.rayT == doctest::Approx(0.0));
    CHECK(clamped.segmentT == doctest::Approx(1.0));
    CHECK(clamped.distance == doctest::Approx(std::sqrt(29.0)));
    // Parallel: the ray point nearest to the segment's start.
    const RaySegmentClosest parallel = closestPoints(Ray{Vec3(-2.0f, 1.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f)},
                                                     Vec3(0.0f), Vec3(1.0f, 0.0f, 0.0f));
    CHECK(parallel.rayT == doctest::Approx(2.0));
    CHECK(parallel.segmentT == doctest::Approx(0.0));
    CHECK(parallel.distance == doctest::Approx(1.0));
    // A degenerate segment is a point.
    const RaySegmentClosest point = closestPoints(Ray{Vec3(0.0f, 2.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f)},
                                                  Vec3(3.0f, 0.0f, 0.0f), Vec3(3.0f, 0.0f, 0.0f));
    CHECK(point.rayT == doctest::Approx(3.0));
    CHECK(point.distance == doctest::Approx(2.0));
}

TEST_CASE("screen rays go through the pixel for perspective and orthographic cameras")
{
    const Vec2 viewport(800.0f, 600.0f);
    const Mat4 view = glm::lookAt(Vec3(0.0f, 0.0f, 10.0f), Vec3(0.0f), Vec3(0.0f, 1.0f, 0.0f));

    const Mat4 perspective = perspectiveReversedZ(std::numbers::pi_v<f32> / 2.0f, 800.0f / 600.0f, 0.1f);
    const Ray center = screenToRay(viewport * 0.5f, viewport, perspective * view);
    CHECK(near(center.origin, Vec3(0.0f, 0.0f, 9.9f)));
    CHECK(near(center.direction, Vec3(0.0f, 0.0f, -1.0f)));
    // The top edge is 45 degrees up; y grows downwards on screen.
    const Ray top = screenToRay(Vec2(400.0f, 0.0f), viewport, perspective * view);
    CHECK(near(top.direction, glm::normalize(Vec3(0.0f, 1.0f, -1.0f))));
    // A point projected to the screen lies on its ray.
    const Vec3 target(1.5f, -0.75f, -3.0f);
    const Vec4 clip = perspective * view * Vec4(target, 1.0f);
    const Vec2 ndc = Vec2(clip) / clip.w;
    const Vec2 pixel((ndc.x + 1.0f) * 0.5f * viewport.x, (1.0f - ndc.y) * 0.5f * viewport.y);
    const Ray toTarget = screenToRay(pixel, viewport, perspective * view);
    CHECK(closestPoints(toTarget, target, target).distance < 1e-3f);

    const Mat4 orthographic = orthographicReversedZ(-4.0f, 4.0f, -3.0f, 3.0f, 0.5f, 50.0f);
    const Ray corner = screenToRay(Vec2(0.0f, 0.0f), viewport, orthographic * view);
    CHECK(near(corner.origin, Vec3(-4.0f, 3.0f, 9.5f)));
    CHECK(near(corner.direction, Vec3(0.0f, 0.0f, -1.0f)));
}

TEST_CASE("Euler angles round-trip and stay continuous near the hint")
{
    const Vec3 angles(0.3f, -1.2f, 2.0f);
    const Quat rotation = quatFromEuler(angles);
    CHECK(near(eulerFromQuat(rotation), angles));
    // yaw * pitch * roll
    const Quat expected = glm::angleAxis(angles.y, Vec3(0.0f, 1.0f, 0.0f)) *
                          glm::angleAxis(angles.x, Vec3(1.0f, 0.0f, 0.0f)) *
                          glm::angleAxis(angles.z, Vec3(0.0f, 0.0f, 1.0f));
    CHECK(angleBetween(rotation, expected) < 1e-5f);

    // Whole turns follow the hint, so an angle dragged past 180 degrees keeps growing.
    const f32 turn = 2.0f * std::numbers::pi_v<f32>;
    CHECK(near(eulerFromQuat(rotation, angles + Vec3(0.0f, turn, 0.0f)), angles + Vec3(0.0f, turn, 0.0f)));
    const Vec3 past(0.0f, 3.3f, 0.0f);
    CHECK(near(eulerFromQuat(quatFromEuler(past), Vec3(0.0f, 3.1f, 0.0f)), past));

    // The equivalent form (pi - x, y + pi, z + pi) is chosen when it is closer to the hint.
    const f32 pi = std::numbers::pi_v<f32>;
    const Vec3 flipped(pi - 0.3f, 0.5f + pi, pi);
    CHECK(near(eulerFromQuat(quatFromEuler(flipped), flipped), flipped));

    // Gimbal lock keeps the hinted roll and still gives the same rotation.
    const Vec3 locked(pi / 2.0f, 0.4f, 0.7f);
    const Vec3 lockedResult = eulerFromQuat(quatFromEuler(locked), Vec3(1.5f, 0.0f, 0.7f));
    CHECK(lockedResult.z == doctest::Approx(0.7));
    CHECK(angleBetween(quatFromEuler(lockedResult), quatFromEuler(locked)) < 1e-3f);
    const Vec3 lockedDown(-pi / 2.0f, 0.4f, 0.7f);
    CHECK(angleBetween(quatFromEuler(eulerFromQuat(quatFromEuler(lockedDown), Vec3(0.0f, 0.0f, -0.2f))),
                       quatFromEuler(lockedDown)) < 1e-3f);
}

TEST_CASE("combine places children and relativeTo inverts it")
{
    Transform parent;
    parent.position = Vec3(1.0f, 2.0f, 3.0f);
    parent.rotation = glm::angleAxis(0.7f, glm::normalize(Vec3(1.0f, 1.0f, 0.0f)));
    parent.scale = Vec3(2.0f, 3.0f, 0.5f);
    Transform local;
    local.position = Vec3(0.5f, -1.0f, 2.0f);
    local.rotation = glm::angleAxis(-0.4f, Vec3(0.0f, 0.0f, 1.0f));
    local.scale = Vec3(1.5f);

    const Transform world = combine(parent, local);
    // The position is exact: it equals the matrix product.
    CHECK(near(world.position, Vec3(parent.matrix() * Vec4(local.position, 1.0f))));
    const Transform back = relativeTo(world, parent);
    CHECK(near(back.position, local.position));
    CHECK(angleBetween(back.rotation, local.rotation) < 1e-5f);
    CHECK(near(back.scale, local.scale));
    // A zero parent scale gives zero instead of infinity.
    parent.scale = Vec3(0.0f, 1.0f, 1.0f);
    CHECK(relativeTo(world, parent).scale.x == 0.0f);
}
