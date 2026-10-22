#include "Budget.hpp"

#include <illusion/core/Math.hpp>
#include <illusion/ecs/Transform.hpp>

#include <random>
#include <vector>

using namespace illusion;

namespace
{
constexpr u32 kObjects = 100000;

// Objects scattered in a 200 m cube around a camera at the origin, so roughly a fifth are visible.
std::vector<Mat4> makeScene()
{
    std::mt19937 random(1234);
    std::uniform_real_distribution<f32> position(-100.0f, 100.0f);
    std::uniform_real_distribution<f32> angle(0.0f, 6.28f);
    std::vector<Mat4> matrices;
    matrices.reserve(kObjects);
    for (u32 i = 0; i < kObjects; ++i)
    {
        Transform transform;
        transform.position = Vec3(position(random), position(random), position(random));
        transform.rotation = glm::angleAxis(angle(random), Vec3(0.0f, 1.0f, 0.0f));
        matrices.push_back(transform.matrix());
    }
    return matrices;
}
} // namespace

// Budgets (ns per object) are roughly 5-10x a desktop result; see Budget.hpp.

ILL_BENCHMARK("math/frustum-cull-spheres", 50.0)
{
    const std::vector<Mat4> matrices = makeScene();
    const Sphere local = Sphere::fromAabb(Aabb{Vec3(-0.5f), Vec3(0.5f)});
    const Frustum frustum =
        Frustum::fromViewProjection(perspectiveReversedZ(1.0f, 16.0f / 9.0f, 0.1f) *
                                    glm::lookAt(Vec3(0.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(0, 1, 0)));
    bench.batch(kObjects).unit("object").run("transform bounds + test", [&] {
        u32 visible = 0;
        for (const Mat4& matrix : matrices)
        {
            visible += frustum.intersects(local.transformed(matrix)) ? 1u : 0u;
        }
        ankerl::nanobench::doNotOptimizeAway(visible);
    });
}

ILL_BENCHMARK("math/ray-aabb", 30.0)
{
    std::mt19937 random(99);
    std::uniform_real_distribution<f32> coordinate(-10.0f, 10.0f);
    std::vector<Aabb> boxes;
    boxes.reserve(1024);
    for (u32 i = 0; i < 1024; ++i)
    {
        const Vec3 center(coordinate(random), coordinate(random), coordinate(random));
        boxes.push_back(Aabb{center - Vec3(0.5f), center + Vec3(0.5f)});
    }
    const Ray ray{Vec3(0.0f, 0.0f, 20.0f), glm::normalize(Vec3(0.05f, -0.02f, -1.0f))};
    bench.batch(boxes.size()).unit("box").run("intersect(Ray, Aabb)", [&] {
        u32 hits = 0;
        for (const Aabb& box : boxes)
        {
            hits += intersect(ray, box) ? 1u : 0u;
        }
        ankerl::nanobench::doNotOptimizeAway(hits);
    });
}
