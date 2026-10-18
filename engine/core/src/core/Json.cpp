#include <illusion/core/Assert.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/core/Memory.hpp>
#include <illusion/core/Profiler.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>
#include <yyjson.h>

namespace illusion
{

namespace detail
{

// State of a JsonMutDocument, kept at a fixed address so that handles survive moves of the document.
struct JsonMutState
{
    yyjson_mut_doc* doc = nullptr;
    LinearAllocator* arena = nullptr;
    bool failed = false;
};

} // namespace detail

namespace
{

// --- Shared helpers ----------------------------------------------------------------------------------

JsonType typeOfTag(u64 tag) noexcept
{
    switch (static_cast<u8>(tag & YYJSON_TYPE_MASK))
    {
    case YYJSON_TYPE_NULL:
        return JsonType::Null;
    case YYJSON_TYPE_BOOL:
        return JsonType::Bool;
    case YYJSON_TYPE_NUM:
        return (tag & YYJSON_SUBTYPE_MASK) == YYJSON_SUBTYPE_REAL ? JsonType::Real : JsonType::Integer;
    case YYJSON_TYPE_STR:
        return JsonType::String;
    case YYJSON_TYPE_ARR:
        return JsonType::Array;
    case YYJSON_TYPE_OBJ:
        return JsonType::Object;
    default:
        return JsonType::Absent; // raw values are never created
    }
}

// yyjson treats a null string as an error, but an empty std::string_view may have null data.
const char* dataOf(std::string_view text) noexcept
{
    return text.data() != nullptr ? text.data() : "";
}

// yyjson_alc over a LinearAllocator. Freed blocks stay used until the arena is reset.
void* arenaMalloc(void* context, size_t size)
{
    return static_cast<LinearAllocator*>(context)->allocate(size);
}

void* arenaRealloc(void* context, void* ptr, size_t oldSize, size_t size)
{
    return static_cast<LinearAllocator*>(context)->reallocate(ptr, oldSize, size);
}

void arenaFree(void*, void*)
{
}

yyjson_alc arenaAllocator(LinearAllocator& arena) noexcept
{
    return yyjson_alc{&arenaMalloc, &arenaRealloc, &arenaFree, &arena};
}

// Stack that lives inline up to N elements and moves to the heap beyond, so that typical documents are
// read and written without allocating. Elements stay contiguous.
template <class T, usize N>
class SmallStack
{
    static_assert(std::is_trivially_copyable_v<T>);

public:
    SmallStack() = default;
    SmallStack(const SmallStack&) = delete;
    SmallStack& operator=(const SmallStack&) = delete;

    void push(const T& value)
    {
        if (m_size == m_capacity)
        {
            grow(m_size + 1);
        }
        m_data[m_size++] = value;
    }

    // Appends @p count uninitialized elements and returns the first.
    T* extend(usize count)
    {
        if (m_capacity - m_size < count)
        {
            grow(m_size + count);
        }
        T* first = m_data + m_size;
        m_size += count;
        return first;
    }

    void pop() noexcept { --m_size; }
    void resize(usize size) noexcept { m_size = size; }
    [[nodiscard]] T& back() noexcept { return m_data[m_size - 1]; }
    [[nodiscard]] T* data() noexcept { return m_data; }
    [[nodiscard]] usize size() const noexcept { return m_size; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

private:
    void grow(usize needed)
    {
        const usize capacity = std::max(needed, m_capacity * 2);
        if (m_data == m_inline)
        {
            m_heap.resize(capacity);
            std::memcpy(m_heap.data(), m_inline, m_size * sizeof(T));
        }
        else
        {
            m_heap.resize(capacity);
        }
        m_data = m_heap.data();
        m_capacity = capacity;
    }

    T m_inline[N];
    std::vector<T> m_heap;
    T* m_data = m_inline;
    usize m_size = 0;
    usize m_capacity = N;
};

// 1-based line and byte column of @p offset in @p text.
void locate(std::string_view text, usize offset, JsonError& error) noexcept
{
    offset = std::min(offset, text.size());
    usize line = 1;
    usize lineStart = 0;
    for (usize i = 0; i < offset; ++i)
    {
        if (text[i] == '\n')
        {
            ++line;
            lineStart = i + 1;
        }
    }
    error.offset = offset;
    error.line = static_cast<u32>(std::min<usize>(line, 0xFFFFFFFFu));
    error.column = static_cast<u32>(std::min<usize>(offset - lineStart + 1, 0xFFFFFFFFu));
}

JsonError makeError(std::string_view text, usize offset, std::string message)
{
    JsonError error;
    locate(text, offset, error);
    error.message = std::move(message);
    return error;
}

JsonError writeError(std::string_view message)
{
    JsonError error;
    error.message = message;
    return error;
}

bool isDelimiter(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',' || c == ':' || c == ']' || c == '}';
}

// Byte offset of the @p index-th value in document order of a valid JSON text: every key, scalar and
// opening bracket counts, which is the order of yyjson's flat value array.
usize valueOffset(std::string_view text, usize index) noexcept
{
    usize count = 0;
    usize i = 0;
    while (i < text.size())
    {
        const char c = text[i];
        if (isDelimiter(c))
        {
            ++i;
            continue;
        }
        if (count++ == index)
        {
            return i;
        }
        if (c == '"')
        {
            for (++i; i < text.size() && text[i] != '"'; ++i)
            {
                if (text[i] == '\\')
                {
                    ++i;
                }
            }
            ++i;
        }
        else if (c == '[' || c == '{')
        {
            ++i;
        }
        else
        {
            while (i < text.size() && !isDelimiter(text[i]))
            {
                ++i;
            }
        }
    }
    return text.size();
}

std::string_view stringOf(const yyjson_val* value) noexcept
{
    return {unsafe_yyjson_get_str(const_cast<yyjson_val*>(value)), unsafe_yyjson_get_len(value)};
}

bool sameString(const yyjson_val* a, const yyjson_val* b) noexcept
{
    const usize length = unsafe_yyjson_get_len(a);
    return length == unsafe_yyjson_get_len(b) && std::memcmp(a->uni.str, b->uni.str, length) == 0;
}

// Shortens a key for an error message.
std::string_view excerpt(std::string_view key) noexcept
{
    constexpr usize kMax = 64;
    if (key.size() <= kMax)
    {
        return key;
    }
    usize length = kMax;
    while (length > 0 && (static_cast<u8>(key[length]) & 0xC0) == 0x80)
    {
        --length;
    }
    return key.substr(0, length);
}

// --- Validation after parsing ------------------------------------------------------------------------

struct KeyEntry
{
    const yyjson_val* key;
};

bool keyLess(const KeyEntry& a, const KeyEntry& b) noexcept
{
    const std::string_view x = stringOf(a.key);
    const std::string_view y = stringOf(b.key);
    const int order = x.compare(y);
    return order != 0 ? order < 0 : a.key < b.key; // equal keys in document order
}

// Checks the depth limit and duplicate keys on the flat value array of a parsed document, without
// recursion. Reports the problem that comes first in the document.
class Validator
{
public:
    Validator(std::string_view text, const JsonReadOptions& options, LinearAllocator* arena)
        : m_text(text)
        , m_options(options)
        , m_arena(arena)
    {
    }

    Expected<void, JsonError> run(const yyjson_doc* doc)
    {
        const yyjson_val* root = doc->root;
        const yyjson_val* end = root + doc->val_read;
        const yyjson_val* duplicate = nullptr; // earliest duplicate key found so far
        for (const yyjson_val* value = root; value < end; ++value)
        {
            if (duplicate != nullptr && value >= duplicate)
            {
                break;
            }
            while (!m_ends.empty() && value >= m_ends.back())
            {
                m_ends.pop();
            }
            if (!unsafe_yyjson_is_ctn(value))
            {
                continue;
            }
            if (m_ends.size() + 1 > m_options.maxDepth)
            {
                return unexpected(
                    makeError(m_text, valueOffset(m_text, static_cast<usize>(value - root)),
                              std::format("nesting is deeper than {} levels", m_options.maxDepth)));
            }
            m_ends.push(unsafe_yyjson_get_next(value));
            if (!m_options.allowDuplicateKeys && unsafe_yyjson_is_obj(value) &&
                unsafe_yyjson_get_len(value) > 1)
            {
                const yyjson_val* found = nullptr;
                if (!findDuplicate(value, found))
                {
                    return unexpected(makeError(m_text, 0, "out of memory"));
                }
                if (found != nullptr && (duplicate == nullptr || found < duplicate))
                {
                    duplicate = found;
                }
            }
        }
        if (duplicate != nullptr)
        {
            return unexpected(makeError(m_text, valueOffset(m_text, static_cast<usize>(duplicate - root)),
                                        std::format("duplicate key \"{}\"", excerpt(stringOf(duplicate)))));
        }
        return {};
    }

private:
    // Sets @p found to the first key of @p object (in document order) that repeats an earlier one. Returns
    // false if the arena is full.
    bool findDuplicate(const yyjson_val* object, const yyjson_val*& found)
    {
        const usize count = unsafe_yyjson_get_len(object);
        constexpr usize kLinearLimit = 16;
        if (count <= kLinearLimit)
        {
            const yyjson_val* keys[kLinearLimit];
            const yyjson_val* key = object + 1;
            for (usize i = 0; i < count; ++i, key = unsafe_yyjson_get_next(key + 1))
            {
                for (usize j = 0; j < i; ++j)
                {
                    if (sameString(keys[j], key))
                    {
                        found = key;
                        return true;
                    }
                }
                keys[i] = key;
            }
            return true;
        }

        KeyEntry* entries = nullptr;
        LinearAllocator::Marker marker = 0;
        if (m_arena != nullptr)
        {
            marker = m_arena->mark();
            entries = m_arena->allocateArray<KeyEntry>(count);
            if (entries == nullptr)
            {
                return false;
            }
        }
        else
        {
            m_heapEntries.resize(count);
            entries = m_heapEntries.data();
        }
        const yyjson_val* key = object + 1;
        for (usize i = 0; i < count; ++i, key = unsafe_yyjson_get_next(key + 1))
        {
            entries[i] = KeyEntry{key};
        }
        std::sort(entries, entries + count, &keyLess);
        for (usize i = 1; i < count; ++i)
        {
            // Within a run of equal keys, the second one in document order is the first duplicate.
            if (sameString(entries[i - 1].key, entries[i].key) &&
                (i < 2 || !sameString(entries[i - 2].key, entries[i].key)))
            {
                if (found == nullptr || entries[i].key < found)
                {
                    found = entries[i].key;
                }
            }
        }
        if (m_arena != nullptr)
        {
            m_arena->rewind(marker);
        }
        return true;
    }

    std::string_view m_text;
    const JsonReadOptions& m_options;
    LinearAllocator* m_arena;
    SmallStack<const yyjson_val*, 256> m_ends;
    std::vector<KeyEntry> m_heapEntries;
};

Expected<yyjson_doc*, JsonError> parseDocument(std::string_view text, const JsonReadOptions& options,
                                               LinearAllocator* arena)
{
    ILL_PROFILE_SCOPE_N("JsonDocument::parse");
    if (text.size() > options.maxBytes)
    {
        return unexpected(makeError(text, options.maxBytes,
                                    std::format("the document is larger than {} bytes", options.maxBytes)));
    }
    const yyjson_alc allocator = arena != nullptr ? arenaAllocator(*arena) : yyjson_alc{};
    yyjson_read_err error{};
    // Without YYJSON_READ_INSITU the input is only read, never modified.
    char* data = const_cast<char*>(dataOf(text));
    yyjson_doc* doc = yyjson_read_opts(data, text.size(), YYJSON_READ_NOFLAG,
                                       arena != nullptr ? &allocator : nullptr, &error);
    if (doc == nullptr)
    {
        if (error.code == YYJSON_READ_ERROR_MEMORY_ALLOCATION)
        {
            ILL_CHECK(arena != nullptr, "out of memory parsing {} bytes of JSON", text.size());
            return unexpected(makeError(text, 0, "out of memory"));
        }
        return unexpected(makeError(text, error.pos, error.msg != nullptr ? error.msg : "invalid JSON"));
    }
    auto valid = Validator(text, options, arena).run(doc);
    if (!valid)
    {
        yyjson_doc_free(doc);
        return unexpected(std::move(valid).error());
    }
    return doc;
}

// --- Writing -----------------------------------------------------------------------------------------

// Navigation over immutable values (contiguous, in document order).
struct ImmutableNodes
{
    using Node = const yyjson_val*;

    static Node firstElement(Node array) noexcept { return array + 1; }
    static Node nextElement(Node element) noexcept { return unsafe_yyjson_get_next(element); }
    static Node firstKey(Node object) noexcept { return object + 1; }
    static Node valueOf(Node key) noexcept { return key + 1; }
    static Node nextKey(Node key) noexcept { return unsafe_yyjson_get_next(key + 1); }
};

// Navigation over mutable values (circular lists: a container points at its last child).
struct MutableNodes
{
    using Node = const yyjson_mut_val*;

    static Node firstElement(Node array) noexcept { return static_cast<Node>(array->uni.ptr)->next; }
    static Node nextElement(Node element) noexcept { return element->next; }
    static Node firstKey(Node object) noexcept { return static_cast<Node>(object->uni.ptr)->next->next; }
    static Node valueOf(Node key) noexcept { return key->next; }
    static Node nextKey(Node key) noexcept { return key->next->next; }
};

template <class Node>
u64 tagOf(Node node) noexcept
{
    return node->tag;
}

template <class Node>
usize lengthOf(Node node) noexcept
{
    return static_cast<usize>(node->tag >> YYJSON_TAG_BIT);
}

template <class Node>
bool isContainer(Node node) noexcept
{
    const u8 type = static_cast<u8>(node->tag & YYJSON_TYPE_MASK);
    return type == YYJSON_TYPE_ARR || type == YYJSON_TYPE_OBJ;
}

// True for UTF-8 sequences that yyjson accepts: no overlong forms, no surrogates, at most U+10FFFF.
// Returns the length of the sequence at @p s (with @p available bytes), or 0 if it is invalid.
usize utf8SequenceLength(const u8* s, usize available) noexcept
{
    const u8 lead = s[0];
    const auto continuation = [&](usize i) {
        return i < available && (s[i] & 0xC0) == 0x80;
    };
    if (lead >= 0xC2 && lead <= 0xDF)
    {
        return continuation(1) ? 2 : 0;
    }
    if (lead >= 0xE0 && lead <= 0xEF)
    {
        if (available < 3 || !continuation(1) || !continuation(2))
        {
            return 0;
        }
        if ((lead == 0xE0 && s[1] < 0xA0) || (lead == 0xED && s[1] > 0x9F))
        {
            return 0;
        }
        return 3;
    }
    if (lead >= 0xF0 && lead <= 0xF4)
    {
        if (available < 4 || !continuation(1) || !continuation(2) || !continuation(3))
        {
            return 0;
        }
        if ((lead == 0xF0 && s[1] < 0x90) || (lead == 0xF4 && s[1] > 0x8F))
        {
            return 0;
        }
        return 4;
    }
    return 0;
}

struct SortedMember
{
    const void* key;
    usize index; // position in the object, to order equal keys
};

template <class Nodes>
class Writer
{
public:
    using Node = typename Nodes::Node;

    Writer(std::string& out, const JsonWriteOptions& options)
        : m_out(out)
        , m_options(options)
    {
    }

    Expected<void, JsonError> write(Node root)
    {
        m_out.clear();
        if (!emit(root))
        {
            m_out.clear();
            return unexpected(writeError(m_error));
        }
        while (!m_frames.empty())
        {
            Frame& frame = m_frames.back();
            if (frame.remaining == 0)
            {
                if (m_options.pretty && !frame.oneLine)
                {
                    newline(m_frames.size() - 1);
                }
                m_out.push_back(frame.object ? '}' : ']');
                if (frame.sorted)
                {
                    m_sorted.resize(frame.sortedStart);
                }
                m_frames.pop();
                continue;
            }
            if (!frame.first)
            {
                m_out.push_back(',');
                if (m_options.pretty && frame.oneLine)
                {
                    m_out.push_back(' ');
                }
            }
            frame.first = false;
            if (m_options.pretty && !frame.oneLine)
            {
                newline(m_frames.size());
            }
            --frame.remaining;
            Node child = nullptr;
            if (frame.object)
            {
                Node key = nullptr;
                if (frame.sorted)
                {
                    key = static_cast<Node>(m_sorted.data()[frame.sortedNext++].key);
                }
                else
                {
                    key = frame.cursor;
                    frame.cursor = frame.remaining != 0 ? Nodes::nextKey(key) : nullptr;
                }
                if (!writeString(key))
                {
                    m_out.clear();
                    return unexpected(writeError(m_error));
                }
                m_out.append(m_options.pretty ? ": " : ":");
                child = Nodes::valueOf(key);
            }
            else
            {
                child = frame.cursor;
                frame.cursor = frame.remaining != 0 ? Nodes::nextElement(child) : nullptr;
            }
            // emit() may push a frame, which invalidates `frame`.
            if (!emit(child))
            {
                m_out.clear();
                return unexpected(writeError(m_error));
            }
        }
        if (m_options.pretty)
        {
            m_out.push_back('\n');
        }
        return {};
    }

private:
    struct Frame
    {
        Node cursor;     // next element or key (unsorted)
        usize remaining; // children not written yet
        usize sortedStart;
        usize sortedNext;
        bool object;
        bool oneLine;
        bool first;
        bool sorted;
    };

    void newline(usize depth)
    {
        m_out.push_back('\n');
        m_out.append(depth * 2, ' ');
    }

    // Writes a scalar or an empty container, or opens a non-empty container.
    bool emit(Node node)
    {
        const u64 tag = tagOf(node);
        switch (static_cast<u8>(tag & YYJSON_TYPE_MASK))
        {
        case YYJSON_TYPE_NULL:
            m_out.append("null");
            return true;
        case YYJSON_TYPE_BOOL:
            m_out.append((tag & YYJSON_SUBTYPE_MASK) == YYJSON_SUBTYPE_TRUE ? "true" : "false");
            return true;
        case YYJSON_TYPE_NUM:
            return writeNumber(node);
        case YYJSON_TYPE_STR:
            return writeString(node);
        case YYJSON_TYPE_ARR:
        case YYJSON_TYPE_OBJ:
            return open(node);
        default:
            m_error = "cannot write a raw value";
            return false;
        }
    }

    bool open(Node node)
    {
        const bool object = (tagOf(node) & YYJSON_TYPE_MASK) == YYJSON_TYPE_OBJ;
        const usize count = lengthOf(node);
        if (count == 0)
        {
            m_out.append(object ? "{}" : "[]");
            return true;
        }
        Frame frame{};
        frame.remaining = count;
        frame.object = object;
        frame.first = true;
        if (object)
        {
            frame.cursor = Nodes::firstKey(node);
            if (m_options.sortKeys && count > 1)
            {
                frame.sorted = true;
                frame.sortedStart = m_sorted.size();
                frame.sortedNext = frame.sortedStart;
                SortedMember* members = m_sorted.extend(count);
                Node key = frame.cursor;
                for (usize i = 0; i < count; ++i, key = i < count ? Nodes::nextKey(key) : key)
                {
                    members[i] = SortedMember{key, i};
                }
                std::sort(members, members + count, [](const SortedMember& a, const SortedMember& b) {
                    const int order =
                        keyView(static_cast<Node>(a.key)).compare(keyView(static_cast<Node>(b.key)));
                    return order != 0 ? order < 0 : a.index < b.index;
                });
            }
        }
        else
        {
            frame.cursor = Nodes::firstElement(node);
            if (m_options.pretty)
            {
                // Arrays of scalars (vectors, colours) stay on one line.
                frame.oneLine = true;
                Node element = frame.cursor;
                for (usize i = 0; i < count; ++i, element = i < count ? Nodes::nextElement(element) : element)
                {
                    if (isContainer(element))
                    {
                        frame.oneLine = false;
                        break;
                    }
                }
            }
        }
        m_out.push_back(object ? '{' : '[');
        m_frames.push(frame);
        return true;
    }

    static std::string_view keyView(Node key) noexcept { return {key->uni.str, lengthOf(key)}; }

    bool writeNumber(Node node)
    {
        if ((tagOf(node) & YYJSON_SUBTYPE_MASK) == YYJSON_SUBTYPE_REAL && !std::isfinite(node->uni.f64))
        {
            m_error = "cannot write a non-finite number";
            return false;
        }
        char buffer[48];
        const char* end = yyjson_write_number(reinterpret_cast<const yyjson_val*>(node), buffer);
        ILL_ASSERT(end != nullptr, "yyjson_write_number failed");
        m_out.append(buffer, static_cast<usize>(end - buffer));
        return true;
    }

    // Writes a quoted, escaped string exactly as yyjson does (no escaping of '/' or non-ASCII characters,
    // short escapes where JSON has them, uppercase \u00XX otherwise). Fails on invalid UTF-8.
    bool writeString(Node node)
    {
        static constexpr char kHex[] = "0123456789ABCDEF";
        const auto* text = reinterpret_cast<const u8*>(node->uni.str);
        const usize length = lengthOf(node);
        m_out.push_back('"');
        usize runStart = 0;
        usize i = 0;
        const auto flush = [&] {
            m_out.append(reinterpret_cast<const char*>(text) + runStart, i - runStart);
        };
        while (i < length)
        {
            const u8 c = text[i];
            if (c >= 0x20 && c != '"' && c != '\\' && c < 0x80)
            {
                ++i;
                continue;
            }
            if (c >= 0x80)
            {
                const usize sequence = utf8SequenceLength(text + i, length - i);
                if (sequence == 0)
                {
                    m_error = "cannot write a string that is not valid UTF-8";
                    return false;
                }
                i += sequence;
                continue;
            }
            flush();
            char escape[6] = {'\\', 'u', '0', '0', kHex[c >> 4], kHex[c & 0xF]};
            usize escapeLength = 6;
            switch (c)
            {
            case '"':
            case '\\':
                escape[1] = static_cast<char>(c);
                escapeLength = 2;
                break;
            case '\b':
                escape[1] = 'b';
                escapeLength = 2;
                break;
            case '\t':
                escape[1] = 't';
                escapeLength = 2;
                break;
            case '\n':
                escape[1] = 'n';
                escapeLength = 2;
                break;
            case '\f':
                escape[1] = 'f';
                escapeLength = 2;
                break;
            case '\r':
                escape[1] = 'r';
                escapeLength = 2;
                break;
            default:
                break;
            }
            m_out.append(escape, escapeLength);
            ++i;
            runStart = i;
        }
        flush();
        m_out.push_back('"');
        return true;
    }

    std::string& m_out;
    const JsonWriteOptions& m_options;
    SmallStack<Frame, 64> m_frames;
    SmallStack<SortedMember, 128> m_sorted;
    std::string_view m_error;
};

} // namespace

// --- JsonType ----------------------------------------------------------------------------------------

std::string_view jsonTypeName(JsonType type) noexcept
{
    switch (type)
    {
    case JsonType::Absent:
        return "absent";
    case JsonType::Null:
        return "null";
    case JsonType::Bool:
        return "boolean";
    case JsonType::Integer:
        return "integer";
    case JsonType::Real:
        return "real";
    case JsonType::String:
        return "string";
    case JsonType::Array:
        return "array";
    case JsonType::Object:
        return "object";
    }
    return "unknown";
}

// --- JsonValue ---------------------------------------------------------------------------------------

JsonType JsonValue::type() const noexcept
{
    return m_value != nullptr ? typeOfTag(m_value->tag) : JsonType::Absent;
}

std::optional<bool> JsonValue::asBool() const noexcept
{
    if (!yyjson_is_bool(const_cast<yyjson_val*>(m_value)))
    {
        return std::nullopt;
    }
    return unsafe_yyjson_get_bool(const_cast<yyjson_val*>(m_value));
}

std::optional<i64> JsonValue::asI64() const noexcept
{
    yyjson_val* value = const_cast<yyjson_val*>(m_value);
    if (yyjson_is_sint(value))
    {
        return unsafe_yyjson_get_sint(value);
    }
    if (yyjson_is_uint(value) && unsafe_yyjson_get_uint(value) <= static_cast<u64>(INT64_MAX))
    {
        return static_cast<i64>(unsafe_yyjson_get_uint(value));
    }
    return std::nullopt;
}

std::optional<u64> JsonValue::asU64() const noexcept
{
    yyjson_val* value = const_cast<yyjson_val*>(m_value);
    if (yyjson_is_uint(value))
    {
        return unsafe_yyjson_get_uint(value);
    }
    if (yyjson_is_sint(value) && unsafe_yyjson_get_sint(value) >= 0)
    {
        return static_cast<u64>(unsafe_yyjson_get_sint(value));
    }
    return std::nullopt;
}

std::optional<f64> JsonValue::asF64() const noexcept
{
    yyjson_val* value = const_cast<yyjson_val*>(m_value);
    if (!yyjson_is_num(value))
    {
        return std::nullopt;
    }
    return yyjson_get_num(value);
}

std::optional<std::string_view> JsonValue::asString() const noexcept
{
    if (!yyjson_is_str(const_cast<yyjson_val*>(m_value)))
    {
        return std::nullopt;
    }
    return stringOf(m_value);
}

usize JsonValue::size() const noexcept
{
    return yyjson_is_ctn(const_cast<yyjson_val*>(m_value)) ? unsafe_yyjson_get_len(m_value) : 0;
}

JsonValue JsonValue::operator[](usize index) const noexcept
{
    return JsonValue(yyjson_arr_get(const_cast<yyjson_val*>(m_value), index));
}

JsonValue JsonValue::find(std::string_view key) const noexcept
{
    return JsonValue(yyjson_obj_getn(const_cast<yyjson_val*>(m_value), dataOf(key), key.size()));
}

JsonValue::ElementRange JsonValue::elements() const noexcept
{
    if (!yyjson_is_arr(const_cast<yyjson_val*>(m_value)))
    {
        return ElementRange({}, 0);
    }
    const usize count = unsafe_yyjson_get_len(m_value);
    return ElementRange(ElementIterator(m_value + 1, count), count);
}

JsonValue::MemberRange JsonValue::members() const noexcept
{
    if (!yyjson_is_obj(const_cast<yyjson_val*>(m_value)))
    {
        return MemberRange({}, 0);
    }
    const usize count = unsafe_yyjson_get_len(m_value);
    return MemberRange(MemberIterator(m_value + 1, count), count);
}

JsonValue::ElementIterator& JsonValue::ElementIterator::operator++() noexcept
{
    ILL_ASSERT(m_remaining > 0, "incrementing an end iterator");
    m_current = unsafe_yyjson_get_next(m_current);
    --m_remaining;
    return *this;
}

JsonMember JsonValue::MemberIterator::operator*() const noexcept
{
    return JsonMember{stringOf(m_key), JsonValue(m_key + 1)};
}

JsonValue::MemberIterator& JsonValue::MemberIterator::operator++() noexcept
{
    ILL_ASSERT(m_remaining > 0, "incrementing an end iterator");
    m_key = unsafe_yyjson_get_next(m_key + 1);
    --m_remaining;
    return *this;
}

namespace
{

// Compares an object key with an escaped reference token ("~0" is '~', "~1" is '/'). Returns false for
// malformed escapes.
bool keyMatchesToken(std::string_view key, std::string_view token) noexcept
{
    usize k = 0;
    for (usize t = 0; t < token.size(); ++t, ++k)
    {
        char c = token[t];
        if (c == '~')
        {
            if (t + 1 >= token.size() || (token[t + 1] != '0' && token[t + 1] != '1'))
            {
                return false;
            }
            c = token[++t] == '0' ? '~' : '/';
        }
        if (k >= key.size() || key[k] != c)
        {
            return false;
        }
    }
    return k == key.size();
}

// Parses an array index token: decimal digits without leading zeros.
std::optional<usize> indexOfToken(std::string_view token) noexcept
{
    if (token.empty() || (token.size() > 1 && token[0] == '0'))
    {
        return std::nullopt;
    }
    usize index = 0;
    for (const char c : token)
    {
        if (c < '0' || c > '9')
        {
            return std::nullopt;
        }
        const auto digit = static_cast<usize>(c - '0');
        if (index > (~usize{0} - digit) / 10)
        {
            return std::nullopt;
        }
        index = index * 10 + digit;
    }
    return index;
}

bool validEscapes(std::string_view token) noexcept
{
    for (usize i = 0; i < token.size(); ++i)
    {
        if (token[i] == '~' && (i + 1 >= token.size() || (token[i + 1] != '0' && token[i + 1] != '1')))
        {
            return false;
        }
        if (token[i] == '~')
        {
            ++i;
        }
    }
    return true;
}

} // namespace

JsonValue JsonValue::pointer(std::string_view pointer) const noexcept
{
    JsonValue current = *this;
    if (pointer.empty() || current.isAbsent())
    {
        return current;
    }
    if (pointer[0] != '/')
    {
        return {};
    }
    usize position = 1;
    for (;;)
    {
        const usize slash = pointer.find('/', position);
        const std::string_view token = pointer.substr(
            position, slash == std::string_view::npos ? std::string_view::npos : slash - position);
        if (!validEscapes(token))
        {
            return {};
        }
        if (current.isArray())
        {
            const std::optional<usize> index = indexOfToken(token);
            if (!index || *index >= current.size())
            {
                return {};
            }
            current = current[*index];
        }
        else if (current.isObject())
        {
            JsonValue next;
            for (const JsonMember member : current.members())
            {
                if (keyMatchesToken(member.key, token))
                {
                    next = member.value;
                    break;
                }
            }
            if (next.isAbsent())
            {
                return {};
            }
            current = next;
        }
        else
        {
            return {};
        }
        if (slash == std::string_view::npos)
        {
            return current;
        }
        position = slash + 1;
    }
}

Expected<void, JsonError> JsonValue::write(std::string& out, const JsonWriteOptions& options) const
{
    ILL_PROFILE_SCOPE_N("JsonValue::write");
    if (m_value == nullptr)
    {
        out.clear();
        return unexpected(writeError("cannot write an absent value"));
    }
    return Writer<ImmutableNodes>(out, options).write(m_value);
}

std::string JsonValue::dump() const
{
    std::string text;
    if (!write(text, JsonWriteOptions{.pretty = false}))
    {
        text.clear();
    }
    return text;
}

// --- JsonDocument ------------------------------------------------------------------------------------

JsonDocument::~JsonDocument()
{
    yyjson_doc_free(m_doc);
}

JsonDocument::JsonDocument(JsonDocument&& other) noexcept
    : m_doc(std::exchange(other.m_doc, nullptr))
{
}

JsonDocument& JsonDocument::operator=(JsonDocument&& other) noexcept
{
    if (this != &other)
    {
        yyjson_doc_free(m_doc);
        m_doc = std::exchange(other.m_doc, nullptr);
    }
    return *this;
}

Expected<JsonDocument, JsonError> JsonDocument::parse(std::string_view text, const JsonReadOptions& options)
{
    auto doc = parseDocument(text, options, nullptr);
    if (!doc)
    {
        return unexpected(std::move(doc).error());
    }
    return JsonDocument(*doc);
}

Expected<JsonDocument, JsonError> JsonDocument::parse(std::string_view text, LinearAllocator& arena,
                                                      const JsonReadOptions& options)
{
    auto doc = parseDocument(text, options, &arena);
    if (!doc)
    {
        return unexpected(std::move(doc).error());
    }
    return JsonDocument(*doc);
}

JsonValue JsonDocument::root() const noexcept
{
    return JsonValue(m_doc != nullptr ? yyjson_doc_get_root(m_doc) : nullptr);
}

// --- JsonMutValue ------------------------------------------------------------------------------------

namespace
{

JsonMutValue absentIn(detail::JsonMutState* state) noexcept
{
    if (state != nullptr)
    {
        state->failed = true;
    }
    return {};
}

// Records an allocation failure: fatal on the heap, a sticky document failure on an arena.
yyjson_mut_val* checked(detail::JsonMutState& state, yyjson_mut_val* value)
{
    if (value == nullptr)
    {
        ILL_CHECK(state.arena != nullptr, "out of memory building a JSON document");
        state.failed = true;
    }
    return value;
}

} // namespace

JsonType JsonMutValue::type() const noexcept
{
    return m_value != nullptr ? typeOfTag(m_value->tag) : JsonType::Absent;
}

usize JsonMutValue::size() const noexcept
{
    return yyjson_mut_is_ctn(m_value) ? unsafe_yyjson_get_len(m_value) : 0;
}

JsonMutValue& JsonMutValue::push(JsonMutValue value)
{
    if (m_value == nullptr || value.m_value == nullptr)
    {
        (void)absentIn(m_state != nullptr ? m_state : value.m_state);
        return *this;
    }
    ILL_ASSERT(yyjson_mut_is_arr(m_value), "push() on a JSON {}", jsonTypeName(type()));
    yyjson_mut_arr_append(m_value, value.m_value);
    return *this;
}

JsonMutValue& JsonMutValue::add(std::string_view key, JsonMutValue value)
{
    if (m_value == nullptr || value.m_value == nullptr)
    {
        (void)absentIn(m_state != nullptr ? m_state : value.m_state);
        return *this;
    }
    ILL_ASSERT(yyjson_mut_is_obj(m_value), "add() on a JSON {}", jsonTypeName(type()));
    yyjson_mut_val* name = checked(*m_state, yyjson_mut_strncpy(m_state->doc, dataOf(key), key.size()));
    if (name != nullptr)
    {
        yyjson_mut_obj_add(m_value, name, value.m_value);
    }
    return *this;
}

JsonMutValue& JsonMutValue::set(std::string_view key, JsonMutValue value)
{
    if (m_value == nullptr || value.m_value == nullptr)
    {
        (void)absentIn(m_state != nullptr ? m_state : value.m_state);
        return *this;
    }
    ILL_ASSERT(yyjson_mut_is_obj(m_value), "set() on a JSON {}", jsonTypeName(type()));
    yyjson_mut_obj_iter iter = yyjson_mut_obj_iter_with(m_value);
    bool replaced = false;
    while (yyjson_mut_val* name = yyjson_mut_obj_iter_next(&iter))
    {
        if (unsafe_yyjson_equals_strn(name, dataOf(key), key.size()))
        {
            if (!replaced)
            {
                value.m_value->next = name->next->next;
                name->next = value.m_value;
                replaced = true;
            }
            else
            {
                yyjson_mut_obj_iter_remove(&iter); // drop later duplicates
            }
        }
    }
    return replaced ? *this : add(key, value);
}

bool JsonMutValue::remove(std::string_view key)
{
    if (m_value == nullptr)
    {
        return false;
    }
    ILL_ASSERT(yyjson_mut_is_obj(m_value), "remove() on a JSON {}", jsonTypeName(type()));
    return yyjson_mut_obj_remove_strn(m_value, dataOf(key), key.size()) != nullptr;
}

JsonMutValue JsonMutValue::addObject(std::string_view key)
{
    if (m_state == nullptr)
    {
        return {};
    }
    const JsonMutValue child(m_state, checked(*m_state, yyjson_mut_obj(m_state->doc)));
    add(key, child);
    return child;
}

JsonMutValue JsonMutValue::addArray(std::string_view key)
{
    if (m_state == nullptr)
    {
        return {};
    }
    const JsonMutValue child(m_state, checked(*m_state, yyjson_mut_arr(m_state->doc)));
    add(key, child);
    return child;
}

JsonMutValue JsonMutValue::pushObject()
{
    if (m_state == nullptr)
    {
        return {};
    }
    const JsonMutValue child(m_state, checked(*m_state, yyjson_mut_obj(m_state->doc)));
    push(child);
    return child;
}

JsonMutValue JsonMutValue::pushArray()
{
    if (m_state == nullptr)
    {
        return {};
    }
    const JsonMutValue child(m_state, checked(*m_state, yyjson_mut_arr(m_state->doc)));
    push(child);
    return child;
}

JsonMutValue JsonMutValue::find(std::string_view key) const noexcept
{
    yyjson_mut_val* value = yyjson_mut_obj_getn(m_value, dataOf(key), key.size());
    return value != nullptr ? JsonMutValue(m_state, value) : JsonMutValue();
}

Expected<void, JsonError> JsonMutValue::write(std::string& out, const JsonWriteOptions& options) const
{
    ILL_PROFILE_SCOPE_N("JsonMutValue::write");
    if (m_state != nullptr && m_state->failed)
    {
        out.clear();
        return unexpected(writeError("the document is incomplete: a builder call ran out of memory or "
                                     "received an absent value"));
    }
    if (m_value == nullptr)
    {
        out.clear();
        return unexpected(writeError("cannot write an absent value"));
    }
    return Writer<MutableNodes>(out, options).write(m_value);
}

std::string JsonMutValue::dump() const
{
    std::string text;
    if (!write(text, JsonWriteOptions{.pretty = false}))
    {
        text.clear();
    }
    return text;
}

// --- JsonMutDocument ---------------------------------------------------------------------------------

JsonMutDocument::JsonMutDocument()
    : m_state(new detail::JsonMutState())
{
    m_state->doc = yyjson_mut_doc_new(nullptr);
    ILL_CHECK(m_state->doc != nullptr, "out of memory creating a JSON document");
}

JsonMutDocument::JsonMutDocument(LinearAllocator& arena)
    : m_state(arena.create<detail::JsonMutState>())
{
    if (m_state == nullptr)
    {
        return; // failed(): the arena cannot even hold the document state
    }
    m_state->arena = &arena;
    const yyjson_alc allocator = arenaAllocator(arena);
    m_state->doc = yyjson_mut_doc_new(&allocator);
    m_state->failed = m_state->doc == nullptr;
}

JsonMutDocument::~JsonMutDocument()
{
    if (m_state == nullptr)
    {
        return;
    }
    yyjson_mut_doc_free(m_state->doc);
    if (m_state->arena == nullptr)
    {
        delete m_state;
    }
}

JsonMutDocument::JsonMutDocument(JsonMutDocument&& other) noexcept
    : m_state(std::exchange(other.m_state, nullptr))
{
}

JsonMutDocument& JsonMutDocument::operator=(JsonMutDocument&& other) noexcept
{
    if (this != &other)
    {
        JsonMutDocument old(std::move(*this));
        m_state = std::exchange(other.m_state, nullptr);
    }
    return *this;
}

bool JsonMutDocument::failed() const noexcept
{
    return m_state == nullptr || m_state->failed;
}

JsonMutValue JsonMutDocument::makeNull()
{
    if (m_state == nullptr || m_state->doc == nullptr)
    {
        return {};
    }
    return {m_state, checked(*m_state, yyjson_mut_null(m_state->doc))};
}

JsonMutValue JsonMutDocument::makeBool(bool value)
{
    if (m_state == nullptr || m_state->doc == nullptr)
    {
        return {};
    }
    return {m_state, checked(*m_state, yyjson_mut_bool(m_state->doc, value))};
}

JsonMutValue JsonMutDocument::makeI64(i64 value)
{
    if (m_state == nullptr || m_state->doc == nullptr)
    {
        return {};
    }
    return {m_state, checked(*m_state, yyjson_mut_sint(m_state->doc, value))};
}

JsonMutValue JsonMutDocument::makeU64(u64 value)
{
    if (m_state == nullptr || m_state->doc == nullptr)
    {
        return {};
    }
    return {m_state, checked(*m_state, yyjson_mut_uint(m_state->doc, value))};
}

JsonMutValue JsonMutDocument::makeF64(f64 value)
{
    if (m_state == nullptr || m_state->doc == nullptr)
    {
        return {};
    }
    return {m_state, checked(*m_state, yyjson_mut_double(m_state->doc, value))};
}

JsonMutValue JsonMutDocument::makeF32(f32 value)
{
    if (m_state == nullptr || m_state->doc == nullptr)
    {
        return {};
    }
    return {m_state, checked(*m_state, yyjson_mut_float(m_state->doc, value))};
}

JsonMutValue JsonMutDocument::makeString(std::string_view text)
{
    if (m_state == nullptr || m_state->doc == nullptr)
    {
        return {};
    }
    return {m_state, checked(*m_state, yyjson_mut_strncpy(m_state->doc, dataOf(text), text.size()))};
}

JsonMutValue JsonMutDocument::makeArray()
{
    if (m_state == nullptr || m_state->doc == nullptr)
    {
        return {};
    }
    return {m_state, checked(*m_state, yyjson_mut_arr(m_state->doc))};
}

JsonMutValue JsonMutDocument::makeObject()
{
    if (m_state == nullptr || m_state->doc == nullptr)
    {
        return {};
    }
    return {m_state, checked(*m_state, yyjson_mut_obj(m_state->doc))};
}

JsonMutValue JsonMutDocument::copy(JsonValue value)
{
    if (m_state == nullptr || m_state->doc == nullptr || value.isAbsent())
    {
        return {m_state, nullptr};
    }
    return {m_state,
            checked(*m_state, yyjson_val_mut_copy(m_state->doc, const_cast<yyjson_val*>(value.m_value)))};
}

JsonMutValue JsonMutDocument::root() const noexcept
{
    if (m_state == nullptr || m_state->doc == nullptr)
    {
        return {};
    }
    yyjson_mut_val* value = yyjson_mut_doc_get_root(m_state->doc);
    return value != nullptr ? JsonMutValue(m_state, value) : JsonMutValue();
}

void JsonMutDocument::setRoot(JsonMutValue value) noexcept
{
    if (m_state == nullptr || m_state->doc == nullptr)
    {
        return;
    }
    if (value.isAbsent())
    {
        m_state->failed = true;
    }
    yyjson_mut_doc_set_root(m_state->doc, value.m_value);
}

Expected<void, JsonError> JsonMutDocument::write(std::string& out, const JsonWriteOptions& options) const
{
    if (failed())
    {
        out.clear();
        return unexpected(writeError("the document is incomplete: a builder call ran out of memory or "
                                     "received an absent value"));
    }
    const JsonMutValue value = root();
    if (value.isAbsent())
    {
        out.clear();
        return unexpected(writeError("the document has no root value"));
    }
    return value.write(out, options);
}

} // namespace illusion
