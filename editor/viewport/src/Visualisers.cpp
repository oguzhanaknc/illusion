#include <illusion/audio/AudioComponents.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/editor/viewport/Visualisers.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/render/DebugDraw.hpp>
#include <illusion/render/SceneComponents.hpp>

#include <array>
#include <cmath>

namespace illusion::editor::viewport
{
namespace
{

// Segments of the circles of spheres, capsules, cylinders and discs.
constexpr u32 kSegments = 16;
constexpr f32 kTwoPi = 6.28318531f;
// Arcs of joint limits use at most this many segments per turn.
constexpr u32 kArcSegments = 32;
// Sizes of the markers, in world units.
constexpr f32 kAnchorSize = 0.1f;
constexpr f32 kAxisLength = 0.5f;
constexpr f32 kLimitRadius = 0.4f;
constexpr f32 kTickSize = 0.08f;
constexpr f32 kSunDisc = 0.5f;
constexpr f32 kSunRay = 2.0f;

// cos and sin of kSegments + 1 points around the unit circle (the last repeats the first).
const std::array<Vec2, kSegments + 1>& unitCircle() noexcept
{
    static const std::array<Vec2, kSegments + 1> points = [] {
        std::array<Vec2, kSegments + 1> out{};
        for (u32 i = 0; i <= kSegments; ++i)
        {
            const f32 angle = kTwoPi * static_cast<f32>(i) / static_cast<f32>(kSegments);
            out[i] = Vec2(std::cos(angle), std::sin(angle));
        }
        return out;
    }();
    return points;
}

bool bitSet(std::span<const u64> bits, u32 index) noexcept
{
    const usize word = index / 64;
    return word < bits.size() && (bits[word] >> (index % 64) & 1u) != 0;
}

Vec3 transformPoint(const Mat4& matrix, Vec3 point) noexcept
{
    return Vec3(matrix * Vec4(point, 1.0f));
}

// A unit vector perpendicular to the unit vector @p axis.
Vec3 perpendicular(Vec3 axis) noexcept
{
    const Vec3 other = std::abs(axis.y) < 0.9f ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
    return glm::normalize(glm::cross(axis, other));
}

// Records lines into the DebugDraw until it is full.
struct Recorder
{
    DebugDraw& draw;
    f32 width = 1.5f;
    bool full = false;

    void line(Vec3 a, Vec3 b, Vec4 color) noexcept
    {
        if (full)
        {
            return;
        }
        const Vec3 d = b - a;
        // Skips degenerate and non-finite lines (the comparison is false for NaN).
        if (!(glm::dot(d, d) > 1e-12f) || !std::isfinite(a.x + a.y + a.z + b.x + b.y + b.z))
        {
            return;
        }
        if (!draw.line(a, b, color, width))
        {
            full = true;
        }
    }

    // A circle of @p radius around @p center in the plane of @p u and @p v, all in the space of @p matrix.
    void circle(const Mat4& matrix, Vec3 center, Vec3 u, Vec3 v, f32 radius, Vec4 color) noexcept
    {
        points(matrix, center, u, v, radius, 0, kSegments, color);
    }

    // Half of a circle: the points from @p first to @p first + kSegments / 2 of unitCircle().
    void halfCircle(const Mat4& matrix, Vec3 center, Vec3 u, Vec3 v, f32 radius, u32 first,
                    Vec4 color) noexcept
    {
        points(matrix, center, u, v, radius, first, first + kSegments / 2, color);
    }

    // Lines between the points @p first to @p last of unitCircle() scaled by @p radius in the plane of @p u
    // and
    // @p v around @p center. The matrix is affine, so the centre and the axes are transformed once.
    void points(const Mat4& matrix, Vec3 center, Vec3 u, Vec3 v, f32 radius, u32 first, u32 last,
                Vec4 color) noexcept
    {
        const std::array<Vec2, kSegments + 1>& unit = unitCircle();
        const Mat3 linear(matrix);
        const Vec3 origin = transformPoint(matrix, center);
        const Vec3 worldU = linear * (u * radius);
        const Vec3 worldV = linear * (v * radius);
        Vec3 previous = origin + worldU * unit[first].x + worldV * unit[first].y;
        for (u32 i = first + 1; i <= last; ++i)
        {
            const Vec3 next = origin + worldU * unit[i].x + worldV * unit[i].y;
            line(previous, next, color);
            previous = next;
        }
    }

    // An arc in world space from angle @p from to @p to (radians) around @p center.
    void arc(Vec3 center, Vec3 u, Vec3 v, f32 radius, f32 from, f32 to, Vec4 color) noexcept
    {
        const f32 span = std::min(std::abs(to - from), kTwoPi);
        const u32 segments = std::max(1u, static_cast<u32>(std::ceil(span / kTwoPi * kArcSegments)));
        const f32 step = (to - from) / static_cast<f32>(segments);
        Vec3 previous = center + (u * std::cos(from) + v * std::sin(from)) * radius;
        for (u32 i = 1; i <= segments; ++i)
        {
            const f32 angle = from + step * static_cast<f32>(i);
            const Vec3 next = center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
            line(previous, next, color);
            previous = next;
        }
    }

    void sphere(const Mat4& matrix, f32 radius, Vec4 color) noexcept
    {
        const Vec3 x(1.0f, 0.0f, 0.0f);
        const Vec3 y(0.0f, 1.0f, 0.0f);
        const Vec3 z(0.0f, 0.0f, 1.0f);
        circle(matrix, Vec3(0.0f), x, y, radius, color);
        circle(matrix, Vec3(0.0f), y, z, radius, color);
        circle(matrix, Vec3(0.0f), z, x, radius, color);
    }

    void box(const Mat4& matrix, Vec3 half, Vec4 color) noexcept
    {
        std::array<Vec3, 8> corners{};
        for (u32 i = 0; i < 8; ++i)
        {
            const Vec3 corner((i & 1u) != 0 ? half.x : -half.x, (i & 2u) != 0 ? half.y : -half.y,
                              (i & 4u) != 0 ? half.z : -half.z);
            corners[i] = transformPoint(matrix, corner);
        }
        for (u32 i = 0; i < 8; ++i)
        {
            for (const u32 bit : {1u, 2u, 4u})
            {
                if ((i & bit) == 0)
                {
                    line(corners[i], corners[i | bit], color);
                }
            }
        }
    }

    // A capsule along the local y axis: the straight part is 2 * @p halfHeight long.
    void capsule(const Mat4& matrix, f32 radius, f32 halfHeight, Vec4 color) noexcept
    {
        const Vec3 x(1.0f, 0.0f, 0.0f);
        const Vec3 y(0.0f, 1.0f, 0.0f);
        const Vec3 z(0.0f, 0.0f, 1.0f);
        const Vec3 top(0.0f, halfHeight, 0.0f);
        circle(matrix, top, x, z, radius, color);
        circle(matrix, -top, x, z, radius, color);
        for (const Vec3 side : {x, -x, z, -z})
        {
            line(transformPoint(matrix, top + side * radius), transformPoint(matrix, -top + side * radius),
                 color);
        }
        // The caps: upper halves above the top circle, lower halves below the bottom one.
        halfCircle(matrix, top, x, y, radius, 0, color);
        halfCircle(matrix, top, z, y, radius, 0, color);
        halfCircle(matrix, -top, x, y, radius, kSegments / 2, color);
        halfCircle(matrix, -top, z, y, radius, kSegments / 2, color);
    }

    void cylinder(const Mat4& matrix, f32 radius, f32 halfHeight, Vec4 color) noexcept
    {
        const Vec3 x(1.0f, 0.0f, 0.0f);
        const Vec3 z(0.0f, 0.0f, 1.0f);
        const Vec3 top(0.0f, halfHeight, 0.0f);
        circle(matrix, top, x, z, radius, color);
        circle(matrix, -top, x, z, radius, color);
        for (const Vec3 side : {x, -x, z, -z})
        {
            line(transformPoint(matrix, top + side * radius), transformPoint(matrix, -top + side * radius),
                 color);
        }
    }

    void cross(Vec3 center, f32 size, Vec4 color) noexcept
    {
        line(center - Vec3(size, 0.0f, 0.0f), center + Vec3(size, 0.0f, 0.0f), color);
        line(center - Vec3(0.0f, size, 0.0f), center + Vec3(0.0f, size, 0.0f), color);
        line(center - Vec3(0.0f, 0.0f, size), center + Vec3(0.0f, 0.0f, size), color);
    }

    // A tick across the line along @p direction at @p point.
    void tick(Vec3 point, Vec3 direction, Vec4 color) noexcept
    {
        const Vec3 side = perpendicular(direction) * kTickSize;
        line(point - side, point + side, color);
    }

    // A cone from @p apex along the unit @p forward with half angle @p half, @p length long.
    void cone(Vec3 apex, Vec3 forward, Vec3 u, Vec3 v, f32 half, f32 length, Vec4 color) noexcept
    {
        const Vec3 center = apex + forward * (length * std::cos(half));
        const f32 radius = length * std::sin(half);
        circle(Mat4(1.0f), center, u, v, radius, color);
        for (const Vec3 side : {u, -u, v, -v})
        {
            line(apex, center + side * radius, color);
        }
    }
};

Mat4 rigidMatrix(const Transform& transform) noexcept
{
    Mat4 matrix = glm::mat4_cast(transform.rotation);
    matrix[3] = Vec4(transform.position, 1.0f);
    return matrix;
}

void recordCamera(Recorder& out, const Transform& transform, const Camera& camera,
                  const VisualiserOptions& options) noexcept
{
    const Mat4 matrix = rigidMatrix(transform);
    const Vec4 color = options.colors.camera;
    const f32 aspect = options.cameraAspect > 0.0f ? options.cameraAspect : 16.0f / 9.0f;
    const f32 nearPlane = std::max(camera.nearPlane, 1e-4f);
    const bool orthographic = camera.projection == CameraProjection::Orthographic;
    const f32 farPlane = orthographic
                             ? std::max(std::min(camera.farPlane, options.frustumLength), nearPlane * 1.5f)
                             : std::max(options.frustumLength, nearPlane * 1.5f);
    const auto rectangle = [&](f32 depth, std::array<Vec3, 4>& corners) {
        const f32 halfHeight =
            orthographic ? camera.orthoHeight * 0.5f : depth * std::tan(camera.fovY * 0.5f);
        const f32 halfWidth = halfHeight * aspect;
        corners = {transformPoint(matrix, Vec3(-halfWidth, -halfHeight, -depth)),
                   transformPoint(matrix, Vec3(halfWidth, -halfHeight, -depth)),
                   transformPoint(matrix, Vec3(halfWidth, halfHeight, -depth)),
                   transformPoint(matrix, Vec3(-halfWidth, halfHeight, -depth))};
        for (u32 i = 0; i < 4; ++i)
        {
            out.line(corners[i], corners[(i + 1) % 4], color);
        }
        return halfHeight;
    };
    std::array<Vec3, 4> nearCorners{};
    std::array<Vec3, 4> farCorners{};
    (void)rectangle(nearPlane, nearCorners);
    const f32 farHalfHeight = rectangle(farPlane, farCorners);
    for (u32 i = 0; i < 4; ++i)
    {
        out.line(nearCorners[i], farCorners[i], color);
    }
    // A triangle above the far rectangle shows which way is up.
    const Vec3 left = farCorners[3];
    const Vec3 right = farCorners[2];
    const Vec3 apex = transformPoint(matrix, Vec3(0.0f, farHalfHeight * 1.4f, -farPlane));
    out.line(left + (right - left) * 0.3f, apex, color);
    out.line(apex, left + (right - left) * 0.7f, color);
    if (!orthographic)
    {
        const Vec3 eye = transform.position;
        for (const Vec3& corner : nearCorners)
        {
            out.line(eye, corner, color);
        }
    }
}

void recordDirectionalLight(Recorder& out, const Transform& transform, Vec4 color) noexcept
{
    const Mat4 matrix = rigidMatrix(transform);
    const Vec3 x(1.0f, 0.0f, 0.0f);
    const Vec3 y(0.0f, 1.0f, 0.0f);
    out.circle(matrix, Vec3(0.0f), x, y, kSunDisc, color);
    const std::array<Vec2, kSegments + 1>& points = unitCircle();
    for (u32 i = 0; i < kSegments; i += kSegments / 4)
    {
        const Vec3 start(points[i].x * kSunDisc, points[i].y * kSunDisc, 0.0f);
        out.line(transformPoint(matrix, start), transformPoint(matrix, start + Vec3(0.0f, 0.0f, -kSunRay)),
                 color);
    }
    // The centre ray ends in an arrowhead.
    const Vec3 tip(0.0f, 0.0f, -kSunRay * 1.25f);
    out.line(transform.position, transformPoint(matrix, tip), color);
    out.line(transformPoint(matrix, tip), transformPoint(matrix, tip + Vec3(0.15f, 0.0f, 0.25f)), color);
    out.line(transformPoint(matrix, tip), transformPoint(matrix, tip + Vec3(-0.15f, 0.0f, 0.25f)), color);
}

void recordCollider(Recorder& out, const Transform& transform, const Collider& collider, Vec4 color) noexcept
{
    const Mat4 matrix =
        transform.matrix() * glm::translate(Mat4(1.0f), collider.offset) * glm::mat4_cast(collider.rotation);
    switch (collider.shape)
    {
    case ColliderShape::Box:
        out.box(matrix, collider.halfExtents, color);
        break;
    case ColliderShape::Sphere:
        out.sphere(matrix, collider.radius, color);
        break;
    case ColliderShape::Capsule:
        out.capsule(matrix, collider.radius, collider.halfHeight, color);
        break;
    case ColliderShape::Cylinder:
        out.cylinder(matrix, collider.radius, collider.halfHeight, color);
        break;
    case ColliderShape::ConvexHull:
    case ColliderShape::Mesh:
        // The geometry lives in the PhysicsWorld: mark the shape's origin.
        out.cross(transformPoint(matrix, Vec3(0.0f)), kAnchorSize * 2.0f, color);
        break;
    }
}

void recordJoint(Recorder& out, World& world, const Transform& transform, const Joint& joint,
                 const VisualiserColors& colors) noexcept
{
    const Vec3 anchor = transformPoint(transform.matrix(), joint.anchor);
    out.cross(anchor, kAnchorSize, colors.joint);
    const Transform* other =
        world.isAlive(joint.connected) ? world.tryGet<Transform>(joint.connected) : nullptr;
    Vec3 end = other != nullptr ? other->position : anchor;
    if (joint.type == JointType::Distance)
    {
        end =
            other != nullptr ? transformPoint(other->matrix(), joint.connectedAnchor) : joint.connectedAnchor;
        out.cross(end, kAnchorSize, colors.joint);
    }
    out.line(anchor, end, colors.joint);
    const f32 axisLength = glm::length(joint.axis);
    const Vec3 axis = axisLength > 1e-6f ? glm::normalize(transform.rotation * (joint.axis / axisLength))
                                         : Vec3(1.0f, 0.0f, 0.0f);
    switch (joint.type)
    {
    case JointType::Hinge:
    {
        out.line(anchor - axis * kAxisLength, anchor + axis * kAxisLength, colors.joint);
        if (joint.limited)
        {
            const Vec3 u = perpendicular(axis);
            const Vec3 v = glm::cross(axis, u);
            out.arc(anchor, u, v, kLimitRadius, joint.minLimit, joint.maxLimit, colors.jointLimit);
            out.line(anchor,
                     anchor + (u * std::cos(joint.minLimit) + v * std::sin(joint.minLimit)) * kLimitRadius,
                     colors.jointLimit);
            out.line(anchor,
                     anchor + (u * std::cos(joint.maxLimit) + v * std::sin(joint.maxLimit)) * kLimitRadius,
                     colors.jointLimit);
        }
        break;
    }
    case JointType::Slider:
        out.line(anchor - axis * kAxisLength, anchor + axis * kAxisLength, colors.joint);
        if (joint.limited)
        {
            out.line(anchor + axis * joint.minLimit, anchor + axis * joint.maxLimit, colors.jointLimit);
            out.tick(anchor + axis * joint.minLimit, axis, colors.jointLimit);
            out.tick(anchor + axis * joint.maxLimit, axis, colors.jointLimit);
        }
        break;
    case JointType::Distance:
    {
        const Vec3 between = end - anchor;
        const f32 length = glm::length(between);
        if (length > 1e-6f)
        {
            const Vec3 direction = between / length;
            for (const f32 limit : {joint.minLimit, joint.maxLimit})
            {
                if (limit > 0.0f)
                {
                    out.tick(anchor + direction * limit, direction, colors.jointLimit);
                }
            }
        }
        break;
    }
    case JointType::Fixed:
    case JointType::Point:
        break;
    }
}

void recordAudio(Recorder& out, const Transform& transform, const AudioSource& source,
                 const VisualiserColors& colors) noexcept
{
    const Mat4 at = glm::translate(Mat4(1.0f), transform.position);
    out.sphere(at, source.minDistance, colors.audioNear);
    out.sphere(at, source.maxDistance, colors.audioFar);
    if (source.coneInnerAngle >= kAudioFullCone && source.coneOuterAngle >= kAudioFullCone)
    {
        return;
    }
    const Vec3 forward = glm::normalize(transform.rotation * Vec3(0.0f, 0.0f, -1.0f));
    const Vec3 u = glm::normalize(transform.rotation * Vec3(1.0f, 0.0f, 0.0f));
    const Vec3 v = glm::normalize(transform.rotation * Vec3(0.0f, 1.0f, 0.0f));
    const f32 length = std::max(source.minDistance, 0.5f);
    if (source.coneInnerAngle < kAudioFullCone)
    {
        out.cone(transform.position, forward, u, v, source.coneInnerAngle * 0.5f, length, colors.audioNear);
    }
    if (source.coneOuterAngle < kAudioFullCone)
    {
        out.cone(transform.position, forward, u, v, source.coneOuterAngle * 0.5f, length, colors.audioFar);
    }
}

} // namespace

VisualiserStats recordVisualisers(World& world, DebugDraw& draw, const VisualiserOptions& options) noexcept
{
    ILL_PROFILE_SCOPE();
    Recorder out{draw, options.lineWidth > 0.0f ? options.lineWidth : 1.0f};
    VisualiserStats stats;
    const auto wanted = [&](Entity entity) {
        return !out.full && !bitSet(options.hidden, entity.index) &&
               (options.all || bitSet(options.selection, entity.index));
    };
    const VisualiserColors& colors = options.colors;
    const ViewportVisualisers& kinds = options.kinds;
    if (kinds.lights)
    {
        world.query<Transform, PointLight>().without<Inactive>().each(
            [&](Entity entity, const Transform& transform, const PointLight& light) {
                if (wanted(entity))
                {
                    out.sphere(glm::translate(Mat4(1.0f), transform.position), light.range, colors.light);
                    ++stats.objects;
                }
            });
        world.query<Transform, DirectionalLight>().without<Inactive>().each(
            [&](Entity entity, const Transform& transform, const DirectionalLight&) {
                if (wanted(entity))
                {
                    recordDirectionalLight(out, transform, colors.light);
                    ++stats.objects;
                }
            });
    }
    if (kinds.cameras)
    {
        world.query<Transform, Camera>().without<Inactive>().each(
            [&](Entity entity, const Transform& transform, const Camera& camera) {
                if (wanted(entity))
                {
                    recordCamera(out, transform, camera, options);
                    ++stats.objects;
                }
            });
    }
    if (kinds.colliders)
    {
        world.query<Transform, Collider>().without<Inactive>().each(
            [&](Entity entity, const Transform& transform, const Collider& collider) {
                if (wanted(entity))
                {
                    const RigidBody* body = world.tryGet<RigidBody>(entity);
                    recordCollider(out, transform, collider,
                                   body != nullptr && body->sensor ? colors.sensor : colors.collider);
                    ++stats.objects;
                }
            });
    }
    if (kinds.characters)
    {
        world.query<Transform, CharacterController>().without<Inactive>().each(
            [&](Entity entity, const Transform& transform, const CharacterController& character) {
                if (wanted(entity))
                {
                    const f32 halfHeight = std::max(character.height * 0.5f - character.radius, 0.0f);
                    const Vec3 center = transform.position + Vec3(0.0f, character.height * 0.5f, 0.0f);
                    out.capsule(glm::translate(Mat4(1.0f), center), character.radius, halfHeight,
                                colors.character);
                    ++stats.objects;
                }
            });
    }
    if (kinds.joints)
    {
        world.query<Transform, Joint>().without<Inactive>().each(
            [&](Entity entity, const Transform& transform, const Joint& joint) {
                if (wanted(entity))
                {
                    recordJoint(out, world, transform, joint, colors);
                    ++stats.objects;
                }
            });
    }
    if (kinds.audio)
    {
        world.query<Transform, AudioSource>().without<Inactive>().each(
            [&](Entity entity, const Transform& transform, const AudioSource& source) {
                if (wanted(entity) && source.spatial)
                {
                    recordAudio(out, transform, source, colors);
                    ++stats.objects;
                }
            });
    }
    stats.full = out.full;
    return stats;
}

} // namespace illusion::editor::viewport
