#include "AssetTestProject.hpp"

#include <illusion/assets/AssetDatabase.hpp>
#include <illusion/assets/AssetFormat.hpp>
#include <illusion/assets/ImportSettings.hpp>
#include <illusion/assets/Mesh.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace illusion;

namespace
{

// Indices of level @p level of @p submesh.
std::span<const u32> levelIndices(const MeshData& mesh, const Submesh& submesh, u32 level)
{
    const SubmeshLod range = submesh.level(level);
    return std::span<const u32>(mesh.indices).subspan(range.indexOffset, range.indexCount);
}

// A sphere split into two submeshes (upper and lower half of the triangles) with different materials.
MeshData twoMaterialSphere()
{
    MeshData mesh = makeSphere(1.0f, 64, 32);
    const u32 half = static_cast<u32>(mesh.indices.size() / 6) * 3;
    mesh.submeshes.clear();
    Submesh upper;
    upper.indexCount = half;
    upper.material = 0;
    Submesh lower;
    lower.indexOffset = half;
    lower.indexCount = static_cast<u32>(mesh.indices.size()) - half;
    lower.material = 1;
    mesh.submeshes = {upper, lower};
    mesh.computeBounds();
    return mesh;
}

// Foliage: @p count square cards of two triangles each (no shared vertices), 0.2 units wide, spread over a
// 10 unit cube by a fixed sequence.
MeshData cardFoliage(u32 count)
{
    MeshData mesh;
    u32 state = 12345u;
    const auto next = [&state] {
        state = state * 1664525u + 1013904223u;
        return static_cast<f32>(state >> 8) / static_cast<f32>(1u << 24);
    };
    for (u32 card = 0; card < count; ++card)
    {
        const Vec3 centre(next() * 10.0f, next() * 10.0f, next() * 10.0f);
        const u32 base = static_cast<u32>(mesh.vertices.size());
        for (const Vec2 corner : {Vec2(-0.1f, -0.1f), Vec2(0.1f, -0.1f), Vec2(0.1f, 0.1f), Vec2(-0.1f, 0.1f)})
        {
            Vertex vertex;
            vertex.position = centre + Vec3(corner.x, corner.y, 0.0f);
            vertex.normal = Vec3(0.0f, 0.0f, 1.0f);
            vertex.uv = corner * 5.0f + Vec2(0.5f);
            mesh.vertices.push_back(vertex);
        }
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    Submesh submesh;
    submesh.indexCount = static_cast<u32>(mesh.indices.size());
    mesh.submeshes = {submesh};
    mesh.computeBounds();
    return mesh;
}

// Largest bounding-box extent of the triangles of @p indices (each triangle of a square card spans it).
f32 largestCard(const MeshData& mesh, std::span<const u32> indices)
{
    f32 largest = 0.0f;
    for (usize i = 0; i + 2 < indices.size(); i += 3)
    {
        Aabb box;
        for (usize corner = 0; corner < 3; ++corner)
        {
            box.expand(mesh.vertices[indices[i + corner]].position);
        }
        const Vec3 extent = box.max - box.min;
        largest = std::max({largest, extent.x, extent.y, extent.z});
    }
    return largest;
}

} // namespace

TEST_CASE("generateLods adds coarser levels with fewer triangles and growing errors")
{
    MeshData mesh = makeSphere(1.0f, 64, 32);
    const usize fullIndices = mesh.indices.size();
    generateLods(mesh);
    const Submesh& submesh = mesh.submeshes[0];
    REQUIRE(submesh.lodCount == kMaxSubmeshLods);
    u32 previousCount = submesh.indexCount;
    f32 previousError = 0.0f;
    for (u32 level = 1; level <= submesh.lodCount; ++level)
    {
        const SubmeshLod range = submesh.level(level);
        CHECK(range.indexCount > 0);
        CHECK(range.indexCount % 3 == 0);
        CHECK(range.indexCount < previousCount);
        CHECK(range.indexCount <= previousCount * 6 / 10 + 3); // about half of the previous level
        CHECK(range.error >= previousError);
        CHECK(range.error <= 0.05f * 2.0f); // MeshLodSettings::maxError of the sphere's size (2)
        CHECK(range.indexOffset >= fullIndices);
        for (const u32 index : levelIndices(mesh, submesh, level))
        {
            CHECK(index < mesh.vertices.size());
        }
        previousCount = range.indexCount;
        previousError = range.error;
    }
    CHECK(submesh.level(0).indexCount == submesh.indexCount);
    CHECK(submesh.level(99).indexOffset == submesh.lods[submesh.lodCount - 1].indexOffset); // clamped
}

TEST_CASE("generateLods is deterministic and replaces earlier levels")
{
    MeshData a = makeSphere(1.0f, 48, 24);
    MeshData b = a;
    generateLods(a);
    generateLods(b);
    CHECK(a.indices == b.indices);
    REQUIRE(a.submeshes[0].lodCount == b.submeshes[0].lodCount);

    const usize size = a.indices.size();
    generateLods(a); // regenerating does not grow the index buffer
    CHECK(a.indices.size() == size);
    CHECK(a.indices == b.indices);
}

TEST_CASE("generateLods follows its settings")
{
    MeshData none = makeSphere(1.0f, 64, 32);
    const usize indices = none.indices.size();
    generateLods(none, MeshLodSettings{.levels = 0});
    CHECK(none.submeshes[0].lodCount == 0);
    CHECK(none.indices.size() == indices);

    MeshData one = makeSphere(1.0f, 64, 32);
    generateLods(one, MeshLodSettings{.levels = 1, .reduction = 0.25f});
    REQUIRE(one.submeshes[0].lodCount == 1);
    CHECK(one.submeshes[0].lods[0].indexCount <= one.submeshes[0].indexCount * 3 / 10);

    // A tight error limit stops simplification early.
    MeshData strict = makeSphere(1.0f, 64, 32);
    generateLods(strict, MeshLodSettings{.maxError = 1.0e-6f});
    CHECK(strict.submeshes[0].lodCount == 0);
}

TEST_CASE("generateLods skips small and unsimplifiable submeshes")
{
    MeshData cube = makeCube(); // 12 triangles: below MeshLodSettings::minTriangles
    generateLods(cube);
    CHECK(cube.submeshes[0].lodCount == 0);

    MeshData plane = makePlane(1.0f, 1); // 2 triangles even with no minimum: no smaller level exists
    generateLods(plane, MeshLodSettings{.minTriangles = 0});
    CHECK(plane.submeshes[0].lodCount == 0);

    MeshData empty;
    generateLods(empty);
    CHECK(empty.indices.empty());
}

TEST_CASE("generateLods simplifies every submesh within its own triangles")
{
    MeshData mesh = twoMaterialSphere();
    generateLods(mesh);
    REQUIRE(mesh.submeshes.size() == 2);
    for (const Submesh& submesh : mesh.submeshes)
    {
        REQUIRE(submesh.lodCount > 0);
        const std::span<const u32> full = levelIndices(mesh, submesh, 0);
        for (u32 level = 1; level <= submesh.lodCount; ++level)
        {
            // Simplification only removes vertices: each level uses vertices of its own submesh.
            for (const u32 index : levelIndices(mesh, submesh, level))
            {
                CHECK(std::find(full.begin(), full.end(), index) != full.end());
            }
        }
    }
}

TEST_CASE("generateLods thins foliage cards and enlarges the cards it keeps")
{
    MeshData mesh = cardFoliage(2000);
    const usize fullVertices = mesh.vertices.size();
    const std::vector<u32> fullIndices = mesh.indices;
    const Aabb fullBounds = mesh.bounds;
    generateLods(mesh, MeshLodSettings{.maxError = 0.5f});
    const Submesh& submesh = mesh.submeshes[0];
    REQUIRE(submesh.lodCount == kMaxSubmeshLods);

    u32 previousTriangles = submesh.indexCount / 3;
    f32 previousError = 0.0f;
    f32 previousCard = 0.2f;
    for (u32 level = 1; level <= submesh.lodCount; ++level)
    {
        CAPTURE(level);
        const SubmeshLod range = submesh.level(level);
        const u32 triangles = range.indexCount / 3;
        // About half of the previous level's cards (a hash decides), each still whole.
        CHECK(triangles % 2 == 0);
        CHECK(triangles > previousTriangles * 4 / 10);
        CHECK(triangles < previousTriangles * 6 / 10);
        CHECK(range.error > previousError);
        // The kept cards are new, enlarged vertices; the full-detail ones are untouched.
        const std::span<const u32> indices = levelIndices(mesh, submesh, level);
        CHECK(std::all_of(indices.begin(), indices.end(), [&](u32 index) { return index >= fullVertices; }));
        const f32 card = largestCard(mesh, indices);
        CHECK(card > previousCard * 1.1f);
        previousTriangles = triangles;
        previousError = range.error;
        previousCard = card;
    }
    // Cards grow by (fraction kept)^-0.3 overall, and the bounds include them.
    CHECK(previousCard == doctest::Approx(0.2f * std::pow(0.125f, -0.3f)).epsilon(0.1));
    CHECK(mesh.bounds.min.x <= fullBounds.min.x);
    CHECK(mesh.bounds.max.y >= fullBounds.max.y);
    CHECK(submesh.bounds.max.z >= fullBounds.max.z);
    // The full detail is unchanged, and collisions still see only it.
    CHECK(std::equal(fullIndices.begin(), fullIndices.end(), mesh.indices.begin()));
    CHECK(fullDetailIndices(mesh).size() == fullIndices.size());

    // Deterministic.
    MeshData again = cardFoliage(2000);
    generateLods(again, MeshLodSettings{.maxError = 0.5f});
    CHECK(again.indices == mesh.indices);
    CHECK(again.vertices.size() == mesh.vertices.size());

    // clearLods drops the appended cards too.
    clearLods(mesh);
    CHECK(mesh.vertices.size() == fullVertices);
    CHECK(mesh.indices == fullIndices);
}

TEST_CASE("card thinning stops at the error limit and skips meshes that are not foliage")
{
    // At 3% of the 10 unit size, the third level's gaps (0.2 * (sqrt(8) - 1) = 0.37) are too large.
    MeshData limited = cardFoliage(2000);
    generateLods(limited, MeshLodSettings{.maxError = 0.03f});
    CHECK(limited.submeshes[0].lodCount == 2);
    CHECK(limited.submeshes[0].level(2).error <= 0.31f);

    // Too few cards to thin.
    MeshData few = cardFoliage(10);
    generateLods(few, MeshLodSettings{.maxError = 0.5f, .minTriangles = 0});
    CHECK(few.submeshes[0].lodCount == 0);
    CHECK(few.vertices.size() == 40);

    // Large separate parts are not cards: a sphere copied 20 times cannot be thinned, only simplified.
    MeshData sphere = makeSphere(1.0f, 64, 32);
    MeshData spheres;
    for (u32 copy = 0; copy < 20; ++copy)
    {
        const u32 base = static_cast<u32>(spheres.vertices.size());
        for (Vertex vertex : sphere.vertices)
        {
            vertex.position.x += 3.0f * static_cast<f32>(copy);
            spheres.vertices.push_back(vertex);
        }
        for (const u32 index : sphere.indices)
        {
            spheres.indices.push_back(base + index);
        }
    }
    Submesh all;
    all.indexCount = static_cast<u32>(spheres.indices.size());
    spheres.submeshes = {all};
    spheres.computeBounds();
    const usize vertices = spheres.vertices.size();
    generateLods(spheres);
    CHECK(spheres.submeshes[0].lodCount == kMaxSubmeshLods);
    CHECK(spheres.vertices.size() == vertices);
}

TEST_CASE("clearLods removes the levels and their indices")
{
    MeshData mesh = makeSphere(1.0f, 64, 32);
    const std::vector<u32> original = mesh.indices;
    generateLods(mesh);
    REQUIRE(mesh.indices.size() > original.size());
    clearLods(mesh);
    CHECK(mesh.submeshes[0].lodCount == 0);
    CHECK(mesh.indices == original);
}

TEST_CASE("fullDetailIndices leaves out the levels of detail")
{
    MeshData mesh = makeSphere(1.0f, 64, 32);
    const std::vector<u32> original = mesh.indices;
    CHECK(fullDetailIndices(mesh).size() == original.size()); // no levels: everything
    generateLods(mesh);
    const std::span<const u32> full = fullDetailIndices(mesh);
    CHECK(std::vector<u32>(full.begin(), full.end()) == original);
    CHECK(fullDetailIndices(MeshData{}).empty());
}

TEST_CASE("selectLod picks the coarsest level within the pixel error")
{
    Submesh submesh;
    submesh.indexCount = 300;
    submesh.lodCount = 3;
    submesh.lods[0] = SubmeshLod{300, 150, 0.01f};
    submesh.lods[1] = SubmeshLod{450, 75, 0.02f};
    submesh.lods[2] = SubmeshLod{525, 36, 0.08f};

    CHECK(selectLod(submesh, 1000.0f, 1.0f) == 0); // 0.01 * 1000 = 10 px
    CHECK(selectLod(submesh, 100.0f, 1.0f) == 1);  // 1 px, 2 px, 8 px
    CHECK(selectLod(submesh, 50.0f, 1.0f) == 2);   // 0.5 px, 1 px, 4 px
    CHECK(selectLod(submesh, 10.0f, 1.0f) == 3);
    CHECK(selectLod(submesh, 10.0f, 0.0f) == 0); // 0 turns levels of detail off
    CHECK(selectLod(submesh, 10.0f, -1.0f) == 0);
    CHECK(selectLod(Submesh{}, 0.0f, 1.0f) == 0);
}

TEST_CASE("levels of detail survive serialization and bad ranges are rejected")
{
    ModelData model;
    model.meshes.push_back(makeSphere(1.0f, 64, 32));
    generateLods(model.meshes[0]);
    const std::optional<ModelData> read = deserializeModel(serializeModel(model));
    REQUIRE(read.has_value());
    const Submesh& original = model.meshes[0].submeshes[0];
    const Submesh& copy = read->meshes[0].submeshes[0];
    REQUIRE(copy.lodCount == original.lodCount);
    for (u32 level = 0; level < original.lodCount; ++level)
    {
        CHECK(copy.lods[level].indexOffset == original.lods[level].indexOffset);
        CHECK(copy.lods[level].indexCount == original.lods[level].indexCount);
        CHECK(copy.lods[level].error == original.lods[level].error);
    }
    CHECK(read->meshes[0].indices == model.meshes[0].indices);

    const auto rejects = [&](auto change) {
        ModelData broken = model;
        change(broken.meshes[0].submeshes[0]);
        return !deserializeModel(serializeModel(broken)).has_value();
    };
    CHECK(rejects([](Submesh& s) { s.lodCount = kMaxSubmeshLods + 1; }));
    CHECK(rejects([](Submesh& s) { s.lods[0].indexCount = 0; }));
    CHECK(rejects([](Submesh& s) { s.lods[0].indexCount = 4; }));
    CHECK(rejects(
        [&](Submesh& s) { s.lods[1].indexOffset = static_cast<u32>(model.meshes[0].indices.size()); }));
    CHECK(rejects([](Submesh& s) { s.lods[2].error = -1.0f; }));
}

TEST_CASE("imported models get levels of detail as their import settings say")
{
    test::AssetProject project;
    test::writeSphereGltf(project.files, "game:/sphere.gltf");
    const auto lodCount = [&]() -> u32 {
        AssetDatabase database(project.files, "");
        const std::optional<ModelData> model = database.loadModel("game:/sphere.gltf");
        REQUIRE(model.has_value());
        REQUIRE(model->meshes.size() == 1);
        return model->meshes[0].submeshes[0].lodCount;
    };
    CHECK(lodCount() == ModelImportSettings{}.lodCount);

    REQUIRE(
        project.files.writeText("game:/sphere.gltf.import.json",
                                R"({"format": "illusion.import", "version": 1, "model": {"lodCount": 1}})") ==
        FileError::None);
    CHECK(lodCount() == 1);

    REQUIRE(
        project.files.writeText("game:/sphere.gltf.import.json",
                                R"({"format": "illusion.import", "version": 1, "model": {"lodCount": 0}})") ==
        FileError::None);
    CHECK(lodCount() == 0);
}
