#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/save/SaveCapture.hpp>
#include <illusion/save/SaveSystem.hpp>
#include <illusion/save/SaveSystemService.hpp>

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

} // namespace

struct SaveSystemService::Impl
{
    explicit Impl(SaveSystem* saveSystem)
        : system(saveSystem)
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
            sent = system->write(request.slot, metadata, system->now(), std::move(captured->world));
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

    // Turns a finished request into an event, applying a load to @p world.
    void finish(SaveCompletion& done, World& world)
    {
        if (done.ticket < acceptFrom)
        {
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
    u64 acceptFrom = 0; // results of earlier tickets are ignored (cancel())
    u32 inFlight = 0;   // requests sent whose results were not taken yet
    u64 slotsVersion = std::numeric_limits<u64>::max();
    std::vector<Pending> pending;
    std::vector<SaveCompletion> completed;
    std::vector<OwnedEvent> owned;       // viewed by events
    std::vector<SaveEvent> events;       // the events of the last update
    std::vector<SaveSlotRecord> records; // viewed by infos
    std::vector<SaveSlotInfo> infos;
};

SaveSystemService::SaveSystemService(SaveSystem* system)
    : m_impl(std::make_unique<Impl>(system))
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
    if (impl.system != nullptr)
    {
        impl.acceptFrom = impl.system->nextTicket();
    }
}

} // namespace illusion
