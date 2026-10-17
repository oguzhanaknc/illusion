#include <illusion/core/Hash.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/core/Serialization.hpp>
#include <illusion/platform/Archive.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <system_error>

#if defined(ILL_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace illusion
{
namespace
{

// Layout: header | file data (each 16-byte aligned) | table of contents.
constexpr u32 kMagic = 0x504C4C49; // "ILLP"
constexpr u32 kVersion = 1;
constexpr u64 kAlignment = 16;
constexpr u64 kHeaderSize = 48;

struct Header
{
    u32 magic;
    u32 version;
    u32 entryCount;
    u32 reserved;
    u64 tocOffset;
    u64 tocSize;
    u64 tocChecksum;
    u64 reserved2;
};
static_assert(sizeof(Header) == kHeaderSize);

void setError(std::string* error, const char* message)
{
    if (error != nullptr)
    {
        *error = message;
    }
}

// A relative path is valid when it survives normalization unchanged (no '.', '..', empty components,
// backslashes or reserved names).
bool isCanonicalRelativePath(std::string_view path)
{
    if (path.empty())
    {
        return false;
    }
    const std::string virtualPath = std::string("x:/").append(path);
    const std::optional<std::string> normalized = VirtualFileSystem::normalizePath(virtualPath);
    return normalized && *normalized == virtualPath;
}

u64 alignUp(u64 value) noexcept
{
    return (value + kAlignment - 1) & ~(kAlignment - 1);
}

// A read-only memory mapping of a whole file. Reads become memory copies, need no locking and let the
// operating system cache and prefetch the archive.
class MappedFile : NonCopyable
{
public:
    MappedFile() = default;
    ~MappedFile() { close(); }

    bool open(const std::filesystem::path& path) noexcept
    {
#if defined(ILL_PLATFORM_WINDOWS)
        m_file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        if (m_file == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(m_file, &size))
        {
            return false;
        }
        m_size = static_cast<u64>(size.QuadPart);
        if (m_size == 0)
        {
            return true; // nothing to map
        }
        m_mapping = CreateFileMappingW(m_file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (m_mapping == nullptr)
        {
            return false;
        }
        m_data = static_cast<const std::byte*>(MapViewOfFile(m_mapping, FILE_MAP_READ, 0, 0, 0));
        return m_data != nullptr;
#else
        m_descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (m_descriptor < 0)
        {
            return false;
        }
        struct stat info{};
        if (fstat(m_descriptor, &info) != 0)
        {
            return false;
        }
        m_size = static_cast<u64>(info.st_size);
        if (m_size == 0)
        {
            return true;
        }
        void* data = mmap(nullptr, static_cast<size_t>(m_size), PROT_READ, MAP_PRIVATE, m_descriptor, 0);
        if (data == MAP_FAILED)
        {
            return false;
        }
        m_data = static_cast<const std::byte*>(data);
        return true;
#endif
    }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept
    {
        return {m_data, static_cast<usize>(m_data != nullptr ? m_size : 0)};
    }

private:
    void close() noexcept
    {
#if defined(ILL_PLATFORM_WINDOWS)
        if (m_data != nullptr)
        {
            UnmapViewOfFile(m_data);
        }
        if (m_mapping != nullptr)
        {
            CloseHandle(m_mapping);
        }
        if (m_file != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_file);
        }
#else
        if (m_data != nullptr)
        {
            munmap(const_cast<std::byte*>(m_data), static_cast<size_t>(m_size));
        }
        if (m_descriptor >= 0)
        {
            ::close(m_descriptor);
        }
#endif
    }

    const std::byte* m_data = nullptr;
    u64 m_size = 0;
#if defined(ILL_PLATFORM_WINDOWS)
    HANDLE m_file = INVALID_HANDLE_VALUE;
    HANDLE m_mapping = nullptr;
#else
    int m_descriptor = -1;
#endif
};

} // namespace

struct ArchiveReader::Impl
{
    MappedFile file;
    std::vector<ArchiveEntry> entries;
};

ArchiveReader::ArchiveReader()
    : m_impl(std::make_unique<Impl>())
{
}

ArchiveReader::~ArchiveReader() = default;

std::unique_ptr<ArchiveReader> ArchiveReader::open(const std::filesystem::path& path, std::string* error)
{
    ILL_PROFILE_SCOPE();
    std::unique_ptr<ArchiveReader> reader(new ArchiveReader());
    Impl& impl = *reader->m_impl;
    if (!impl.file.open(path))
    {
        setError(error, "cannot open archive");
        return nullptr;
    }
    const std::span<const std::byte> bytes = impl.file.bytes();
    const u64 fileSize = bytes.size();
    Header header{};
    if (fileSize < kHeaderSize)
    {
        setError(error, "archive is truncated");
        return nullptr;
    }
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (header.magic != kMagic)
    {
        setError(error, "not an archive");
        return nullptr;
    }
    if (header.version != kVersion)
    {
        setError(error, "unsupported archive version");
        return nullptr;
    }
    if (header.tocOffset < kHeaderSize || header.tocOffset > fileSize ||
        header.tocSize != fileSize - header.tocOffset)
    {
        setError(error, "archive is truncated");
        return nullptr;
    }
    const std::span<const std::byte> toc =
        bytes.subspan(static_cast<usize>(header.tocOffset), static_cast<usize>(header.tocSize));
    if (xxhash64(toc) != header.tocChecksum)
    {
        setError(error, "archive table of contents is damaged");
        return nullptr;
    }
    BinaryReader in(toc);
    // Each entry takes at least 28 bytes in the table, which bounds the count before allocating.
    if (header.entryCount > toc.size() / 28)
    {
        setError(error, "archive table of contents is damaged");
        return nullptr;
    }
    impl.entries.resize(header.entryCount);
    for (ArchiveEntry& entry : impl.entries)
    {
        in.readString(entry.path);
        in.read(entry.offset);
        in.read(entry.size);
        in.read(entry.contentHash);
        if (!in.ok() || !isCanonicalRelativePath(entry.path) || entry.offset < kHeaderSize ||
            entry.offset > header.tocOffset || entry.size > header.tocOffset - entry.offset)
        {
            setError(error, "archive table of contents is damaged");
            return nullptr;
        }
    }
    const bool sorted =
        std::is_sorted(impl.entries.begin(), impl.entries.end(),
                       [](const ArchiveEntry& a, const ArchiveEntry& b) { return a.path < b.path; });
    const bool unique = std::adjacent_find(impl.entries.begin(), impl.entries.end(),
                                           [](const ArchiveEntry& a, const ArchiveEntry& b) {
                                               return a.path == b.path;
                                           }) == impl.entries.end();
    if (!in.ok() || in.remaining() != 0 || !sorted || !unique)
    {
        setError(error, "archive table of contents is damaged");
        return nullptr;
    }
    return reader;
}

const ArchiveEntry* ArchiveReader::find(std::string_view relativePath) const noexcept
{
    const auto& entries = m_impl->entries;
    const auto it =
        std::lower_bound(entries.begin(), entries.end(), relativePath,
                         [](const ArchiveEntry& entry, std::string_view path) { return entry.path < path; });
    return it != entries.end() && it->path == relativePath ? &*it : nullptr;
}

FileError ArchiveReader::read(const ArchiveEntry& entry, std::vector<std::byte>& out) const
{
    // Entries were bounds-checked when the archive was opened.
    const std::span<const std::byte> data =
        m_impl->file.bytes().subspan(static_cast<usize>(entry.offset), static_cast<usize>(entry.size));
    if (xxhash64(data) != entry.contentHash)
    {
        return FileError::IoError;
    }
    out.assign(data.begin(), data.end());
    return FileError::None;
}

bool ArchiveReader::isDirectory(std::string_view relativePath) const noexcept
{
    if (relativePath.empty())
    {
        return true;
    }
    // Entries are sorted, so the first path not less than "dir/" tells whether any lives inside it.
    const std::string prefix = std::string(relativePath).append("/");
    const auto& entries = m_impl->entries;
    const auto it = std::lower_bound(
        entries.begin(), entries.end(), prefix,
        [](const ArchiveEntry& entry, const std::string& path) { return entry.path < path; });
    return it != entries.end() && it->path.starts_with(prefix);
}

FileError ArchiveReader::list(std::string_view relativePath, std::vector<DirectoryEntry>& out) const
{
    if (!isDirectory(relativePath))
    {
        return FileError::NotFound;
    }
    out.clear();
    const std::string prefix = relativePath.empty() ? std::string() : std::string(relativePath).append("/");
    for (const ArchiveEntry& entry : m_impl->entries)
    {
        if (!entry.path.starts_with(prefix))
        {
            continue;
        }
        const std::string_view rest = std::string_view(entry.path).substr(prefix.size());
        const usize slash = rest.find('/');
        DirectoryEntry item;
        item.name = std::string(rest.substr(0, slash));
        item.isDirectory = slash != std::string_view::npos;
        item.size = item.isDirectory ? 0 : entry.size;
        if (out.empty() || out.back().name != item.name)
        {
            out.push_back(std::move(item));
        }
    }
    // Entry order is by full path, so "a/x" sorts after "a.txt": sort the names explicitly.
    std::sort(out.begin(), out.end(),
              [](const DirectoryEntry& a, const DirectoryEntry& b) { return a.name < b.name; });
    out.erase(std::unique(out.begin(), out.end(),
                          [](const DirectoryEntry& a, const DirectoryEntry& b) { return a.name == b.name; }),
              out.end());
    return FileError::None;
}

std::span<const ArchiveEntry> ArchiveReader::entries() const noexcept
{
    return m_impl->entries;
}

FileError writeArchive(const std::filesystem::path& path, std::span<const ArchiveInput> files)
{
    ILL_PROFILE_SCOPE();
    std::vector<const ArchiveInput*> sorted;
    sorted.reserve(files.size());
    for (const ArchiveInput& file : files)
    {
        if (!isCanonicalRelativePath(file.path))
        {
            return FileError::InvalidPath;
        }
        sorted.push_back(&file);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const ArchiveInput* a, const ArchiveInput* b) { return a->path < b->path; });
    for (usize i = 1; i < sorted.size(); ++i)
    {
        if (sorted[i - 1]->path == sorted[i]->path)
        {
            return FileError::InvalidPath;
        }
    }

    std::vector<ArchiveEntry> entries;
    entries.reserve(sorted.size());
    u64 offset = kHeaderSize;
    for (const ArchiveInput* file : sorted)
    {
        offset = alignUp(offset);
        entries.push_back(ArchiveEntry{file->path, offset, file->data.size(), xxhash64(file->data)});
        offset += file->data.size();
    }
    std::vector<std::byte> toc;
    BinaryWriter writer(toc);
    for (const ArchiveEntry& entry : entries)
    {
        writer.writeString(entry.path);
        writer.write(entry.offset);
        writer.write(entry.size);
        writer.write(entry.contentHash);
    }
    Header header{};
    header.magic = kMagic;
    header.version = kVersion;
    header.entryCount = static_cast<u32>(entries.size());
    header.tocOffset = offset;
    header.tocSize = toc.size();
    header.tocChecksum = xxhash64(toc);

    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return FileError::AccessDenied;
        }
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        u64 position = kHeaderSize;
        const char zeros[kAlignment] = {};
        for (usize i = 0; i < sorted.size(); ++i)
        {
            out.write(zeros, static_cast<std::streamsize>(entries[i].offset - position));
            out.write(reinterpret_cast<const char*>(sorted[i]->data.data()),
                      static_cast<std::streamsize>(sorted[i]->data.size()));
            position = entries[i].offset + entries[i].size;
        }
        out.write(reinterpret_cast<const char*>(toc.data()), static_cast<std::streamsize>(toc.size()));
        out.flush();
        if (!out)
        {
            return FileError::IoError;
        }
    }
    std::error_code code;
    std::filesystem::rename(temporary, path, code);
    if (code)
    {
        std::filesystem::remove(temporary, code);
        return FileError::IoError;
    }
    return FileError::None;
}

FileError packDirectory(const std::filesystem::path& directory, const std::filesystem::path& archive)
{
    std::error_code code;
    if (!std::filesystem::is_directory(directory, code))
    {
        return FileError::NotFound;
    }
    std::vector<ArchiveInput> files;
    for (std::filesystem::recursive_directory_iterator it(directory, code), end; !code && it != end;
         it.increment(code))
    {
        if (!it->is_regular_file(code))
        {
            continue;
        }
        const std::u8string relative =
            std::filesystem::relative(it->path(), directory, code).generic_u8string();
        ArchiveInput input;
        input.path.assign(reinterpret_cast<const char*>(relative.data()), relative.size());
        std::ifstream file(it->path(), std::ios::binary | std::ios::ate);
        if (!file)
        {
            return FileError::AccessDenied;
        }
        input.data.resize(static_cast<usize>(file.tellg()));
        file.seekg(0);
        if (!input.data.empty() && !file.read(reinterpret_cast<char*>(input.data.data()),
                                              static_cast<std::streamsize>(input.data.size())))
        {
            return FileError::IoError;
        }
        files.push_back(std::move(input));
    }
    if (code)
    {
        return FileError::IoError;
    }
    return writeArchive(archive, files);
}

} // namespace illusion
