#pragma once

// A StoreService whose cloud files live in memory, for the cloud mirror of saved games (ADR 0058). It has no
// achievements, stats or overlay.

#include <illusion/store/StoreService.hpp>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace illusion::test
{

class CloudTestStore final : public StoreService
{
public:
    CloudTestStore() = default;
    ~CloudTestStore() override = default;

    bool isAvailable = true;
    bool full = false; // writes fail with QuotaExceeded
    std::map<std::string, std::vector<std::byte>, std::less<>> files;
    u32 writes = 0;
    u32 removals = 0;

    [[nodiscard]] bool available() const override { return isAvailable; }
    [[nodiscard]] std::string_view storeName() const override { return "test"; }
    [[nodiscard]] std::string_view userName() const override { return "tester"; }
    [[nodiscard]] u64 userId() const override { return 1; }
    Expected<void, StoreError> unlockAchievement(std::string_view) override { return unavailable(); }
    Expected<void, StoreError> clearAchievement(std::string_view) override { return unavailable(); }
    [[nodiscard]] Expected<bool, StoreError> isAchievementUnlocked(std::string_view) const override
    {
        return unexpected(StoreError::Unavailable);
    }
    Expected<void, StoreError> setAchievementProgress(std::string_view, AchievementProgress) override
    {
        return unavailable();
    }
    [[nodiscard]] Expected<AchievementProgress, StoreError>
    achievementProgress(std::string_view) const override
    {
        return unexpected(StoreError::Unavailable);
    }
    Expected<void, StoreError> setStatInt(std::string_view, i32) override { return unavailable(); }
    [[nodiscard]] Expected<i32, StoreError> statInt(std::string_view) const override
    {
        return unexpected(StoreError::Unavailable);
    }
    Expected<void, StoreError> setStatFloat(std::string_view, f32) override { return unavailable(); }
    [[nodiscard]] Expected<f32, StoreError> statFloat(std::string_view) const override
    {
        return unexpected(StoreError::Unavailable);
    }
    Expected<void, StoreError> storeStats() override { return unavailable(); }

    Expected<void, StoreError> writeCloudFile(std::string_view name, std::span<const std::byte> data) override
    {
        ++writes;
        if (full)
        {
            return unexpected(StoreError::QuotaExceeded);
        }
        files.insert_or_assign(std::string(name), std::vector<std::byte>(data.begin(), data.end()));
        return {};
    }
    Expected<u64, StoreError> readCloudFile(std::string_view name, std::span<std::byte> out) override
    {
        const auto found = files.find(name);
        if (found == files.end())
        {
            return unexpected(StoreError::NotFound);
        }
        std::copy_n(found->second.begin(), std::min(out.size(), found->second.size()), out.begin());
        return found->second.size();
    }
    Expected<void, StoreError> removeCloudFile(std::string_view name) override
    {
        ++removals;
        const auto found = files.find(name);
        if (found == files.end())
        {
            return unexpected(StoreError::NotFound);
        }
        files.erase(found);
        return {};
    }
    u32 listCloudFiles(std::span<std::string_view> out) override
    {
        usize i = 0;
        for (const auto& [name, bytes] : files)
        {
            if (i < out.size())
            {
                out[i] = name;
            }
            ++i;
        }
        return static_cast<u32>(i);
    }
    [[nodiscard]] bool cloudFileExists(std::string_view name) const override { return files.contains(name); }
    [[nodiscard]] Expected<StoreQuota, StoreError> cloudQuota() const override
    {
        return StoreQuota{1u << 20, full ? 0u : 1u << 20};
    }
    [[nodiscard]] bool overlayActive() const override { return false; }
    bool openOverlay(StoreOverlayPage) override { return false; }
    [[nodiscard]] std::optional<std::string_view> glyphPath(GamepadOrigin) const override
    {
        return std::nullopt;
    }

private:
    static Expected<void, StoreError> unavailable() { return unexpected(StoreError::Unavailable); }
};

} // namespace illusion::test
