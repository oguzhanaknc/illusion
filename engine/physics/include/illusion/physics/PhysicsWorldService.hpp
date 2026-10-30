#pragma once

/// @file
/// The host's implementation of PhysicsService over PhysicsWorld.
///
/// @code
/// illusion::PhysicsWorld physics(world, jobs);
/// illusion::PhysicsWorldService physicsService(physics);
/// illusion::ServiceSet services;
/// services.provide<illusion::PhysicsService>(physicsService);
/// modules.attachWorld(world, illusion::WorldRole::Simulation, std::move(services));
/// @endcode

#include <illusion/physics/PhysicsService.hpp>
#include <illusion/physics/PhysicsWorld.hpp>

#include <vector>

namespace illusion
{

/// PhysicsService that forwards every call to a PhysicsWorld, which must outlive it. Created by the host; a
/// shared module that constructs one fails to link, since only the host has the library's code (ADR 0041).
class PhysicsWorldService final : public PhysicsService
{
public:
    /// Serves @p physics.
    explicit PhysicsWorldService(PhysicsWorld& physics) noexcept
        : m_physics(physics)
    {
    }
    ~PhysicsWorldService() override;

    /// The PhysicsWorld it serves.
    [[nodiscard]] PhysicsWorld& physics() const noexcept { return m_physics; }

    /// @name PhysicsService
    /// @{
    void setGravity(const Vec3& gravity) override;
    [[nodiscard]] Vec3 gravity() const override;
    void setLayersCollide(u32 a, u32 b, bool collide) override;
    [[nodiscard]] bool layersCollide(u32 a, u32 b) const override;
    void rebuild(Entity entity) override;
    void setLinearVelocity(Entity entity, const Vec3& velocity) override;
    [[nodiscard]] Vec3 linearVelocity(Entity entity) const override;
    void setAngularVelocity(Entity entity, const Vec3& velocity) override;
    [[nodiscard]] Vec3 angularVelocity(Entity entity) const override;
    void addForce(Entity entity, const Vec3& force) override;
    void addTorque(Entity entity, const Vec3& torque) override;
    void addImpulse(Entity entity, const Vec3& impulse) override;
    [[nodiscard]] f32 mass(Entity entity) const override;
    [[nodiscard]] bool isAwake(Entity entity) const override;
    PhysicsMeshHandle createMesh(std::span<const Vec3> vertices, std::span<const u32> indices) override;
    PhysicsMeshHandle createConvexHull(std::span<const Vec3> points) override;
    void destroyMesh(PhysicsMeshHandle mesh) override;
    [[nodiscard]] bool isMeshValid(PhysicsMeshHandle mesh) const override;
    void setCharacterVelocity(Entity entity, const Vec3& velocity) override;
    [[nodiscard]] Vec3 characterVelocity(Entity entity) const override;
    [[nodiscard]] bool isCharacterGrounded(Entity entity) const override;
    [[nodiscard]] f32 jointPosition(Entity entity) const override;
    [[nodiscard]] std::span<const Entity> brokenJoints() const override;
    [[nodiscard]] std::optional<RaycastHit> raycast(const Vec3& origin, const Vec3& direction,
                                                    f32 maxDistance, u32 layerMask,
                                                    Entity ignore) const override;
    [[nodiscard]] std::optional<ShapeCastHit> shapeCast(const Collider& shape, const Vec3& position,
                                                        const Quat& rotation, const Vec3& direction,
                                                        f32 maxDistance, u32 layerMask,
                                                        Entity ignore) const override;
    [[nodiscard]] u32 overlap(const Collider& shape, const Vec3& position, const Quat& rotation,
                              std::span<Entity> out, u32 layerMask, Entity ignore) const override;
    [[nodiscard]] std::span<const ContactEvent> contactEvents() const override;
    /// @}

private:
    PhysicsWorld& m_physics;
    mutable std::vector<Entity> m_overlaps; // reused by overlap()
};

} // namespace illusion
