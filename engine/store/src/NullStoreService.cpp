#include <illusion/core/Profiler.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/store/NullStoreService.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace illusion
{
namespace
{

char upper(char c) noexcept
{
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

// Windows gives these stems to devices, whatever the extension; a cloud file must not be named after one on
// any platform, so that a game behaves the same everywhere.
bool isDeviceName(std::string_view name) noexcept
{
    const std::string_view stem = name.substr(0, name.find('.'));
    if (stem.size() != 3 && stem.size() != 4)
    {
        return false;
    }
    std::array<char, 4> text{};
    for (usize i = 0; i < stem.size(); ++i)
    {
        text[i] = upper(stem[i]);
    }
    const std::string_view word(text.data(), 3);
    if (stem.size() == 3)
    {
        return word == "CON" || word == "PRN" || word == "AUX" || word == "NUL";
    }
    return (word == "COM" || word == "LPT") && text[3] >= '1' && text[3] <= '9';
}

bool isCloudName(std::string_view name) noexcept
{
    return isValidStoreName(name) && !isDeviceName(name);
}

StoreError storeErrorOf(FileError error) noexcept
{
    switch (error)
    {
    case FileError::NotFound:
        return StoreError::NotFound;
    case FileError::InvalidPath:
    case FileError::UnknownMount:
    case FileError::ReadOnly:
    case FileError::Unsupported:
        return StoreError::Unavailable;
    default:
        return StoreError::IoError;
    }
}

} // namespace

NullStoreService::NullStoreService(VirtualFileSystem& files, const NullStoreDesc& desc)
    : m_files(files)
    , m_directory(desc.cloudDirectory)
    , m_quota(desc.cloudQuotaBytes)
{
    while (m_directory.size() > 1 && m_directory.back() == '/' && !m_directory.ends_with(":/"))
    {
        m_directory.pop_back();
    }
}

NullStoreService::~NullStoreService() = default;

void NullStoreService::update()
{
    ILL_PROFILE_SCOPE();
}

bool NullStoreService::available() const
{
    return false;
}

std::string_view NullStoreService::storeName() const
{
    return "none";
}

std::string_view NullStoreService::userName() const
{
    return {};
}

u64 NullStoreService::userId() const
{
    return 0;
}

Expected<void, StoreError> NullStoreService::unlockAchievement(std::string_view name)
{
    if (!isValidStoreName(name))
    {
        return unexpected(StoreError::InvalidName);
    }
    auto it = m_achievements.find(name);
    if (it == m_achievements.end())
    {
        it = m_achievements.emplace(std::string(name), detail::NullAchievement{}).first;
    }
    it->second.unlocked = true;
    return {};
}

Expected<void, StoreError> NullStoreService::clearAchievement(std::string_view name)
{
    if (!isValidStoreName(name))
    {
        return unexpected(StoreError::InvalidName);
    }
    if (const auto it = m_achievements.find(name); it != m_achievements.end())
    {
        m_achievements.erase(it);
    }
    return {};
}

Expected<bool, StoreError> NullStoreService::isAchievementUnlocked(std::string_view name) const
{
    if (!isValidStoreName(name))
    {
        return unexpected(StoreError::InvalidName);
    }
    const auto it = m_achievements.find(name);
    return it != m_achievements.end() && it->second.unlocked;
}

Expected<void, StoreError> NullStoreService::setAchievementProgress(std::string_view name,
                                                                    AchievementProgress progress)
{
    if (!isValidStoreName(name))
    {
        return unexpected(StoreError::InvalidName);
    }
    if (progress.max == 0)
    {
        return unexpected(StoreError::InvalidValue);
    }
    auto it = m_achievements.find(name);
    if (it == m_achievements.end())
    {
        it = m_achievements.emplace(std::string(name), detail::NullAchievement{}).first;
    }
    detail::NullAchievement& achievement = it->second;
    if (achievement.unlocked)
    {
        return {};
    }
    achievement.progress = {std::min(progress.current, progress.max), progress.max};
    achievement.unlocked = achievement.progress.current == achievement.progress.max;
    return {};
}

Expected<AchievementProgress, StoreError> NullStoreService::achievementProgress(std::string_view name) const
{
    if (!isValidStoreName(name))
    {
        return unexpected(StoreError::InvalidName);
    }
    const auto it = m_achievements.find(name);
    if (it == m_achievements.end())
    {
        return AchievementProgress{};
    }
    const detail::NullAchievement& achievement = it->second;
    if (achievement.unlocked)
    {
        const u32 max = std::max(achievement.progress.max, 1u);
        return AchievementProgress{max, max};
    }
    return achievement.progress;
}

Expected<void, StoreError> NullStoreService::setStatInt(std::string_view name, i32 value)
{
    if (!isValidStoreName(name))
    {
        return unexpected(StoreError::InvalidName);
    }
    auto it = m_stats.find(name);
    if (it == m_stats.end())
    {
        it = m_stats.emplace(std::string(name), detail::NullStat{}).first;
    }
    else if (it->second.isFloat)
    {
        return unexpected(StoreError::TypeMismatch);
    }
    it->second.integer = value;
    return {};
}

Expected<i32, StoreError> NullStoreService::statInt(std::string_view name) const
{
    if (!isValidStoreName(name))
    {
        return unexpected(StoreError::InvalidName);
    }
    const auto it = m_stats.find(name);
    if (it == m_stats.end())
    {
        return unexpected(StoreError::NotFound);
    }
    if (it->second.isFloat)
    {
        return unexpected(StoreError::TypeMismatch);
    }
    return it->second.integer;
}

Expected<void, StoreError> NullStoreService::setStatFloat(std::string_view name, f32 value)
{
    if (!isValidStoreName(name))
    {
        return unexpected(StoreError::InvalidName);
    }
    if (!std::isfinite(value))
    {
        return unexpected(StoreError::InvalidValue);
    }
    auto it = m_stats.find(name);
    if (it == m_stats.end())
    {
        it = m_stats.emplace(std::string(name), detail::NullStat{.isFloat = true}).first;
    }
    else if (!it->second.isFloat)
    {
        return unexpected(StoreError::TypeMismatch);
    }
    it->second.real = value;
    return {};
}

Expected<f32, StoreError> NullStoreService::statFloat(std::string_view name) const
{
    if (!isValidStoreName(name))
    {
        return unexpected(StoreError::InvalidName);
    }
    const auto it = m_stats.find(name);
    if (it == m_stats.end())
    {
        return unexpected(StoreError::NotFound);
    }
    if (!it->second.isFloat)
    {
        return unexpected(StoreError::TypeMismatch);
    }
    return it->second.real;
}

Expected<void, StoreError> NullStoreService::storeStats()
{
    return {};
}

std::string NullStoreService::pathOf(std::string_view name) const
{
    std::string path = m_directory;
    if (!path.ends_with('/'))
    {
        path += '/';
    }
    path += name;
    return path;
}

Expected<u64, StoreError> NullStoreService::usedBytes(std::string_view except) const
{
    std::vector<DirectoryEntry> entries;
    if (const FileError error = m_files.list(m_directory, entries); error != FileError::None)
    {
        if (error == FileError::NotFound)
        {
            return u64{0};
        }
        return unexpected(storeErrorOf(error));
    }
    u64 used = 0;
    for (const DirectoryEntry& entry : entries)
    {
        if (!entry.isDirectory && isCloudName(entry.name) && entry.name != except)
        {
            used += entry.size;
        }
    }
    return used;
}

Expected<void, StoreError> NullStoreService::writeCloudFile(std::string_view name,
                                                            std::span<const std::byte> data)
{
    if (!isCloudName(name))
    {
        return unexpected(StoreError::InvalidName);
    }
    if (data.size() > kMaxCloudFileBytes)
    {
        return unexpected(StoreError::TooLarge);
    }
    const Expected<u64, StoreError> used = usedBytes(name);
    if (!used)
    {
        return unexpected(used.error());
    }
    if (*used + data.size() > m_quota)
    {
        return unexpected(StoreError::QuotaExceeded);
    }
    if (const FileError error = m_files.writeFile(pathOf(name), data); error != FileError::None)
    {
        return unexpected(storeErrorOf(error));
    }
    return {};
}

Expected<u64, StoreError> NullStoreService::readCloudFile(std::string_view name, std::span<std::byte> out)
{
    if (!isCloudName(name))
    {
        return unexpected(StoreError::InvalidName);
    }
    const std::string path = pathOf(name);
    FileStat stat;
    if (const FileError error = m_files.stat(path, stat); error != FileError::None)
    {
        return unexpected(storeErrorOf(error));
    }
    if (stat.isDirectory)
    {
        return unexpected(StoreError::NotFound);
    }
    if (out.empty())
    {
        return stat.size;
    }
    std::vector<std::byte> bytes;
    if (const FileError error = m_files.readFile(path, bytes); error != FileError::None)
    {
        return unexpected(storeErrorOf(error));
    }
    if (!bytes.empty())
    {
        std::memcpy(out.data(), bytes.data(), std::min(bytes.size(), out.size()));
    }
    return static_cast<u64>(bytes.size());
}

Expected<void, StoreError> NullStoreService::removeCloudFile(std::string_view name)
{
    if (!isCloudName(name))
    {
        return unexpected(StoreError::InvalidName);
    }
    const std::string path = pathOf(name);
    FileStat stat;
    if (const FileError error = m_files.stat(path, stat); error != FileError::None)
    {
        return unexpected(storeErrorOf(error));
    }
    if (stat.isDirectory)
    {
        return unexpected(StoreError::NotFound);
    }
    if (const FileError error = m_files.remove(path); error != FileError::None)
    {
        return unexpected(storeErrorOf(error));
    }
    return {};
}

u32 NullStoreService::listCloudFiles(std::span<std::string_view> out)
{
    m_listed.clear();
    std::vector<DirectoryEntry> entries;
    if (m_files.list(m_directory, entries) == FileError::None)
    {
        for (DirectoryEntry& entry : entries)
        {
            if (!entry.isDirectory && isCloudName(entry.name))
            {
                m_listed.push_back(std::move(entry.name));
            }
        }
    }
    std::sort(m_listed.begin(), m_listed.end());
    for (usize i = 0; i < std::min(out.size(), m_listed.size()); ++i)
    {
        out[i] = m_listed[i];
    }
    return static_cast<u32>(m_listed.size());
}

bool NullStoreService::cloudFileExists(std::string_view name) const
{
    if (!isCloudName(name))
    {
        return false;
    }
    FileStat stat;
    return m_files.stat(pathOf(name), stat) == FileError::None && !stat.isDirectory;
}

Expected<StoreQuota, StoreError> NullStoreService::cloudQuota() const
{
    const Expected<u64, StoreError> used = usedBytes({});
    if (!used)
    {
        return unexpected(used.error());
    }
    return StoreQuota{.totalBytes = m_quota, .availableBytes = m_quota - std::min(*used, m_quota)};
}

bool NullStoreService::overlayActive() const
{
    return false;
}

bool NullStoreService::openOverlay(StoreOverlayPage page)
{
    static_cast<void>(page);
    return false;
}

std::optional<std::string_view> NullStoreService::glyphPath(GamepadOrigin origin) const
{
    static_cast<void>(origin);
    return std::nullopt;
}

} // namespace illusion
