#pragma once

// The testable parts of runPlayer(): the command line, the data and user mounts and the window description.

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/platform/Window.hpp>
#include <illusion/player/Player.hpp>

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace illusion
{
class VirtualFileSystem;
struct WindowSettings;
} // namespace illusion

namespace illusion::detail
{

// What the command line asks for (see runPlayer()).
struct PlayerOptions
{
    std::optional<WindowMode> mode;                     // --windowed, --borderless, --fullscreen
    std::optional<UVec2> size;                          // --size <width>x<height>
    std::optional<std::filesystem::path> dataDirectory; // --data, relative to the executable
    std::optional<std::filesystem::path> userDirectory; // --user-data
    bool fixedStep = false;                             // --fixed-step
    u64 frames = 0;                                     // --frames; 0 runs until the game ends
    std::filesystem::path screenshot;                   // --screenshot; empty for none
    bool noDialogs = false;                             // --no-dialogs
};

// What the failure dialog tells the player; each has a text in the player's string table.
enum class PlayerProblem
{
    BadArguments,
    DataMissing,
    DataDamaged,
    UserData,
    NoWindowSystem,
    WindowFailed,
    NoGraphics,
    GraphicsFailed,
    ModuleFailed,
    GameFailed,
};

// Why the player cannot start, with the exit code it ends with. `message` is English, for the log; the
// dialog shows failureText().
struct PlayerFailure
{
    PlayerExit exit = PlayerExit::StartFailed;
    std::string message;
    PlayerProblem problem = PlayerProblem::GameFailed;
    std::string detail; // The file or cause that the text of the problem names; may be empty.
};

// The text of @p failure for the player, in the active language (English for now).
[[nodiscard]] std::string failureText(const PlayerFailure& failure);

// @p path as UTF-8 text, for messages.
[[nodiscard]] std::string pathText(const std::filesystem::path& path);

// Reads @p arguments (the command line without the program name). macOS's `-psn_...` argument is ignored.
[[nodiscard]] Expected<PlayerOptions, PlayerFailure>
parsePlayerArguments(std::span<const std::string_view> arguments);

// True if @p arguments contain `--no-dialogs`, so that a command line that cannot be read shows no dialog.
[[nodiscard]] bool wantsNoDialogs(std::span<const std::string_view> arguments) noexcept;

// The directory of the archives: --data or PlayerDesc::dataDirectory, relative to @p executableDirectory (to
// Contents/Resources when it is the Contents/MacOS directory of a macOS bundle).
[[nodiscard]] std::filesystem::path playerDataDirectory(const PlayerDesc& desc, const PlayerOptions& options,
                                                        const std::filesystem::path& executableDirectory);

// Mounts `assets`, `settings` and `cache` from the archives `<name>.illpak` of @p dataDirectory. A missing or
// damaged archive fails with PlayerExit::DataMissing, naming it.
[[nodiscard]] Expected<void, PlayerFailure> mountPlayerData(VirtualFileSystem& files,
                                                            const std::filesystem::path& dataDirectory);

// Creates @p userDirectory and mounts it read-write as `user`.
[[nodiscard]] Expected<void, PlayerFailure> mountUserData(VirtualFileSystem& files,
                                                          const std::filesystem::path& userDirectory);

// The window of @p settings with the command line's overrides, titled with the game's name, sized in pixels.
[[nodiscard]] WindowDesc playerWindowDesc(const PlayerDesc& desc, const WindowSettings& settings,
                                          const PlayerOptions& options);

} // namespace illusion::detail
