#include "PlayerSetup.hpp"

#include <illusion/platform/FileSystem.hpp>
#include <illusion/player/PlayerStrings.hpp>
#include <illusion/runtime/ProjectSettings.hpp>
#include <illusion/text/Localization.hpp>
#include <illusion/text/TextOutput.hpp>

#include <array>
#include <charconv>
#include <format>
#include <system_error>

namespace illusion::detail
{
namespace
{

constexpr u32 kMaxWindowSize = 16384;

PlayerFailure badArgument(std::string message)
{
    return PlayerFailure{PlayerExit::BadArguments, message, PlayerProblem::BadArguments, message};
}

template <class T>
std::optional<T> parseNumber(std::string_view text) noexcept
{
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
    {
        return std::nullopt;
    }
    return value;
}

std::optional<UVec2> parseSize(std::string_view text) noexcept
{
    const usize x = text.find('x');
    if (x == std::string_view::npos)
    {
        return std::nullopt;
    }
    const std::optional<u32> width = parseNumber<u32>(text.substr(0, x));
    const std::optional<u32> height = parseNumber<u32>(text.substr(x + 1));
    if (!width || !height || *width == 0 || *height == 0 || *width > kMaxWindowSize ||
        *height > kMaxWindowSize)
    {
        return std::nullopt;
    }
    return UVec2(*width, *height);
}

} // namespace

std::string pathText(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

Expected<PlayerOptions, PlayerFailure> parsePlayerArguments(std::span<const std::string_view> arguments)
{
    PlayerOptions options;
    for (usize i = 0; i < arguments.size(); ++i)
    {
        const std::string_view argument = arguments[i];
        const auto value = [&]() -> std::optional<std::string_view> {
            if (i + 1 < arguments.size())
            {
                return arguments[++i];
            }
            return std::nullopt;
        };
        if (argument == "--windowed")
        {
            options.mode = WindowMode::Windowed;
        }
        else if (argument == "--borderless")
        {
            options.mode = WindowMode::Borderless;
        }
        else if (argument == "--fullscreen")
        {
            options.mode = WindowMode::Fullscreen;
        }
        else if (argument == "--fixed-step")
        {
            options.fixedStep = true;
        }
        else if (argument == "--no-dialogs")
        {
            options.noDialogs = true;
        }
        else if (argument == "--size")
        {
            const std::optional<std::string_view> text = value();
            const std::optional<UVec2> size = text ? parseSize(*text) : std::nullopt;
            if (!size)
            {
                return unexpected(badArgument(std::format(
                    "--size needs <width>x<height> between 1 and {}, e.g. --size 1280x720", kMaxWindowSize)));
            }
            options.size = size;
        }
        else if (argument == "--frames")
        {
            const std::optional<std::string_view> text = value();
            const std::optional<u64> frames = text ? parseNumber<u64>(*text) : std::nullopt;
            if (!frames || *frames == 0)
            {
                return unexpected(badArgument("--frames needs a positive number of frames"));
            }
            options.frames = *frames;
        }
        else if (argument == "--screenshot" || argument == "--data" || argument == "--user-data")
        {
            const std::optional<std::string_view> text = value();
            if (!text || text->empty())
            {
                return unexpected(badArgument(std::format("{} needs a path", argument)));
            }
            if (argument == "--screenshot")
            {
                options.screenshot = utf8Path(*text);
            }
            else if (argument == "--data")
            {
                options.dataDirectory = utf8Path(*text);
            }
            else
            {
                options.userDirectory = utf8Path(*text);
            }
        }
        else if (!argument.starts_with("-psn_"))
        {
            return unexpected(badArgument(std::format("unknown argument '{}'", argument)));
        }
    }
    return options;
}

bool wantsNoDialogs(std::span<const std::string_view> arguments) noexcept
{
    for (const std::string_view argument : arguments)
    {
        if (argument == "--no-dialogs")
        {
            return true;
        }
    }
    return false;
}

std::filesystem::path playerDataDirectory(const PlayerDesc& desc, const PlayerOptions& options,
                                          const std::filesystem::path& executableDirectory)
{
    std::filesystem::path base = executableDirectory;
    if (base.filename() == "MacOS" && base.parent_path().filename() == "Contents")
    {
        base = base.parent_path() / "Resources";
    }
    // An absolute directory replaces the base.
    return base / (options.dataDirectory ? *options.dataDirectory : utf8Path(desc.dataDirectory));
}

Expected<void, PlayerFailure> mountPlayerData(VirtualFileSystem& files,
                                              const std::filesystem::path& dataDirectory)
{
    constexpr std::array<std::string_view, 3> kMounts{"assets", "settings", "cache"};
    for (const std::string_view mount : kMounts)
    {
        const std::filesystem::path archive = dataDirectory / utf8Path(std::format("{}.illpak", mount));
        const FileError error = files.mountArchive(mount, archive);
        if (error == FileError::None)
        {
            continue;
        }
        const std::string path = pathText(archive);
        if (error == FileError::NotFound)
        {
            return unexpected(PlayerFailure{PlayerExit::DataMissing,
                                            std::format("the game data is missing: no {}", path),
                                            PlayerProblem::DataMissing, path});
        }
        return unexpected(PlayerFailure{
            PlayerExit::DataMissing,
            std::format("the game data is damaged: cannot read {} ({})", path, fileErrorName(error)),
            PlayerProblem::DataDamaged, path});
    }
    return {};
}

Expected<void, PlayerFailure> mountUserData(VirtualFileSystem& files,
                                            const std::filesystem::path& userDirectory)
{
    std::error_code code;
    std::filesystem::create_directories(userDirectory, code);
    if (const FileError error = files.mount("user", userDirectory, MountAccess::ReadWrite);
        error != FileError::None)
    {
        return unexpected(PlayerFailure{PlayerExit::StartFailed,
                                        std::format("cannot use the user data directory {} ({})",
                                                    pathText(userDirectory), fileErrorName(error)),
                                        PlayerProblem::UserData, pathText(userDirectory)});
    }
    return {};
}

std::string failureText(const PlayerFailure& failure)
{
    text::TextKey key = player::strings::kFailureGameFailed;
    switch (failure.problem)
    {
    case PlayerProblem::BadArguments:
        key = player::strings::kFailureBadArguments;
        break;
    case PlayerProblem::DataMissing:
        key = player::strings::kFailureDataMissing;
        break;
    case PlayerProblem::DataDamaged:
        key = player::strings::kFailureDataDamaged;
        break;
    case PlayerProblem::UserData:
        key = player::strings::kFailureUserData;
        break;
    case PlayerProblem::NoWindowSystem:
        key = player::strings::kFailureNoWindowSystem;
        break;
    case PlayerProblem::WindowFailed:
        key = player::strings::kFailureWindowFailed;
        break;
    case PlayerProblem::NoGraphics:
        key = player::strings::kFailureNoGraphics;
        break;
    case PlayerProblem::GraphicsFailed:
        key = player::strings::kFailureGraphicsFailed;
        break;
    case PlayerProblem::ModuleFailed:
        key = player::strings::kFailureModuleFailed;
        break;
    case PlayerProblem::GameFailed:
        break;
    }
    text::Localization localization;
    localization.addTable(player::strings::kTable);
    std::string out;
    text::TextOutput output(out);
    const Expected<void, text::MessageError> formatted =
        localization.format(key, {text::MessageArgument("detail", failure.detail)}, output);
    return formatted ? out : failure.message;
}

WindowDesc playerWindowDesc(const PlayerDesc& desc, const WindowSettings& settings,
                            const PlayerOptions& options)
{
    WindowDesc window;
    window.title = std::string(desc.name);
    window.width = options.size ? options.size->x : settings.width;
    window.height = options.size ? options.size->y : settings.height;
    window.mode = options.mode.value_or(settings.mode);
    window.resizable = settings.resizable;
    window.scaleToMonitor = false;
    return window;
}

} // namespace illusion::detail
