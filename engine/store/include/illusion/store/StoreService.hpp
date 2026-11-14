#pragma once

/// @file
/// The store part of the scripting API (ADR 0056): achievements, stats, cloud files, the overlay and glyphs
/// of the store the game runs in, or of no store at all.
///
/// @code
/// if (illusion::StoreService* store = context.service<illusion::StoreService>())
/// {
///     if (!store->overlayActive())
///     {
///         static_cast<void>(store->unlockAchievement("FIRST_WIN"));
///         static_cast<void>(store->storeStats());
///     }
/// }
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/input/Gamepad.hpp>

#include <optional>
#include <span>
#include <string_view>

namespace illusion
{

/// Why a store call failed.
enum class StoreError : u8
{
    Unavailable,   ///< The store cannot do it now: not running, no user, or no storage.
    InvalidName,   ///< The name is not valid (isValidStoreName()).
    InvalidValue,  ///< A value is out of range, e.g. a progress with max 0 or a float that is not finite.
    NotFound,      ///< No achievement, stat or cloud file has that name.
    TypeMismatch,  ///< The stat holds the other type (an int read as a float, or the reverse).
    TooLarge,      ///< The cloud file is larger than kMaxCloudFileBytes.
    QuotaExceeded, ///< The cloud storage has no room for the file.
    IoError,       ///< Reading or writing failed.
};

/// Stable name of @p error, e.g. "QuotaExceeded".
[[nodiscard]] constexpr std::string_view storeErrorName(StoreError error) noexcept
{
    switch (error)
    {
    case StoreError::Unavailable:
        return "Unavailable";
    case StoreError::InvalidName:
        return "InvalidName";
    case StoreError::InvalidValue:
        return "InvalidValue";
    case StoreError::NotFound:
        return "NotFound";
    case StoreError::TypeMismatch:
        return "TypeMismatch";
    case StoreError::TooLarge:
        return "TooLarge";
    case StoreError::QuotaExceeded:
        return "QuotaExceeded";
    case StoreError::IoError:
        return "IoError";
    }
    return "IoError";
}

/// Longest achievement, stat or cloud file name, in bytes.
inline constexpr usize kMaxStoreNameLength = 64;

/// Largest cloud file, in bytes (100 MiB).
inline constexpr u64 kMaxCloudFileBytes = 100ull * 1024 * 1024;

/// True if @p name can name an achievement, a stat or a cloud file: 1 to kMaxStoreNameLength ASCII letters,
/// digits, '_', '-' and '.', not starting or ending with '.'.
[[nodiscard]] constexpr bool isValidStoreName(std::string_view name) noexcept
{
    if (name.empty() || name.size() > kMaxStoreNameLength || name.front() == '.' || name.back() == '.')
    {
        return false;
    }
    for (const char c : name)
    {
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool digit = c >= '0' && c <= '9';
        if (!letter && !digit && c != '_' && c != '-' && c != '.')
        {
            return false;
        }
    }
    return true;
}

/// Progress towards an achievement; reaching max unlocks it.
struct AchievementProgress
{
    u32 current = 0; ///< Progress so far, at most max.
    u32 max = 0;     ///< Progress that unlocks the achievement; 0 when no progress was set.

    /// Equal when both fields are.
    friend constexpr bool operator==(const AchievementProgress&, const AchievementProgress&) = default;
};

/// Size of the cloud storage, in bytes.
struct StoreQuota
{
    u64 totalBytes = 0;     ///< What the game may store in all.
    u64 availableBytes = 0; ///< What is still free.
};

/// A page of the store's overlay.
enum class StoreOverlayPage : u8
{
    Friends,      ///< The player's friends.
    Community,    ///< The game's community hub.
    Achievements, ///< The game's achievements.
    Stats,        ///< The game's stats.
    Settings,     ///< The overlay's settings.
};

/// A control of a gamepad, for glyphPath(): a button, or an axis when button is GamepadButton::Count.
struct GamepadOrigin
{
    GamepadType type = GamepadType::Generic;     ///< Controller model whose symbol is wanted.
    GamepadButton button = GamepadButton::Count; ///< The button, or Count for an axis.
    GamepadAxis axis = GamepadAxis::Count;       ///< The axis when button is Count.
};

/// The store the game runs in (Steam, or none), implemented by the host (NullStoreService, SteamStoreService)
/// and found by modules with `context.service<StoreService>()`. Results are returned or polled, never called
/// back. String views and span contents it returns stay valid until the store's next update or the next call
/// of the same function. Adding a function changes the module API version.
/// @ingroup scripting_api
class StoreService : NonCopyable
{
public:
    /// True when a store runs and a user is signed in.
    [[nodiscard]] virtual bool available() const = 0;
    /// Stable name of the store: "none" or "steam".
    [[nodiscard]] virtual std::string_view storeName() const = 0;
    /// Display name of the signed-in user; empty without one.
    [[nodiscard]] virtual std::string_view userName() const = 0;
    /// Store id of the signed-in user; 0 without one.
    [[nodiscard]] virtual u64 userId() const = 0;

    /// Unlocks the achievement @p name; unlocking an unlocked achievement succeeds.
    virtual Expected<void, StoreError> unlockAchievement(std::string_view name) = 0;
    /// Locks the achievement @p name again and clears its progress (for testing a game).
    virtual Expected<void, StoreError> clearAchievement(std::string_view name) = 0;
    /// Whether the achievement @p name is unlocked.
    [[nodiscard]] virtual Expected<bool, StoreError> isAchievementUnlocked(std::string_view name) const = 0;
    /// Records progress towards @p name; max must be positive and current is clamped to it. Reaching max
    /// unlocks the achievement; an unlocked achievement keeps its state.
    virtual Expected<void, StoreError> setAchievementProgress(std::string_view name,
                                                              AchievementProgress progress) = 0;
    /// The progress of @p name: {max, max} once unlocked, {0, 0} before any progress.
    [[nodiscard]] virtual Expected<AchievementProgress, StoreError>
    achievementProgress(std::string_view name) const = 0;

    /// Sets the integer stat @p name.
    virtual Expected<void, StoreError> setStatInt(std::string_view name, i32 value) = 0;
    /// The integer stat @p name.
    [[nodiscard]] virtual Expected<i32, StoreError> statInt(std::string_view name) const = 0;
    /// Sets the float stat @p name; @p value must be finite.
    virtual Expected<void, StoreError> setStatFloat(std::string_view name, f32 value) = 0;
    /// The float stat @p name.
    [[nodiscard]] virtual Expected<f32, StoreError> statFloat(std::string_view name) const = 0;
    /// Sends the changed stats and achievements to the store.
    virtual Expected<void, StoreError> storeStats() = 0;

    /// Writes @p data as the cloud file @p name, replacing it.
    virtual Expected<void, StoreError> writeCloudFile(std::string_view name,
                                                      std::span<const std::byte> data) = 0;
    /// Copies the start of the cloud file @p name into @p out and returns the file's size; it was read whole
    /// when the size is at most `out.size()`. An empty @p out asks only for the size.
    virtual Expected<u64, StoreError> readCloudFile(std::string_view name, std::span<std::byte> out) = 0;
    /// Removes the cloud file @p name.
    virtual Expected<void, StoreError> removeCloudFile(std::string_view name) = 0;
    /// Writes the names of the cloud files, sorted, into @p out and returns how many there are.
    virtual u32 listCloudFiles(std::span<std::string_view> out) = 0;
    /// True if the cloud file @p name exists.
    [[nodiscard]] virtual bool cloudFileExists(std::string_view name) const = 0;
    /// Size and free space of the cloud storage.
    [[nodiscard]] virtual Expected<StoreQuota, StoreError> cloudQuota() const = 0;

    /// True while the store's overlay covers the game; poll it each frame (games usually pause meanwhile).
    [[nodiscard]] virtual bool overlayActive() const = 0;
    /// Asks the store to open its overlay at @p page; false when it has none.
    virtual bool openOverlay(StoreOverlayPage page) = 0;

    /// Path of an image of @p origin in the store's style, or nothing.
    [[nodiscard]] virtual std::optional<std::string_view> glyphPath(GamepadOrigin origin) const = 0;

protected:
    StoreService() = default;
    virtual ~StoreService() = default;
};

} // namespace illusion
