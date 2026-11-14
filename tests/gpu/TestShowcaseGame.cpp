// The exported showcase (samples/showcase built by illusion_add_game() as showcase_game) on its packed data:
// after 300 fixed steps its screenshot matches the golden image of the editor's Game view after 300 fixed
// steps of Play mode (editor/tests/gpu/TestShowcaseGpu.cpp), since both run the same GameSession (ADR 0042).

#include <illusion/assets/Image.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/platform/Process.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

using namespace illusion;

namespace
{

std::vector<std::byte> readBytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::vector<std::byte> result(bytes.size());
    std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
}

std::string text(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

} // namespace

TEST_CASE("the exported showcase renders its scene like the editor's Play mode after 300 fixed steps")
{
    const std::filesystem::path screenshot = utf8Path(SHOWCASE_GAME_CAPTURE);
    const std::filesystem::path user = screenshot.parent_path() / "showcase-user";
    std::error_code code;
    std::filesystem::remove(screenshot, code);
    std::filesystem::remove_all(user, code);

    ProcessOptions options;
    options.executable = utf8Path(SHOWCASE_GAME);
    options.arguments = {"--windowed",  "--size",   "480x270",      "--fixed-step",
                         "--frames",    "300",      "--screenshot", text(screenshot),
                         "--user-data", text(user), "--no-dialogs"};
    options.captureOutput = false;
    options.captureError = false;
    Expected<Process, std::string> process = Process::spawn(options);
    REQUIRE_MESSAGE(process.hasValue(), (process ? std::string() : process.error()));
    const std::optional<i32> exit = process->wait(std::chrono::minutes(10));
    REQUIRE(exit.has_value());
    CHECK(*exit == 0);
    REQUIRE(std::filesystem::exists(screenshot));
    CHECK(std::filesystem::exists(user / "player.log"));

    const std::optional<Image> actual = decodeImage(readBytes(screenshot), true);
    const std::optional<Image> golden = decodeImage(readBytes(utf8Path(SHOWCASE_GAME_GOLDEN)), true);
    REQUIRE(actual.has_value());
    REQUIRE(golden.has_value());
    REQUIRE(actual->width() == golden->width());
    REQUIRE(actual->height() == golden->height());

    // The tolerance of the editor's golden images (editor/tests/gpu/GoldenSupport.hpp).
    const std::span<const std::byte> a = actual->mip(0);
    const std::span<const std::byte> b = golden->mip(0);
    u64 total = 0;
    u64 different = 0;
    for (usize i = 0; i < a.size(); i += 4)
    {
        int worst = 0;
        for (usize c = 0; c < 4; ++c)
        {
            const int d = std::abs(static_cast<int>(a[i + c]) - static_cast<int>(b[i + c]));
            total += static_cast<u64>(d);
            worst = std::max(worst, d);
        }
        different += worst > 24 ? 1u : 0u;
    }
    const f64 meanAbsolute = static_cast<f64>(total) / static_cast<f64>(a.size());
    const f64 differentFraction = static_cast<f64>(different) / static_cast<f64>(a.size() / 4);
    INFO("mean difference " << meanAbsolute << ", pixels off " << differentFraction * 100.0 << "% (capture "
                            << text(screenshot) << ")");
    CHECK(meanAbsolute < 1.0);
    CHECK(differentFraction < 0.003);
}
