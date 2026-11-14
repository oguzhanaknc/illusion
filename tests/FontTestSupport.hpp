#pragma once

// The font of the tests (Roboto Regular, ILLUSION_TEST_FONT) and Fonts baked from it.

#include <illusion/assets/Font.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <cstddef>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>

namespace illusion::test
{

/// The bytes of the test font file; empty when it cannot be read.
inline std::vector<std::byte> readTestFontFile()
{
    std::ifstream file(utf8Path(ILLUSION_TEST_FONT), std::ios::binary);
    const std::vector<char> chars((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(chars.size());
    for (std::size_t i = 0; i < chars.size(); ++i)
    {
        bytes[i] = static_cast<std::byte>(chars[i]);
    }
    return bytes;
}

/// The test font baked with ASCII at 32 pixels per em, once per process.
inline const Font& testFont()
{
    static const Font font = [] {
        static constexpr CodePointRange kAscii[] = {{0x20, 0x7E}};
        FontBakeSettings settings;
        settings.pixelSize = 32;
        settings.ranges = kAscii;
        return std::move(Font::bake(readTestFontFile(), settings)).value();
    }();
    return font;
}

} // namespace illusion::test
