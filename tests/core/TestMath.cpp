#include <illusion/core/Math.hpp>
#include <illusion/ecs/Transform.hpp>

#include <doctest/doctest.h>

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
