#pragma once

/// @file
/// CPU-side meshes: vertex format, submeshes, bounds and procedural shapes.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <optional>
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

/// A range of indices drawn with one material.
struct Submesh
{
    u32 indexOffset = 0; ///< First index.
    u32 indexCount = 0;  ///< Number of indices (a multiple of 3).
    i32 material = -1;   ///< Material index in the owning model, or -1 for the default material.
    Aabb bounds;         ///< Bounds of this submesh.
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
