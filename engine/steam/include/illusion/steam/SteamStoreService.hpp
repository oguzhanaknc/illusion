#pragma once

/// @file
/// The Steam store (ADR 0056): StoreService over the Steamworks API. Built only when ILLUSION_STEAMWORKS_SDK
/// names a Steamworks SDK; the Steam headers stay in its sources.
///
/// @code
/// // What the generated main.cpp of a game exported with the store "steam" does:
/// desc.store = &illusion::createSteamStore;
/// desc.storeAppId = 480;
/// @endcode

#include <illusion/store/StoreBackend.hpp>

#include <memory>

namespace illusion
{

/// Starts the Steamworks API for the app @p appId and returns its store, or null when Steam is not running,
/// the user does not own the app or the API does not start. Call it once, before the graphics device is
/// created, so that the overlay can hook it. The store shuts the API down when destroyed.
[[nodiscard]] std::unique_ptr<StoreBackend> createSteamStore(u32 appId);

/// StoreService over Steamworks: achievements and stats (ISteamUserStats), cloud files (ISteamRemoteStorage)
/// and the overlay (ISteamFriends, ISteamUtils). glyphPath() returns nothing (ADR 0056). Created by
/// createSteamStore().
class SteamStoreService final : public StoreBackend
{
public:
    ~SteamStoreService() override;

    /// @name StoreBackend
    /// @{
    void update() override;
    [[nodiscard]] bool cloudIsRemote() const noexcept override;
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
    struct Impl;
    explicit SteamStoreService(std::unique_ptr<Impl> impl) noexcept;
    friend std::unique_ptr<StoreBackend> createSteamStore(u32 appId);

    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion
