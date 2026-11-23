#include <illusion/camera/CameraBody.hpp>
#include <illusion/camera/CameraDamping.hpp>

#include <algorithm>
#include <cmath>

namespace illusion
{

namespace
{

constexpr Vec3 kUp{0.0f, 1.0f, 0.0f};
constexpr Vec3 kRight{1.0f, 0.0f, 0.0f};

// std::clamp with bounds that may come in either order from authored data.
f32 clampBetween(f32 value, f32 a, f32 b) noexcept
{
    return std::max(std::min(a, b), std::min(value, std::max(a, b)));
}

f32 wrapPath(f32 at, f32 end) noexcept
{
    f32 wrapped = std::fmod(at, end);
    if (wrapped < 0.0f)
    {
        wrapped += end;
    }
    return wrapped >= end ? 0.0f : wrapped;
}

f32 limitPath(f32 at, f32 end, bool closed) noexcept
{
    if (!(end > 0.0f))
    {
        return 0.0f;
    }
    return closed ? wrapPath(at, end) : std::clamp(at, 0.0f, end);
}

struct PathSegment
{
    const CameraPathPoint* start;
    const CameraPathPoint* end;
    f32 t;
};

// Requires at least two points, or one on a closed path.
PathSegment locate(std::span<const CameraPathPoint> points, bool closed, f32 at) noexcept
{
    const usize count = points.size();
    const usize segments = closed ? count : count - 1;
    const f32 u = limitPath(at, static_cast<f32>(segments), closed);
    const usize index = std::min(static_cast<usize>(u), segments - 1);
    return {&points[index], &points[(index + 1) % count], u - static_cast<f32>(index)};
}

} // namespace

CameraPlacement stepCameraFollow(const CameraFollowSettings& settings, CameraFollowState& state,
                                 const CameraTarget& target, f32 dt) noexcept
{
    const bool local = settings.space == CameraFollowSpace::Target;
    const Quat frame = local ? glm::normalize(target.rotation) : Quat(1.0f, 0.0f, 0.0f, 0.0f);
    const Vec3 goal = target.position + frame * settings.offset;
    if (!state.initialised)
    {
        state.position = goal;
        state.velocity = Vec3(0.0f);
        state.rotation = frame;
        state.initialised = true;
        return {state.position, state.rotation};
    }
    // Damp the offset from the goal in the frame's axes, so that per-axis damping follows the target's turns.
    const Quat inverse = glm::conjugate(frame);
    Vec3 offset = inverse * (state.position - goal);
    Vec3 velocity = inverse * state.velocity;
    smoothDamp(offset, velocity, Vec3(0.0f), settings.damping, dt);
    state.position = goal + frame * offset;
    state.velocity = frame * velocity;
    state.rotation = dampRotation(state.rotation, frame, settings.rotationDamping, dt);
    return {state.position, state.rotation};
}

CameraPlacement stepCameraOrbit(const CameraOrbitSettings& settings, CameraOrbitState& state,
                                const CameraOrbitInput& input, f32 dt) noexcept
{
    const bool first = !state.initialised;
    if (first)
    {
        state.yaw = settings.yaw;
        state.pitch = settings.pitch;
        state.distance = settings.distance;
    }
    else
    {
        state.yaw += input.yaw;
        state.pitch += input.pitch;
        state.distance += input.zoom;
    }
    state.yaw = settings.limitYaw ? clampBetween(state.yaw, settings.minYaw, settings.maxYaw)
                                  : wrapCameraAngle(state.yaw);
    state.pitch = clampBetween(state.pitch, settings.minPitch, settings.maxPitch);
    state.distance = clampBetween(state.distance, settings.minDistance, settings.maxDistance);
    if (first)
    {
        state.smoothYaw = state.yaw;
        state.smoothPitch = state.pitch;
        state.yawVelocity = 0.0f;
        state.pitchVelocity = 0.0f;
        state.initialised = true;
    }
    else
    {
        const f32 yawGoal =
            settings.limitYaw ? state.yaw : state.smoothYaw + wrapCameraAngle(state.yaw - state.smoothYaw);
        smoothDamp(state.smoothYaw, state.yawVelocity, yawGoal, settings.damping, dt);
        if (!settings.limitYaw)
        {
            state.smoothYaw = wrapCameraAngle(state.smoothYaw);
        }
        smoothDamp(state.smoothPitch, state.pitchVelocity, state.pitch, settings.damping, dt);
    }
    const Quat rotation = glm::angleAxis(state.smoothYaw, kUp) * glm::angleAxis(state.smoothPitch, kRight);
    const Vec3 pivot = input.target.position + settings.pivotOffset;
    return {pivot + rotation * Vec3(0.0f, 0.0f, state.distance), rotation};
}

CameraPlacement stepCameraSpringArm(const CameraSpringArmSettings& settings, CameraSpringArmState& state,
                                    const CameraSpringArmInput& input, f32 dt) noexcept
{
    const bool first = !state.initialised;
    state.initialised = true;
    if (first)
    {
        state.side = settings.side;
        state.sideVelocity = 0.0f;
    }
    else
    {
        smoothDamp(state.side, state.sideVelocity, settings.side, settings.sideDamping, dt);
    }
    const Quat rotation = glm::normalize(input.rotation);
    const Vec3 start = input.target.position + Vec3(0.0f, settings.shoulderOffset.y, 0.0f);
    const Vec3 arm = rotation * Vec3(settings.shoulderOffset.x * state.side, 0.0f,
                                     settings.shoulderOffset.z + settings.armLength);
    const f32 full = glm::length(arm);
    if (!(full > 1e-6f))
    {
        state.length = 0.0f;
        return {start, rotation};
    }
    const Vec3 direction = arm / full;
    f32 goal = full;
    if (input.collision != nullptr)
    {
        const std::optional<f32> hit = input.collision->sphereCast(
            start, direction, full, std::max(settings.collisionRadius, 0.0f), settings.collisionLayers);
        if (hit)
        {
            goal = std::clamp(*hit, 0.0f, full);
        }
    }
    const f32 step = std::max(dt, 0.0f);
    if (first)
    {
        state.length = goal;
    }
    else if (goal < state.length)
    {
        state.length =
            settings.pullInSpeed > 0.0f ? std::max(goal, state.length - settings.pullInSpeed * step) : goal;
    }
    else
    {
        state.length = settings.recoverySpeed > 0.0f
                           ? std::min(goal, state.length + settings.recoverySpeed * step)
                           : goal;
    }
    return {start + direction * state.length, rotation};
}

f32 cameraPathEnd(std::span<const CameraPathPoint> points, bool closed) noexcept
{
    if (points.empty())
    {
        return 0.0f;
    }
    return static_cast<f32>(closed ? points.size() : points.size() - 1);
}

Vec3 cameraPathPosition(std::span<const CameraPathPoint> points, bool closed, f32 at) noexcept
{
    if (points.empty())
    {
        return Vec3(0.0f);
    }
    if (points.size() == 1 && !closed)
    {
        return points[0].position;
    }
    const PathSegment segment = locate(points, closed, at);
    const f32 t = segment.t;
    const f32 t2 = t * t;
    const f32 t3 = t2 * t;
    return (2.0f * t3 - 3.0f * t2 + 1.0f) * segment.start->position +
           (t3 - 2.0f * t2 + t) * segment.start->tangent + (-2.0f * t3 + 3.0f * t2) * segment.end->position +
           (t3 - t2) * segment.end->tangent;
}

Vec3 cameraPathTangent(std::span<const CameraPathPoint> points, bool closed, f32 at) noexcept
{
    if (points.empty())
    {
        return Vec3(0.0f);
    }
    if (points.size() == 1 && !closed)
    {
        return points[0].tangent;
    }
    const PathSegment segment = locate(points, closed, at);
    const f32 t = segment.t;
    const f32 t2 = t * t;
    return (6.0f * t2 - 6.0f * t) * segment.start->position +
           (3.0f * t2 - 4.0f * t + 1.0f) * segment.start->tangent +
           (-6.0f * t2 + 6.0f * t) * segment.end->position + (3.0f * t2 - 2.0f * t) * segment.end->tangent;
}

f32 nearestCameraPathPosition(std::span<const CameraPathPoint> points, bool closed, Vec3 point) noexcept
{
    const f32 end = cameraPathEnd(points, closed);
    if (!(end > 0.0f))
    {
        return 0.0f;
    }
    const auto distance = [&](f32 at) {
        const Vec3 delta = cameraPathPosition(points, closed, at) - point;
        return glm::dot(delta, delta);
    };
    // Coarse samples per segment, then a golden-section search around the best one.
    constexpr u32 kSamples = 8;
    constexpr f32 kStep = 1.0f / static_cast<f32>(kSamples);
    const u32 total = static_cast<u32>(end) * kSamples;
    f32 best = 0.0f;
    f32 bestDistance = distance(0.0f);
    for (u32 i = 1; i <= total; ++i)
    {
        const f32 at = static_cast<f32>(i) * kStep;
        const f32 d = distance(at);
        if (d < bestDistance)
        {
            best = at;
            bestDistance = d;
        }
    }
    f32 low = closed ? best - kStep : std::max(best - kStep, 0.0f);
    f32 high = closed ? best + kStep : std::min(best + kStep, end);
    constexpr f32 kRatio = 0.618034f;
    f32 a = high - kRatio * (high - low);
    f32 b = low + kRatio * (high - low);
    f32 da = distance(a);
    f32 db = distance(b);
    for (u32 i = 0; i < 24; ++i)
    {
        if (da < db)
        {
            high = b;
            b = a;
            db = da;
            a = high - kRatio * (high - low);
            da = distance(a);
        }
        else
        {
            low = a;
            a = b;
            da = db;
            b = low + kRatio * (high - low);
            db = distance(b);
        }
    }
    const f32 refined = 0.5f * (low + high);
    if (distance(refined) < bestDistance)
    {
        best = refined;
    }
    return limitPath(best, end, closed);
}

CameraPlacement stepCameraDolly(const CameraDollySettings& settings, CameraDollyState& state,
                                const CameraDollyInput& input, f32 dt) noexcept
{
    const Quat identity(1.0f, 0.0f, 0.0f, 0.0f);
    if (input.path.empty())
    {
        state.position = 0.0f;
        state.velocity = 0.0f;
        state.initialised = true;
        return {input.target.position, identity};
    }
    const bool closed = settings.closed;
    const f32 end = cameraPathEnd(input.path, closed);
    const f32 goal = limitPath(settings.mode == CameraDollyMode::Parameter
                                   ? settings.position
                                   : nearestCameraPathPosition(input.path, closed, input.target.position),
                               end, closed);
    if (!state.initialised)
    {
        state.position = goal;
        state.velocity = 0.0f;
        state.initialised = true;
    }
    else
    {
        f32 target = goal;
        if (closed && end > 0.0f)
        {
            const f32 delta = goal - state.position;
            target = state.position + delta - end * std::round(delta / end);
        }
        smoothDamp(state.position, state.velocity, target, settings.damping, dt);
        state.position = limitPath(state.position, end, closed);
    }
    const Vec3 position = cameraPathPosition(input.path, closed, state.position);
    Vec3 forward = cameraPathTangent(input.path, closed, state.position);
    if (!(glm::dot(forward, forward) > 1e-12f))
    {
        constexpr f32 kChord = 1e-3f;
        forward = cameraPathPosition(input.path, closed, state.position + kChord) -
                  cameraPathPosition(input.path, closed, state.position - kChord);
    }
    return {position, cameraLookRotation(forward, identity)};
}

} // namespace illusion
