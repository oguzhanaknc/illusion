#pragma once

/// @file
/// Little-endian binary serialization for persisted data (cooked assets, caches, archives).

#include <illusion/core/Base.hpp>

#include <bit>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace illusion
{

static_assert(std::endian::native == std::endian::little,
              "persisted formats are little-endian; big-endian hosts are not supported");

/// Types written and read as raw bytes: trivially copyable values without pointers.
template <class T>
concept TriviallySerializable = std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>;

/// Appends values to a byte buffer.
class BinaryWriter
{
public:
    /// Writes into @p out (appending to what it already holds).
    explicit BinaryWriter(std::vector<std::byte>& out) noexcept
        : m_out(&out)
    {
    }

    /// Appends the bytes of @p value.
    template <TriviallySerializable T>
    void write(const T& value)
    {
        writeBytes(std::as_bytes(std::span(&value, 1)));
    }

    /// Appends raw bytes.
    void writeBytes(std::span<const std::byte> bytes)
    {
        m_out->insert(m_out->end(), bytes.begin(), bytes.end());
    }

    /// Appends a string as a u32 length followed by its bytes.
    void writeString(std::string_view text)
    {
        write(static_cast<u32>(text.size()));
        writeBytes(std::as_bytes(std::span(text.data(), text.size())));
    }

    /// Appends a vector of trivially serializable values as a u64 count followed by the elements.
    template <TriviallySerializable T>
    void writeVector(std::span<const T> values)
    {
        write(static_cast<u64>(values.size()));
        writeBytes(std::as_bytes(values));
    }

    /// Bytes written so far (the size of the target buffer).
    [[nodiscard]] usize size() const noexcept { return m_out->size(); }

private:
    std::vector<std::byte>* m_out;
};

/// Reads values back from a byte span. Every read is bounds-checked; after the first failure the reader
/// stays failed and further reads return false, so a sequence of reads needs only one final check.
class BinaryReader
{
public:
    /// Reads from @p data, which must outlive the reader.
    explicit BinaryReader(std::span<const std::byte> data) noexcept
        : m_data(data)
    {
    }

    /// Reads one value. Returns false (and fails the reader) if not enough bytes remain.
    template <TriviallySerializable T>
    bool read(T& value) noexcept
    {
        return readBytes(std::as_writable_bytes(std::span(&value, 1)));
    }

    /// Fills @p out from the stream.
    bool readBytes(std::span<std::byte> out) noexcept
    {
        if (m_failed || out.size() > remaining())
        {
            m_failed = true;
            return false;
        }
        if (!out.empty())
        {
            std::memcpy(out.data(), m_data.data() + m_position, out.size());
        }
        m_position += out.size();
        return true;
    }

    /// Returns a view of the next @p size bytes and skips them, or an empty span (failing the reader) if
    /// fewer remain.
    [[nodiscard]] std::span<const std::byte> view(usize size) noexcept
    {
        if (m_failed || size > remaining())
        {
            m_failed = true;
            return {};
        }
        const std::span<const std::byte> result = m_data.subspan(m_position, size);
        m_position += size;
        return result;
    }

    /// Reads a string written by BinaryWriter::writeString().
    bool readString(std::string& out)
    {
        u32 length = 0;
        if (!read(length))
        {
            return false;
        }
        const std::span<const std::byte> bytes = view(length);
        if (m_failed)
        {
            return false;
        }
        out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return true;
    }

    /// Reads a vector written by BinaryWriter::writeVector(). Fails without allocating if the stored
    /// count does not fit in the remaining data, so corrupted counts cannot exhaust memory.
    template <TriviallySerializable T>
    bool readVector(std::vector<T>& out)
    {
        u64 count = 0;
        if (!read(count) || count > remaining() / sizeof(T))
        {
            m_failed = true;
            return false;
        }
        out.resize(static_cast<usize>(count));
        return readBytes(std::as_writable_bytes(std::span(out)));
    }

    /// Bytes left to read.
    [[nodiscard]] usize remaining() const noexcept { return m_data.size() - m_position; }

    /// Current read position.
    [[nodiscard]] usize position() const noexcept { return m_position; }

    /// True while no read has failed.
    [[nodiscard]] bool ok() const noexcept { return !m_failed; }

    /// Marks the stream as invalid (for format errors found by the caller).
    void fail() noexcept { m_failed = true; }

private:
    std::span<const std::byte> m_data;
    usize m_position = 0;
    bool m_failed = false;
};

} // namespace illusion
