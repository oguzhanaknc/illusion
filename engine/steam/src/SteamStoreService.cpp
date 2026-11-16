// StoreService over the Steamworks API (ADR 0056). This file is the only place that includes Steam headers.
// It is compiled only with a user-supplied Steamworks SDK (1.61 or newer, which requests the current stats
// during SteamAPI_Init()), so neither the engine's CI nor its tests build it.

#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/steam/SteamStoreService.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <steam/steam_api.h>
#include <string>
#include <vector>

namespace illusion
{
namespace
{

// A validated store name with the terminating zero the Steamworks API needs.
class SteamName
{
public:
    explicit SteamName(std::string_view name) noexcept
        : m_valid(isValidStoreName(name))
    {
        if (m_valid)
        {
            std::copy(name.begin(), name.end(), m_text.begin());
            m_text[name.size()] = '\0';
        }
    }

    [[nodiscard]] bool valid() const noexcept { return m_valid; }
    [[nodiscard]] const char* c_str() const noexcept { return m_text.data(); }

private:
    std::array<char, kMaxStoreNameLength + 1> m_text{};
    bool m_valid = false;
};

const char* overlayDialog(StoreOverlayPage page) noexcept
{
    switch (page)
    {
    case StoreOverlayPage::Friends:
        return "friends";
    case StoreOverlayPage::Community:
        return "community";
    case StoreOverlayPage::Achievements:
        return "achievements";
    case StoreOverlayPage::Stats:
        return "stats";
    case StoreOverlayPage::Settings:
        return "settings";
    }
    return "friends";
}

} // namespace

struct SteamStoreService::Impl
{
    // Registers with the Steamworks API on construction, so an Impl is made only after SteamAPI_Init().
    STEAM_CALLBACK(Impl, onOverlay, GameOverlayActivated_t);

    bool overlay = false;
    std::string user;
    u64 id = 0;
    std::map<std::string, AchievementProgress, std::less<>> progress; // what the game set in this run
    std::vector<std::string> listed;                                  // names of the last listCloudFiles()
};

void SteamStoreService::Impl::onOverlay(GameOverlayActivated_t* event)
{
    overlay = event->m_bActive != 0;
}

std::unique_ptr<StoreBackend> createSteamStore(u32 appId)
{
    if (!SteamAPI_Init())
    {
        ILL_LOG_WARN("Steam",
                     "the Steamworks API did not start (Steam is not running or the app is not owned)");
        return nullptr;
    }
    if (SteamUtils() != nullptr && SteamUtils()->GetAppID() != appId)
    {
        ILL_LOG_WARN("Steam", "Steam runs the app {}, the game was exported for {}", SteamUtils()->GetAppID(),
                     appId);
    }
    auto impl = std::make_unique<SteamStoreService::Impl>();
    if (SteamFriends() != nullptr)
    {
        impl->user = SteamFriends()->GetPersonaName();
    }
    if (SteamUser() != nullptr)
    {
        impl->id = SteamUser()->GetSteamID().ConvertToUint64();
    }
    ILL_LOG_INFO("Steam", "the Steamworks API runs for the app {}", appId);
    return std::unique_ptr<StoreBackend>(new SteamStoreService(std::move(impl)));
}

SteamStoreService::SteamStoreService(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}

SteamStoreService::~SteamStoreService()
{
    m_impl.reset(); // unregisters the callback before the API goes
    SteamAPI_Shutdown();
}

void SteamStoreService::update()
{
    ILL_PROFILE_SCOPE();
    SteamAPI_RunCallbacks();
}

bool SteamStoreService::cloudIsRemote() const noexcept
{
    return true;
}

bool SteamStoreService::available() const
{
    return SteamUser() != nullptr && SteamUser()->BLoggedOn();
}

std::string_view SteamStoreService::storeName() const
{
    return "steam";
}

std::string_view SteamStoreService::userName() const
{
    return m_impl->user;
}

u64 SteamStoreService::userId() const
{
    return m_impl->id;
}

Expected<void, StoreError> SteamStoreService::unlockAchievement(std::string_view name)
{
    const SteamName steam(name);
    if (!steam.valid())
    {
        return unexpected(StoreError::InvalidName);
    }
    if (SteamUserStats() == nullptr)
    {
        return unexpected(StoreError::Unavailable);
    }
    if (!SteamUserStats()->SetAchievement(steam.c_str()))
    {
        return unexpected(StoreError::NotFound);
    }
    return {};
}

Expected<void, StoreError> SteamStoreService::clearAchievement(std::string_view name)
{
    const SteamName steam(name);
    if (!steam.valid())
    {
        return unexpected(StoreError::InvalidName);
    }
    if (SteamUserStats() == nullptr)
    {
        return unexpected(StoreError::Unavailable);
    }
    if (!SteamUserStats()->ClearAchievement(steam.c_str()))
    {
        return unexpected(StoreError::NotFound);
    }
    if (const auto it = m_impl->progress.find(name); it != m_impl->progress.end())
    {
        m_impl->progress.erase(it);
    }
    return {};
}

Expected<bool, StoreError> SteamStoreService::isAchievementUnlocked(std::string_view name) const
{
    const SteamName steam(name);
    if (!steam.valid())
    {
        return unexpected(StoreError::InvalidName);
    }
    if (SteamUserStats() == nullptr)
    {
        return unexpected(StoreError::Unavailable);
    }
    bool achieved = false;
    if (!SteamUserStats()->GetAchievement(steam.c_str(), &achieved))
    {
        return unexpected(StoreError::NotFound);
    }
    return achieved;
}

Expected<void, StoreError> SteamStoreService::setAchievementProgress(std::string_view name,
                                                                     AchievementProgress progress)
{
    const Expected<bool, StoreError> unlocked = isAchievementUnlocked(name);
    if (!unlocked)
    {
        return unexpected(unlocked.error());
    }
    if (progress.max == 0)
    {
        return unexpected(StoreError::InvalidValue);
    }
    if (*unlocked)
    {
        return {};
    }
    const SteamName steam(name);
    progress.current = std::min(progress.current, progress.max);
    auto it = m_impl->progress.find(name);
    if (it == m_impl->progress.end())
    {
        it = m_impl->progress.emplace(std::string(name), progress).first;
    }
    it->second = progress;
    if (progress.current == progress.max)
    {
        static_cast<void>(SteamUserStats()->SetAchievement(steam.c_str()));
    }
    else
    {
        static_cast<void>(
            SteamUserStats()->IndicateAchievementProgress(steam.c_str(), progress.current, progress.max));
    }
    return {};
}

Expected<AchievementProgress, StoreError> SteamStoreService::achievementProgress(std::string_view name) const
{
    const Expected<bool, StoreError> unlocked = isAchievementUnlocked(name);
    if (!unlocked)
    {
        return unexpected(unlocked.error());
    }
    const auto it = m_impl->progress.find(name);
    const AchievementProgress set = it != m_impl->progress.end() ? it->second : AchievementProgress{};
    if (*unlocked)
    {
        const u32 max = std::max(set.max, 1u);
        return AchievementProgress{max, max};
    }
    return set;
}

Expected<void, StoreError> SteamStoreService::setStatInt(std::string_view name, i32 value)
{
    const SteamName steam(name);
    if (!steam.valid())
    {
        return unexpected(StoreError::InvalidName);
    }
    if (SteamUserStats() == nullptr)
    {
        return unexpected(StoreError::Unavailable);
    }
    if (!SteamUserStats()->SetStat(steam.c_str(), static_cast<int32>(value)))
    {
        float other = 0.0f;
        return unexpected(SteamUserStats()->GetStat(steam.c_str(), &other) ? StoreError::TypeMismatch
                                                                           : StoreError::NotFound);
    }
    return {};
}

Expected<i32, StoreError> SteamStoreService::statInt(std::string_view name) const
{
    const SteamName steam(name);
    if (!steam.valid())
    {
        return unexpected(StoreError::InvalidName);
    }
    if (SteamUserStats() == nullptr)
    {
        return unexpected(StoreError::Unavailable);
    }
    int32 value = 0;
    if (!SteamUserStats()->GetStat(steam.c_str(), &value))
    {
        float other = 0.0f;
        return unexpected(SteamUserStats()->GetStat(steam.c_str(), &other) ? StoreError::TypeMismatch
                                                                           : StoreError::NotFound);
    }
    return static_cast<i32>(value);
}

Expected<void, StoreError> SteamStoreService::setStatFloat(std::string_view name, f32 value)
{
    const SteamName steam(name);
    if (!steam.valid())
    {
        return unexpected(StoreError::InvalidName);
    }
    if (!std::isfinite(value))
    {
        return unexpected(StoreError::InvalidValue);
    }
    if (SteamUserStats() == nullptr)
    {
        return unexpected(StoreError::Unavailable);
    }
    if (!SteamUserStats()->SetStat(steam.c_str(), value))
    {
        int32 other = 0;
        return unexpected(SteamUserStats()->GetStat(steam.c_str(), &other) ? StoreError::TypeMismatch
                                                                           : StoreError::NotFound);
    }
    return {};
}

Expected<f32, StoreError> SteamStoreService::statFloat(std::string_view name) const
{
    const SteamName steam(name);
    if (!steam.valid())
    {
        return unexpected(StoreError::InvalidName);
    }
    if (SteamUserStats() == nullptr)
    {
        return unexpected(StoreError::Unavailable);
    }
    float value = 0.0f;
    if (!SteamUserStats()->GetStat(steam.c_str(), &value))
    {
        int32 other = 0;
        return unexpected(SteamUserStats()->GetStat(steam.c_str(), &other) ? StoreError::TypeMismatch
                                                                           : StoreError::NotFound);
    }
    return value;
}

Expected<void, StoreError> SteamStoreService::storeStats()
{
    if (SteamUserStats() == nullptr || !SteamUserStats()->StoreStats())
    {
        return unexpected(StoreError::Unavailable);
    }
    return {};
}

Expected<void, StoreError> SteamStoreService::writeCloudFile(std::string_view name,
                                                             std::span<const std::byte> data)
{
    const SteamName steam(name);
    if (!steam.valid())
    {
        return unexpected(StoreError::InvalidName);
    }
    if (data.size() > kMaxCloudFileBytes)
    {
        return unexpected(StoreError::TooLarge);
    }
    ISteamRemoteStorage* storage = SteamRemoteStorage();
    if (storage == nullptr)
    {
        return unexpected(StoreError::Unavailable);
    }
    uint64 total = 0;
    uint64 available = 0;
    if (storage->GetQuota(&total, &available))
    {
        const u64 replaced =
            storage->FileExists(steam.c_str()) ? static_cast<u64>(storage->GetFileSize(steam.c_str())) : 0;
        if (data.size() > available + replaced)
        {
            return unexpected(StoreError::QuotaExceeded);
        }
    }
    if (!storage->FileWrite(steam.c_str(), data.data(), static_cast<int32>(data.size())))
    {
        return unexpected(StoreError::IoError);
    }
    return {};
}

Expected<u64, StoreError> SteamStoreService::readCloudFile(std::string_view name, std::span<std::byte> out)
{
    const SteamName steam(name);
    if (!steam.valid())
    {
        return unexpected(StoreError::InvalidName);
    }
    ISteamRemoteStorage* storage = SteamRemoteStorage();
    if (storage == nullptr)
    {
        return unexpected(StoreError::Unavailable);
    }
    if (!storage->FileExists(steam.c_str()))
    {
        return unexpected(StoreError::NotFound);
    }
    const u64 size = static_cast<u64>(std::max<int32>(storage->GetFileSize(steam.c_str()), 0));
    const u64 wanted = std::min<u64>(size, out.size());
    if (wanted > 0 && storage->FileRead(steam.c_str(), out.data(), static_cast<int32>(wanted)) !=
                          static_cast<int32>(wanted))
    {
        return unexpected(StoreError::IoError);
    }
    return size;
}

Expected<void, StoreError> SteamStoreService::removeCloudFile(std::string_view name)
{
    const SteamName steam(name);
    if (!steam.valid())
    {
        return unexpected(StoreError::InvalidName);
    }
    ISteamRemoteStorage* storage = SteamRemoteStorage();
    if (storage == nullptr)
    {
        return unexpected(StoreError::Unavailable);
    }
    if (!storage->FileExists(steam.c_str()))
    {
        return unexpected(StoreError::NotFound);
    }
    if (!storage->FileDelete(steam.c_str()))
    {
        return unexpected(StoreError::IoError);
    }
    return {};
}

u32 SteamStoreService::listCloudFiles(std::span<std::string_view> out)
{
    std::vector<std::string>& listed = m_impl->listed;
    listed.clear();
    if (ISteamRemoteStorage* storage = SteamRemoteStorage())
    {
        const int32 count = storage->GetFileCount();
        for (int32 i = 0; i < count; ++i)
        {
            int32 size = 0;
            const char* file = storage->GetFileNameAndSize(i, &size);
            if (file != nullptr && isValidStoreName(file))
            {
                listed.emplace_back(file);
            }
        }
    }
    std::sort(listed.begin(), listed.end());
    for (usize i = 0; i < std::min(out.size(), listed.size()); ++i)
    {
        out[i] = listed[i];
    }
    return static_cast<u32>(listed.size());
}

bool SteamStoreService::cloudFileExists(std::string_view name) const
{
    const SteamName steam(name);
    return steam.valid() && SteamRemoteStorage() != nullptr &&
           SteamRemoteStorage()->FileExists(steam.c_str());
}

Expected<StoreQuota, StoreError> SteamStoreService::cloudQuota() const
{
    uint64 total = 0;
    uint64 available = 0;
    if (SteamRemoteStorage() == nullptr || !SteamRemoteStorage()->GetQuota(&total, &available))
    {
        return unexpected(StoreError::Unavailable);
    }
    return StoreQuota{.totalBytes = total, .availableBytes = available};
}

bool SteamStoreService::overlayActive() const
{
    return m_impl->overlay;
}

bool SteamStoreService::openOverlay(StoreOverlayPage page)
{
    if (SteamFriends() == nullptr || SteamUtils() == nullptr || !SteamUtils()->IsOverlayEnabled())
    {
        return false;
    }
    SteamFriends()->ActivateGameOverlay(overlayDialog(page));
    return true;
}

std::optional<std::string_view> SteamStoreService::glyphPath(GamepadOrigin origin) const
{
    static_cast<void>(origin);
    return std::nullopt;
}

} // namespace illusion
