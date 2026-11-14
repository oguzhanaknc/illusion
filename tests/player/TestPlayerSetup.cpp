// The parts of runPlayer() that need no window: the command line, the window it asks for and the mounts of
// the packed data and the user directory.

#include "TestSupport.hpp"

#include <illusion/platform/Archive.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/runtime/ProjectSettings.hpp>

#include <doctest/doctest.h>

#include <PlayerSetup.hpp>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace illusion;
using namespace illusion::detail;

namespace
{

Expected<PlayerOptions, PlayerFailure> parse(std::initializer_list<std::string_view> arguments)
{
    const std::vector<std::string_view> list(arguments);
    return parsePlayerArguments(list);
}

// Writes an archive holding @p path with @p text.
void writeTextArchive(const std::filesystem::path& archive, std::string path, std::string_view text)
{
    ArchiveInput input;
    input.path = std::move(path);
    input.data.resize(text.size());
    std::memcpy(input.data.data(), text.data(), text.size());
    REQUIRE(writeArchive(archive, std::span(&input, 1)) == FileError::None);
}

// A data directory with the three archives of a game.
void writeGameData(const std::filesystem::path& directory)
{
    std::filesystem::create_directories(directory);
    writeTextArchive(directory / "assets.illpak", "scenes/main.scene", "{}");
    writeTextArchive(directory / "settings.illpak", "render.json", "{}");
    writeTextArchive(directory / "cache.illpak", "index.json", "{}");
}

PlayerDesc gameDesc()
{
    PlayerDesc desc;
    desc.name = "Test Game";
    desc.organization = "Test";
    desc.startupScene = "assets:/scenes/main.scene";
    return desc;
}

} // namespace

TEST_CASE("an empty player command line runs the game with its settings")
{
    const Expected<PlayerOptions, PlayerFailure> options = parse({});
    REQUIRE(options.hasValue());
    CHECK_FALSE(options->mode.has_value());
    CHECK_FALSE(options->size.has_value());
    CHECK_FALSE(options->dataDirectory.has_value());
    CHECK_FALSE(options->fixedStep);
    CHECK(options->frames == 0);
    CHECK(options->screenshot.empty());
    CHECK_FALSE(options->noDialogs);
}

TEST_CASE("the player command line reads every option")
{
    const Expected<PlayerOptions, PlayerFailure> options = parse(
        {"--fullscreen", "--size", "1920x1080", "--data", "packed", "--user-data", "saves", "--fixed-step",
         "--frames", "300", "--screenshot", "out/shot.png", "--no-dialogs", "-psn_0_12345"});
    REQUIRE(options.hasValue());
    CHECK(options->mode == WindowMode::Fullscreen);
    CHECK(options->size == UVec2(1920, 1080));
    CHECK(options->dataDirectory == std::filesystem::path("packed"));
    CHECK(options->userDirectory == std::filesystem::path("saves"));
    CHECK(options->fixedStep);
    CHECK(options->frames == 300);
    CHECK(options->screenshot == std::filesystem::path("out/shot.png"));
    CHECK(options->noDialogs);

    // The last window mode wins.
    CHECK(parse({"--borderless", "--windowed"})->mode == WindowMode::Windowed);
    CHECK(parse({"--windowed", "--borderless"})->mode == WindowMode::Borderless);
}

TEST_CASE("the player command line rejects unknown arguments and bad values")
{
    const auto rejects = [](std::initializer_list<std::string_view> arguments, std::string_view needle) {
        const Expected<PlayerOptions, PlayerFailure> options = parse(arguments);
        REQUIRE_FALSE(options.hasValue());
        CHECK(options.error().exit == PlayerExit::BadArguments);
        INFO(options.error().message);
        CHECK(options.error().message.find(needle) != std::string::npos);
    };
    rejects({"--speed"}, "unknown argument '--speed'");
    rejects({"game.scene"}, "unknown argument");
    rejects({"--size"}, "--size");
    rejects({"--size", "1280"}, "--size");
    rejects({"--size", "0x720"}, "--size");
    rejects({"--size", "1280x720x2"}, "--size");
    rejects({"--size", "99999x720"}, "--size");
    rejects({"--frames"}, "--frames");
    rejects({"--frames", "0"}, "--frames");
    rejects({"--frames", "-3"}, "--frames");
    rejects({"--frames", "12a"}, "--frames");
    rejects({"--screenshot"}, "--screenshot needs a path");
    rejects({"--data", ""}, "--data needs a path");
    rejects({"--user-data"}, "--user-data needs a path");

    const std::array<std::string_view, 2> withDialogsOff{"--bogus", "--no-dialogs"};
    CHECK(wantsNoDialogs(withDialogsOff));
    CHECK_FALSE(wantsNoDialogs(std::span<const std::string_view>()));
}

TEST_CASE("the player's window comes from the window settings with the command line's overrides")
{
    const PlayerDesc desc = gameDesc();
    WindowSettings settings;
    settings.width = 800;
    settings.height = 600;
    settings.mode = WindowMode::Borderless;
    settings.resizable = false;

    const WindowDesc plain = playerWindowDesc(desc, settings, PlayerOptions{});
    CHECK(plain.title == "Test Game");
    CHECK(plain.width == 800);
    CHECK(plain.height == 600);
    CHECK(plain.mode == WindowMode::Borderless);
    CHECK_FALSE(plain.resizable);
    CHECK_FALSE(plain.scaleToMonitor); // sizes are pixels

    PlayerOptions options;
    options.mode = WindowMode::Windowed;
    options.size = UVec2(480, 270);
    const WindowDesc overridden = playerWindowDesc(desc, settings, options);
    CHECK(overridden.width == 480);
    CHECK(overridden.height == 270);
    CHECK(overridden.mode == WindowMode::Windowed);
}

TEST_CASE("the player finds its data next to the executable, in a macOS bundle or where --data says")
{
    PlayerDesc desc = gameDesc();
    const std::filesystem::path root = std::filesystem::path("games") / "Test";
    CHECK(playerDataDirectory(desc, PlayerOptions{}, root) == root / "data");
    desc.dataDirectory = "content";
    CHECK(playerDataDirectory(desc, PlayerOptions{}, root) == root / "content");
    const std::filesystem::path bundle = std::filesystem::path("Test.app") / "Contents";
    CHECK(playerDataDirectory(desc, PlayerOptions{}, bundle / "MacOS") == bundle / "Resources" / "content");

    PlayerOptions options;
    options.dataDirectory = std::filesystem::path("packed");
    CHECK(playerDataDirectory(desc, options, root) == root / "packed");
    const std::filesystem::path absolute = std::filesystem::temp_directory_path() / "packed";
    options.dataDirectory = absolute;
    CHECK(playerDataDirectory(desc, options, root) == absolute);
}

TEST_CASE("the player mounts assets, settings and cache from the archives of its data directory")
{
    test::TempDirectory directory;
    writeGameData(directory.path / "data");
    VirtualFileSystem files;
    const Expected<void, PlayerFailure> mounted = mountPlayerData(files, directory.path / "data");
    REQUIRE_MESSAGE(mounted.hasValue(), (mounted ? std::string() : mounted.error().message));
    CHECK(files.exists("assets:/scenes/main.scene"));
    CHECK(files.exists("settings:/render.json"));
    CHECK(files.exists("cache:/index.json"));
    // Archives are read-only.
    CHECK(files.writeText("assets:/new.txt", "x") != FileError::None);
}

TEST_CASE("a missing or damaged archive of the data directory ends the player with DataMissing")
{
    test::TempDirectory directory;
    const std::filesystem::path data = directory.path / "data";
    writeGameData(data);

    SUBCASE("missing")
    {
        std::filesystem::remove(data / "cache.illpak");
        VirtualFileSystem files;
        const Expected<void, PlayerFailure> mounted = mountPlayerData(files, data);
        REQUIRE_FALSE(mounted.hasValue());
        CHECK(mounted.error().exit == PlayerExit::DataMissing);
        CHECK(mounted.error().message.find("cache.illpak") != std::string::npos);
        CHECK(mounted.error().message.find("missing") != std::string::npos);
    }
    SUBCASE("damaged")
    {
        {
            std::ofstream broken(data / "settings.illpak", std::ios::binary | std::ios::trunc);
            broken << "not an archive";
        }
        VirtualFileSystem files;
        const Expected<void, PlayerFailure> mounted = mountPlayerData(files, data);
        REQUIRE_FALSE(mounted.hasValue());
        CHECK(mounted.error().exit == PlayerExit::DataMissing);
        CHECK(mounted.error().message.find("settings.illpak") != std::string::npos);
        CHECK(mounted.error().message.find("damaged") != std::string::npos);
    }
    SUBCASE("no data directory")
    {
        VirtualFileSystem files;
        const Expected<void, PlayerFailure> mounted = mountPlayerData(files, directory.path / "elsewhere");
        REQUIRE_FALSE(mounted.hasValue());
        CHECK(mounted.error().exit == PlayerExit::DataMissing);
        CHECK(mounted.error().message.find("assets.illpak") != std::string::npos);
    }
}

TEST_CASE("the player creates the user directory and mounts it read-write")
{
    test::TempDirectory directory;
    const std::filesystem::path user = directory.path / "Org" / "Game";
    VirtualFileSystem files;
    const Expected<void, PlayerFailure> mounted = mountUserData(files, user);
    REQUIRE(mounted.hasValue());
    CHECK(std::filesystem::is_directory(user));
    REQUIRE(files.writeText("user:/player.log", "started") == FileError::None);
    CHECK(std::filesystem::exists(user / "player.log"));
}

TEST_CASE("every problem of the player has a localized message that names its detail")
{
    constexpr std::array kProblems{
        PlayerProblem::BadArguments, PlayerProblem::DataMissing,    PlayerProblem::DataDamaged,
        PlayerProblem::UserData,     PlayerProblem::NoWindowSystem, PlayerProblem::WindowFailed,
        PlayerProblem::NoGraphics,   PlayerProblem::GraphicsFailed, PlayerProblem::ModuleFailed,
        PlayerProblem::GameFailed,
    };
    for (const PlayerProblem problem : kProblems)
    {
        const PlayerFailure failure{PlayerExit::StartFailed, "english log text", problem, "the-detail"};
        const std::string text = failureText(failure);
        CHECK_FALSE(text.empty());
        CHECK(text != failure.message);
        CHECK(text.find('{') == std::string::npos);
    }
    const PlayerFailure data{PlayerExit::DataMissing, "log", PlayerProblem::DataMissing,
                             "data/assets.illpak"};
    CHECK(failureText(data).find("data/assets.illpak") != std::string::npos);
}

TEST_CASE("every failure the player produces maps to a dialog text for its exit code")
{
    test::TempDirectory directory;
    {
        const Expected<PlayerOptions, PlayerFailure> options = parse({"--bogus"});
        REQUIRE_FALSE(options.hasValue());
        CHECK(options.error().exit == PlayerExit::BadArguments);
        CHECK_FALSE(failureText(options.error()).empty());
    }
    {
        VirtualFileSystem files;
        const Expected<void, PlayerFailure> mounted = mountPlayerData(files, directory.path / "elsewhere");
        REQUIRE_FALSE(mounted.hasValue());
        CHECK(mounted.error().exit == PlayerExit::DataMissing);
        CHECK(failureText(mounted.error()).find("elsewhere") != std::string::npos);
    }
    {
        const std::filesystem::path blocker = directory.path / "blocker";
        std::ofstream(blocker) << "a file";
        VirtualFileSystem files;
        const Expected<void, PlayerFailure> mounted = mountUserData(files, blocker);
        REQUIRE_FALSE(mounted.hasValue());
        CHECK(mounted.error().exit == PlayerExit::StartFailed);
        CHECK_FALSE(failureText(mounted.error()).empty());
    }
}
