#pragma once

/// @file
/// Cooks the mesh assets of ConvexHull and Mesh Colliders (Collider::mesh) into PhysicsWorld geometry.
///
/// @code
/// illusion::PhysicsAssetResolver colliders(physics, streamer);
/// while (running)
/// {
///     streamer.update();
///     colliders.update(); // before physics.update()
///     physics.update(deltaSeconds);
/// }
/// @endcode

#include <illusion/core/Base.hpp>

#include <memory>

namespace illusion
{

class AssetStreamer;
class PhysicsWorld;
class World;

/// Counters of a PhysicsAssetResolver.
struct PhysicsAssetStatistics
{
    u32 geometries = 0; ///< Shapes cooked (one per mesh asset, mesh index and shape kind).
    u32 pending = 0;    ///< Colliders waiting for their model after the last update().
    u32 failures = 0;   ///< Mesh assets that could not be cooked, since construction.
};

/// Keeps Collider::geometry of the ConvexHull and Mesh Colliders of a PhysicsWorld's World in step with their
/// Collider::mesh assets. Models load through an AssetStreamer; each mesh is cooked once per shape kind (a
/// triangle mesh of all its submeshes, or the convex hull of its vertices) and shared by every Collider that
/// uses it. A Collider whose geometry changes is rebuilt. Entities with the Inactive tag are skipped. Use on
/// the thread that updates the PhysicsWorld.
class PhysicsAssetResolver : NonCopyable
{
public:
    /// @p physics and @p streamer must outlive the resolver.
    PhysicsAssetResolver(PhysicsWorld& physics, AssetStreamer& streamer);

    /// Destroys the cooked geometry (bodies keep theirs until they are rebuilt or removed).
    ~PhysicsAssetResolver();

    /// Cooks finished models and updates the Colliders whose asset changed. Call before
    /// PhysicsWorld::update().
    void update();

    /// Calls update() and waits for the streamer until no Collider waits for its model.
    void waitIdle();

    /// Counters.
    [[nodiscard]] const PhysicsAssetStatistics& statistics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion
