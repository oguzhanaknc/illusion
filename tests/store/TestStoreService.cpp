// StoreService (ADR 0056): store names, NullStoreService's achievements, stats, cloud files in the virtual
// file system, overlay and glyphs, and the counting proxy of the module tests.

#include "TestSupport.hpp"
#include "modules/CountingServices.hpp"

#include <illusion/platform/FileSystem.hpp>
#include <illusion/store/NullStoreService.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace illusion;

namespace
{

std::vector<std::byte> bytesOf(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

std::string textOf(std::span<const std::byte> bytes)
{
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

// A user mount on a temporary directory.
struct UserFiles
{
    UserFiles() { REQUIRE(files.mount("user", directory.path, MountAccess::ReadWrite) == FileError::None); }

    test::TempDirectory directory;
    VirtualFileSystem files;
};

} // namespace

TEST_CASE("store names are 1 to 64 portable characters that neither start nor end with a dot")
{
    CHECK(isValidStoreName("ACH_WIN_ONE_GAME"));
    CHECK(isValidStoreName("save-1.json"));
    CHECK(isValidStoreName(std::string(kMaxStoreNameLength, 'a')));
    CHECK_FALSE(isValidStoreName(""));
    CHECK_FALSE(isValidStoreName(std::string(kMaxStoreNameLength + 1, 'a')));
    CHECK_FALSE(isValidStoreName(".hidden"));
    CHECK_FALSE(isValidStoreName("save."));
    CHECK_FALSE(isValidStoreName("dir/save"));
    CHECK_FALSE(isValidStoreName("..\\save"));
    CHECK_FALSE(isValidStoreName("with space"));
    CHECK_FALSE(isValidStoreName("ü"));
    CHECK(storeErrorName(StoreError::QuotaExceeded) == "QuotaExceeded");
    CHECK(storeErrorName(StoreError::Unavailable) == "Unavailable");
}

TEST_CASE("the null store is no store: no user, no overlay and no glyphs")
{
    UserFiles user;
    NullStoreService store(user.files);
    store.update();
    CHECK_FALSE(store.available());
    CHECK(store.storeName() == "none");
    CHECK(store.userName().empty());
    CHECK(store.userId() == 0);
    CHECK_FALSE(store.overlayActive());
    CHECK_FALSE(store.openOverlay(StoreOverlayPage::Achievements));
    CHECK_FALSE(store.overlayActive());
    CHECK_FALSE(store.glyphPath(GamepadOrigin{.type = GamepadType::PS5, .button = GamepadButton::South}));
    CHECK_FALSE(store.glyphPath(GamepadOrigin{.type = GamepadType::XboxOne, .axis = GamepadAxis::LeftX}));
    CHECK(store.cloudDirectory() == "user:/cloud");
    CHECK(store.storeStats().hasValue());
}

TEST_CASE("the null store unlocks, queries and clears achievements in memory")
{
    UserFiles user;
    NullStoreService store(user.files);
    CHECK_FALSE(*store.isAchievementUnlocked("WIN"));
    REQUIRE(store.unlockAchievement("WIN").hasValue());
    REQUIRE(store.unlockAchievement("WIN").hasValue()); // again: still unlocked
    CHECK(*store.isAchievementUnlocked("WIN"));
    CHECK(*store.achievementProgress("WIN") == AchievementProgress{1, 1});
    REQUIRE(store.clearAchievement("WIN").hasValue());
    CHECK_FALSE(*store.isAchievementUnlocked("WIN"));
    CHECK(store.clearAchievement("NEVER_SET").hasValue());

    CHECK(store.unlockAchievement("").error() == StoreError::InvalidName);
    CHECK(store.clearAchievement("a b").error() == StoreError::InvalidName);
    CHECK(store.isAchievementUnlocked(".x").error() == StoreError::InvalidName);
    CHECK(store.achievementProgress("x/y").error() == StoreError::InvalidName);

    // A second store starts empty: achievements are not files.
    NullStoreService other(user.files);
    REQUIRE(store.unlockAchievement("WIN").hasValue());
    CHECK_FALSE(*other.isAchievementUnlocked("WIN"));
}

TEST_CASE("achievement progress is clamped, unlocks at its max and stays once unlocked")
{
    UserFiles user;
    NullStoreService store(user.files);
    CHECK(*store.achievementProgress("WALK") == AchievementProgress{0, 0});
    REQUIRE(store.setAchievementProgress("WALK", {1, 4}).hasValue());
    CHECK(*store.achievementProgress("WALK") == AchievementProgress{1, 4});
    CHECK_FALSE(*store.isAchievementUnlocked("WALK"));
    CHECK(store.setAchievementProgress("WALK", {1, 0}).error() == StoreError::InvalidValue);
    CHECK(*store.achievementProgress("WALK") == AchievementProgress{1, 4});

    REQUIRE(store.setAchievementProgress("WALK", {9, 4}).hasValue());
    CHECK(*store.isAchievementUnlocked("WALK"));
    CHECK(*store.achievementProgress("WALK") == AchievementProgress{4, 4});
    REQUIRE(store.setAchievementProgress("WALK", {2, 4}).hasValue());
    CHECK(*store.achievementProgress("WALK") == AchievementProgress{4, 4});

    REQUIRE(store.clearAchievement("WALK").hasValue());
    CHECK(*store.achievementProgress("WALK") == AchievementProgress{0, 0});
    CHECK(store.setAchievementProgress("", {1, 2}).error() == StoreError::InvalidName);
}

TEST_CASE("the null store keeps integer and float stats apart")
{
    UserFiles user;
    NullStoreService store(user.files);
    CHECK(store.statInt("kills").error() == StoreError::NotFound);
    CHECK(store.statFloat("distance").error() == StoreError::NotFound);
    REQUIRE(store.setStatInt("kills", 3).hasValue());
    REQUIRE(store.setStatInt("kills", -7).hasValue());
    CHECK(*store.statInt("kills") == -7);
    REQUIRE(store.setStatFloat("distance", 12.5f).hasValue());
    CHECK(*store.statFloat("distance") == 12.5f);

    CHECK(store.setStatFloat("kills", 1.0f).error() == StoreError::TypeMismatch);
    CHECK(store.statFloat("kills").error() == StoreError::TypeMismatch);
    CHECK(store.setStatInt("distance", 1).error() == StoreError::TypeMismatch);
    CHECK(store.statInt("distance").error() == StoreError::TypeMismatch);

    CHECK(store.setStatFloat("bad", std::numeric_limits<f32>::quiet_NaN()).error() ==
          StoreError::InvalidValue);
    CHECK(store.setStatFloat("bad", std::numeric_limits<f32>::infinity()).error() ==
          StoreError::InvalidValue);
    CHECK(store.statFloat("bad").error() == StoreError::NotFound);
    CHECK(store.setStatInt("no name", 1).error() == StoreError::InvalidName);
    CHECK(store.statInt("").error() == StoreError::InvalidName);
    CHECK(store.statFloat(std::string(65, 'x')).error() == StoreError::InvalidName);
    CHECK(store.storeStats().hasValue());
}

TEST_CASE("cloud files are written, read, listed and removed as files of user:/cloud")
{
    UserFiles user;
    NullStoreService store(user.files);
    std::array<std::string_view, 4> names{};
    CHECK(store.listCloudFiles(names) == 0); // no directory yet
    CHECK_FALSE(store.cloudFileExists("slot1.save"));
    CHECK(store.readCloudFile("slot1.save", {}).error() == StoreError::NotFound);
    CHECK(store.removeCloudFile("slot1.save").error() == StoreError::NotFound);

    const std::vector<std::byte> save = bytesOf("level=3");
    REQUIRE(store.writeCloudFile("slot1.save", save).hasValue());
    REQUIRE(store.writeCloudFile("a.cfg", bytesOf("")).hasValue());
    REQUIRE(store.writeCloudFile("Z-last", bytesOf("z")).hasValue());
    CHECK(std::filesystem::exists(user.directory.path / "cloud" / "slot1.save"));
    CHECK(store.cloudFileExists("slot1.save"));

    // The size alone, then the whole file, then only its start.
    CHECK(*store.readCloudFile("slot1.save", {}) == save.size());
    std::array<std::byte, 16> buffer{};
    CHECK(*store.readCloudFile("slot1.save", buffer) == save.size());
    CHECK(textOf(std::span(buffer).first(save.size())) == "level=3");
    std::array<std::byte, 3> small{};
    CHECK(*store.readCloudFile("slot1.save", small) == save.size());
    CHECK(textOf(small) == "lev");
    CHECK(*store.readCloudFile("a.cfg", buffer) == 0);

    // Sorted by name; a short span gets the first names and the count of all.
    CHECK(store.listCloudFiles(names) == 3);
    CHECK(names[0] == "Z-last");
    CHECK(names[1] == "a.cfg");
    CHECK(names[2] == "slot1.save");
    std::array<std::string_view, 1> one{};
    CHECK(store.listCloudFiles(one) == 3);
    CHECK(one[0] == "Z-last");
    CHECK(store.listCloudFiles({}) == 3);

    REQUIRE(store.writeCloudFile("slot1.save", bytesOf("level=4")).hasValue());
    CHECK(*store.readCloudFile("slot1.save", buffer) == 7);
    CHECK(textOf(std::span(buffer).first(7)) == "level=4");
    REQUIRE(store.removeCloudFile("slot1.save").hasValue());
    CHECK_FALSE(store.cloudFileExists("slot1.save"));
    CHECK(store.listCloudFiles(names) == 2);
}

TEST_CASE("cloud files persist across stores on the same directory")
{
    UserFiles user;
    {
        NullStoreService first(user.files);
        REQUIRE(first.writeCloudFile("progress.json", bytesOf("{\"level\":7}")).hasValue());
    }
    NullStoreService second(user.files);
    CHECK(second.cloudFileExists("progress.json"));
    std::array<std::byte, 32> buffer{};
    const Expected<u64, StoreError> read = second.readCloudFile("progress.json", buffer);
    REQUIRE(read.hasValue());
    CHECK(textOf(std::span(buffer).first(*read)) == "{\"level\":7}");
}

TEST_CASE("cloud names that are not store names or that Windows reserves are rejected")
{
    UserFiles user;
    NullStoreService store(user.files);
    const std::vector<std::byte> data = bytesOf("x");
    for (const std::string_view name : {"", "../escape", "dir/file", ".hidden", "file.", "con", "CON.save",
                                        "nul.txt", "Com1", "lpt9.dat", "a b"})
    {
        CAPTURE(name);
        CHECK(store.writeCloudFile(name, data).error() == StoreError::InvalidName);
        CHECK(store.readCloudFile(name, {}).error() == StoreError::InvalidName);
        CHECK(store.removeCloudFile(name).error() == StoreError::InvalidName);
        CHECK_FALSE(store.cloudFileExists(name));
    }
    CHECK(store.writeCloudFile("console.save", data).hasValue());
    CHECK(store.writeCloudFile("com10", data).hasValue());
    CHECK(store.writeCloudFile("lpt0", data).hasValue());
}

TEST_CASE("the cloud quota counts the files and refuses a write that does not fit")
{
    UserFiles user;
    NullStoreService store(user.files, NullStoreDesc{.cloudQuotaBytes = 10});
    CHECK(store.cloudQuota()->totalBytes == 10);
    CHECK(store.cloudQuota()->availableBytes == 10);
    REQUIRE(store.writeCloudFile("a", bytesOf("123456")).hasValue());
    CHECK(store.cloudQuota()->availableBytes == 4);
    CHECK(store.writeCloudFile("b", bytesOf("12345")).error() == StoreError::QuotaExceeded);
    CHECK_FALSE(store.cloudFileExists("b"));
    REQUIRE(store.writeCloudFile("b", bytesOf("1234")).hasValue());
    CHECK(store.cloudQuota()->availableBytes == 0);
    // Replacing a file counts its new size only.
    REQUIRE(store.writeCloudFile("a", bytesOf("12")).hasValue());
    CHECK(store.cloudQuota()->availableBytes == 4);
    REQUIRE(store.removeCloudFile("b").hasValue());
    CHECK(store.cloudQuota()->availableBytes == 8);

    // Files that are not cloud files, and directories, do not count and are not listed.
    REQUIRE(user.files.writeText("user:/cloud/.hidden", "123456789") == FileError::None);
    REQUIRE(user.files.createDirectory("user:/cloud/folder") == FileError::None);
    CHECK(store.cloudQuota()->availableBytes == 8);
    std::array<std::string_view, 4> names{};
    CHECK(store.listCloudFiles(names) == 1);
    CHECK(store.readCloudFile("folder", {}).error() == StoreError::NotFound);
    CHECK_FALSE(store.cloudFileExists("folder"));
}

TEST_CASE("a cloud file over 100 MiB is too large whatever the quota")
{
    UserFiles user;
    NullStoreService store(user.files, NullStoreDesc{.cloudQuotaBytes = 4 * kMaxCloudFileBytes});
    CHECK(store.writeCloudFile("empty", std::span<const std::byte>{}).hasValue());
    std::vector<std::byte> big(kMaxCloudFileBytes + 1);
    CHECK(store.writeCloudFile("big", big).error() == StoreError::TooLarge);
    CHECK_FALSE(store.cloudFileExists("big"));
}

TEST_CASE("cloud files need a writable mount, else the store is unavailable")
{
    test::TempDirectory directory;
    VirtualFileSystem files;
    NullStoreService unmounted(files);
    CHECK(unmounted.writeCloudFile("save", bytesOf("x")).error() == StoreError::Unavailable);
    CHECK(unmounted.cloudQuota().error() == StoreError::Unavailable);
    CHECK(unmounted.readCloudFile("save", {}).error() == StoreError::Unavailable);
    std::array<std::string_view, 2> names{};
    CHECK(unmounted.listCloudFiles(names) == 0);

    REQUIRE(files.mount("user", directory.path, MountAccess::ReadOnly) == FileError::None);
    NullStoreService readOnly(files);
    CHECK(readOnly.writeCloudFile("save", bytesOf("x")).error() == StoreError::Unavailable);
    CHECK(readOnly.cloudQuota()->availableBytes == readOnly.cloudQuota()->totalBytes);
}

TEST_CASE("a null store can keep its cloud files in another directory")
{
    UserFiles user;
    NullStoreService store(user.files, NullStoreDesc{.cloudDirectory = "user:/saves/cloud/"});
    CHECK(store.cloudDirectory() == "user:/saves/cloud");
    REQUIRE(store.writeCloudFile("slot", bytesOf("1")).hasValue());
    CHECK(std::filesystem::exists(user.directory.path / "saves" / "cloud" / "slot"));
}

TEST_CASE("the counting store forwards every call and counts it once")
{
    UserFiles user;
    NullStoreService inner(user.files);
    test::CountingStore store(&inner);
    StoreService& service = store;
    CHECK_FALSE(service.available());
    CHECK(service.storeName() == "none");
    CHECK(service.userName().empty());
    CHECK(service.userId() == 0);
    CHECK(service.unlockAchievement("A").hasValue());
    CHECK(service.clearAchievement("A").hasValue());
    CHECK_FALSE(*service.isAchievementUnlocked("A"));
    CHECK(service.setAchievementProgress("B", {1, 2}).hasValue());
    CHECK(*service.achievementProgress("B") == AchievementProgress{1, 2});
    CHECK(service.setStatInt("i", 2).hasValue());
    CHECK(*service.statInt("i") == 2);
    CHECK(service.setStatFloat("f", 0.5f).hasValue());
    CHECK(*service.statFloat("f") == 0.5f);
    CHECK(service.storeStats().hasValue());
    const std::vector<std::byte> data = bytesOf("ab");
    CHECK(service.writeCloudFile("c", data).hasValue());
    CHECK(*service.readCloudFile("c", {}) == 2);
    std::array<std::string_view, 2> names{};
    CHECK(service.listCloudFiles(names) == 1);
    CHECK(service.cloudFileExists("c"));
    CHECK(service.removeCloudFile("c").hasValue());
    CHECK(service.cloudQuota().hasValue());
    CHECK_FALSE(service.overlayActive());
    CHECK_FALSE(service.openOverlay(StoreOverlayPage::Friends));
    CHECK_FALSE(service.glyphPath({}));
    CHECK(store.everyFunctionCalledOnce());
}

TEST_CASE("polling the null store each frame allocates nothing")
{
    UserFiles user;
    NullStoreService store(user.files);
    REQUIRE(store.unlockAchievement("WIN").hasValue());
    REQUIRE(store.setStatInt("score", 1).hasValue());
    const test::ScopedAllocationCounter allocations;
    for (int frame = 0; frame < 10; ++frame)
    {
        store.update();
        CHECK_FALSE(store.overlayActive());
        CHECK(*store.isAchievementUnlocked("WIN"));
        CHECK(store.setStatInt("score", frame).hasValue());
    }
    CHECK(allocations.count() == 0);
}
