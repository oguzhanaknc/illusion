#pragma once

// Golden images of the editor UI. Captures are compared with the PNGs in editor/tests/data/golden; every
// capture is also written to ILLUSION_UI_CAPTURE_DIR (the build tree by default) so that CI can publish them
// for design review. Run with ILLUSION_UPDATE_GOLDENS=1 to rewrite the goldens from the current captures
// after an intended visual change, and review the diff before committing.

#include <illusion/assets/Image.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/render/RenderDevice.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <span>
#include <string_view>
#include <vector>

namespace illusion::test
{

/// A headless device with validation, for the editor's GPU tests.
inline RenderDeviceDesc headlessDeviceDesc()
{
    RenderDeviceDesc desc;
    desc.applicationName = "illusion editor GPU tests";
    desc.headless = true;
    desc.validation = ValidationMode::Enabled;
    return desc;
}

/// Where captures are written.
inline std::filesystem::path captureDirectory()
{
    if (const char* directory = std::getenv("ILLUSION_UI_CAPTURE_DIR");
        directory != nullptr && directory[0] != '\0')
    {
        return utf8Path(directory);
    }
    return utf8Path(ILLUSION_UI_CAPTURE_DIR);
}

/// True when the goldens are to be rewritten.
inline bool updateGoldens()
{
    const char* update = std::getenv("ILLUSION_UPDATE_GOLDENS");
    return update != nullptr && std::string_view(update) == "1";
}

/// The bytes of @p path.
inline std::vector<std::byte> readBinaryFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::vector<std::byte> result(bytes.size());
    std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
}

/// Writes @p bytes to @p path, creating its directory.
inline void writeBinaryFile(const std::filesystem::path& path, std::span<const std::byte> bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(file.good());
}

/// How far two images of the same size are apart.
struct ImageDifference
{
    f64 meanAbsolute = 0.0;      ///< Mean absolute channel difference (0-255).
    f64 differentFraction = 0.0; ///< Fraction of pixels with a channel off by more than 24.
};

/// Compares two RGBA8 images of the same size.
inline ImageDifference compareImages(const Image& a, const Image& b)
{
    ImageDifference difference;
    const std::span<const std::byte> pa = a.mip(0);
    const std::span<const std::byte> pb = b.mip(0);
    u64 total = 0;
    u64 different = 0;
    for (usize i = 0; i < pa.size(); i += 4)
    {
        int worst = 0;
        for (usize c = 0; c < 4; ++c)
        {
            const int d = std::abs(static_cast<int>(pa[i + c]) - static_cast<int>(pb[i + c]));
            total += static_cast<u64>(d);
            worst = std::max(worst, d);
        }
        if (worst > 24)
        {
            ++different;
        }
    }
    difference.meanAbsolute = static_cast<f64>(total) / static_cast<f64>(pa.size());
    difference.differentFraction = static_cast<f64>(different) / static_cast<f64>(pa.size() / 4);
    return difference;
}

/// Writes @p actual to the capture directory as `<name>.png` and checks it against the golden of that name
/// (or rewrites the golden, see updateGoldens()).
inline void checkGolden(std::string_view name, const Image& actual)
{
    const std::vector<std::byte> png = encodePng(actual);
    writeBinaryFile(captureDirectory() / std::format("{}.png", name), png);
    const std::filesystem::path goldenPath = utf8Path(ILLUSION_GOLDEN_DIR) / std::format("{}.png", name);
    if (updateGoldens())
    {
        writeBinaryFile(goldenPath, png);
        return;
    }
    REQUIRE_MESSAGE(std::filesystem::exists(goldenPath),
                    "missing golden " << goldenPath.string() << " (run with ILLUSION_UPDATE_GOLDENS=1)");
    const std::optional<Image> expected = decodeImage(readBinaryFile(goldenPath), true);
    REQUIRE(expected.has_value());
    REQUIRE(expected->width() == actual.width());
    REQUIRE(expected->height() == actual.height());
    const ImageDifference difference = compareImages(actual, *expected);
    INFO("mean difference " << difference.meanAbsolute << ", pixels off "
                            << difference.differentFraction * 100.0 << "% (capture written to "
                            << captureDirectory().string() << ")");
    CHECK(difference.meanAbsolute < 1.0);
    CHECK(difference.differentFraction < 0.003);
}

} // namespace illusion::test
