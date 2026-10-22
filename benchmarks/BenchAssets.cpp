#include "Budget.hpp"

#include <illusion/assets/AssetFormat.hpp>
#include <illusion/assets/Environment.hpp>
#include <illusion/assets/Image.hpp>
#include <illusion/assets/Mesh.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/platform/Archive.hpp>

#include <filesystem>
#include <format>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace illusion;

namespace
{

// A 512x512 RGBA8 image with some structure (so PNG compression does real work).
Image makeTestImage()
{
    Image image = makeSolidImage(512, 512, Vec4(0.0f), ImageFormat::RGBA8Srgb);
    auto* pixels = reinterpret_cast<u8*>(image.data.data());
    for (u32 i = 0; i < 512 * 512; ++i)
    {
        pixels[i * 4 + 0] = static_cast<u8>(i * 7);
        pixels[i * 4 + 1] = static_cast<u8>(i / 512);
        pixels[i * 4 + 2] = static_cast<u8>((i % 512) ^ (i / 512));
        pixels[i * 4 + 3] = 255;
    }
    return image;
}

} // namespace

// Budgets are roughly 5-10x a desktop result; see Budget.hpp.

ILL_BENCHMARK("assets/sh-irradiance", 200.0)
{
    const Image sky = makeSkyImage(512);
    const usize texels = static_cast<usize>(sky.width()) * sky.height();
    bench.batch(texels).unit("texel").run("computeShIrradiance (512x256)", [&] {
        ankerl::nanobench::doNotOptimizeAway(computeShIrradiance(sky));
    });
}

ILL_BENCHMARK("assets/content-hash", 0.5)
{
    const std::vector<std::byte> data(1 << 20, std::byte{0x5A});
    bench.batch(data.size()).unit("byte").run("xxhash64 over 1 MiB", [&] {
        ankerl::nanobench::doNotOptimizeAway(xxhash64(data));
    });
}

ILL_BENCHMARK("assets/image-load", 200.0)
{
    Image image = makeTestImage();
    const std::vector<std::byte> png = encodePng(image);
    generateMips(image);
    const std::vector<std::byte> cooked =
        writeAssetFile(AssetFile{AssetType::Image, kImageFormatVersion, {}, serializeImage(image)});
    const usize texels = 512 * 512;
    // Importing: decode the PNG and build mips (what the import cache saves on every later load).
    bench.batch(texels).unit("texel").run("import: decode PNG + generate mips (512x512)", [&] {
        std::optional<Image> decoded = decodeImage(png, true);
        generateMips(*decoded);
        ankerl::nanobench::doNotOptimizeAway(decoded);
    });
    bench.batch(texels).unit("texel").run("cached: verify + deserialize (512x512 with mips)", [&] {
        const std::optional<AssetFile> file = readAssetFile(cooked);
        ankerl::nanobench::doNotOptimizeAway(deserializeImage(file->payload));
    });
}

ILL_BENCHMARK("assets/model-load", 250.0)
{
    ModelData model;
    for (int i = 0; i < 16; ++i)
    {
        model.meshes.push_back(makeSphere(1.0f, 64, 32));
    }
    const std::vector<std::byte> bytes = serializeModel(model);
    usize vertices = 0;
    for (const MeshData& mesh : model.meshes)
    {
        vertices += mesh.vertices.size();
    }
    bench.batch(vertices).unit("vertex").run("deserialize model (16 spheres, validated)", [&] {
        ankerl::nanobench::doNotOptimizeAway(deserializeModel(bytes));
    });
}

ILL_BENCHMARK("platform/archive", 2000.0)
{
    // 10k small files in one archive: lookups are binary searches over the sorted table of contents.
    std::vector<ArchiveInput> files;
    for (int i = 0; i < 10000; ++i)
    {
        files.push_back(ArchiveInput{std::format("assets/dir{}/file{}.bin", i % 100, i),
                                     std::vector<std::byte>(256, std::byte{static_cast<unsigned char>(i)})});
    }
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                       std::format("illusion-bench-{}.illpak", std::random_device{}());
    if (writeArchive(path, files) != FileError::None)
    {
        return;
    }
    {
        const std::unique_ptr<ArchiveReader> reader = ArchiveReader::open(path);
        u32 index = 0;
        bench.run("find among 10k entries", [&] {
            ankerl::nanobench::doNotOptimizeAway(reader->find(files[index].path));
            index = (index + 7919) % 10000;
        });
        std::vector<std::byte> out;
        bench.run("find + read a 256-byte file", [&] {
            reader->read(*reader->find(files[index].path), out);
            ankerl::nanobench::doNotOptimizeAway(out.data());
            index = (index + 7919) % 10000;
        });
    }
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

ILL_BENCHMARK("assets/path-intern", 300.0)
{
    AssetPathTable table;
    std::vector<std::string> paths;
    for (int i = 0; i < 1024; ++i)
    {
        paths.push_back(std::format("game:/models/props/crate_{}.glb#mesh/{}", i, i % 4));
        (void)table.intern(paths.back());
    }
    usize next = 0;
    bench.run("intern a recorded, normalized path (1024 paths)", [&] {
        const AssetId id = table.intern(paths[next++ & 1023]);
        ankerl::nanobench::doNotOptimizeAway(id);
    });
}
