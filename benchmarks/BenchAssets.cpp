#include "Budget.hpp"

#include <illusion/assets/AssetFormat.hpp>
#include <illusion/assets/Environment.hpp>
#include <illusion/assets/Image.hpp>
#include <illusion/assets/Mesh.hpp>
#include <illusion/core/Hash.hpp>

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
