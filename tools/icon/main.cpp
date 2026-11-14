// illusion_icon: the application icons of an exported game (ADR 0055; run by illusion_add_game()).
//
//   illusion_icon <image.png | -> [--ico <file.ico>] [--png <size> <file.png>] [--iconset <directory>]
//
// Reads a square image ("-" for the engine's icon) and writes any of: a Windows icon with every size of
// kWindowsIconSizes, one PNG of <size> pixels, and a macOS `.iconset` directory for `iconutil -c icns`.

#include <illusion/assets/AppIcon.hpp>
#include <illusion/assets/Image.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <array>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace illusion;

namespace
{

constexpr u32 kEngineIconSize = 1024;
constexpr u32 kMaxSize = 4096;

int usage()
{
    std::fprintf(stderr, "usage: illusion_icon <image.png | -> [--ico <file.ico>] [--png <size> <file.png>] "
                         "[--iconset <directory>]\n");
    return 2;
}

std::string text(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

bool readBytes(const std::filesystem::path& path, std::vector<std::byte>& out)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        return false;
    }
    out.resize(static_cast<usize>(file.tellg()));
    file.seekg(0);
    return out.empty() || static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()),
                                                      static_cast<std::streamsize>(out.size())));
}

bool writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes)
{
    std::error_code code;
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path(), code);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file.good())
    {
        std::fprintf(stderr, "illusion_icon: cannot write %s\n", text(path).c_str());
        return false;
    }
    return true;
}

bool writePng(const Image& source, u32 size, const std::filesystem::path& path)
{
    const Image image = source.width() == size ? source : resizeImage(source, size, size);
    return writeBytes(path, encodePng(image));
}

std::optional<Image> loadSource(std::string_view input)
{
    if (input == "-")
    {
        return makeEngineIcon(kEngineIconSize);
    }
    const std::filesystem::path path = utf8Path(input);
    std::vector<std::byte> bytes;
    if (!readBytes(path, bytes))
    {
        std::fprintf(stderr, "illusion_icon: cannot read %s\n", std::string(input).c_str());
        return std::nullopt;
    }
    std::string error;
    std::optional<Image> image = decodeImage(bytes, true, &error);
    if (!image || image->format != ImageFormat::RGBA8Srgb)
    {
        std::fprintf(stderr, "illusion_icon: %s is not an 8-bit image%s%s\n", std::string(input).c_str(),
                     error.empty() ? "" : ": ", error.c_str());
        return std::nullopt;
    }
    if (image->width() != image->height())
    {
        std::fprintf(stderr, "illusion_icon: %s is %ux%u pixels; an icon must be square\n",
                     std::string(input).c_str(), image->width(), image->height());
        return std::nullopt;
    }
    return image;
}

int run(const std::vector<std::string>& arguments)
{
    if (arguments.size() < 3)
    {
        return usage();
    }
    const std::optional<Image> source = loadSource(arguments[0]);
    if (!source)
    {
        return 1;
    }
    for (usize i = 1; i < arguments.size(); ++i)
    {
        const std::string_view option = arguments[i];
        if (option == "--ico" && i + 1 < arguments.size())
        {
            std::vector<Image> images;
            for (const u32 size : kWindowsIconSizes)
            {
                images.push_back(resizeImage(*source, size, size));
            }
            if (!writeBytes(utf8Path(arguments[++i]), encodeIco(images)))
            {
                return 1;
            }
        }
        else if (option == "--png" && i + 2 < arguments.size())
        {
            const std::string_view number = arguments[i + 1];
            u32 size = 0;
            const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), size);
            if (error != std::errc{} || end != number.data() + number.size() || size == 0 || size > kMaxSize)
            {
                std::fprintf(stderr, "illusion_icon: --png needs a size of 1 to %u pixels\n", kMaxSize);
                return 2;
            }
            if (!writePng(*source, size, utf8Path(arguments[i + 2])))
            {
                return 1;
            }
            i += 2;
        }
        else if (option == "--iconset" && i + 1 < arguments.size())
        {
            const std::filesystem::path directory = utf8Path(arguments[++i]);
            constexpr std::array<u32, 5> kSizes{16, 32, 128, 256, 512};
            for (const u32 size : kSizes)
            {
                const std::string name = "icon_" + std::to_string(size) + "x" + std::to_string(size);
                if (!writePng(*source, size, directory / (name + ".png")) ||
                    !writePng(*source, size * 2, directory / (name + "@2x.png")))
                {
                    return 1;
                }
            }
        }
        else
        {
            return usage();
        }
    }
    return 0;
}

} // namespace

#if defined(_WIN32)
// Windows passes narrow arguments in the ANSI code page, which cannot hold every path; take the UTF-16
// arguments and convert them to UTF-8.
int wmain(int argc, wchar_t** argv)
{
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i)
    {
        const std::u8string text = std::filesystem::path(argv[i]).u8string();
        arguments.emplace_back(reinterpret_cast<const char*>(text.data()), text.size());
    }
    return run(arguments);
}
#else
int main(int argc, char** argv)
{
    return run(std::vector<std::string>(argv + 1, argv + argc));
}
#endif
