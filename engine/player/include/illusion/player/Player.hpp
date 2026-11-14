#pragma once

/// @file
/// The player: the executable side of an exported game (ADR 0055). runPlayer() opens the window, mounts the
/// packed data, adds the game's static modules and runs a GameSession until the game ends.
///
/// @code
/// ILL_DECLARE_STATIC_MODULE(tetris);
///
/// int main(int argc, char** argv)
/// {
///     static constexpr illusion::PlayerModuleEntry kModules[] = {illusionModuleEntry_tetris};
///     illusion::PlayerDesc desc;
///     desc.name = "Tetris";
///     desc.organization = "Example";
///     desc.version = "1.0.0";
///     desc.startupScene = "assets:/scenes/main.scene";
///     desc.modules = kModules;
///     return illusion::runPlayer(desc, argc, argv);
/// }
/// @endcode

#include <illusion/core/Base.hpp>

#include <memory>
#include <span>
#include <string_view>

namespace illusion
{

class StoreBackend;
struct ModuleApi;

extern "C"
{
    /// Entry point of a static module: illusionModuleEntry_<id> of ILL_MODULE().
    using PlayerModuleEntry = const ModuleApi* (*)();
}

/// Starts the store of a game built with one for the app @p appId (createSteamStore()); null when it does not
/// start, and the player then uses NullStoreService (ADR 0056).
using PlayerStoreFactory = std::unique_ptr<StoreBackend> (*)(u32 appId);

/// Process exit codes of runPlayer().
enum class PlayerExit : int
{
    Ok = 0,           ///< The game ran and ended.
    StartFailed = 1,  ///< No window, no Vulkan device, or a module or the startup scene failed to load.
    DataMissing = 2,  ///< An archive of the data directory is missing or damaged.
    BadArguments = 3, ///< The command line could not be read.
};

/// What a game is, fixed when it is exported.
struct PlayerDesc
{
    std::string_view name{};         ///< Product name: window title and user data directory. Required.
    std::string_view organization{}; ///< Parent of the user data directory. Required.
    std::string_view version{};      ///< Version of the game, written to the log.
    std::string_view startupScene{}; ///< Scene loaded first, e.g. "assets:/scenes/main.scene". Required.
    /// Static modules, added to the ModuleHost in this order.
    std::span<const PlayerModuleEntry> modules{};
    /// Directory of `assets.illpak`, `settings.illpak` and `cache.illpak`, relative to the executable
    /// (to `Contents/Resources` inside a macOS bundle).
    std::string_view dataDirectory = "data";
    /// The store the game was built with, started before the window opens; null for none.
    PlayerStoreFactory store = nullptr;
    u32 storeAppId = 0; ///< The game's id in that store (Steam's app id).
};

/// Runs the game described by @p desc and returns its exit code (a PlayerExit value). Call it once, from
/// main(). Mounts `assets`, `settings` and `cache` from the data directory and `user` (read-write) on
/// userDataDirectory(organization, name); logs to `user:/player.log`; shows a failed start in a native
/// message box unless `--no-dialogs` is given. The game's modules get the store of PlayerDesc::store, or a
/// NullStoreService with its cloud files in `user:/cloud`.
///
/// Command line: `--windowed`, `--borderless`, `--fullscreen` and `--size <width>x<height>` override the
/// window settings; `--data <directory>` overrides PlayerDesc::dataDirectory (relative to the executable
/// likewise); `--user-data <directory>` mounts `user` there instead of the user data directory (for tests
/// and portable installs); `--fixed-step` runs the
/// session deterministically; `--frames <count>` ends the game after that many frames and
/// `--screenshot <file.png>` writes the last one; `--no-dialogs` keeps failures to the log and stderr.
[[nodiscard]] int runPlayer(const PlayerDesc& desc, int argc, char** argv);

} // namespace illusion
