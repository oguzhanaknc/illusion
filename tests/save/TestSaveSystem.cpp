// The save thread and the host's SaveService (ADR 0058): request order, the slot list, backups, corrupt and
// newer files, writes stopped after each step, and shutdown.

#include "TestSupport.hpp"

#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/save/SaveCapture.hpp>
#include <illusion/save/SaveComponents.hpp>
#include <illusion/save/SaveSystem.hpp>
#include <illusion/save/SaveSystemService.hpp>

#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace illusion;

namespace
{

constexpr i64 kNow = 1'700'000'000;

// What a test reads back from pollEvents().
struct Seen
{
    SaveEventType type = SaveEventType::Failed;
    std::string slot;
    SaveError error = SaveError::IoError;
};

// A VFS layer that counts reads and simulates a crash: an atomic write takes three steps (the temporary file
// is written, flushed, renamed over the target), and once a write reaches step stopAfter + 1 every operation
// fails, leaving the files as the crash would. Runs on the save thread, so it uses no doctest assertion.
class StoppingStorage final : public SaveStorage
{
public:
    StoppingStorage(VirtualFileSystem& files, u32 stopAfter) noexcept
        : m_files(files)
        , m_inner(files)
        , m_stopAfter(stopAfter)
    {
    }

    FileError createDirectory(std::string_view path) override
    {
        return crashed() ? FileError::IoError : m_inner.createDirectory(path);
    }
    FileError list(std::string_view path, std::vector<DirectoryEntry>& out) override
    {
        return crashed() ? FileError::IoError : m_inner.list(path, out);
    }
    FileError read(std::string_view path, std::vector<std::byte>& out) override
    {
        ++reads;
        return crashed() ? FileError::IoError : m_inner.read(path, out);
    }
    FileError writeAtomic(std::string_view path, std::span<const std::byte> data) override
    {
        const std::string temporary = std::string(path) + ".tmp-test";
        if (!step())
        {
            return FileError::IoError;
        }
        if (const FileError written = m_files.writeFile(temporary, data); written != FileError::None)
        {
            return written;
        }
        if (!step()) // the flush
        {
            return FileError::IoError;
        }
        if (!step())
        {
            return FileError::IoError;
        }
        static_cast<void>(m_files.remove(path));
        return m_files.rename(temporary, path);
    }
    FileError remove(std::string_view path) override
    {
        return crashed() ? FileError::IoError : m_inner.remove(path);
    }

    u32 reads = 0;

private:
    [[nodiscard]] bool crashed() const noexcept { return m_crashed; }

    [[nodiscard]] bool step() noexcept
    {
        m_crashed = m_crashed || m_steps >= m_stopAfter;
        if (m_crashed)
        {
            return false;
        }
        ++m_steps;
        return true;
    }

    VirtualFileSystem& m_files;
    VfsSaveStorage m_inner;
    u32 m_stopAfter = 0;
    u32 m_steps = 0;
    bool m_crashed = false;
};

struct Fixture
{
    Fixture()
    {
        reflectCoreComponents();
        registerSaveComponents();
        REQUIRE(files.mount("user", directory.path, MountAccess::ReadWrite) == FileError::None);
        Transform at;
        at.position.x = 1.0f;
        world.create(Name{"Hero"}, at, Saved{});
    }

    [[nodiscard]] SaveSystemDesc desc(SaveStorage* storage = nullptr)
    {
        SaveSystemDesc result;
        result.files = &files;
        result.storage = storage;
        result.clock = [] {
            return kNow;
        };
        return result;
    }

    // desc() with a grey slot image.
    [[nodiscard]] SaveSystemDesc withImage(SaveStorage* storage = nullptr)
    {
        SaveSystemDesc result = desc(storage);
        result.thumbnail = [](SaveThumbnail& thumbnail) {
            thumbnail.width = kSaveThumbnailWidth;
            thumbnail.height = kSaveThumbnailHeight;
            thumbnail.rgba.assign(static_cast<usize>(thumbnail.width) * thumbnail.height * 4, std::byte{128});
            return true;
        };
        return result;
    }

    // The x position of the saved Hero (found again, since a load recreates it).
    [[nodiscard]] f32 heroX()
    {
        f32 x = std::numeric_limits<f32>::quiet_NaN();
        world.query<const Name, const Transform>().each([&x](const Name& name, const Transform& transform) {
            if (name.value == "Hero")
            {
                x = transform.position.x;
            }
        });
        return x;
    }

    void setHeroX(f32 x)
    {
        world.query<const Name, Transform>().each([x](const Name& name, Transform& transform) {
            if (name.value == "Hero")
            {
                transform.position.x = x;
            }
        });
    }

    // One update of the session: waits for the thread, so results arrive in the next update.
    void update(SaveSystemService& service) { service.update(world, &guids, true); }

    [[nodiscard]] static std::vector<Seen> events(SaveSystemService& service)
    {
        std::array<SaveEvent, 16> polled{};
        const u32 count = service.pollEvents(polled);
        std::vector<Seen> result;
        for (u32 i = 0; i < count && i < polled.size(); ++i)
        {
            result.push_back(Seen{polled[i].type, std::string(polled[i].slot), polled[i].error});
        }
        return result;
    }

    // Saves the World as @p slot with @p title and waits for the Saved event.
    void saveNow(SaveSystemService& service, std::string_view slot, std::string_view title = "Title")
    {
        REQUIRE(service.save(slot, SaveMetadata{title, 12.5, 3, "level-2"}).hasValue());
        update(service);
        update(service);
        const std::vector<Seen> seen = events(service);
        REQUIRE(seen.size() == 1);
        REQUIRE(seen[0].type == SaveEventType::Saved);
    }

    // Loads @p slot through a new SaveSystem (a restarted game) and returns the single event.
    [[nodiscard]] Seen loadFresh(std::string_view slot)
    {
        SaveSystem system(desc());
        SaveSystemService service(&system);
        REQUIRE(service.load(slot).hasValue());
        update(service);
        update(service);
        const std::vector<Seen> seen = events(service);
        REQUIRE(seen.size() == 1);
        return seen[0];
    }

    // Flips one byte of the file @p path at @p offset.
    void corrupt(std::string_view path, usize offset)
    {
        std::vector<std::byte> bytes;
        REQUIRE(files.readFile(path, bytes) == FileError::None);
        REQUIRE(offset < bytes.size());
        bytes[offset] ^= std::byte{0x5A};
        REQUIRE(files.writeFile(path, bytes) == FileError::None);
    }

    test::TempDirectory directory;
    VirtualFileSystem files;
    World world;
    GuidGenerator guids{11};
};

} // namespace

TEST_CASE("a slot saved through SaveSystemService loads back into the World")
{
    Fixture f;
    SaveSystem system(f.desc());
    SaveSystemService service(&system);
    CHECK(service.available());
    CHECK_FALSE(service.busy());
    REQUIRE(service.save("slot1", SaveMetadata{"Chapter 2", 12.5, 3, "level-2"}).hasValue());
    CHECK(service.busy());
    f.update(service); // captures and sends
    CHECK(Fixture::events(service).empty());
    CHECK(service.busy());
    f.update(service);
    const std::vector<Seen> saved = Fixture::events(service);
    REQUIRE(saved.size() == 1);
    CHECK(saved[0].type == SaveEventType::Saved);
    CHECK(saved[0].slot == "slot1");
    CHECK_FALSE(service.busy());
    CHECK(f.files.exists("user:/saves/slot1.save"));
    CHECK_FALSE(f.files.exists("user:/saves/slot1.save.bak"));

    const std::optional<SaveSlotInfo> info = service.slot("slot1");
    REQUIRE(info.has_value());
    CHECK(info->name == "slot1");
    CHECK(info->state == SaveSlotState::Ok);
    CHECK(info->savedAt == kNow);
    CHECK(info->metadata.title == "Chapter 2");
    CHECK(info->metadata.playSeconds == 12.5);
    CHECK(info->metadata.gameVersion == 3u);
    CHECK(info->metadata.custom == "level-2");
    CHECK(info->screenshot.empty());

    f.setHeroX(5.0f);
    REQUIRE(service.load("slot1").hasValue());
    f.update(service); // sends the load
    CHECK(f.heroX() == 5.0f);
    f.update(service); // applies it
    CHECK(f.heroX() == 1.0f);
    const std::vector<Seen> loaded = Fixture::events(service);
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].type == SaveEventType::Loaded);
    CHECK(loaded[0].slot == "slot1");
    f.update(service);
    CHECK(Fixture::events(service).empty()); // events last one update
}

TEST_CASE("save requests keep their call order")
{
    Fixture f;
    SaveSystem system(f.desc());
    SaveSystemService service(&system);
    f.setHeroX(2.0f);
    REQUIRE(service.save("a", SaveMetadata{}).hasValue());
    REQUIRE(service.load("a").hasValue()); // reads what the save before it wrote
    REQUIRE(service.remove("a").hasValue());
    REQUIRE(service.load("a").hasValue()); // finds nothing
    REQUIRE(service.save("b", SaveMetadata{}).hasValue());
    f.update(service);
    f.setHeroX(7.0f);
    f.update(service);
    const std::vector<Seen> seen = Fixture::events(service);
    REQUIRE(seen.size() == 5);
    CHECK(seen[0].type == SaveEventType::Saved);
    CHECK(seen[1].type == SaveEventType::Loaded);
    CHECK(seen[2].type == SaveEventType::Removed);
    CHECK(seen[3].type == SaveEventType::Failed);
    CHECK(seen[3].error == SaveError::NotFound);
    CHECK(seen[4].type == SaveEventType::Saved);
    CHECK(seen[4].slot == "b");
    CHECK(f.heroX() == 2.0f); // the load applied the state of the first save

    std::array<SaveSlotInfo, 4> slots{};
    REQUIRE(service.slots(slots) == 1);
    CHECK(slots[0].name == "b");
}

TEST_CASE("the save thread finishes requests in request order")
{
    Fixture f;
    SaveSystem system(f.desc());
    std::vector<u64> tickets;
    for (int i = 0; i < 24; ++i)
    {
        const std::string slot = "slot" + std::to_string(i % 5);
        const Expected<u64, SaveError> ticket =
            i % 3 == 2 ? system.remove(slot) : system.write(slot, SaveMetadata{}, kNow + i, "{}");
        REQUIRE(ticket.hasValue());
        tickets.push_back(*ticket);
    }
    system.waitIdle();
    CHECK_FALSE(system.busy());
    std::vector<SaveCompletion> done;
    system.takeCompleted(done);
    REQUIRE(done.size() == tickets.size());
    for (usize i = 0; i < done.size(); ++i)
    {
        CHECK(done[i].ticket == tickets[i]);
    }
}

TEST_CASE("invalid names, invalid metadata and a missing save system fail at once")
{
    Fixture f;
    SaveSystemService none(nullptr);
    CHECK_FALSE(none.available());
    CHECK(none.save("slot1", SaveMetadata{}).error() == SaveError::Unavailable);
    CHECK(none.load("slot1").error() == SaveError::Unavailable);
    CHECK(none.remove("slot1").error() == SaveError::Unavailable);
    CHECK_FALSE(none.busy());
    f.update(none);

    SaveSystem system(f.desc());
    SaveSystemService service(&system);
    CHECK(service.save("", SaveMetadata{}).error() == SaveError::InvalidName);
    CHECK(service.save("../escape", SaveMetadata{}).error() == SaveError::InvalidName);
    CHECK(service.load("with space").error() == SaveError::InvalidName);
    CHECK(service.remove(std::string(kMaxSaveSlotNameLength + 1, 'a')).error() == SaveError::InvalidName);
    const std::string longTitle(kMaxSaveTitleBytes + 1, 't');
    CHECK(service.save("slot1", SaveMetadata{longTitle, 0.0, 0, {}}).error() == SaveError::InvalidValue);
    CHECK(service.save("slot1", SaveMetadata{{}, -1.0, 0, {}}).error() == SaveError::InvalidValue);
    CHECK(service.save("slot1", SaveMetadata{{}, 0.0, 0, "\xFF"}).error() == SaveError::InvalidValue);
    CHECK_FALSE(service.busy()); // nothing was queued

    // A directory that cannot be created makes the system unavailable.
    VirtualFileSystem unmounted;
    SaveSystemDesc unmountedDesc = f.desc();
    unmountedDesc.files = &unmounted;
    SaveSystem unavailable(unmountedDesc);
    CHECK_FALSE(unavailable.available());
    SaveSystemService unavailableService(&unavailable);
    CHECK(unavailableService.save("slot1", SaveMetadata{}).error() == SaveError::Unavailable);
    CHECK(unavailable.write("slot1", SaveMetadata{}, kNow, "{}").error() == SaveError::Unavailable);
}

TEST_CASE("loading or removing a slot that does not exist fails with NotFound")
{
    Fixture f;
    const Seen loaded = f.loadFresh("nothing");
    CHECK(loaded.type == SaveEventType::Failed);
    CHECK(loaded.error == SaveError::NotFound);
    CHECK(loaded.slot == "nothing");

    SaveSystem system(f.desc());
    SaveSystemService service(&system);
    REQUIRE(service.remove("nothing").hasValue());
    f.update(service);
    f.update(service);
    const std::vector<Seen> seen = Fixture::events(service);
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].type == SaveEventType::Failed);
    CHECK(seen[0].error == SaveError::NotFound);
}

TEST_CASE("replacing a slot keeps the old file as its backup, and a corrupt slot file loads the backup")
{
    Fixture f;
    {
        SaveSystem system(f.desc());
        SaveSystemService service(&system);
        f.saveNow(service, "slot1", "first");
        f.setHeroX(3.0f);
        f.saveNow(service, "slot1", "second");
        CHECK(f.files.exists("user:/saves/slot1.save.bak"));
    }
    f.corrupt("user:/saves/slot1.save", kSaveHeaderBytes + 10);

    SaveSystem system(f.desc());
    SaveSystemService service(&system);
    f.update(service);
    const std::optional<SaveSlotInfo> info = service.slot("slot1");
    REQUIRE(info.has_value());
    CHECK(info->state == SaveSlotState::Backup);
    CHECK(info->metadata.title == "first");

    f.setHeroX(9.0f);
    REQUIRE(service.load("slot1").hasValue());
    f.update(service);
    f.update(service);
    const std::vector<Seen> seen = Fixture::events(service);
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].type == SaveEventType::LoadedBackup);
    CHECK(f.heroX() == 1.0f);

    // A missing slot file reads the backup too.
    REQUIRE(f.files.remove("user:/saves/slot1.save") == FileError::None);
    CHECK(f.loadFresh("slot1").type == SaveEventType::LoadedBackup);
}

TEST_CASE("a corrupt slot without a backup is listed as Corrupt and fails to load")
{
    Fixture f;
    {
        SaveSystem system(f.desc());
        SaveSystemService service(&system);
        f.saveNow(service, "slot1");
    }
    f.corrupt("user:/saves/slot1.save", kSaveHeaderBytes + 2);
    SaveSystem system(f.desc());
    SaveSystemService service(&system);
    f.update(service);
    const std::optional<SaveSlotInfo> info = service.slot("slot1");
    REQUIRE(info.has_value());
    CHECK(info->state == SaveSlotState::Corrupt);
    CHECK(info->metadata.title.empty());
    const Seen loaded = f.loadFresh("slot1");
    CHECK(loaded.type == SaveEventType::Failed);
    CHECK(loaded.error == SaveError::Corrupt);

    // Saving over a corrupt file does not make it the backup.
    f.saveNow(service, "slot1");
    CHECK_FALSE(f.files.exists("user:/saves/slot1.save.bak"));
    CHECK(service.slot("slot1")->state == SaveSlotState::Ok);
}

TEST_CASE(
    "a slot written by a newer container version is NewerVersion and is not loaded, not even its backup")
{
    Fixture f;
    {
        SaveSystem system(f.desc());
        SaveSystemService service(&system);
        f.saveNow(service, "slot1");
        f.saveNow(service, "slot1");
    }
    std::vector<std::byte> bytes;
    REQUIRE(f.files.readFile("user:/saves/slot1.save", bytes) == FileError::None);
    bytes[4] = std::byte{static_cast<u8>(kSaveFormatVersion + 1)}; // the version field, little-endian
    REQUIRE(f.files.writeFile("user:/saves/slot1.save", bytes) == FileError::None);

    SaveSystem system(f.desc());
    SaveSystemService service(&system);
    f.update(service);
    REQUIRE(service.slot("slot1").has_value());
    CHECK(service.slot("slot1")->state == SaveSlotState::NewerVersion);
    const Seen loaded = f.loadFresh("slot1");
    CHECK(loaded.type == SaveEventType::Failed);
    CHECK(loaded.error == SaveError::NewerVersion);
}

TEST_CASE("a write stopped after each step leaves the previous save loadable")
{
    // Steps of replacing a slot: the backup's temporary file is written, flushed and renamed, then the slot
    // file's temporary file is written, flushed and renamed; then the same three steps of its image.
    constexpr u32 kSlotSteps = 6;
    constexpr u32 kSteps = kSlotSteps + 3;
    for (u32 stopAfter = 0; stopAfter <= kSteps; ++stopAfter)
    {
        CAPTURE(stopAfter);
        Fixture f;
        {
            SaveSystem system(f.withImage());
            SaveSystemService service(&system);
            f.saveNow(service, "slot1", "previous");
        }
        f.setHeroX(4.0f);
        {
            StoppingStorage stopping(f.files, stopAfter);
            SaveSystem system(f.withImage(&stopping));
            REQUIRE(system.available());
            SaveSystemService service(&system);
            REQUIRE(service.save("slot1", SaveMetadata{"next", 0.0, 0, {}}).hasValue());
            f.update(service);
            f.update(service);
            const std::vector<Seen> seen = Fixture::events(service);
            REQUIRE(seen.size() == 1);
            // A failure at the image step does not fail the save.
            CHECK(seen[0].type == (stopAfter >= kSlotSteps ? SaveEventType::Saved : SaveEventType::Failed));
        }

        // The game starts again: the slot is listed and loads, with the new state only once the last
        // rename of the slot file happened. An image is listed in every case: the new one, or the previous
        // save's, which the crash left (ADR 0058).
        SaveSystem system(f.desc());
        SaveSystemService service(&system);
        f.update(service);
        std::array<SaveSlotInfo, 4> slots{};
        REQUIRE(service.slots(slots) == 1);
        CHECK(slots[0].state == SaveSlotState::Ok);
        CHECK(slots[0].metadata.title == (stopAfter >= kSlotSteps ? "next" : "previous"));
        CHECK(slots[0].screenshot == "user:/saves/slot1.png");
        f.setHeroX(8.0f);
        const Seen loaded = f.loadFresh("slot1");
        CHECK(loaded.type == SaveEventType::Loaded);
    }
}

TEST_CASE("the slot list is scanned once and kept current without reading files")
{
    Fixture f;
    REQUIRE(f.files.createDirectory("user:/saves") == FileError::None);
    REQUIRE(f.files.writeText("user:/saves/notes.txt", "not a slot") == FileError::None);
    REQUIRE(f.files.writeText("user:/saves/bad name.save", "not a slot") == FileError::None);
    REQUIRE(f.files.createDirectory("user:/saves/folder.save") == FileError::None);
    {
        SaveSystem system(f.desc());
        SaveSystemService service(&system);
        f.saveNow(service, "b");
        f.saveNow(service, "a");
        f.saveNow(service, "c");
    }
    StoppingStorage counting(f.files, std::numeric_limits<u32>::max());
    SaveSystem system(f.desc(&counting));
    SaveSystemService service(&system);
    f.update(service);
    std::array<SaveSlotInfo, 2> two{};
    CHECK(service.slots(two) == 3); // the total, though only two fit
    CHECK(two[0].name == "a");
    CHECK(two[1].name == "b");
    const u32 scanned = counting.reads;
    CHECK(scanned == 3);
    for (int i = 0; i < 10; ++i)
    {
        f.update(service);
        static_cast<void>(service.slots(two));
        static_cast<void>(service.slot("c"));
    }
    CHECK(counting.reads == scanned);

    REQUIRE(service.remove("b").hasValue());
    f.update(service);
    f.update(service);
    f.saveNow(service, "d");
    std::array<SaveSlotInfo, 8> all{};
    REQUIRE(service.slots(all) == 3);
    CHECK(all[0].name == "a");
    CHECK(all[1].name == "c");
    CHECK(all[2].name == "d");
    CHECK_FALSE(service.slot("b").has_value());
}

TEST_CASE("reading the slot list and the events does not allocate")
{
    Fixture f;
    SaveSystem system(f.desc());
    SaveSystemService service(&system);
    f.saveNow(service, "slot1");
    REQUIRE(service.save("slot2", SaveMetadata{}).hasValue());
    REQUIRE(service.load("missing").hasValue());
    f.update(service);
    f.update(service);
    system.waitIdle();
    std::array<SaveSlotInfo, 4> slots{};
    std::array<SaveEvent, 4> events{};
    u32 total = 0;
    bool busy = true;
    {
        const test::ScopedAllocationCounter counter;
        total += service.slots(slots);
        total += service.pollEvents(events);
        total += service.slot("slot2").has_value() ? 1u : 0u;
        busy = service.busy() || !service.available();
        const usize allocations = counter.count();
        CHECK(allocations == 0);
    }
    CHECK(total == 2 + 2 + 1);
    CHECK_FALSE(busy);
    {
        // An update with nothing to do does not allocate either.
        const test::ScopedAllocationCounter counter;
        service.update(f.world, &f.guids, false);
        const usize allocations = counter.count();
        CHECK(allocations == 0);
    }
}

TEST_CASE("results of requests sent before cancel() are dropped, and no load is applied")
{
    Fixture f;
    SaveSystem system(f.desc());
    SaveSystemService service(&system);
    f.saveNow(service, "slot1");
    f.setHeroX(6.0f);
    REQUIRE(service.load("slot1").hasValue());
    f.update(service); // sent
    REQUIRE(service.save("slot2", SaveMetadata{}).hasValue());
    service.cancel(); // drops slot2 before it is captured
    CHECK_FALSE(service.busy());
    f.update(service);
    f.update(service);
    CHECK(Fixture::events(service).empty());
    CHECK(f.heroX() == 6.0f);
    CHECK_FALSE(f.files.exists("user:/saves/slot2.save"));

    // Requests after cancel() work as before.
    f.saveNow(service, "slot3");
}

TEST_CASE("destroying a save system finishes the queued writes and removals")
{
    Fixture f;
    const Expected<SaveCapture, SaveFailure> captured = captureSave(f.world);
    REQUIRE(captured.hasValue());
    {
        SaveSystem system(f.desc());
        REQUIRE(system.write("gone", SaveMetadata{}, kNow, captured->world).hasValue());
        for (int i = 0; i < 16; ++i)
        {
            REQUIRE(
                system.write("slot" + std::to_string(i), SaveMetadata{}, kNow, captured->world).hasValue());
        }
        REQUIRE(system.load("slot3").hasValue());
        REQUIRE(system.remove("gone").hasValue());
    }
    for (int i = 0; i < 16; ++i)
    {
        CHECK(f.files.exists("user:/saves/slot" + std::to_string(i) + ".save"));
    }
    CHECK_FALSE(f.files.exists("user:/saves/gone.save"));
    CHECK(f.loadFresh("slot15").type == SaveEventType::Loaded);
}

TEST_CASE("a save system reports its directory and clock and is idle after its first scan")
{
    Fixture f;
    SaveSystem system(f.desc());
    CHECK(system.available());
    CHECK(system.directory() == "user:/saves");
    CHECK(system.now() == kNow);
    system.waitIdle();
    CHECK_FALSE(system.busy());
    std::vector<SaveSlotRecord> records;
    CHECK(system.copySlots(records) == system.slotsVersion());
    CHECK(records.empty());
}
