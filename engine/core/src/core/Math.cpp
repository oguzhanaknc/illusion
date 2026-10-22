#include <illusion/core/Math.hpp>

#include <glm/gtc/constants.hpp>

namespace illusion
{

namespace
{

constexpr f32 kParallelEpsilon = 1e-8f;

// Shifts @p angle by whole turns to the value closest to @p reference.
f32 nearestTurn(f32 angle, f32 reference) noexcept
{
    const f32 turn = glm::two_pi<f32>();
    return angle + turn * std::round((reference - angle) / turn);
}

Vec3 nearestTurns(Vec3 angles, Vec3 reference) noexcept
{
    return Vec3(nearestTurn(angles.x, reference.x), nearestTurn(angles.y, reference.y),
                nearestTurn(angles.z, reference.z));
}

} // namespace

Quat quatFromEuler(Vec3 radians) noexcept
{
    return glm::angleAxis(radians.y, Vec3(0.0f, 1.0f, 0.0f)) *
           glm::angleAxis(radians.x, Vec3(1.0f, 0.0f, 0.0f)) *
           glm::angleAxis(radians.z, Vec3(0.0f, 0.0f, 1.0f));
}

Vec3 eulerFromQuat(const Quat& rotation, Vec3 hint) noexcept
{
    // R = Ry * Rx * Rz. With r(row, column) = m[column][row]: r(1,2) = -sin(x), r(0,2) = sin(y) cos(x),
    // r(2,2) = cos(y) cos(x), r(1,0) = cos(x) sin(z), r(1,1) = cos(x) cos(z).
    const Mat3 m = glm::mat3_cast(glm::normalize(rotation));
    const f32 sinX = -m[2][1];
    const f32 cosX = std::sqrt(m[2][0] * m[2][0] + m[2][2] * m[2][2]);
    const f32 x = std::atan2(sinX, cosX);
    if (cosX < 1e-6f)
    {
        // Gimbal lock: only yaw - roll (pitch +90°) or yaw + roll (pitch -90°) is defined. Keep the hinted
        // roll.
        const f32 z = hint.z;
        const f32 y = sinX > 0.0f ? z + std::atan2(m[1][0], m[0][0]) : std::atan2(-m[1][0], m[0][0]) - z;
        return Vec3(nearestTurn(x, hint.x), nearestTurn(y, hint.y), z);
    }
    const Vec3 first(x, std::atan2(m[2][0], m[2][2]), std::atan2(m[0][1], m[1][1]));
    // The same rotation as (pi - x, y + pi, z + pi).
    const Vec3 second(glm::pi<f32>() - first.x, first.y + glm::pi<f32>(), first.z + glm::pi<f32>());
    const Vec3 a = nearestTurns(first, hint);
    const Vec3 b = nearestTurns(second, hint);
    const Vec3 da = a - hint;
    const Vec3 db = b - hint;
    return glm::dot(da, da) <= glm::dot(db, db) ? a : b;
}

std::optional<f32> intersect(const Ray& ray, const Aabb& box) noexcept
{
    if (!box.isValid())
    {
        return std::nullopt;
    }
    f32 enter = 0.0f;
    f32 exit = std::numeric_limits<f32>::max();
    for (int axis = 0; axis < 3; ++axis)
    {
        const f32 origin = ray.origin[axis];
        const f32 direction = ray.direction[axis];
        if (std::abs(direction) < kParallelEpsilon)
        {
            if (origin < box.min[axis] || origin > box.max[axis])
            {
                return std::nullopt;
            }
            continue;
        }
        const f32 inverse = 1.0f / direction;
        f32 entry = (box.min[axis] - origin) * inverse;
        f32 leave = (box.max[axis] - origin) * inverse;
        if (entry > leave)
        {
            std::swap(entry, leave);
        }
        enter = std::max(enter, entry);
        exit = std::min(exit, leave);
        if (enter > exit)
        {
            return std::nullopt;
        }
    }
    return enter;
}

std::optional<f32> intersect(const Ray& ray, const Sphere& sphere) noexcept
{
    const Vec3 offset = ray.origin - sphere.center;
    const f32 c = glm::dot(offset, offset) - sphere.radius * sphere.radius;
    if (c <= 0.0f)
    {
        return 0.0f; // the origin is inside
    }
    const f32 a = glm::dot(ray.direction, ray.direction);
    const f32 b = glm::dot(offset, ray.direction);
    if (b >= 0.0f || a < kParallelEpsilon)
    {
        return std::nullopt; // pointing away from the sphere
    }
    const f32 discriminant = b * b - a * c;
    if (discriminant < 0.0f)
    {
        return std::nullopt;
    }
    return (-b - std::sqrt(discriminant)) / a;
}

std::optional<f32> intersect(const Ray& ray, const Plane& plane) noexcept
{
    const f32 denominator = glm::dot(plane.normal, ray.direction);
    if (std::abs(denominator) < kParallelEpsilon)
    {
        return std::nullopt;
    }
    const f32 t = -plane.signedDistance(ray.origin) / denominator;
    if (t < 0.0f)
    {
        return std::nullopt;
    }
    return t;
}

std::optional<RayTriangleHit> intersect(const Ray& ray, Vec3 a, Vec3 b, Vec3 c) noexcept
{
    const Vec3 edge1 = b - a;
    const Vec3 edge2 = c - a;
    const Vec3 p = glm::cross(ray.direction, edge2);
    const f32 determinant = glm::dot(edge1, p);
    // Relative to the sizes involved, so that tiny and huge triangles behave alike.
    const f32 scale = glm::length(ray.direction) * glm::length(edge1) * glm::length(edge2);
    if (!(std::abs(determinant) > 1e-7f * scale))
    {
        return std::nullopt; // parallel to the triangle's plane, or a degenerate triangle
    }
    const f32 inverse = 1.0f / determinant;
    const Vec3 s = ray.origin - a;
    const f32 u = glm::dot(s, p) * inverse;
    if (u < 0.0f || u > 1.0f)
    {
        return std::nullopt;
    }
    const Vec3 q = glm::cross(s, edge1);
    const f32 v = glm::dot(ray.direction, q) * inverse;
    if (v < 0.0f || u + v > 1.0f)
    {
        return std::nullopt;
    }
    const f32 t = glm::dot(edge2, q) * inverse;
    if (t < 0.0f)
    {
        return std::nullopt;
    }
    return RayTriangleHit{t, u, v};
}

std::optional<f32> intersect(const Ray& ray, const Obb& box) noexcept
{
    // In the box's frame the box is axis-aligned; the rotation keeps ray parameters unchanged.
    const Quat inverse = glm::inverse(glm::normalize(box.rotation));
    const Ray local{inverse * (ray.origin - box.center), inverse * ray.direction};
    return intersect(local, Aabb{-box.halfExtents, box.halfExtents});
}

RaySegmentClosest closestPoints(const Ray& ray, Vec3 start, Vec3 end) noexcept
{
    // Minimizes |origin + d t - (start + e s)| for t >= 0 and s in [0, 1].
    const Vec3 d = ray.direction;
    const Vec3 e = end - start;
    const Vec3 r = ray.origin - start;
    const f32 dd = glm::dot(d, d);
    const f32 ee = glm::dot(e, e);
    const f32 de = glm::dot(d, e);
    const f32 dr = glm::dot(d, r);
    const f32 er = glm::dot(e, r);

    // Point on the segment closest to the ray point at t, and the ray point closest to the segment point s.
    const auto segmentFor = [&](f32 t) {
        return ee > 0.0f ? std::clamp((t * de + er) / ee, 0.0f, 1.0f) : 0.0f;
    };
    const auto rayFor = [&](f32 s) {
        return dd > 0.0f ? std::max((s * de - dr) / dd, 0.0f) : 0.0f;
    };

    f32 t = 0.0f;
    f32 s = 0.0f;
    const f32 denominator = dd * ee - de * de;
    if (denominator > kParallelEpsilon * dd * ee)
    {
        s = std::clamp((dd * er - de * dr) / denominator, 0.0f, 1.0f);
        t = rayFor(s);
        s = segmentFor(t);
    }
    else
    {
        // Parallel: the ray point nearest to the segment's start.
        t = rayFor(0.0f);
        s = segmentFor(t);
    }
    const Vec3 onRay = ray.at(t);
    const Vec3 onSegment = start + e * s;
    return RaySegmentClosest{t, s, glm::length(onRay - onSegment)};
}

Ray screenToRay(Vec2 pixel, Vec2 viewportSize, const Mat4& viewProjection) noexcept
{
    const Vec2 ndc(2.0f * pixel.x / viewportSize.x - 1.0f, 1.0f - 2.0f * pixel.y / viewportSize.y);
    const Mat4 inverse = glm::inverse(viewProjection);
    // Depth 1 is the near plane; 0.5 is a finite point further along the ray for both perspective (twice the
    // near distance) and orthographic projections (half way to the far plane), where depth 0 can be infinite.
    const Vec4 nearPoint = inverse * Vec4(ndc, 1.0f, 1.0f);
    const Vec4 farPoint = inverse * Vec4(ndc, 0.5f, 1.0f);
    const Vec3 origin = Vec3(nearPoint) / nearPoint.w;
    const Vec3 further = Vec3(farPoint) / farPoint.w;
    return Ray{origin, glm::normalize(further - origin)};
}

} // namespace illusion
