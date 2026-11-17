#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/save/SaveCapture.hpp>
#include <illusion/save/SaveSystem.hpp>
#include <illusion/save/SaveSystemService.hpp>
#include <illusion/store/StoreService.hpp>

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace illusion
{
namespace
{

enum class PendingType : u8
{
    Save,
    Load,
    Remove,
};

// A request of a module, kept until the next update() captures and sends it.
struct Pending
{
    PendingType type = PendingType::Save;
    std::string slot;
    std::string title;
    std::string custom;
    f64 playSeconds = 0.0;
    u32 gameVersion = 0;
};

// An event of pollEvents() with the slot name it views.
struct OwnedEvent
{
    SaveEventType type = SaveEventType::Failed;
    SaveError error = SaveError::IoError;
    std::string slot;
};

constexpr usize kReservedRequests = 16;
constexpr std::string_view kCloudSuffix = ".save";

} // namespace

struct SaveSystemService::Impl
{
    Impl(SaveSystem* saveSystem, StoreService* store)
        : system(saveSystem)
        , cloud(saveSystem != nullptr ? store : nullptr)
        , acceptFrom(saveSystem != nullptr ? saveSystem->nextTicket() : 0)
    {
        pending.reserve(kReservedRequests);
        completed.reserve(kReservedRequests);
        owned.reserve(kReservedRequests);
        events.reserve(kReservedRequests);
    }

    [[nodiscard]] Expected<void, SaveError> check(std::string_view slot) const
    {
        if (!isValidSaveSlotName(slot))
        {
            return unexpected(SaveError::InvalidName);
        }
        if (system == nullptr || !system->available())
        {
            return unexpected(SaveError::Unavailable);
        }
        return {};
    }

    void addEvent(SaveEventType type, SaveError error, std::string_view slot)
    {
        owned.push_back(OwnedEvent{type, error, std::string(slot)});
    }

    // Sends one request of a module; a failure becomes an event of this update.
    void send(Pending& request, World& world, GuidGenerator* guids)
    {
        Expected<u64, SaveError> sent = unexpected(SaveError::IoError);
        switch (request.type)
        {
        case PendingType::Save:
        {
            Expected<SaveCapture, SaveFailure> captured =
                captureSave(world, SaveCaptureOptions{.guids = guids});
            if (!captured)
            {
                ILL_LOG_ERROR("Save", "cannot capture slot '{}': {}", request.slot, captured.error().message);
                addEvent(SaveEventType::Failed, captured.error().error, request.slot);
                return;
            }
            const SaveMetadata metadata{request.title, request.playSeconds, request.gameVersion,
                                        request.custom};
            sent = system->write(request.slot, metadata, system->now(), std::move(captured->world),
                                 cloud != nullptr);
            break;
        }
        case PendingType::Load:
            sent = system->load(request.slot);
            break;
        case PendingType::Remove:
            sent = system->remove(request.slot);
            break;
        }
        if (sent)
        {
            ++inFlight;
        }
        else
        {
            addEvent(SaveEventType::Failed, sent.error(), request.slot);
        }
    }

    // Queues every `<slot>.save` cloud file for SaveSystem::importFile(), which keeps the newer one.
    void importCloudFiles()
    {
        const u32 count = cloud->listCloudFiles({});
        std::vector<std::string_view> names(count);
        const u32 listed = std::min(cloud->listCloudFiles(names), count);
        for (u32 i = 0; i < listed; ++i)
        {
            const std::string_view name = names.at(i);
            if (!name.ends_with(kCloudSuffix) ||
                !isValidSaveSlotName(name.substr(0, name.size() - kCloudSuffix.size())))
            {
                continue;
            }
            const std::string slot(name.substr(0, name.size() - kCloudSuffix.size()));
            const Expected<u64, StoreError> size = cloud->readCloudFile(name, {});
            if (!size)
            {
                ILL_LOG_WARN("Save", "cannot read the cloud file '{}' ({})", name,
                             storeErrorName(size.error()));
                continue;
            }
            if (*size > kSaveHeaderBytes + kMaxSavePayloadBytes)
            {
                ILL_LOG_WARN("Save", "ignoring the cloud file '{}': {} bytes is larger than a slot file",
                             name, *size);
                continue;
            }
            std::vector<std::byte> bytes(static_cast<usize>(*size));
            const Expected<u64, StoreError> read = cloud->readCloudFile(name, bytes);
            if (!read || *read != bytes.size())
            {
                ILL_LOG_WARN("Save", "cannot read the cloud file '{}' ({})", name,
                             read ? std::string_view("its size changed") : storeErrorName(read.error()));
                continue;
            }
            if (const Expected<u64, SaveError> queued = system->importFile(slot, std::move(bytes)); !queued)
            {
                ILL_LOG_WARN("Save", "cannot take the cloud file '{}' ({})", name,
                             saveErrorName(queued.error()));
            }
        }
    }

    // Mirrors a finished write or removal to the cloud; failures are only logged.
    void mirror(const SaveCompletion& done)
    {
        if (cloud == nullptr)
        {
            return;
        }
        std::string name = done.slot;
        name += kCloudSuffix;
        if (done.request == SaveRequestType::Write && done.type == SaveEventType::Saved && !done.file.empty())
        {
            if (Expected<void, StoreError> written = cloud->writeCloudFile(name, done.file); !written)
            {
                ILL_LOG_WARN("Save", "cannot copy slot '{}' to the cloud ({})", done.slot,
                             storeErrorName(written.error()));
            }
        }
        else if (done.request == SaveRequestType::Remove && done.type == SaveEventType::Removed)
        {
            if (Expected<void, StoreError> removed = cloud->removeCloudFile(name);
                !removed && removed.error() != StoreError::NotFound)
            {
                ILL_LOG_WARN("Save", "cannot remove slot '{}' from the cloud ({})", done.slot,
                             storeErrorName(removed.error()));
            }
        }
    }

    // Turns a finished request into an event, applying a load to @p world.
    void finish(SaveCompletion& done, World& world)
    {
        // Writes and removals happen even after cancel(), so the cloud follows them too.
        mirror(done);
        if (done.ticket < acceptFrom)
        {
            return;
        }
        if (done.request == SaveRequestType::Import)
        {
            if (done.type == SaveEventType::Failed)
            {
                ILL_LOG_WARN("Save", "ignoring the cloud file of slot '{}' ({})", done.slot,
                             saveErrorName(done.error));
            }
            else if (done.imported)
            {
                ILL_LOG_INFO("Save", "slot '{}' was replaced by its newer cloud file", done.slot);
            }
            return;
        }
        --inFlight;
        if (done.type == SaveEventType::Loaded || done.type == SaveEventType::LoadedBackup)
        {
            Expected<SaveApplied, SaveFailure> applied = applySave(world, done.world);
            if (!applied)
            {
                ILL_LOG_ERROR("Save", "cannot apply slot '{}': {}", done.slot, applied.error().message);
                addEvent(SaveEventType::Failed, applied.error().error, done.slot);
                return;
            }
        }
        addEvent(done.type, done.error, done.slot);
    }

    void refreshSlots()
    {
        if (system->slotsVersion() == slotsVersion)
        {
            return;
        }
        slotsVersion = system->copySlots(records);
        infos.clear();
        infos.reserve(records.size());
        for (const SaveSlotRecord& record : records)
        {
            infos.push_back(record.info());
        }
    }

    SaveSystem* system = nullptr;
    StoreService* cloud = nullptr;
    bool cloudImported = false; // the first update queued the cloud's slot files
    u64 acceptFrom = 0;         // results of earlier tickets are ignored (cancel())
    u32 inFlight = 0;           // requests sent whose results were not taken yet
    u64 slotsVersion = std::numeric_limits<u64>::max();
    std::vector<Pending> pending;
    std::vector<SaveCompletion> completed;
    std::vector<OwnedEvent> owned;       // viewed by events
    std::vector<SaveEvent> events;       // the events of the last update
    std::vector<SaveSlotRecord> records; // viewed by infos
    std::vector<SaveSlotInfo> infos;
};

SaveSystemService::SaveSystemService(SaveSystem* system, StoreService* cloud)
    : m_impl(std::make_unique<Impl>(system, cloud))
{
}

SaveSystemService::~SaveSystemService() = default;

bool SaveSystemService::available() const
{
    return m_impl->system != nullptr && m_impl->system->available();
}

Expected<void, SaveError> SaveSystemService::save(std::string_view slot, const SaveMetadata& metadata)
{
    if (!isValidSaveSlotName(slot))
    {
        return unexpected(SaveError::InvalidName);
    }
    if (!isValidSaveMetadata(metadata))
    {
        return unexpected(SaveError::InvalidValue);
    }
    if (Expected<void, SaveError> checked = m_impl->check(slot); !checked)
    {
        return checked;
    }
    m_impl->pending.push_back(Pending{PendingType::Save, std::string(slot), std::string(metadata.title),
                                      std::string(metadata.custom), metadata.playSeconds,
                                      metadata.gameVersion});
    return {};
}

Expected<void, SaveError> SaveSystemService::load(std::string_view slot)
{
    if (Expected<void, SaveError> checked = m_impl->check(slot); !checked)
    {
        return checked;
    }
    m_impl->pending.push_back(Pending{PendingType::Load, std::string(slot), {}, {}, 0.0, 0});
    return {};
}

Expected<void, SaveError> SaveSystemService::remove(std::string_view slot)
{
    if (Expected<void, SaveError> checked = m_impl->check(slot); !checked)
    {
        return checked;
    }
    m_impl->pending.push_back(Pending{PendingType::Remove, std::string(slot), {}, {}, 0.0, 0});
    return {};
}

bool SaveSystemService::busy() const
{
    return !m_impl->pending.empty() || m_impl->inFlight > 0;
}

u32 SaveSystemService::slots(std::span<SaveSlotInfo> out)
{
    const std::vector<SaveSlotInfo>& infos = m_impl->infos;
    std::copy_n(infos.begin(), std::min(out.size(), infos.size()), out.begin());
    return static_cast<u32>(infos.size());
}

std::optional<SaveSlotInfo> SaveSystemService::slot(std::string_view slot) const
{
    const auto found = std::ranges::find(m_impl->infos, slot, &SaveSlotInfo::name);
    if (found == m_impl->infos.end())
    {
        return std::nullopt;
    }
    return *found;
}

u32 SaveSystemService::pollEvents(std::span<SaveEvent> out)
{
    const std::vector<SaveEvent>& events = m_impl->events;
    std::copy_n(events.begin(), std::min(out.size(), events.size()), out.begin());
    return static_cast<u32>(events.size());
}

void SaveSystemService::update(World& world, GuidGenerator* guids, bool wait)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    impl.events.clear();
    impl.owned.clear();
    if (impl.system == nullptr)
    {
        return;
    }
    if (wait)
    {
        impl.system->waitIdle();
    }
    impl.completed.clear();
    impl.system->takeCompleted(impl.completed);
    for (SaveCompletion& done : impl.completed)
    {
        impl.finish(done, world);
    }
    if (impl.cloud != nullptr && !impl.cloudImported && impl.system->available())
    {
        impl.cloudImported = true;
        impl.importCloudFiles();
    }
    for (Pending& request : impl.pending)
    {
        impl.send(request, world, guids);
    }
    impl.pending.clear();
    for (const OwnedEvent& event : impl.owned)
    {
        impl.events.push_back(SaveEvent{event.type, event.slot, event.error});
    }
    impl.refreshSlots();
}

void SaveSystemService::cancel()
{
    Impl& impl = *m_impl;
    impl.pending.clear();
    impl.events.clear();
    impl.owned.clear();
    impl.inFlight = 0;
    if (impl.system == nullptr)
    {
        return;
    }
    if (impl.cloud != nullptr)
    {
        // The last save before the session stops ("save and quit") reaches the cloud too.
        impl.system->waitIdle();
        impl.completed.clear();
        impl.system->takeCompleted(impl.completed);
        for (const SaveCompletion& done : impl.completed)
        {
            impl.mirror(done);
        }
        impl.completed.clear();
    }
    impl.acceptFrom = impl.system->nextTicket();
}

} // namespace illusion
