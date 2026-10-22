#include <illusion/assets/Mesh.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace illusion
{

void MeshData::computeBounds()
{
    bounds = Aabb{};
    for (const Vertex& vertex : vertices)
    {
        bounds.expand(vertex.position);
    }
    for (Submesh& submesh : submeshes)
    {
        submesh.bounds = Aabb{};
        for (u32 i = 0; i < submesh.indexCount; ++i)
        {
            const u32 index = indices[submesh.indexOffset + i];
            if (index < vertices.size())
            {
                submesh.bounds.expand(vertices[index].position);
            }
        }
    }
}

void computeNormals(MeshData& mesh)
{
    for (Vertex& vertex : mesh.vertices)
    {
        vertex.normal = Vec3(0.0f);
    }
    for (usize i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const u32 a = mesh.indices[i];
        const u32 b = mesh.indices[i + 1];
        const u32 c = mesh.indices[i + 2];
        if (a >= mesh.vertices.size() || b >= mesh.vertices.size() || c >= mesh.vertices.size())
        {
            continue;
        }
        // Area-weighted face normal.
        const Vec3 normal = glm::cross(mesh.vertices[b].position - mesh.vertices[a].position,
                                       mesh.vertices[c].position - mesh.vertices[a].position);
        mesh.vertices[a].normal += normal;
        mesh.vertices[b].normal += normal;
        mesh.vertices[c].normal += normal;
    }
    for (Vertex& vertex : mesh.vertices)
    {
        const f32 length = glm::length(vertex.normal);
        vertex.normal = length > 0.0f ? vertex.normal / length : Vec3(0.0f, 1.0f, 0.0f);
    }
}

MeshData makeCube(f32 size)
{
    MeshData mesh;
    const f32 h = size * 0.5f;
    struct Face
    {
        Vec3 normal;
        Vec3 tangent;
    };
    const Face faces[] = {
        {{1, 0, 0}, {0, 0, -1}}, {{-1, 0, 0}, {0, 0, 1}}, {{0, 1, 0}, {1, 0, 0}},
        {{0, -1, 0}, {1, 0, 0}}, {{0, 0, 1}, {1, 0, 0}},  {{0, 0, -1}, {-1, 0, 0}},
    };
    for (const Face& face : faces)
    {
        const Vec3 bitangent = glm::cross(face.normal, face.tangent);
        const auto base = static_cast<u32>(mesh.vertices.size());
        const Vec2 uvs[] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        const Vec2 corners[] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (usize i = 0; i < 4; ++i)
        {
            Vertex v;
            v.position = (face.normal + face.tangent * corners[i].x + bitangent * corners[i].y) * h;
            v.normal = face.normal;
            v.tangent = Vec4(face.tangent, 1.0f);
            v.uv = uvs[i];
            mesh.vertices.push_back(v);
        }
        for (const u32 index : {0u, 1u, 2u, 0u, 2u, 3u})
        {
            mesh.indices.push_back(base + index);
        }
    }
    mesh.submeshes.push_back(Submesh{0, static_cast<u32>(mesh.indices.size()), -1, {}});
    mesh.computeBounds();
    return mesh;
}

MeshData makeSphere(f32 radius, u32 segments, u32 rings)
{
    MeshData mesh;
    segments = std::max(segments, 3u);
    rings = std::max(rings, 2u);
    const f32 pi = std::numbers::pi_v<f32>;
    for (u32 ring = 0; ring <= rings; ++ring)
    {
        const f32 v = static_cast<f32>(ring) / static_cast<f32>(rings);
        const f32 phi = v * pi; // 0 at the top
        for (u32 segment = 0; segment <= segments; ++segment)
        {
            const f32 u = static_cast<f32>(segment) / static_cast<f32>(segments);
            const f32 theta = u * 2.0f * pi;
            const Vec3 normal(std::sin(phi) * std::cos(theta), std::cos(phi),
                              -std::sin(phi) * std::sin(theta));
            Vertex vertex;
            vertex.position = normal * radius;
            vertex.normal = normal;
            vertex.tangent = Vec4(-std::sin(theta), 0.0f, -std::cos(theta), 1.0f);
            vertex.uv = Vec2(u, v);
            mesh.vertices.push_back(vertex);
        }
    }
    const u32 stride = segments + 1;
    for (u32 ring = 0; ring < rings; ++ring)
    {
        for (u32 segment = 0; segment < segments; ++segment)
        {
            const u32 a = ring * stride + segment;
            const u32 b = a + stride;
            for (const u32 index : {a, b, a + 1, a + 1, b, b + 1})
            {
                mesh.indices.push_back(index);
            }
        }
    }
    mesh.submeshes.push_back(Submesh{0, static_cast<u32>(mesh.indices.size()), -1, {}});
    mesh.computeBounds();
    return mesh;
}

MeshData makePlane(f32 size, u32 subdivisions)
{
    MeshData mesh;
    subdivisions = std::max(subdivisions, 1u);
    const u32 stride = subdivisions + 1;
    for (u32 z = 0; z <= subdivisions; ++z)
    {
        for (u32 x = 0; x <= subdivisions; ++x)
        {
            const f32 u = static_cast<f32>(x) / static_cast<f32>(subdivisions);
            const f32 v = static_cast<f32>(z) / static_cast<f32>(subdivisions);
            Vertex vertex;
            vertex.position = Vec3((u - 0.5f) * size, 0.0f, (v - 0.5f) * size);
            vertex.normal = Vec3(0.0f, 1.0f, 0.0f);
            vertex.tangent = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
            vertex.uv = Vec2(u, v);
            mesh.vertices.push_back(vertex);
        }
    }
    for (u32 z = 0; z < subdivisions; ++z)
    {
        for (u32 x = 0; x < subdivisions; ++x)
        {
            const u32 a = z * stride + x;
            const u32 b = a + stride;
            for (const u32 index : {a, b, a + 1, a + 1, b, b + 1})
            {
                mesh.indices.push_back(index);
            }
        }
    }
    mesh.submeshes.push_back(Submesh{0, static_cast<u32>(mesh.indices.size()), -1, {}});
    mesh.computeBounds();
    return mesh;
}

MeshData makeCapsule(f32 radius, f32 halfHeight, u32 segments, u32 rings)
{
    MeshData mesh;
    segments = std::max(segments, 3u);
    const u32 hemisphereRings = std::max(rings / 2, 1u);
    const u32 rows = 2 * hemisphereRings + 2; // two hemispheres; the equator appears twice
    const f32 pi = std::numbers::pi_v<f32>;
    for (u32 row = 0; row < rows; ++row)
    {
        const bool top = row <= hemisphereRings;
        const u32 step = top ? row : row - 1;
        const f32 phi =
            static_cast<f32>(step) / static_cast<f32>(hemisphereRings) * pi * 0.5f; // 0 at the top
        const f32 v = static_cast<f32>(row) / static_cast<f32>(rows - 1);
        for (u32 segment = 0; segment <= segments; ++segment)
        {
            const f32 u = static_cast<f32>(segment) / static_cast<f32>(segments);
            const f32 theta = u * 2.0f * pi;
            const Vec3 normal(std::sin(phi) * std::cos(theta), std::cos(phi),
                              -std::sin(phi) * std::sin(theta));
            Vertex vertex;
            vertex.position = normal * radius + Vec3(0.0f, top ? halfHeight : -halfHeight, 0.0f);
            vertex.normal = normal;
            vertex.tangent = Vec4(-std::sin(theta), 0.0f, -std::cos(theta), 1.0f);
            vertex.uv = Vec2(u, v);
            mesh.vertices.push_back(vertex);
        }
    }
    const u32 stride = segments + 1;
    for (u32 row = 0; row + 1 < rows; ++row)
    {
        for (u32 segment = 0; segment < segments; ++segment)
        {
            const u32 a = row * stride + segment;
            const u32 b = a + stride;
            for (const u32 index : {a, b, a + 1, a + 1, b, b + 1})
            {
                mesh.indices.push_back(index);
            }
        }
    }
    mesh.submeshes.push_back(Submesh{0, static_cast<u32>(mesh.indices.size()), -1, {}});
    mesh.computeBounds();
    return mesh;
}

MeshData makeCylinder(f32 radius, f32 halfHeight, u32 segments)
{
    MeshData mesh;
    segments = std::max(segments, 3u);
    const f32 pi = std::numbers::pi_v<f32>;
    // Side: two rows of vertices with outward normals.
    for (u32 row = 0; row < 2; ++row)
    {
        for (u32 segment = 0; segment <= segments; ++segment)
        {
            const f32 u = static_cast<f32>(segment) / static_cast<f32>(segments);
            const f32 theta = u * 2.0f * pi;
            const Vec3 normal(std::cos(theta), 0.0f, -std::sin(theta));
            Vertex vertex;
            vertex.position = normal * radius + Vec3(0.0f, row == 0 ? halfHeight : -halfHeight, 0.0f);
            vertex.normal = normal;
            vertex.tangent = Vec4(-std::sin(theta), 0.0f, -std::cos(theta), 1.0f);
            vertex.uv = Vec2(u, static_cast<f32>(row));
            mesh.vertices.push_back(vertex);
        }
    }
    const u32 stride = segments + 1;
    for (u32 segment = 0; segment < segments; ++segment)
    {
        const u32 a = segment;
        const u32 b = a + stride;
        for (const u32 index : {a, b, a + 1, a + 1, b, b + 1})
        {
            mesh.indices.push_back(index);
        }
    }
    // Caps: a centre vertex and a ring each. Tangent w = 0 lets shaders derive the tangent frame.
    for (const f32 side : {1.0f, -1.0f})
    {
        const auto center = static_cast<u32>(mesh.vertices.size());
        Vertex middle;
        middle.position = Vec3(0.0f, side * halfHeight, 0.0f);
        middle.normal = Vec3(0.0f, side, 0.0f);
        middle.uv = Vec2(0.5f);
        mesh.vertices.push_back(middle);
        for (u32 segment = 0; segment <= segments; ++segment)
        {
            const f32 theta = static_cast<f32>(segment) / static_cast<f32>(segments) * 2.0f * pi;
            Vertex vertex = middle;
            vertex.position += Vec3(std::cos(theta), 0.0f, -std::sin(theta)) * radius;
            vertex.uv = Vec2(0.5f + 0.5f * std::cos(theta), 0.5f + 0.5f * std::sin(theta) * side);
            mesh.vertices.push_back(vertex);
        }
        for (u32 segment = 0; segment < segments; ++segment)
        {
            const u32 current = center + 1 + segment;
            const u32 next = current + 1;
            for (const u32 index :
                 side > 0.0f ? std::array{center, current, next} : std::array{center, next, current})
            {
                mesh.indices.push_back(index);
            }
        }
    }
    mesh.submeshes.push_back(Submesh{0, static_cast<u32>(mesh.indices.size()), -1, {}});
    mesh.computeBounds();
    return mesh;
}

std::optional<MeshData> makeBuiltinMesh(std::string_view path)
{
    if (path == "builtin:/mesh/cube")
    {
        return makeCube();
    }
    if (path == "builtin:/mesh/sphere")
    {
        return makeSphere();
    }
    if (path == "builtin:/mesh/plane")
    {
        return makePlane();
    }
    if (path == "builtin:/mesh/capsule")
    {
        return makeCapsule();
    }
    if (path == "builtin:/mesh/cylinder")
    {
        return makeCylinder();
    }
    return std::nullopt;
}

} // namespace illusion
