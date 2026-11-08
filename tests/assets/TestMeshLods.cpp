#include "AssetTestProject.hpp"

#include <illusion/assets/AssetDatabase.hpp>
#include <illusion/assets/AssetFormat.hpp>
#include <illusion/assets/ImportSettings.hpp>
#include <illusion/assets/Mesh.hpp>

#include <doctest/doctest.h>

#include <algorithm>
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
