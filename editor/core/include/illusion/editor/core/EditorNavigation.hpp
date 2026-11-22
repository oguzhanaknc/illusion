#pragma once

/// @file
/// The navigation of the edit World (ADR 0062): a NavWorld that loads or bakes the navmeshes of the edited
/// scene and answers the `nav.*` commands, never advanced (its agents do not move). EditorContext creates it
/// on first use.

#include <illusion/core/Base.hpp>
#include <illusion/nav/NavWorld.hpp>

#include <memory>
#include <string_view>

namespace illusion
{
struct AssetRef;
class VirtualFileSystem;
class JobSystem;
class World;
struct MeshData;
} // namespace illusion

namespace illusion::editor
{

/// The NavWorld of the edit World with the Job System and asset streamer it runs on. Use it on the editor
/// thread.
class EditorNavigation : NonCopyable
{
public:
    /// Navigation of @p world; @p files and @p world must outlive it.
    EditorNavigation(World& world, VirtualFileSystem& files);
    ~EditorNavigation();

    /// The NavWorld.
    [[nodiscard]] NavWorld& world() noexcept;
    [[nodiscard]] const NavWorld& world() const noexcept;

    /// The Job System that bakes run on.
    [[nodiscard]] JobSystem& jobs() noexcept;

    /// Advances loads and bakes of the edit World's navmeshes without moving agents; once per frame.
    void update();

    /// Updates until no navmesh waits for its data or tiles.
    void waitIdle();

    /// Finishes the loads that finished since the last call (models of Mesh Colliders).
    void pump();

    /// The mesh of a ConvexHull or Mesh Collider for collectNavGeometry(), requested on first use; null while
    /// it loads (setting @p waits) or if it cannot be read.
    [[nodiscard]] const MeshData* findMesh(const AssetRef& mesh, u32 meshIndex, bool& waits);

    /// Loads the `.navmesh` file at @p path again, if a surface uses it (it was just written).
    void reload(std::string_view path);

    /// Forgets the meshes of Colliders read so far, so that the next bake reads them again.
    void forgetMeshes();

    /// Draws the debug view into the viewports (`nav.debug`).
    bool debugEnabled = false;
    /// What the debug view draws.
    NavDebugOptions debugOptions;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion::editor
