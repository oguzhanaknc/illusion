// Slot images and the cloud mirror of saved games (ADR 0058): the sidecar `<slot>.png`, cloud files newer
// and older than the local slot, corrupt cloud files, a full cloud and removals.

#include "TestSupport.hpp"
#include "save/CloudTestStore.hpp"

#include <illusion/assets/Image.hpp>
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
#include <string>
#include <vector>

using namespace illusion;

namespace
{

constexpr i64 kLocalTime = 1'700'000'000;

// Fails every write of a slot image; everything else goes to the files.
class FailingImageStorage final : public SaveStorage
{
public:
    explicit FailingImageStorage(VirtualFileSystem& files) noexcept
        : m_inner(files)
    {
    }

    FileError createDirectory(std::string_view path) override { return m_inner.createDirectory(path); }
    FileError list(std::string_view path, std::vector<DirectoryEntry>& out) override
    {
        return m_inner.list(path, out);
    }
    FileError read(std::string_view path, std::vector<std::byte>& out) override
    {
        return m_inner.read(path, out);
    }
    FileError writeAtomic(std::string_view path, std::span<const std::byte> data) override
    {
        return path.ends_with(".png") ? FileError::IoError : m_inner.writeAtomic(path, data);
    }
    FileError remove(std::string_view path) override { return m_inner.remove(path); }

private:
    VfsSaveStorage m_inner;
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
        result.clock = [this] {
            return now;
        };
        return result;
    }

    // A slot image of one colour, counting the calls.
    [[nodiscard]] SaveSystemDesc withImage(SaveStorage* storage = nullptr)
    {
        SaveSystemDesc result = desc(storage);
        result.thumbnail = [this](SaveThumbnail& thumbnail) {
            ++thumbnails;
            thumbnail.width = kSaveThumbnailWidth;
            thumbnail.height = kSaveThumbnailHeight;
            thumbnail.rgba.assign(static_cast<usize>(thumbnail.width) * thumbnail.height * 4, std::byte{200});
            return true;
        };
        return result;
    }

    void update(SaveSystemService& service) { service.update(world, &guids, true); }

    [[nodiscard]] static std::vector<SaveEventType> events(SaveSystemService& service)
    {
        std::array<SaveEvent, 8> polled{};
        const u32 count = service.pollEvents(polled);
        std::vector<SaveEventType> result;
        for (u32 i = 0; i < count && i < polled.size(); ++i)
        {
            result.push_back(polled[i].type);
        }
        return result;
    }

    void saveNow(SaveSystemService& service, std::string_view slot, std::string_view title)
    {
        REQUIRE(service.save(slot, SaveMetadata{title, 1.0, 1, {}}).hasValue());
        update(service);
        update(service);
        const std::vector<SaveEventType> seen = events(service);
        REQUIRE(seen.size() == 1);
        REQUIRE(seen[0] == SaveEventType::Saved);
    }

    // A slot file of the World titled @p title, written at @p savedAt.
    [[nodiscard]] std::vector<std::byte> slotFile(std::string_view title, i64 savedAt)
    {
        const Expected<SaveCapture, SaveFailure> captured = captureSave(world);
        REQUIRE(captured.hasValue());
        std::vector<std::byte> bytes;
        REQUIRE(encodeSaveFile(SaveMetadata{title, 2.0, 1, {}}, savedAt, captured->world, bytes).hasValue());
        return bytes;
    }

    [[nodiscard]] std::string titleOf(std::string_view path)
    {
        std::vector<std::byte> bytes;
        REQUIRE(files.readFile(path, bytes) == FileError::None);
        const Expected<DecodedSave, SaveError> decoded = decodeSaveFile(bytes);
        REQUIRE(decoded.hasValue());
        return decoded->metadata.title;
    }

    test::TempDirectory directory;
    VirtualFileSystem files;
    World world;
    GuidGenerator guids{11};
    i64 now = kLocalTime;
    u32 thumbnails = 0;
};

} // namespace

TEST_CASE("a saved slot gets the image of the thumbnail function as its sidecar")
{
    Fixture f;
    SaveSystem system(f.withImage());
    SaveSystemService service(&system);
    f.saveNow(service, "slot1", "First");
    CHECK(f.thumbnails == 1);
    REQUIRE(service.slot("slot1").has_value());
    CHECK(service.slot("slot1")->screenshot == "user:/saves/slot1.png");
    std::vector<std::byte> png;
    REQUIRE(f.files.readFile("user:/saves/slot1.png", png) == FileError::None);
    const std::optional<Image> image = decodeImage(png, true);
    REQUIRE(image.has_value());
    CHECK(image->width() == kSaveThumbnailWidth);
    CHECK(image->height() == kSaveThumbnailHeight);
    CHECK(image->data.at(0) == std::byte{200});

    // A restarted game lists the image from its scan.
    SaveSystem restarted(f.desc());
    SaveSystemService again(&restarted);
    f.update(again);
    REQUIRE(again.slot("slot1").has_value());
    CHECK(again.slot("slot1")->screenshot == "user:/saves/slot1.png");

    // A slot saved without an image drops the image of the save it replaces.
    f.saveNow(again, "slot1", "Second");
    CHECK(again.slot("slot1")->screenshot.empty());
    CHECK_FALSE(f.files.exists("user:/saves/slot1.png"));
}

TEST_CASE("removing a slot removes its image, and an image alone is no slot")
{
    Fixture f;
    SaveSystem system(f.withImage());
    SaveSystemService service(&system);
    f.saveNow(service, "slot1", "First");
    REQUIRE(f.files.exists("user:/saves/slot1.png"));
    REQUIRE(service.remove("slot1").hasValue());
    f.update(service);
    f.update(service);
    CHECK(Fixture::events(service) == std::vector<SaveEventType>{SaveEventType::Removed});
    CHECK_FALSE(f.files.exists("user:/saves/slot1.png"));

    REQUIRE(f.files.writeText("user:/saves/orphan.png", "not an image") == FileError::None);
    SaveSystem restarted(f.desc());
    SaveSystemService again(&restarted);
    f.update(again);
    std::array<SaveSlotInfo, 2> slots{};
    CHECK(again.slots(slots) == 0);
}

TEST_CASE("a slot whose image cannot be written is saved without one")
{
    Fixture f;
    {
        SaveSystem system(f.withImage());
        SaveSystemService service(&system);
        f.saveNow(service, "slot1", "First");
    }
    FailingImageStorage failing(f.files);
    SaveSystem system(f.withImage(&failing));
    SaveSystemService service(&system);
    f.saveNow(service, "slot1", "Second");
    REQUIRE(service.slot("slot1").has_value());
    CHECK(service.slot("slot1")->state == SaveSlotState::Ok);
    CHECK(service.slot("slot1")->metadata.title == "Second");
    CHECK(service.slot("slot1")->screenshot.empty());
    CHECK_FALSE(f.files.exists("user:/saves/slot1.png")); // the old image belonged to the replaced save
}

TEST_CASE("a thumbnail function without a frame or with a wrong size gives no image")
{
    Fixture f;
    SaveSystemDesc desc = f.desc();
    bool frame = false;
    desc.thumbnail = [&frame](SaveThumbnail& thumbnail) {
        thumbnail.width = 4;
        thumbnail.height = 4;
        thumbnail.rgba.assign(8, std::byte{1}); // too short
        return frame;
    };
    SaveSystem system(desc);
    SaveSystemService service(&system);
    f.saveNow(service, "slot1", "None");
    CHECK(service.slot("slot1")->screenshot.empty());
    frame = true;
    f.saveNow(service, "slot1", "Wrong");
    CHECK(service.slot("slot1")->screenshot.empty());
    CHECK_FALSE(f.files.exists("user:/saves/slot1.png"));
}

TEST_CASE("a newer cloud file replaces the local slot, which becomes its backup")
{
    Fixture f;
    {
        SaveSystem system(f.withImage());
        SaveSystemService service(&system);
        f.saveNow(service, "slot1", "local");
    }
    test::CloudTestStore cloud;
    cloud.files["slot1.save"] = f.slotFile("cloud", kLocalTime + 60);
    cloud.files["slot2.save"] = f.slotFile("only in the cloud", kLocalTime - 60);
    cloud.files["notes.txt"] = f.slotFile("not a slot", kLocalTime + 60);
    cloud.files["bad name.save"] = f.slotFile("not a slot name", kLocalTime + 60);

    SaveSystem system(f.desc());
    SaveSystemService service(&system, &cloud);
    f.update(service); // queues the imports
    CHECK_FALSE(service.busy());
    f.update(service);
    CHECK(Fixture::events(service).empty());
    std::array<SaveSlotInfo, 4> slots{};
    REQUIRE(service.slots(slots) == 2);
    CHECK(slots[0].name == "slot1");
    CHECK(slots[0].metadata.title == "cloud");
    CHECK(slots[0].savedAt == kLocalTime + 60);
    CHECK(slots[0].screenshot.empty());
    CHECK(slots[1].name == "slot2");
    CHECK(slots[1].metadata.title == "only in the cloud");
    CHECK(f.titleOf("user:/saves/slot1.save.bak") == "local");
    CHECK_FALSE(f.files.exists("user:/saves/slot1.png"));
    CHECK(cloud.writes == 0); // an import is not mirrored back
}

TEST_CASE("an older or equally old cloud file changes nothing")
{
    Fixture f;
    {
        SaveSystem system(f.withImage());
        SaveSystemService service(&system);
        f.saveNow(service, "older", "local");
        f.saveNow(service, "same", "local");
    }
    test::CloudTestStore cloud;
    cloud.files["older.save"] = f.slotFile("cloud", kLocalTime - 1);
    cloud.files["same.save"] = f.slotFile("cloud", kLocalTime);

    SaveSystem system(f.desc());
    SaveSystemService service(&system, &cloud);
    f.update(service);
    f.update(service);
    CHECK(Fixture::events(service).empty());
    CHECK(service.slot("older")->metadata.title == "local");
    CHECK(service.slot("same")->metadata.title == "local");
    CHECK(service.slot("older")->screenshot == "user:/saves/older.png");
    CHECK_FALSE(f.files.exists("user:/saves/older.save.bak"));
}

TEST_CASE("a corrupt cloud file is ignored and the local slot is kept")
{
    Fixture f;
    {
        SaveSystem system(f.desc());
        SaveSystemService service(&system);
        f.saveNow(service, "slot1", "local");
    }
    test::CloudTestStore cloud;
    std::vector<std::byte> corrupt = f.slotFile("cloud", kLocalTime + 60);
    corrupt.back() ^= std::byte{0x5A};
    cloud.files["slot1.save"] = corrupt;
    cloud.files["slot2.save"] = std::vector<std::byte>(3, std::byte{1});

    SaveSystem system(f.desc());
    SaveSystemService service(&system, &cloud);
    f.update(service);
    f.update(service);
    CHECK(Fixture::events(service).empty());
    std::array<SaveSlotInfo, 4> slots{};
    REQUIRE(service.slots(slots) == 1);
    CHECK(slots[0].metadata.title == "local");
    CHECK_FALSE(f.files.exists("user:/saves/slot2.save"));
}

TEST_CASE("written slots are copied to the cloud, and a full cloud does not fail the save")
{
    Fixture f;
    test::CloudTestStore cloud;
    SaveSystem system(f.desc());
    SaveSystemService service(&system, &cloud);
    f.saveNow(service, "slot1", "First");
    REQUIRE(cloud.files.contains("slot1.save"));
    std::vector<std::byte> local;
    REQUIRE(f.files.readFile("user:/saves/slot1.save", local) == FileError::None);
    CHECK(cloud.files.at("slot1.save") == local);

    cloud.full = true;
    f.saveNow(service, "slot2", "Second"); // still a single Saved event
    CHECK(cloud.writes == 2);
    CHECK_FALSE(cloud.files.contains("slot2.save"));
    CHECK(f.files.exists("user:/saves/slot2.save"));
}

TEST_CASE("removing a slot removes its cloud file")
{
    Fixture f;
    test::CloudTestStore cloud;
    SaveSystem system(f.desc());
    SaveSystemService service(&system, &cloud);
    f.saveNow(service, "slot1", "First");
    REQUIRE(cloud.files.contains("slot1.save"));
    REQUIRE(service.remove("slot1").hasValue());
    f.update(service);
    f.update(service);
    CHECK(Fixture::events(service) == std::vector<SaveEventType>{SaveEventType::Removed});
    CHECK_FALSE(cloud.files.contains("slot1.save"));

    // A removal that finds nothing is not mirrored.
    REQUIRE(service.remove("slot1").hasValue());
    f.update(service);
    f.update(service);
    CHECK(cloud.removals == 1);
}

TEST_CASE("cloud imports sent before cancel() are dropped, and later requests still work")
{
    Fixture f;
    test::CloudTestStore cloud;
    cloud.files["slot1.save"] = f.slotFile("cloud", kLocalTime);
    SaveSystem system(f.desc());
    SaveSystemService service(&system, &cloud);
    f.update(service); // queues the import
    service.cancel();
    f.update(service);
    CHECK(Fixture::events(service).empty());
    CHECK(service.slot("slot1")->metadata.title == "cloud"); // the file was still taken
    f.saveNow(service, "slot2", "after");
}

TEST_CASE("an idle update of a mirrored service does not allocate")
{
    Fixture f;
    test::CloudTestStore cloud;
    SaveSystem system(f.desc());
    SaveSystemService service(&system, &cloud);
    f.saveNow(service, "slot1", "First");
    system.waitIdle();
    std::array<SaveSlotInfo, 2> slots{};
    std::array<SaveEvent, 2> events{};
    const test::ScopedAllocationCounter counter;
    service.update(f.world, &f.guids, false);
    const u32 total = service.slots(slots) + service.pollEvents(events);
    const usize allocations = counter.count();
    CHECK(allocations == 0);
    CHECK(total == 1);
}
