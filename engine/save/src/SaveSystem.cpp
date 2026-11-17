#include <illusion/assets/Image.hpp>
#include <illusion/core/Assert.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/core/Thread.hpp>
#include <illusion/save/SaveSystem.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace illusion
{
namespace
{

constexpr std::string_view kSlotSuffix = ".save";
constexpr std::string_view kBackupSuffix = ".save.bak";
constexpr std::string_view kImageSuffix = ".png";

struct Request
{
    bool scan = false; // the first job: list the directory
    SaveRequestType type = SaveRequestType::Write;
    u64 ticket = 0;
    std::string slot;
    std::string title;
    std::string custom;
    f64 playSeconds = 0.0;
    u32 gameVersion = 0;
    i64 savedAt = 0;
    std::string world;
    bool keepFile = false;
    SaveThumbnail thumbnail;     // Write: the image, empty without one
    std::vector<std::byte> file; // Import: the slot file
};

// A slot file read and checked; world views the bytes it was read from.
struct SlotFile
{
    SaveSlotState state = SaveSlotState::Corrupt;
    SaveError error = SaveError::NotFound; // why it cannot be loaded (state Corrupt or NewerVersion)
    bool exists = false;                   // the slot file or its backup is there
    SaveFileMetadata metadata;
    std::string_view world;
};

// What the save thread did for one request, applied to the shared state under the lock.
struct Outcome
{
    std::optional<SaveCompletion> completion;
    std::optional<std::vector<SaveSlotRecord>> slots; // the scan: the whole list
    std::optional<SaveSlotRecord> record;             // a slot that exists now
    std::optional<bool> image;                        // record has an image; empty keeps what the list says
    std::string erased;                               // a slot that no longer exists
};

i64 systemSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// The slot name of a directory entry @p name, or empty when it is no slot file or backup.
std::string_view slotOfEntry(std::string_view name) noexcept
{
    for (const std::string_view suffix : {kBackupSuffix, kSlotSuffix})
    {
        if (name.size() > suffix.size() && name.ends_with(suffix))
        {
            const std::string_view slot = name.substr(0, name.size() - suffix.size());
            return isValidSaveSlotName(slot) ? slot : std::string_view();
        }
    }
    return {};
}

// The slot name of the image @p name, or empty when it is no slot image.
std::string_view slotOfImage(std::string_view name) noexcept
{
    if (name.size() > kImageSuffix.size() && name.ends_with(kImageSuffix))
    {
        const std::string_view slot = name.substr(0, name.size() - kImageSuffix.size());
        return isValidSaveSlotName(slot) ? slot : std::string_view();
    }
    return {};
}

[[nodiscard]] bool isValidThumbnail(const SaveThumbnail& thumbnail) noexcept
{
    return thumbnail.width > 0 && thumbnail.height > 0 && thumbnail.width <= 4096 &&
           thumbnail.height <= 4096 &&
           thumbnail.rgba.size() == static_cast<usize>(thumbnail.width) * thumbnail.height * 4;
}

SaveError readError(FileError error) noexcept
{
    return error == FileError::NotFound ? SaveError::NotFound : SaveError::IoError;
}

} // namespace

FileError VfsSaveStorage::createDirectory(std::string_view path)
{
    return m_files.createDirectory(path);
}

FileError VfsSaveStorage::list(std::string_view path, std::vector<DirectoryEntry>& out)
{
    return m_files.list(path, out);
}

FileError VfsSaveStorage::read(std::string_view path, std::vector<std::byte>& out)
{
    return m_files.readFile(path, out);
}

FileError VfsSaveStorage::writeAtomic(std::string_view path, std::span<const std::byte> data)
{
    return m_files.writeFileAtomic(path, data);
}

FileError VfsSaveStorage::remove(std::string_view path)
{
    return m_files.remove(path);
}

struct SaveSystem::Impl
{
    explicit Impl(const SaveSystemDesc& systemDesc)
        : desc(systemDesc)
    {
        if (desc.storage == nullptr && desc.files != nullptr)
        {
            ownedStorage = std::make_unique<VfsSaveStorage>(*desc.files);
        }
        storage = desc.storage != nullptr ? desc.storage : ownedStorage.get();
        if (storage != nullptr)
        {
            const FileError created = storage->createDirectory(desc.directory);
            isAvailable = created == FileError::None || created == FileError::AlreadyExists;
            if (!isAvailable)
            {
                ILL_LOG_WARN("Save", "saved games are unavailable: cannot create '{}' ({})", desc.directory,
                             fileErrorName(created));
            }
        }
    }

    [[nodiscard]] std::string pathOf(std::string_view slot, std::string_view suffix) const
    {
        std::string path;
        path.reserve(desc.directory.size() + 1 + slot.size() + suffix.size());
        path += desc.directory;
        if (!path.empty() && path.back() != '/')
        {
            path += '/';
        }
        path += slot;
        path += suffix;
        return path;
    }

    // Starts the thread with the scan as its first job. Called with the lock held.
    void ensureStarted()
    {
        if (thread.joinable() || stopping || !isAvailable)
        {
            return;
        }
        Request scan;
        scan.scan = true;
        queue.push_front(std::move(scan));
        thread = std::thread([this] { run(); });
    }

    [[nodiscard]] u64 enqueue(Request request)
    {
        u64 ticket = 0;
        {
            const std::lock_guard lock(mutex);
            ensureStarted();
            ticket = nextTicket++;
            request.ticket = ticket;
            queue.push_back(std::move(request));
        }
        wake.notify_one();
        return ticket;
    }

    void run()
    {
        setCurrentThreadName("Save");
        ILL_PROFILE_THREAD("Save");
        for (;;)
        {
            Request request;
            bool skip = false;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [this] { return !queue.empty() || stopping; });
                if (queue.empty())
                {
                    return;
                }
                request = std::move(queue.front());
                queue.pop_front();
                running = true;
                // Nobody applies a load once the system shuts down.
                skip = stopping && !request.scan && request.type == SaveRequestType::Load;
            }
            Outcome outcome;
            if (!skip)
            {
                outcome = process(request);
            }
            {
                const std::lock_guard lock(mutex);
                publish(std::move(outcome));
                running = false;
            }
            idle.notify_all();
        }
    }

    // Applies @p outcome to the shared state. Called with the lock held.
    void publish(Outcome outcome)
    {
        bool changed = false;
        if (outcome.slots)
        {
            slots = std::move(*outcome.slots);
            changed = true;
        }
        if (!outcome.erased.empty())
        {
            const auto found = std::ranges::find(slots, outcome.erased, &SaveSlotRecord::name);
            if (found != slots.end())
            {
                slots.erase(found);
                changed = true;
            }
        }
        if (outcome.record)
        {
            SaveSlotRecord& record = *outcome.record;
            const auto at = std::ranges::lower_bound(slots, record.name, {}, &SaveSlotRecord::name);
            const bool listed = at != slots.end() && at->name == record.name;
            if (outcome.image)
            {
                record.screenshot = *outcome.image ? pathOf(record.name, kImageSuffix) : std::string();
            }
            else if (listed)
            {
                record.screenshot = std::move(at->screenshot);
            }
            if (listed)
            {
                *at = std::move(record);
            }
            else
            {
                slots.insert(at, std::move(record));
            }
            changed = true;
        }
        if (changed)
        {
            ++slotsVersion;
        }
        if (outcome.completion)
        {
            completed.push_back(std::move(*outcome.completion));
        }
    }

    // Everything below runs on the save thread without the lock.

    [[nodiscard]] Outcome process(Request& request)
    {
        ILL_PROFILE_SCOPE();
        if (request.scan)
        {
            return scan();
        }
        switch (request.type)
        {
        case SaveRequestType::Write:
            return write(request);
        case SaveRequestType::Load:
            return load(request);
        case SaveRequestType::Remove:
            return remove(request);
        case SaveRequestType::Import:
            return importFile(request);
        }
        return {};
    }

    // Reads and checks the file @p path into @p bytes.
    [[nodiscard]] Expected<DecodedSave, SaveError> readFile(std::string_view path,
                                                            std::vector<std::byte>& bytes)
    {
        const FileError read = storage->read(path, bytes);
        if (read != FileError::None)
        {
            return unexpected(readError(read));
        }
        return decodeSaveFile(bytes);
    }

    // Reads the slot file of @p slot into slotBytes, or its backup into backupBytes when the slot file is
    // missing or corrupt.
    [[nodiscard]] SlotFile readSlot(std::string_view slot)
    {
        SlotFile result;
        Expected<DecodedSave, SaveError> main = readFile(pathOf(slot, kSlotSuffix), slotBytes);
        if (main)
        {
            result.state = SaveSlotState::Ok;
            result.exists = true;
            result.metadata = std::move(main->metadata);
            result.world = main->world;
            return result;
        }
        result.exists = main.error() != SaveError::NotFound;
        if (main.error() == SaveError::NewerVersion)
        {
            result.state = SaveSlotState::NewerVersion;
            result.error = SaveError::NewerVersion;
            return result;
        }
        Expected<DecodedSave, SaveError> backup = readFile(pathOf(slot, kBackupSuffix), backupBytes);
        if (backup)
        {
            result.state = SaveSlotState::Backup;
            result.exists = true;
            result.metadata = std::move(backup->metadata);
            result.world = backup->world;
            return result;
        }
        result.exists = result.exists || backup.error() != SaveError::NotFound;
        result.error = main.error() != SaveError::NotFound ? main.error() : backup.error();
        return result;
    }

    [[nodiscard]] static SaveSlotRecord recordOf(std::string_view slot, SlotFile& file)
    {
        SaveSlotRecord record;
        record.name = std::string(slot);
        record.state = file.state;
        if (file.state == SaveSlotState::Ok || file.state == SaveSlotState::Backup)
        {
            record.metadata = std::move(file.metadata);
        }
        return record;
    }

    // The slot list entry of @p slot as the files are now: set, or erased when neither file exists.
    void reclassify(std::string_view slot, Outcome& outcome)
    {
        SlotFile file = readSlot(slot);
        if (file.exists)
        {
            outcome.record = recordOf(slot, file);
        }
        else
        {
            outcome.erased = std::string(slot);
        }
    }

    [[nodiscard]] static SaveCompletion completion(const Request& request, SaveEventType type,
                                                   SaveError error = SaveError::IoError)
    {
        SaveCompletion done;
        done.ticket = request.ticket;
        done.request = request.type;
        done.type = type;
        done.error = error;
        done.slot = request.slot;
        return done;
    }

    [[nodiscard]] Outcome scan()
    {
        Outcome outcome;
        std::vector<DirectoryEntry> entries;
        std::vector<std::string> names;
        std::vector<std::string> images;
        if (const FileError listed = storage->list(desc.directory, entries); listed != FileError::None)
        {
            ILL_LOG_WARN("Save", "cannot list '{}' ({})", desc.directory, fileErrorName(listed));
        }
        for (const DirectoryEntry& entry : entries)
        {
            if (entry.isDirectory)
            {
                continue;
            }
            if (const std::string_view slot = slotOfEntry(entry.name); !slot.empty())
            {
                names.emplace_back(slot);
            }
            else if (const std::string_view imaged = slotOfImage(entry.name); !imaged.empty())
            {
                images.emplace_back(imaged);
            }
        }
        std::ranges::sort(names);
        const auto [first, last] = std::ranges::unique(names);
        names.erase(first, last);
        std::ranges::sort(images);
        std::vector<SaveSlotRecord> list;
        list.reserve(names.size());
        for (const std::string& name : names)
        {
            SlotFile file = readSlot(name);
            if (file.exists)
            {
                list.push_back(recordOf(name, file));
                if (std::ranges::binary_search(images, name))
                {
                    list.back().screenshot = pathOf(name, kImageSuffix);
                }
            }
        }
        outcome.slots = std::move(list);
        return outcome;
    }

    [[nodiscard]] Outcome write(const Request& request)
    {
        Outcome outcome;
        const SaveMetadata metadata{request.title, request.playSeconds, request.gameVersion, request.custom};
        if (Expected<void, SaveError> encoded =
                encodeSaveFile(metadata, request.savedAt, request.world, encodedBytes);
            !encoded)
        {
            outcome.completion = completion(request, SaveEventType::Failed, encoded.error());
            return outcome;
        }
        const std::string slotPath = pathOf(request.slot, kSlotSuffix);
        // The file being replaced becomes the backup, if it is valid; an invalid one keeps the older backup.
        if (readFile(slotPath, slotBytes))
        {
            if (const FileError backedUp =
                    storage->writeAtomic(pathOf(request.slot, kBackupSuffix), slotBytes);
                backedUp != FileError::None)
            {
                ILL_LOG_WARN("Save", "cannot back up slot '{}' ({})", request.slot, fileErrorName(backedUp));
                outcome.completion = completion(request, SaveEventType::Failed, SaveError::IoError);
                reclassify(request.slot, outcome);
                return outcome;
            }
        }
        if (const FileError written = storage->writeAtomic(slotPath, encodedBytes);
            written != FileError::None)
        {
            ILL_LOG_WARN("Save", "cannot write slot '{}' ({})", request.slot, fileErrorName(written));
            outcome.completion = completion(request, SaveEventType::Failed, SaveError::IoError);
            reclassify(request.slot, outcome);
            return outcome;
        }
        Expected<DecodedSave, SaveError> decoded = decodeSaveFile(encodedBytes);
        ILL_CHECK(decoded.hasValue(), "a slot file just encoded must decode");
        SaveSlotRecord record;
        record.name = request.slot;
        record.metadata = std::move(decoded->metadata);
        outcome.record = std::move(record);
        outcome.image = writeImage(request);
        SaveCompletion done = completion(request, SaveEventType::Saved);
        if (request.keepFile)
        {
            done.file = encodedBytes;
        }
        outcome.completion = std::move(done);
        return outcome;
    }

    // Writes the image of a written slot, or removes the previous slot's one when there is none or it
    // cannot be written; returns whether the slot has an image now.
    [[nodiscard]] bool writeImage(const Request& request)
    {
        const std::string imagePath = pathOf(request.slot, kImageSuffix);
        if (!request.thumbnail.rgba.empty())
        {
            Image image;
            image.format = ImageFormat::RGBA8Srgb;
            image.mips.push_back(ImageMip{request.thumbnail.width, request.thumbnail.height, 0,
                                          request.thumbnail.rgba.size()});
            image.data = request.thumbnail.rgba;
            const std::vector<std::byte> png = encodePng(image);
            const FileError written = png.empty() ? FileError::IoError : storage->writeAtomic(imagePath, png);
            if (written == FileError::None)
            {
                return true;
            }
            ILL_LOG_WARN("Save", "cannot write the image of slot '{}' ({})", request.slot,
                         fileErrorName(written));
        }
        return !removeImage(request.slot);
    }

    // Removes the image of @p slot; false when it is still there.
    bool removeImage(std::string_view slot)
    {
        const FileError removed = storage->remove(pathOf(slot, kImageSuffix));
        if (removed != FileError::None && removed != FileError::NotFound)
        {
            ILL_LOG_WARN("Save", "cannot remove the image of slot '{}' ({})", slot, fileErrorName(removed));
            return false;
        }
        return true;
    }

    [[nodiscard]] Outcome importFile(const Request& request)
    {
        Outcome outcome;
        if (request.file.size() > kSaveHeaderBytes + kMaxSavePayloadBytes)
        {
            outcome.completion = completion(request, SaveEventType::Failed, SaveError::TooLarge);
            return outcome;
        }
        Expected<DecodedSave, SaveError> imported = decodeSaveFile(request.file);
        if (!imported)
        {
            outcome.completion = completion(request, SaveEventType::Failed, imported.error());
            return outcome;
        }
        const std::string slotPath = pathOf(request.slot, kSlotSuffix);
        const Expected<DecodedSave, SaveError> local = readFile(slotPath, slotBytes);
        // A local file of a newer container version is kept: this program cannot tell which one is newer.
        const bool keepLocal = local ? local->metadata.savedAt >= imported->metadata.savedAt
                                     : local.error() == SaveError::NewerVersion;
        if (keepLocal)
        {
            outcome.completion = completion(request, SaveEventType::Saved);
            return outcome;
        }
        if (local)
        {
            if (const FileError backedUp =
                    storage->writeAtomic(pathOf(request.slot, kBackupSuffix), slotBytes);
                backedUp != FileError::None)
            {
                ILL_LOG_WARN("Save", "cannot back up slot '{}' ({})", request.slot, fileErrorName(backedUp));
                outcome.completion = completion(request, SaveEventType::Failed, SaveError::IoError);
                return outcome;
            }
        }
        if (const FileError written = storage->writeAtomic(slotPath, request.file);
            written != FileError::None)
        {
            ILL_LOG_WARN("Save", "cannot write slot '{}' ({})", request.slot, fileErrorName(written));
            outcome.completion = completion(request, SaveEventType::Failed, SaveError::IoError);
            reclassify(request.slot, outcome);
            return outcome;
        }
        SaveSlotRecord record;
        record.name = request.slot;
        record.metadata = std::move(imported->metadata);
        outcome.record = std::move(record);
        outcome.image = !removeImage(request.slot);
        SaveCompletion done = completion(request, SaveEventType::Saved);
        done.imported = true;
        outcome.completion = std::move(done);
        return outcome;
    }

    [[nodiscard]] Outcome load(const Request& request)
    {
        Outcome outcome;
        SlotFile file = readSlot(request.slot);
        if (file.state == SaveSlotState::Ok || file.state == SaveSlotState::Backup)
        {
            SaveCompletion done =
                completion(request, file.state == SaveSlotState::Ok ? SaveEventType::Loaded
                                                                    : SaveEventType::LoadedBackup);
            done.world = std::string(file.world);
            outcome.completion = std::move(done);
        }
        else
        {
            outcome.completion = completion(request, SaveEventType::Failed, file.error);
        }
        if (file.exists)
        {
            outcome.record = recordOf(request.slot, file);
        }
        else
        {
            outcome.erased = request.slot;
        }
        return outcome;
    }

    [[nodiscard]] Outcome remove(const Request& request)
    {
        Outcome outcome;
        const FileError slotRemoved = storage->remove(pathOf(request.slot, kSlotSuffix));
        const FileError backupRemoved = storage->remove(pathOf(request.slot, kBackupSuffix));
        const auto failed = [](FileError error) {
            return error != FileError::None && error != FileError::NotFound;
        };
        if (failed(slotRemoved) || failed(backupRemoved))
        {
            ILL_LOG_WARN("Save", "cannot remove slot '{}' ({}, {})", request.slot, fileErrorName(slotRemoved),
                         fileErrorName(backupRemoved));
            outcome.completion = completion(request, SaveEventType::Failed, SaveError::IoError);
            reclassify(request.slot, outcome);
            return outcome;
        }
        static_cast<void>(removeImage(request.slot));
        const bool found = slotRemoved == FileError::None || backupRemoved == FileError::None;
        outcome.completion = found ? completion(request, SaveEventType::Removed)
                                   : completion(request, SaveEventType::Failed, SaveError::NotFound);
        outcome.erased = request.slot;
        return outcome;
    }

    SaveSystemDesc desc;
    std::unique_ptr<VfsSaveStorage> ownedStorage;
    SaveStorage* storage = nullptr;
    bool isAvailable = false;

    mutable std::mutex mutex;
    std::condition_variable wake; // a request was queued or the system stops
    std::condition_variable idle; // a request finished
    std::deque<Request> queue;
    std::vector<SaveCompletion> completed;
    std::vector<SaveSlotRecord> slots; // sorted by name
    u64 slotsVersion = 0;
    u64 nextTicket = 1;
    bool running = false;
    bool stopping = false;
    std::thread thread;

    // Buffers of the save thread.
    std::vector<std::byte> slotBytes;
    std::vector<std::byte> backupBytes;
    std::vector<std::byte> encodedBytes;

    SaveThumbnail thumbnail; // filled by desc.thumbnail on the requesting thread
};

SaveSystem::SaveSystem(const SaveSystemDesc& desc)
    : m_impl(std::make_unique<Impl>(desc))
{
}

SaveSystem::~SaveSystem()
{
    {
        const std::lock_guard lock(m_impl->mutex);
        m_impl->stopping = true;
    }
    m_impl->wake.notify_all();
    if (m_impl->thread.joinable())
    {
        m_impl->thread.join();
    }
}

bool SaveSystem::available() const noexcept
{
    return m_impl->isAvailable;
}

std::string_view SaveSystem::directory() const noexcept
{
    return m_impl->desc.directory;
}

i64 SaveSystem::now() const
{
    return m_impl->desc.clock ? m_impl->desc.clock() : systemSeconds();
}

Expected<u64, SaveError> SaveSystem::write(std::string_view slot, const SaveMetadata& metadata, i64 savedAt,
                                           std::string world, bool keepFile)
{
    if (!isValidSaveSlotName(slot))
    {
        return unexpected(SaveError::InvalidName);
    }
    if (!isValidSaveMetadata(metadata))
    {
        return unexpected(SaveError::InvalidValue);
    }
    if (!m_impl->isAvailable)
    {
        return unexpected(SaveError::Unavailable);
    }
    Request request;
    request.type = SaveRequestType::Write;
    request.slot = std::string(slot);
    request.title = std::string(metadata.title);
    request.custom = std::string(metadata.custom);
    request.playSeconds = metadata.playSeconds;
    request.gameVersion = metadata.gameVersion;
    request.savedAt = savedAt;
    request.world = std::move(world);
    request.keepFile = keepFile;
    if (m_impl->desc.thumbnail)
    {
        SaveThumbnail& thumbnail = m_impl->thumbnail;
        if (m_impl->desc.thumbnail(thumbnail))
        {
            if (isValidThumbnail(thumbnail))
            {
                request.thumbnail.width = thumbnail.width;
                request.thumbnail.height = thumbnail.height;
                request.thumbnail.rgba.assign(thumbnail.rgba.begin(), thumbnail.rgba.end());
            }
            else
            {
                ILL_LOG_WARN("Save",
                             "slot '{}' gets no image: the thumbnail function returned {}x{} pixels in {} "
                             "bytes",
                             slot, thumbnail.width, thumbnail.height, thumbnail.rgba.size());
            }
        }
    }
    return m_impl->enqueue(std::move(request));
}

Expected<u64, SaveError> SaveSystem::importFile(std::string_view slot, std::vector<std::byte> file)
{
    if (!isValidSaveSlotName(slot))
    {
        return unexpected(SaveError::InvalidName);
    }
    if (!m_impl->isAvailable)
    {
        return unexpected(SaveError::Unavailable);
    }
    Request request;
    request.type = SaveRequestType::Import;
    request.slot = std::string(slot);
    request.file = std::move(file);
    return m_impl->enqueue(std::move(request));
}

Expected<u64, SaveError> SaveSystem::load(std::string_view slot)
{
    if (!isValidSaveSlotName(slot))
    {
        return unexpected(SaveError::InvalidName);
    }
    if (!m_impl->isAvailable)
    {
        return unexpected(SaveError::Unavailable);
    }
    Request request;
    request.type = SaveRequestType::Load;
    request.slot = std::string(slot);
    return m_impl->enqueue(std::move(request));
}

Expected<u64, SaveError> SaveSystem::remove(std::string_view slot)
{
    if (!isValidSaveSlotName(slot))
    {
        return unexpected(SaveError::InvalidName);
    }
    if (!m_impl->isAvailable)
    {
        return unexpected(SaveError::Unavailable);
    }
    Request request;
    request.type = SaveRequestType::Remove;
    request.slot = std::string(slot);
    return m_impl->enqueue(std::move(request));
}

u64 SaveSystem::nextTicket() const
{
    const std::lock_guard lock(m_impl->mutex);
    return m_impl->nextTicket;
}

bool SaveSystem::busy() const
{
    const std::lock_guard lock(m_impl->mutex);
    m_impl->ensureStarted();
    return m_impl->running || !m_impl->queue.empty();
}

void SaveSystem::waitIdle()
{
    std::unique_lock lock(m_impl->mutex);
    m_impl->ensureStarted();
    m_impl->idle.wait(lock, [impl = m_impl.get()] { return !impl->running && impl->queue.empty(); });
}

void SaveSystem::takeCompleted(std::vector<SaveCompletion>& out)
{
    const std::lock_guard lock(m_impl->mutex);
    for (SaveCompletion& done : m_impl->completed)
    {
        out.push_back(std::move(done));
    }
    m_impl->completed.clear();
}

u64 SaveSystem::slotsVersion() const
{
    const std::lock_guard lock(m_impl->mutex);
    m_impl->ensureStarted();
    return m_impl->slotsVersion;
}

u64 SaveSystem::copySlots(std::vector<SaveSlotRecord>& out) const
{
    const std::lock_guard lock(m_impl->mutex);
    m_impl->ensureStarted();
    out = m_impl->slots;
    return m_impl->slotsVersion;
}

} // namespace illusion
