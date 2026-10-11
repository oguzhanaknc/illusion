#include <illusion/core/Assert.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <random>
#include <system_error>

#if defined(ILL_PLATFORM_WINDOWS)
#include <shlobj.h>
#include <windows.h>
#elif defined(ILL_PLATFORM_MACOS)
#include <mach-o/dyld.h>
#include <pwd.h>
#include <unistd.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

namespace illusion
{
namespace
{

std::filesystem::path pathFromUtf8(std::string_view text)
{
    return std::filesystem::path(
        std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string utf8FromPath(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

bool isValidMountName(std::string_view name) noexcept
{
    return !name.empty() && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

char toLowerAscii(char c) noexcept
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return toLowerAscii(x) == toLowerAscii(y);
           });
}

// Names Windows maps to devices regardless of extension ("nul.txt" is the NUL device). Rejected on every
// platform so a virtual path behaves the same everywhere.
bool isReservedDeviceName(std::string_view component) noexcept
{
    const std::string_view stem = component.substr(0, component.find('.'));
    for (const std::string_view name : {"con", "prn", "aux", "nul", "conin$", "conout$"})
    {
        if (equalsIgnoreCase(stem, name))
        {
            return true;
        }
    }
    if (stem.size() == 4 && stem[3] >= '1' && stem[3] <= '9' &&
        (equalsIgnoreCase(stem.substr(0, 3), "com") || equalsIgnoreCase(stem.substr(0, 3), "lpt")))
    {
        return true;
    }
    return false;
}

// Validates the part after "mount:/". Empty means the mount root.
bool isValidRelativePath(std::string_view relative) noexcept
{
    if (relative.empty())
    {
        return true;
    }
    usize start = 0;
    while (start <= relative.size())
    {
        const usize end = std::min(relative.find('/', start), relative.size());
        const std::string_view component = relative.substr(start, end - start);
        // Windows strips trailing dots and spaces, which would make "save." and "save" the same file.
        if (component.empty() || component == "." || component == ".." || component.back() == '.' ||
            component.back() == ' ' || isReservedDeviceName(component))
        {
            return false;
        }
        for (const char c : component)
        {
            if (c == '\\' || c == ':' || static_cast<unsigned char>(c) < 0x20)
            {
                return false;
            }
        }
        if (end == relative.size())
        {
            break;
        }
        start = end + 1;
    }
    return true;
}

bool splitVirtualPath(std::string_view virtualPath, std::string_view& mount,
                      std::string_view& relative) noexcept
{
    const usize separator = virtualPath.find(":/");
    if (separator == std::string_view::npos)
    {
        return false;
    }
    mount = virtualPath.substr(0, separator);
    relative = virtualPath.substr(separator + 2);
    return isValidMountName(mount) && isValidRelativePath(relative);
}

FileError errorFromCode(const std::error_code& code) noexcept
{
    if (code == std::errc::no_such_file_or_directory || code == std::errc::not_a_directory)
    {
        return FileError::NotFound;
    }
    if (code == std::errc::permission_denied || code == std::errc::operation_not_permitted)
    {
        return FileError::AccessDenied;
    }
    return FileError::IoError;
}

// Reads a whole file into a contiguous container of 1-byte elements (std::string or std::vector<std::byte>).
template <class Container>
FileError readBytes(const std::filesystem::path& path, Container& out)
{
    std::error_code code;
    const auto status = std::filesystem::status(path, code);
    if (code || !std::filesystem::is_regular_file(status))
    {
        return code ? errorFromCode(code) : FileError::NotFound;
    }
    const auto size = std::filesystem::file_size(path, code);
    if (code)
    {
        return errorFromCode(code);
    }
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return FileError::AccessDenied;
    }
    out.resize(static_cast<usize>(size));
    if (size > 0 && !file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size)))
    {
        return FileError::IoError;
    }
    return FileError::None;
}

std::atomic<u64> g_tempCounter{0};

// Random per-process token: temporary names must not collide when several processes (editor and game,
// two game instances) write the same file at the same time.
u64 processToken()
{
    static const u64 token = [] {
        std::random_device device;
        return (static_cast<u64>(device()) << 32) ^ static_cast<u64>(device());
    }();
    return token;
}

} // namespace

std::string_view fileErrorName(FileError error) noexcept
{
    switch (error)
    {
    case FileError::None:
        return "None";
    case FileError::InvalidPath:
        return "InvalidPath";
    case FileError::UnknownMount:
        return "UnknownMount";
    case FileError::NotFound:
        return "NotFound";
    case FileError::ReadOnly:
        return "ReadOnly";
    case FileError::AccessDenied:
        return "AccessDenied";
    case FileError::IoError:
        return "IoError";
    }
    return "Unknown";
}

struct VirtualFileSystem::Mount
{
    std::string name;
    std::filesystem::path root;
    MountAccess access;
};

struct VirtualFileSystem::Impl
{
    std::vector<Mount> mounts;

    const Mount* find(std::string_view name) const noexcept
    {
        const auto it = std::find_if(mounts.begin(), mounts.end(),
                                     [name](const Mount& mount) { return mount.name == name; });
        return it != mounts.end() ? &*it : nullptr;
    }
};

VirtualFileSystem::VirtualFileSystem()
    : m_impl(std::make_unique<Impl>())
{
}

VirtualFileSystem::~VirtualFileSystem() = default;

FileError VirtualFileSystem::mount(std::string_view name, const std::filesystem::path& root,
                                   MountAccess access)
{
    if (!isValidMountName(name))
    {
        return FileError::InvalidPath;
    }
    std::error_code code;
    if (!std::filesystem::is_directory(root, code))
    {
        return FileError::NotFound;
    }
    std::filesystem::path absolute = std::filesystem::absolute(root, code);
    if (code)
    {
        return errorFromCode(code);
    }
    unmount(name);
    m_impl->mounts.push_back(Mount{std::string(name), absolute.lexically_normal(), access});
    return FileError::None;
}

bool VirtualFileSystem::unmount(std::string_view name)
{
    return std::erase_if(m_impl->mounts, [name](const Mount& mount) { return mount.name == name; }) > 0;
}

bool VirtualFileSystem::isMounted(std::string_view name) const
{
    return m_impl->find(name) != nullptr;
}

FileError VirtualFileSystem::resolveMount(std::string_view virtualPath, const Mount*& mount,
                                          std::filesystem::path& out) const
{
    std::string_view mountName;
    std::string_view relative;
    if (!splitVirtualPath(virtualPath, mountName, relative))
    {
        return FileError::InvalidPath;
    }
    mount = m_impl->find(mountName);
    if (mount == nullptr)
    {
        return FileError::UnknownMount;
    }
    out = relative.empty() ? mount->root : mount->root / pathFromUtf8(relative);
    return FileError::None;
}

FileError VirtualFileSystem::resolve(std::string_view virtualPath, std::filesystem::path& out) const
{
    const Mount* mount = nullptr;
    return resolveMount(virtualPath, mount, out);
}

FileError VirtualFileSystem::readFile(std::string_view virtualPath, std::vector<std::byte>& out) const
{
    std::filesystem::path path;
    if (const FileError error = resolve(virtualPath, path); error != FileError::None)
    {
        return error;
    }
    return readBytes(path, out);
}

FileError VirtualFileSystem::readText(std::string_view virtualPath, std::string& out) const
{
    std::filesystem::path path;
    if (const FileError error = resolve(virtualPath, path); error != FileError::None)
    {
        return error;
    }
    return readBytes(path, out);
}

FileError VirtualFileSystem::writeFile(std::string_view virtualPath, std::span<const std::byte> data)
{
    const Mount* mount = nullptr;
    std::filesystem::path path;
    if (const FileError error = resolveMount(virtualPath, mount, path); error != FileError::None)
    {
        return error;
    }
    if (mount->access != MountAccess::ReadWrite)
    {
        return FileError::ReadOnly;
    }
    if (path == mount->root)
    {
        return FileError::InvalidPath;
    }

    std::error_code code;
    std::filesystem::create_directories(path.parent_path(), code);
    if (code)
    {
        return errorFromCode(code);
    }

    std::filesystem::path temporary = path;
    temporary += ".tmp-" + std::to_string(processToken()) + "-" +
                 std::to_string(g_tempCounter.fetch_add(1, std::memory_order_relaxed));
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            return FileError::AccessDenied;
        }
        if (!data.empty())
        {
            file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        }
        file.flush();
        if (!file)
        {
            file.close();
            std::filesystem::remove(temporary, code);
            return FileError::IoError;
        }
    }
    std::filesystem::rename(temporary, path, code);
    if (code)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return errorFromCode(code);
    }
    return FileError::None;
}

FileError VirtualFileSystem::writeText(std::string_view virtualPath, std::string_view text)
{
    return writeFile(virtualPath, std::as_bytes(std::span<const char>(text.data(), text.size())));
}

bool VirtualFileSystem::exists(std::string_view virtualPath) const
{
    std::filesystem::path path;
    if (resolve(virtualPath, path) != FileError::None)
    {
        return false;
    }
    std::error_code code;
    return std::filesystem::exists(path, code);
}

FileError VirtualFileSystem::list(std::string_view virtualPath, std::vector<DirectoryEntry>& out) const
{
    std::filesystem::path path;
    if (const FileError error = resolve(virtualPath, path); error != FileError::None)
    {
        return error;
    }
    std::error_code code;
    if (!std::filesystem::is_directory(path, code))
    {
        return code ? errorFromCode(code) : FileError::NotFound;
    }
    out.clear();
    for (std::filesystem::directory_iterator it(path, code), end; !code && it != end; it.increment(code))
    {
        DirectoryEntry entry;
        entry.name = utf8FromPath(it->path().filename());
        std::error_code entryCode;
        entry.isDirectory = it->is_directory(entryCode);
        entry.size = entry.isDirectory ? 0 : static_cast<u64>(it->file_size(entryCode));
        if (entryCode)
        {
            entry.size = 0;
        }
        out.push_back(std::move(entry));
    }
    if (code)
    {
        return errorFromCode(code);
    }
    std::sort(out.begin(), out.end(),
              [](const DirectoryEntry& a, const DirectoryEntry& b) { return a.name < b.name; });
    return FileError::None;
}

std::filesystem::path executableDirectory()
{
#if defined(ILL_PLATFORM_WINDOWS)
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        ILL_CHECK(length != 0, "GetModuleFileNameW failed");
        if (length < buffer.size())
        {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return std::filesystem::path(buffer).parent_path();
#elif defined(ILL_PLATFORM_MACOS)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    ILL_CHECK(_NSGetExecutablePath(buffer.data(), &size) == 0, "_NSGetExecutablePath failed");
    buffer.resize(std::char_traits<char>::length(buffer.c_str()));
    std::error_code code;
    const auto canonical = std::filesystem::canonical(buffer, code);
    return (code ? std::filesystem::path(buffer) : canonical).parent_path();
#else
    std::error_code code;
    const auto executable = std::filesystem::read_symlink("/proc/self/exe", code);
    ILL_CHECK(!code, "cannot read /proc/self/exe");
    return executable.parent_path();
#endif
}

namespace
{

#if !defined(ILL_PLATFORM_WINDOWS)
std::filesystem::path homeDirectory()
{
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
    {
        return std::filesystem::path(home);
    }
    if (const passwd* entry = getpwuid(getuid()); entry != nullptr && entry->pw_dir != nullptr)
    {
        return std::filesystem::path(entry->pw_dir);
    }
    return std::filesystem::temp_directory_path();
}
#endif

} // namespace

std::filesystem::path userDataDirectory(std::string_view organization, std::string_view application)
{
    std::filesystem::path base;
#if defined(ILL_PLATFORM_WINDOWS)
    PWSTR folder = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &folder)))
    {
        base = std::filesystem::path(folder);
    }
    CoTaskMemFree(folder);
    if (base.empty())
    {
        base = std::filesystem::temp_directory_path();
    }
#elif defined(ILL_PLATFORM_MACOS)
    base = homeDirectory() / "Library" / "Application Support";
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg != nullptr && xdg[0] == '/')
    {
        base = std::filesystem::path(xdg);
    }
    else
    {
        base = homeDirectory() / ".local" / "share";
    }
#endif
    return base / pathFromUtf8(organization) / pathFromUtf8(application);
}

} // namespace illusion
