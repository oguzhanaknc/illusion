#include <illusion/assets/Mesh.hpp>
#include <illusion/core/Profiler.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <meshoptimizer.h>
#include <numbers>
#include <numeric>
#include <vector>

namespace illusion
{

namespace
{

// One submesh over every index of a procedural mesh, drawn with the default material.
Submesh wholeSubmesh(const MeshData& mesh)
{
    Submesh submesh;
    submesh.indexCount = static_cast<u32>(mesh.indices.size());
    return submesh;
}

// Weight of the normal in the simplifier's error metric, relative to positions in units of the mesh size.
constexpr f32 kNormalWeight = 0.5f;

// A level must remove at least this fraction of the previous level's triangles to be kept.
constexpr f32 kMinReduction = 0.1f;

// Foliage made of cards (leaves, needle sprigs): parts that share no vertices. The simplifier cannot reduce
// them (every vertex is on a border), so their levels keep a part of the cards instead. A submesh is thinned
// when it has at least kMinCards parts of at most kMaxCardTriangles triangles on average.
constexpr u32 kMinCards = 16;
constexpr u32 kMaxCardTriangles = 64;
// Kept cards grow about their centres by (fraction of the cards kept)^-kCardScaleExponent to make up for the
// leaf area of the dropped ones. Less than the 1/2 that keeps the area: kept cards overlap less than the
// dense originals did.
constexpr f32 kCardScaleExponent = 0.3f;

// Whether card @p index is kept when @p fraction of the cards are: a hash of the index against the fraction,
// so the kept cards are spread evenly over the foliage.
bool keepsCard(u32 index, f32 fraction) noexcept
{
    return static_cast<f64>(index * 2654435761u) < static_cast<f64>(fraction) * 4294967296.0;
}

// The cards of a triangle list: the vertex-connected parts, numbered in the order of their first triangle.
struct Cards
{
    std::vector<u32> ofTriangle; // card of each triangle
    u32 count = 0;
    f32 meanSize = 0.0f; // mean of the cards' largest bounding-box extents
};

u32 findRoot(std::vector<u32>& parent, u32 vertex) noexcept
{
    while (parent[vertex] != vertex)
    {
        parent[vertex] = parent[parent[vertex]];
        vertex = parent[vertex];
    }
    return vertex;
}

Cards findCards(const MeshData& mesh, std::span<const u32> indices)
{
    std::vector<u32> parent(mesh.vertices.size());
    std::iota(parent.begin(), parent.end(), 0u);
    for (usize i = 0; i + 2 < indices.size(); i += 3)
    {
        const u32 a = findRoot(parent, indices[i]);
        parent[findRoot(parent, indices[i + 1])] = a;
        parent[findRoot(parent, indices[i + 2])] = a;
    }
    constexpr u32 kNone = ~0u;
    std::vector<u32> cardOfRoot(mesh.vertices.size(), kNone);
    Cards cards;
    cards.ofTriangle.resize(indices.size() / 3);
    std::vector<Aabb> bounds;
    for (usize t = 0; t < cards.ofTriangle.size(); ++t)
    {
        u32& card = cardOfRoot[findRoot(parent, indices[3 * t])];
        if (card == kNone)
        {
            card = cards.count++;
            bounds.emplace_back();
        }
        cards.ofTriangle[t] = card;
        for (usize corner = 0; corner < 3; ++corner)
        {
            bounds[card].expand(mesh.vertices[indices[3 * t + corner]].position);
        }
    }
    f64 total = 0.0;
    for (const Aabb& box : bounds)
    {
        const Vec3 extent = box.max - box.min;
        total += static_cast<f64>(std::max({extent.x, extent.y, extent.z}));
    }
    cards.meanSize = cards.count > 0 ? static_cast<f32>(total / cards.count) : 0.0f;
    return cards;
}

// Writes to @p out the triangles of the cards of @p source that keepsCard() keeps at @p fraction, over new
// vertices appended to @p mesh: copies of the cards' vertices scaled by @p scale about each card's centre.
// Returns the number of cards kept.
u32 thinCards(MeshData& mesh, std::span<const u32> source, const Cards& cards, f32 fraction, f32 scale,
              std::vector<u32>& out)
{
    constexpr u32 kNone = ~0u;
    std::vector<Vec3> centreSum(cards.count, Vec3(0.0f));
    std::vector<u32> vertexCount(cards.count, 0);
    std::vector<u32> copyOf(mesh.vertices.size(), kNone);
    for (usize i = 0; i < source.size(); ++i)
    {
        const u32 card = cards.ofTriangle[i / 3];
        if (keepsCard(card, fraction) && copyOf[source[i]] == kNone)
        {
            copyOf[source[i]] = 0; // counted
            centreSum[card] += mesh.vertices[source[i]].position;
            ++vertexCount[card];
        }
    }
    std::fill(copyOf.begin(), copyOf.end(), kNone);
    u32 kept = 0;
    for (u32 card = 0; card < cards.count; ++card)
    {
        kept += vertexCount[card] > 0 ? 1u : 0u;
    }
    out.clear();
    for (usize i = 0; i < source.size(); ++i)
    {
        const u32 card = cards.ofTriangle[i / 3];
        if (vertexCount[card] == 0)
        {
            continue;
        }
        u32& copy = copyOf[source[i]];
        if (copy == kNone)
        {
            const Vec3 centre = centreSum[card] / static_cast<f32>(vertexCount[card]);
            Vertex vertex = mesh.vertices[source[i]];
            vertex.position = centre + (vertex.position - centre) * scale;
            copy = static_cast<u32>(mesh.vertices.size());
            mesh.vertices.push_back(vertex);
        }
        out.push_back(copy);
    }
    return kept;
}

} // namespace

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

u32 selectLod(const Submesh& submesh, f32 pixelsPerUnit, f32 maxPixelError) noexcept
{
    if (!(maxPixelError > 0.0f))
    {
        return 0;
    }
    u32 level = 0;
    const u32 count = std::min(submesh.lodCount, kMaxSubmeshLods);
    while (level < count && submesh.lods[level].error * pixelsPerUnit <= maxPixelError)
    {
        ++level;
    }
    return level;
}

std::span<const u32> fullDetailIndices(const MeshData& mesh) noexcept
{
    usize end = mesh.indices.size();
    for (const Submesh& submesh : mesh.submeshes)
    {
        for (u32 level = 0; level < submesh.lodCount && level < kMaxSubmeshLods; ++level)
        {
            end = std::min(end, static_cast<usize>(submesh.lods[level].indexOffset));
        }
    }
    return std::span<const u32>(mesh.indices).first(end);
}

void clearLods(MeshData& mesh)
{
    usize fullEnd = 0;
    usize lodStart = mesh.indices.size();
    bool any = false;
    for (Submesh& submesh : mesh.submeshes)
    {
        fullEnd = std::max(fullEnd, static_cast<usize>(submesh.indexOffset) + submesh.indexCount);
        for (u32 level = 0; level < submesh.lodCount && level < kMaxSubmeshLods; ++level)
        {
            lodStart = std::min(lodStart, static_cast<usize>(submesh.lods[level].indexOffset));
            any = true;
        }
        submesh.lodCount = 0;
        submesh.lods = {};
    }
    if (any && lodStart >= fullEnd && fullEnd <= mesh.indices.size())
    {
        // Thinned foliage levels append their own vertices after every full-detail one.
        const auto highest = [&](usize begin, usize end) {
            u32 top = 0;
            for (usize i = begin; i < end; ++i)
            {
                top = std::max(top, mesh.indices[i]);
            }
            return top;
        };
        const u32 fullTop = highest(0, fullEnd);
        if (highest(lodStart, mesh.indices.size()) > fullTop && fullTop < mesh.vertices.size())
        {
            mesh.vertices.resize(usize{fullTop} + 1);
        }
        mesh.indices.resize(fullEnd);
    }
}

void generateLods(MeshData& mesh, const MeshLodSettings& settings)
{
    ILL_PROFILE_SCOPE();
    clearLods(mesh);
    const u32 levels = std::min(settings.levels, kMaxSubmeshLods);
    if (levels == 0 || mesh.vertices.empty() || !(settings.reduction > 0.0f && settings.reduction < 1.0f))
    {
        return;
    }
    const std::array<f32, 3> normalWeights{kNormalWeight, kNormalWeight, kNormalWeight};
    std::vector<u32> source;
    std::vector<u32> simplified;
    for (Submesh& submesh : mesh.submeshes)
    {
        if (static_cast<usize>(submesh.indexOffset) + submesh.indexCount > mesh.indices.size() ||
            submesh.indexCount / 3 < std::max(settings.minTriangles, 1u))
        {
            continue;
        }
        source.assign(mesh.indices.begin() + submesh.indexOffset,
                      mesh.indices.begin() + submesh.indexOffset + submesh.indexCount);
        if (std::any_of(source.begin(), source.end(),
                        [&](u32 index) { return index >= mesh.vertices.size(); }))
        {
            continue;
        }
        Aabb bounds;
        for (const u32 index : source)
        {
            bounds.expand(mesh.vertices[index].position);
        }
        const Vec3 extent = bounds.max - bounds.min;
        const f32 size = std::max({extent.x, extent.y, extent.z});
        if (!(size > 0.0f))
        {
            continue;
        }
        const f32 maxError = settings.maxError * size;
        f32 previousError = 0.0f;
        // Card thinning, once the simplifier makes no progress: the card count and mean card size of the
        // first thinned level's source.
        u32 originalCards = 0;
        f32 cardSize = 0.0f;
        for (u32 level = 0; level < levels; ++level)
        {
            const usize target =
                static_cast<usize>(static_cast<f32>(source.size() / 3) * settings.reduction) * 3;
            simplified.resize(source.size());
            f32 error = 0.0f;
            usize count = 0;
            if (originalCards == 0)
            {
                count = meshopt_simplifyWithAttributes(
                    simplified.data(), source.data(), source.size(), &mesh.vertices[0].position.x,
                    mesh.vertices.size(), sizeof(Vertex), &mesh.vertices[0].normal.x, sizeof(Vertex),
                    normalWeights.data(), normalWeights.size(), nullptr, target, maxError,
                    meshopt_SimplifyLockBorder | meshopt_SimplifyErrorAbsolute, &error);
            }
            if (count == 0 ||
                static_cast<f32>(count) > static_cast<f32>(source.size()) * (1.0f - kMinReduction))
            {
                // The simplifier is stuck: thin the cards if this is foliage.
                const Cards cards = findCards(mesh, source);
                if (cards.count < kMinCards || source.size() / 3 > usize{cards.count} * kMaxCardTriangles)
                {
                    break;
                }
                if (originalCards == 0)
                {
                    originalCards = cards.count;
                    cardSize = cards.meanSize;
                }
                const usize vertexEnd = mesh.vertices.size();
                const u32 kept = thinCards(mesh, source, cards, settings.reduction,
                                           std::pow(settings.reduction, -kCardScaleExponent), simplified);
                // The gaps left between the kept cards grow with their spacing, as 1/sqrt(fraction kept).
                const f32 fraction = static_cast<f32>(kept) / static_cast<f32>(originalCards);
                error = cardSize * (1.0f / std::sqrt(fraction) - 1.0f);
                if (kept < kMinCards || error > maxError ||
                    static_cast<f32>(simplified.size()) >
                        static_cast<f32>(source.size()) * (1.0f - kMinReduction))
                {
                    mesh.vertices.resize(vertexEnd);
                    break;
                }
                for (usize v = vertexEnd; v < mesh.vertices.size(); ++v)
                {
                    submesh.bounds.expand(mesh.vertices[v].position);
                    mesh.bounds.expand(mesh.vertices[v].position);
                }
                count = simplified.size();
            }
            simplified.resize(count);
            meshopt_optimizeVertexCache(simplified.data(), simplified.data(), count, mesh.vertices.size());
            previousError = std::max(previousError, error);
            submesh.lods[level] =
                SubmeshLod{static_cast<u32>(mesh.indices.size()), static_cast<u32>(count), previousError};
            submesh.lodCount = level + 1;
            mesh.indices.insert(mesh.indices.end(), simplified.begin(), simplified.end());
            source.swap(simplified);
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
    mesh.submeshes.push_back(wholeSubmesh(mesh));
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
    mesh.submeshes.push_back(wholeSubmesh(mesh));
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
    mesh.submeshes.push_back(wholeSubmesh(mesh));
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
    mesh.submeshes.push_back(wholeSubmesh(mesh));
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
    mesh.submeshes.push_back(wholeSubmesh(mesh));
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
