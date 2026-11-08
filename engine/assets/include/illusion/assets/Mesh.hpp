#pragma once

/// @file
/// CPU-side meshes: vertex format, submeshes, bounds and procedural shapes.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace illusion
{

/// Vertex format used by all meshes (48 bytes).
struct Vertex
{
    Vec3 position{0.0f};           ///< Object-space position.
    Vec3 normal{0.0f, 1.0f, 0.0f}; ///< Unit normal.
    /// Tangent (xyz) and bitangent sign (w). w = 0 means "no tangent": shaders then derive the tangent
    /// frame from screen-space derivatives.
    Vec4 tangent{0.0f};
    Vec2 uv{0.0f}; ///< Texture coordinates (origin top-left).
};
static_assert(sizeof(Vertex) == 48, "Vertex layout must match the shaders");

/// Most levels of detail a submesh carries besides its full detail (level 0).
inline constexpr u32 kMaxSubmeshLods = 3;

/// A simplified version of a submesh: a range of indices into the same vertices (see generateLods()).
struct SubmeshLod
{
    u32 indexOffset = 0; ///< First index.
    u32 indexCount = 0;  ///< Number of indices (a multiple of 3, greater than 0).
    /// Largest distance between this level's surface and the full-detail surface, in object-space units.
    /// The renderer projects it to pixels to choose a level; levels have non-decreasing errors.
    f32 error = 0.0f;
};

/// A range of indices drawn with one material, with optional coarser levels of detail.
struct Submesh
{
    u32 indexOffset = 0; ///< First index.
    u32 indexCount = 0;  ///< Number of indices (a multiple of 3).
    i32 material = -1;   ///< Material index in the owning model, or -1 for the default material.
    Aabb bounds;         ///< Bounds of this submesh.
    u32 lodCount = 0;    ///< Number of coarser levels in lods (at most kMaxSubmeshLods).
    /// Coarser levels, from finer to coarser: lods[0] is level 1. Only the first lodCount are used.
    std::array<SubmeshLod, kMaxSubmeshLods> lods{};

    /// The index range of level @p level (0: full detail; clamped to the coarsest level).
    [[nodiscard]] SubmeshLod level(u32 level) const noexcept
    {
        if (level == 0 || lodCount == 0)
        {
            return SubmeshLod{indexOffset, indexCount, 0.0f};
        }
        return lods[(level < lodCount ? level : lodCount) - 1];
    }
};

/// Triangle mesh with one vertex and index buffer shared by its submeshes.
struct MeshData
{
    std::vector<Vertex> vertices;   ///< Vertices.
    std::vector<u32> indices;       ///< Triangle list indices.
    std::vector<Submesh> submeshes; ///< At least one submesh for a valid mesh.
    Aabb bounds;                    ///< Bounds of all vertices.

    /// Recomputes bounds of the mesh and of every submesh from the vertices.
    void computeBounds();
};

/// How generateLods() simplifies submeshes.
struct MeshLodSettings
{
    u32 levels = kMaxSubmeshLods; ///< Coarser levels to make per submesh (0 to kMaxSubmeshLods).
    f32 reduction = 0.5f;         ///< Each level aims for this fraction of the previous level's triangles.
    /// Largest error a level may have, as a fraction of the submesh's size; simplification stops there.
    f32 maxError = 0.05f;
    /// Submeshes with fewer triangles get no levels.
    u32 minTriangles = 64;
};

/// Adds coarser levels of detail to every submesh of @p mesh (replacing any it had): each level is a
/// simplified copy of the full-detail triangles that keeps the vertices (new indices are appended to
/// MeshData::indices), keeps the borders between materials closed and accounts for normals, and records its
/// object-space error. A level that removes less than a tenth of the previous level's triangles, or would
/// exceed MeshLodSettings::maxError, is not made, so meshes that cannot be simplified get fewer levels or
/// none. Deterministic: the same mesh and settings always give the same indices.
void generateLods(MeshData& mesh, const MeshLodSettings& settings = {});

/// The coarsest level of @p submesh (0 to Submesh::lodCount) whose error stays at or below @p maxPixelError
/// pixels when one object-space unit covers @p pixelsPerUnit pixels. Returns 0 (full detail) when
/// @p maxPixelError is 0 or less, or when the submesh has no levels.
[[nodiscard]] u32 selectLod(const Submesh& submesh, f32 pixelsPerUnit, f32 maxPixelError) noexcept;

/// The full-detail part of MeshData::indices: every index before the first level of detail (generateLods()
/// appends the levels after the full-detail triangles), or all indices when the mesh has no levels. Use it
/// for collision, picking and anything else that needs the real surface once.
[[nodiscard]] std::span<const u32> fullDetailIndices(const MeshData& mesh) noexcept;

/// Removes the levels of detail of every submesh and the indices only they used (when every level sits
/// after the full-detail indices, as generateLods() leaves them).
void clearLods(MeshData& mesh);

/// Computes flat-shaded normals for meshes that have none (each triangle's face normal, accumulated).
void computeNormals(MeshData& mesh);

/// Unit cube centered at the origin with per-face normals and UVs.
[[nodiscard]] MeshData makeCube(f32 size = 1.0f);

/// UV sphere with tangents.
[[nodiscard]] MeshData makeSphere(f32 radius = 0.5f, u32 segments = 32, u32 rings = 16);

/// Plane in the XZ plane facing +y.
[[nodiscard]] MeshData makePlane(f32 size = 1.0f, u32 subdivisions = 1);

/// Capsule along the y axis: a cylinder of half height @p halfHeight capped by hemispheres of @p radius
/// (total height 2 * (halfHeight + radius), like a Capsule Collider). @p rings counts both hemispheres.
[[nodiscard]] MeshData makeCapsule(f32 radius = 0.5f, f32 halfHeight = 0.5f, u32 segments = 32,
                                   u32 rings = 16);

/// Closed cylinder along the y axis from -@p halfHeight to +@p halfHeight (like a Cylinder Collider).
[[nodiscard]] MeshData makeCylinder(f32 radius = 0.5f, f32 halfHeight = 0.5f, u32 segments = 32);

/// The mesh of the built-in asset @p path (`builtin:/mesh/cube`, `sphere`, `plane`, `capsule` or `cylinder`,
/// made with the default arguments of makeCube() and the others), or std::nullopt for any other path.
[[nodiscard]] std::optional<MeshData> makeBuiltinMesh(std::string_view path);

} // namespace illusion
