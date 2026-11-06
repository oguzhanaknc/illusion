#pragma once

/// @file
/// EditorPhysicsQueries: the PhysicsQueryHost of `transform.drop_to_ground` with `"onto": "colliders"`. Each
/// query fills a scratch PhysicsWorld with static bodies from the Colliders of the edit World and a ground
/// plane at y = 0, then sweeps the falling entities' Colliders (or casts a ray) straight down. ConvexHull
/// and Mesh Colliders made from mesh assets are loaded and cooked during the query, and their geometry is
/// kept for the next ones (a model file that changed is loaded again).
///
/// @code
/// illusion::JobSystem jobs;
/// illusion::editor::EditorPhysicsQueries physics(editor, jobs);
/// // transform.drop_to_ground {"entities": ["$selection"], "onto": "colliders"} now uses it
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/editor/core/PhysicsQueryHost.hpp>

#include <memory>

namespace illusion
{
class JobSystem;
} // namespace illusion

namespace illusion::editor
{

class EditorContext;

/// The editor's physics queries. Registers itself with EditorContext::setPhysicsQueries(); the destructor
/// unregisters. Use it on the editor thread; @p jobs must outlive it.
///
/// Triangle Mesh Colliders are surfaces but cannot be swept: a falling entity whose only Colliders are
/// triangle meshes falls like one without any.
class EditorPhysicsQueries final : public PhysicsQueryHost, NonCopyable
{
public:
    /// Registers with @p editor as its physics queries; the scratch simulations run their work on @p jobs.
    EditorPhysicsQueries(EditorContext& editor, JobSystem& jobs);

    /// Unregisters from the editor.
    ~EditorPhysicsQueries() override;

    /// @copydoc PhysicsQueryHost::dropDistances
    void dropDistances(World& world, WorldPoseFunction pose, std::span<const DropQuery> queries,
                       std::span<f32> out) override;

private:
    struct Impl;
    EditorContext& m_editor;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion::editor
