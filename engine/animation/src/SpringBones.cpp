#include "PoseDetail.hpp"

#include <illusion/animation/SpringBones.hpp>

#include <algorithm>
#include <cmath>

namespace illusion
{
namespace
{

constexpr f32 kEpsilon = 1e-6f;

[[nodiscard]] Mat4 poseMatrix(const JointPose& pose) noexcept
{
    Mat4 matrix = glm::mat4_cast(pose.rotation);
    matrix[0] *= pose.scale.x;
    matrix[1] *= pose.scale.y;
    matrix[2] *= pose.scale.z;
    matrix[3] = Vec4(pose.translation, 1.0f);
    return matrix;
}

// The rotation of @p matrix with its scale removed.
[[nodiscard]] Quat unitRotation(const Mat4& matrix) noexcept
{
    Mat3 basis(matrix);
    for (glm::length_t c = 0; c < 3; ++c)
    {
        const f32 length = glm::length(basis[c]);
        if (!(length > kEpsilon))
        {
            return Quat{1.0f, 0.0f, 0.0f, 0.0f};
        }
        basis[c] /= length;
    }
    return glm::normalize(glm::quat_cast(basis));
}

[[nodiscard]] bool finite(Vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

// The entry of @p chain holding the parent of entry @p k, or -1 when the parent is not in the chain.
[[nodiscard]] i32 parentEntry(const Skeleton& skeleton, std::span<const u32> chain, usize k) noexcept
{
    const i32 parent = skeleton.joints[chain[k]].parent;
    for (usize p = k; p-- > 0;)
    {
        if (static_cast<i32>(chain[p]) == parent)
        {
            return static_cast<i32>(p);
        }
    }
    return -1;
}

// The world matrix of the parent of @p joint (the skeleton's root parent for a root).
[[nodiscard]] Mat4 parentWorld(const Skeleton& skeleton, const Mat4& world, std::span<const JointPose> local,
                               u32 joint) noexcept
{
    const i32 parent = skeleton.joints[joint].parent;
    return world * (parent >= 0 ? detail::jointModelMatrix(skeleton, local, static_cast<u32>(parent))
                                : skeleton.rootTransform);
}

// Writes the animated world matrix of every entry into its particle.
void animatedMatrices(const Skeleton& skeleton, std::span<const u32> chain, const Mat4& world,
                      std::span<const JointPose> local, std::span<SpringParticle> particles) noexcept
{
    for (usize k = 0; k < chain.size(); ++k)
    {
        const i32 parent = k == 0 ? -1 : parentEntry(skeleton, chain, k);
        particles[k].matrix =
            parent >= 0 ? particles[static_cast<usize>(parent)].matrix * poseMatrix(local[chain[k]])
                        : parentWorld(skeleton, world, local, chain[k]) * poseMatrix(local[chain[k]]);
    }
}

} // namespace

void resetSpringChain(const Skeleton& skeleton, std::span<const u32> chain, const Mat4& world,
                      std::span<const JointPose> local, std::span<SpringParticle> particles) noexcept
{
    if (chain.empty() || particles.size() < chain.size())
    {
        return;
    }
    animatedMatrices(skeleton, chain, world, local, particles);
    for (usize k = 0; k < chain.size(); ++k)
    {
        particles[k].position = particles[k].previous = Vec3(particles[k].matrix[3]);
    }
}

void stepSpringChain(const Skeleton& skeleton, std::span<const u32> chain, const SpringSolve& solve,
                     std::span<const SpringSphere> spheres, u32 steps, std::span<SpringParticle> particles,
                     std::span<JointPose> local) noexcept
{
    if (chain.empty() || particles.size() < chain.size())
    {
        return;
    }
    const f32 stiffness = std::isfinite(solve.stiffness) ? std::clamp(solve.stiffness, 0.0f, 1.0f) : 0.0f;
    const f32 keep = std::isfinite(solve.damping) ? 1.0f - std::clamp(solve.damping, 0.0f, 1.0f) : 1.0f;
    const f32 radius = std::isfinite(solve.radius) ? std::max(solve.radius, 0.0f) : 0.0f;
    const f32 step = std::isfinite(solve.stepSeconds) ? std::max(solve.stepSeconds, 0.0f) : 0.0f;
    const Vec3 fall = finite(solve.gravity) ? solve.gravity * (step * step) : Vec3(0.0f);

    animatedMatrices(skeleton, chain, solve.world, local, particles);
    for (u32 s = 0; s < steps; ++s)
    {
        particles[0].position = particles[0].previous = Vec3(particles[0].matrix[3]);
        for (usize k = 1; k < chain.size(); ++k)
        {
            SpringParticle& particle = particles[k];
            const Vec3 animated(particle.matrix[3]);
            const i32 parent = parentEntry(skeleton, chain, k);
            if (parent < 0)
            {
                particle.position = particle.previous = animated;
                continue;
            }
            const SpringParticle& anchor = particles[static_cast<usize>(parent)];
            const Vec3 bone = animated - Vec3(anchor.matrix[3]);
            const Vec3 target = anchor.position + bone;
            Vec3 next = particle.position + (particle.position - particle.previous) * keep + fall;
            next += (target - next) * stiffness;
            const Vec3 reach = next - anchor.position;
            const f32 length = glm::length(reach);
            next = length > kEpsilon ? anchor.position + reach * (glm::length(bone) / length) : target;
            for (const SpringSphere& sphere : spheres)
            {
                const Vec3 away = next - sphere.center;
                const f32 distance = glm::length(away);
                const f32 limit = sphere.radius + radius;
                if (distance < limit)
                {
                    next = distance > kEpsilon ? sphere.center + away * (limit / distance)
                                               : sphere.center + Vec3(0.0f, limit, 0.0f);
                }
            }
            if (!finite(next))
            {
                next = target;
            }
            particle.previous = particle.position;
            particle.position = next;
        }
    }

    // Turns each joint, parents first, so that its first child in the chain points at that child's particle.
    // The particles' matrices become the joints' new world matrices on the way.
    for (usize k = 0; k < chain.size(); ++k)
    {
        const i32 parent = k == 0 ? -1 : parentEntry(skeleton, chain, k);
        const Mat4 above = parent >= 0 ? particles[static_cast<usize>(parent)].matrix
                                       : parentWorld(skeleton, solve.world, local, chain[k]);
        Mat4 current = above * poseMatrix(local[chain[k]]);
        usize child = k + 1;
        while (child < chain.size() && parentEntry(skeleton, chain, child) != static_cast<i32>(k))
        {
            ++child;
        }
        if (child < chain.size())
        {
            const Vec3 origin(current[3]);
            const Vec3 from = Vec3(current * Vec4(local[chain[child]].translation, 1.0f)) - origin;
            const Vec3 to = particles[child].position - origin;
            const f32 fromLength = glm::length(from);
            const f32 toLength = glm::length(to);
            if (fromLength > kEpsilon && toLength > kEpsilon && std::isfinite(fromLength + toLength))
            {
                const Quat turn =
                    detail::shortestArc(from / fromLength, to / toLength, Vec3(0.0f, 0.0f, 1.0f));
                const Quat rotation = unitRotation(above);
                JointPose& pose = local[chain[k]];
                pose.rotation = glm::normalize(glm::inverse(rotation) * turn * rotation * pose.rotation);
                current = above * poseMatrix(pose);
            }
        }
        particles[k].matrix = current;
    }
}

} // namespace illusion
