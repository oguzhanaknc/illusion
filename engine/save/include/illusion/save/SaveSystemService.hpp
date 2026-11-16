#pragma once

/// @file
/// The host's implementation of SaveService over a SaveSystem (docs/adr/0058-saved-games.md).
///
/// @code
/// illusion::SaveSystemService saveService(&saves);
/// services.provide<illusion::SaveService>(saveService);
/// // each update, after the modules ran:
/// saveService.update(world, &guids, fixedStep);
/// @endcode

#include <illusion/save/SaveService.hpp>

#include <memory>

namespace illusion
{

class GuidGenerator;
class SaveSystem;
class StoreService;
class World;

/// SaveService over a SaveSystem; the SaveSystem must outlive it. GameSession owns one and calls update()
/// after its Modules step. Created by the host; a shared module that constructs one fails to link, since
/// only the host has the library's code (ADR 0041).
class SaveSystemService final : public SaveService
{
public:
    /// Serves @p system; null makes a service that is not available(). With @p cloud (a store whose cloud
    /// files are not local files) slot files are mirrored there: the first update() takes every
    /// `<slot>.save` cloud file that is newer than the local slot (SaveSystem::importFile()), each written
    /// slot is copied with writeCloudFile() and each removed one removed with removeCloudFile(), on the
    /// calling thread. Cloud failures (no quota, a corrupt cloud file) are logged and never become events.
    explicit SaveSystemService(SaveSystem* system, StoreService* cloud = nullptr);
    ~SaveSystemService() override;

    /// @name SaveService
    /// @{
    [[nodiscard]] bool available() const override;
    Expected<void, SaveError> save(std::string_view slot, const SaveMetadata& metadata) override;
    Expected<void, SaveError> load(std::string_view slot) override;
    Expected<void, SaveError> remove(std::string_view slot) override;
    [[nodiscard]] bool busy() const override;
    u32 slots(std::span<SaveSlotInfo> out) override;
    [[nodiscard]] std::optional<SaveSlotInfo> slot(std::string_view slot) const override;
    u32 pollEvents(std::span<SaveEvent> out) override;
    /// @}

    /// Runs on the World's thread once per update. With @p wait (fixed steps), first waits for the save
    /// thread, so that every result arrives in the update after its request. Then applies the loads that
    /// finished (applySave()) and makes the finished requests the events of pollEvents(); captures
    /// (captureSave(), ids from @p guids or GuidGenerator::shared()) and sends the requests made since the
    /// last call, in call order; and refreshes the slot list.
    void update(World& world, GuidGenerator* guids, bool wait);

    /// Drops the requests not sent yet and ignores the results of those already sent: their writes and
    /// removals still happen, their events and loads do not. Called by GameSession::stop().
    void cancel();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion
