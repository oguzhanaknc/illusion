#pragma once

/// @file
/// The store of a game that runs without one (ADR 0056): achievements and stats in memory, cloud files in a
/// directory of the virtual file system.
///
/// @code
/// illusion::NullStoreService store(files); // cloud files in user:/cloud
/// sessionDesc.store = &store;
/// @endcode

#include <illusion/store/StoreBackend.hpp>

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

class VirtualFileSystem;

namespace detail
{
// What a NullStoreService keeps of an achievement.
struct NullAchievement
{
    bool unlocked = false;
    AchievementProgress progress{};
};

// What a NullStoreService keeps of a stat.
struct NullStat
{
    bool isFloat = false;
    i32 integer = 0;
    f32 real = 0.0f;
};
} // namespace detail

/// Settings of a NullStoreService.
struct NullStoreDesc
{
    /// Directory of the cloud files, on a read-write mount; created on the first write.
    std::string_view cloudDirectory = "user:/cloud";
    u64 cloudQuotaBytes = 100ull * 1024 * 1024; ///< What the cloud files may take in all.
};

/// StoreService without a store: available() is false and storeName() "none". Achievements, their progress
/// and stats live in memory for as long as the object; cloud files are files of
/// NullStoreDesc::cloudDirectory, so they persist across runs. openOverlay() returns false and glyphPath()
/// nothing. Not thread-safe.
class NullStoreService final : public StoreBackend
{
public:
    /// Keeps its cloud files in @p files, which must outlive it.
    explicit NullStoreService(VirtualFileSystem& files, const NullStoreDesc& desc = {});
    ~NullStoreService() override;

    /// The cloud directory it uses.
    [[nodiscard]] std::string_view cloudDirectory() const noexcept { return m_directory; }

    /// @name StoreBackend
    /// @{
    void update() override;
    [[nodiscard]] bool available() const override;
    [[nodiscard]] std::string_view storeName() const override;
    [[nodiscard]] std::string_view userName() const override;
    [[nodiscard]] u64 userId() const override;
    Expected<void, StoreError> unlockAchievement(std::string_view name) override;
    Expected<void, StoreError> clearAchievement(std::string_view name) override;
    [[nodiscard]] Expected<bool, StoreError> isAchievementUnlocked(std::string_view name) const override;
    Expected<void, StoreError> setAchievementProgress(std::string_view name,
                                                      AchievementProgress progress) override;
    [[nodiscard]] Expected<AchievementProgress, StoreError>
    achievementProgress(std::string_view name) const override;
    Expected<void, StoreError> setStatInt(std::string_view name, i32 value) override;
    [[nodiscard]] Expected<i32, StoreError> statInt(std::string_view name) const override;
    Expected<void, StoreError> setStatFloat(std::string_view name, f32 value) override;
    [[nodiscard]] Expected<f32, StoreError> statFloat(std::string_view name) const override;
    Expected<void, StoreError> storeStats() override;
    Expected<void, StoreError> writeCloudFile(std::string_view name,
                                              std::span<const std::byte> data) override;
    Expected<u64, StoreError> readCloudFile(std::string_view name, std::span<std::byte> out) override;
    Expected<void, StoreError> removeCloudFile(std::string_view name) override;
    u32 listCloudFiles(std::span<std::string_view> out) override;
    [[nodiscard]] bool cloudFileExists(std::string_view name) const override;
    [[nodiscard]] Expected<StoreQuota, StoreError> cloudQuota() const override;
    [[nodiscard]] bool overlayActive() const override;
    bool openOverlay(StoreOverlayPage page) override;
    [[nodiscard]] std::optional<std::string_view> glyphPath(GamepadOrigin origin) const override;
    /// @}

private:
    [[nodiscard]] std::string pathOf(std::string_view name) const;
    [[nodiscard]] Expected<u64, StoreError> usedBytes(std::string_view except) const;

    VirtualFileSystem& m_files;
    std::string m_directory;
    u64 m_quota = 0;
    std::map<std::string, detail::NullAchievement, std::less<>> m_achievements;
    std::map<std::string, detail::NullStat, std::less<>> m_stats;
    std::vector<std::string> m_listed; // names of the last listCloudFiles()
};

} // namespace illusion
