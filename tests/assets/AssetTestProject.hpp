#pragma once

// A writable copy of the glTF test data mounted as "game", plus a "cache" mount, for asset tests, and
// writeSphereGltf() for a model dense enough to get levels of detail.

#include "TestSupport.hpp"

#include <illusion/assets/Mesh.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::test
{

struct AssetProject
{
    TempDirectory directory;
    VirtualFileSystem files;

    AssetProject()
    {
        const std::filesystem::path source = utf8Path(ILLUSION_TEST_DATA_DIR) / "gltf";
        std::filesystem::copy(source, directory.path / "game", std::filesystem::copy_options::recursive);
        std::filesystem::create_directories(directory.path / "cache");
        REQUIRE(files.mount("game", directory.path / "game", MountAccess::ReadWrite) == FileError::None);
        REQUIRE(files.mount("cache", directory.path / "cache", MountAccess::ReadWrite) == FileError::None);
    }
};

// Writes a glTF model of one UV sphere (64 x 32 segments, 4096 triangles) with a separate buffer file.
inline void writeSphereGltf(VirtualFileSystem& files, std::string_view path)
{
    const MeshData sphere = makeSphere(1.0f, 64, 32);
    const usize vertexCount = sphere.vertices.size();
    std::vector<std::byte> buffer(vertexCount * sizeof(Vec3) * 2 + sphere.indices.size() * sizeof(u32));
    std::byte* out = buffer.data();
    for (const Vertex& vertex : sphere.vertices)
    {
        std::memcpy(out, &vertex.position, sizeof(Vec3));
        out += sizeof(Vec3);
    }
    for (const Vertex& vertex : sphere.vertices)
    {
        std::memcpy(out, &vertex.normal, sizeof(Vec3));
        out += sizeof(Vec3);
    }
    std::memcpy(out, sphere.indices.data(), sphere.indices.size() * sizeof(u32));
    const std::string name(path.substr(path.rfind('/') + 1));
    REQUIRE(files.writeFile(std::string(path) + ".bin", buffer) == FileError::None);
    const usize attributeBytes = vertexCount * sizeof(Vec3);
    const std::string gltf = std::format(
        R"({{"asset": {{"version": "2.0"}}, "scene": 0, "scenes": [{{"nodes": [0]}}], "nodes": [{{"mesh": 0}}],
"meshes": [{{"primitives": [{{"attributes": {{"POSITION": 0, "NORMAL": 1}}, "indices": 2}}]}}],
"buffers": [{{"uri": "{}.bin", "byteLength": {}}}],
"bufferViews": [{{"buffer": 0, "byteOffset": 0, "byteLength": {}}}, {{"buffer": 0, "byteOffset": {}, "byteLength": {}}},
{{"buffer": 0, "byteOffset": {}, "byteLength": {}}}],
"accessors": [{{"bufferView": 0, "componentType": 5126, "count": {}, "type": "VEC3", "min": [-1, -1, -1], "max": [1, 1, 1]}},
{{"bufferView": 1, "componentType": 5126, "count": {}, "type": "VEC3"}},
{{"bufferView": 2, "componentType": 5125, "count": {}, "type": "SCALAR"}}]}})",
        name, buffer.size(), attributeBytes, attributeBytes, attributeBytes, attributeBytes * 2,
        sphere.indices.size() * sizeof(u32), vertexCount, vertexCount, sphere.indices.size());
    REQUIRE(files.writeText(path, gltf) == FileError::None);
}

} // namespace illusion::test
