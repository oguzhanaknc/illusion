#pragma once

/// @file
/// JSON documents: a strict, non-throwing reader with cheap views, a builder, and a canonical writer.
///
/// ```cpp
/// auto parsed = JsonDocument::parse(R"({"name": "crate", "size": [1, 2, 3]})");
/// if (!parsed)
/// {
///     const JsonError& error = parsed.error();
///     ILL_LOG_ERROR("Tools", "line {}, column {}: {}", error.line, error.column, error.message);
///     return;
/// }
/// const JsonValue root = parsed->root();
/// const std::string_view name = root.find("name").asString().value_or("unnamed");
/// const i64 height = root.pointer("/size/1").asI64().value_or(0);
///
/// JsonMutDocument doc;
/// JsonMutValue object = doc.makeObject();
/// object.add("name", doc.makeString(name));
/// object.add("height", doc.makeI64(height));
/// doc.setRoot(object);
/// std::string text;
/// if (const auto written = doc.write(text); !written)
/// {
///     ILL_LOG_ERROR("Tools", "{}", written.error().message);
/// }
/// ```
///
/// The reader accepts standard JSON only (RFC 8259): it rejects comments, trailing commas, NaN and
/// Infinity literals, invalid UTF-8, trailing content, duplicate object keys (by default) and documents
/// that are nested too deeply or are too large. Integers keep their exact 64-bit value. Nothing throws:
/// failures are returned as JsonError. The implementation (yyjson) is private to illusion::core.
///
/// Pretty output is canonical: two-space indentation, arrays of scalars on one line, the shortest text
/// that reads back to the same number, and a final newline. Scene files use this format.

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>

#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

struct yyjson_doc;
struct yyjson_val;
struct yyjson_mut_val;

namespace illusion
{

class LinearAllocator;
class JsonMutDocument;

/// Implementation details. Not part of the public API.
namespace detail
{
struct JsonMutState;
} // namespace detail

/// Why reading or writing JSON failed.
struct JsonError
{
    usize offset = 0;    ///< Byte offset of the problem in the input (0 for write errors).
    u32 line = 0;        ///< 1-based line of the problem in the input (0 for write errors).
    u32 column = 0;      ///< 1-based column of the problem, counted in bytes (0 for write errors).
    std::string message; ///< Human-readable description in English.
};

/// Limits and policies of JsonDocument::parse().
struct JsonReadOptions
{
    /// Maximum nesting of arrays and objects: `[]` has depth 1, `[[]]` depth 2, a scalar depth 0.
    u32 maxDepth = 128;
    /// Maximum size of the input in bytes.
    usize maxBytes = usize{64} * 1024 * 1024;
    /// Accept objects with repeated keys. find() and pointer() then return the first occurrence.
    bool allowDuplicateKeys = false;
};

/// Formatting of JsonValue::write(), JsonMutValue::write() and JsonMutDocument::write().
struct JsonWriteOptions
{
    /// Canonical, human-readable layout (see the file documentation); false writes no whitespace at all.
    bool pretty = true;
    /// Write object members ordered by the bytes of their keys, at every level, instead of in insertion
    /// or file order.
    bool sortKeys = false;
};

/// Kind of a JSON value. Absent stands for a value that does not exist (a missing key, an index out of
/// range, a failed pointer), so lookups can be chained without checks in between.
enum class JsonType : u8
{
    Absent,  ///< No value.
    Null,    ///< `null`.
    Bool,    ///< `true` or `false`.
    Integer, ///< A number without fraction or exponent that fits in i64 or u64.
    Real,    ///< Any other number (stored as f64).
    String,  ///< A UTF-8 string.
    Array,   ///< An ordered list of values.
    Object   ///< An ordered list of key/value members.
};

/// Returns the lowercase name of @p type, e.g. "object", for messages.
[[nodiscard]] std::string_view jsonTypeName(JsonType type) noexcept;

struct JsonMember;

/// Read-only view of a value in a JsonDocument: two pointers, cheap to copy. A view is valid while its
/// document lives (moving the document keeps it valid). A default-constructed view is absent.
class JsonValue
{
public:
    class ElementIterator;
    class MemberIterator;
    class ElementRange;
    class MemberRange;

    /// Creates an absent view.
    constexpr JsonValue() noexcept = default;

    /// The kind of value.
    [[nodiscard]] JsonType type() const noexcept;

    /// True if there is no value.
    [[nodiscard]] bool isAbsent() const noexcept { return m_value == nullptr; }
    /// True for `null`.
    [[nodiscard]] bool isNull() const noexcept { return type() == JsonType::Null; }
    /// True for `true` and `false`.
    [[nodiscard]] bool isBool() const noexcept { return type() == JsonType::Bool; }
    /// True for integers (see JsonType::Integer).
    [[nodiscard]] bool isInteger() const noexcept { return type() == JsonType::Integer; }
    /// True for numbers that are not integers.
    [[nodiscard]] bool isReal() const noexcept { return type() == JsonType::Real; }
    /// True for any number.
    [[nodiscard]] bool isNumber() const noexcept { return isInteger() || isReal(); }
    /// True for strings.
    [[nodiscard]] bool isString() const noexcept { return type() == JsonType::String; }
    /// True for arrays.
    [[nodiscard]] bool isArray() const noexcept { return type() == JsonType::Array; }
    /// True for objects.
    [[nodiscard]] bool isObject() const noexcept { return type() == JsonType::Object; }

    /// The boolean, or nullopt if this is not a boolean.
    [[nodiscard]] std::optional<bool> asBool() const noexcept;
    /// The integer if it is in the range of i64, otherwise nullopt (reals are not converted).
    [[nodiscard]] std::optional<i64> asI64() const noexcept;
    /// The integer if it is in the range of u64, otherwise nullopt (reals are not converted).
    [[nodiscard]] std::optional<u64> asU64() const noexcept;
    /// The number as f64 (integers are converted, possibly rounded), or nullopt if this is not a number.
    [[nodiscard]] std::optional<f64> asF64() const noexcept;
    /// The string (valid while the document lives; may contain null bytes), or nullopt.
    [[nodiscard]] std::optional<std::string_view> asString() const noexcept;

    /// Number of elements of an array or members of an object; 0 for anything else.
    [[nodiscard]] usize size() const noexcept;

    /// Element @p index of an array, or an absent view if this is not an array or @p index is out of
    /// range. Takes time linear in @p index unless the array holds only scalars; iterate with elements()
    /// to visit every element.
    [[nodiscard]] JsonValue operator[](usize index) const noexcept;

    /// Value of the first member called @p key, or an absent view if this is not an object or has no such
    /// member. Linear in the number of members.
    [[nodiscard]] JsonValue find(std::string_view key) const noexcept;

    /// Elements of an array in file order (empty for anything else).
    [[nodiscard]] ElementRange elements() const noexcept;

    /// Members of an object in file order (empty for anything else).
    [[nodiscard]] MemberRange members() const noexcept;

    /// Resolves a JSON Pointer (RFC 6901) relative to this value: "" is this value, "/a/0" is element 0
    /// of member "a", "~0" and "~1" in a token stand for '~' and '/'. Array indices are decimal without
    /// leading zeros; "-" (the end of an array) and anything malformed give an absent view.
    [[nodiscard]] JsonValue pointer(std::string_view pointer) const noexcept;

    /// Replaces the contents of @p out with this value as JSON text. Reuses the capacity of @p out, so
    /// writing into the same string again does not allocate once it is large enough (unless the value is
    /// nested more than 64 levels deep, or, with sortKeys, more than 128 members of the objects being
    /// written are open at once). Fails for an absent value; @p out is then empty.
    Expected<void, JsonError> write(std::string& out, const JsonWriteOptions& options = {}) const;

    /// Returns this value as compact JSON text for diagnostics ("" for an absent value).
    [[nodiscard]] std::string dump() const;

    /// Two views are equal when they refer to the same value of the same document (not a deep comparison).
    [[nodiscard]] friend bool operator==(const JsonValue& a, const JsonValue& b) noexcept = default;

private:
    friend class JsonDocument;
    friend class JsonMutDocument;

    explicit JsonValue(const yyjson_val* value) noexcept
        : m_value(value)
    {
    }

    const yyjson_val* m_value = nullptr;
};

/// A member of an object: its key and its value.
struct JsonMember
{
    std::string_view key; ///< The key (valid while the document lives).
    JsonValue value;      ///< The value.
};

/// Forward iterator over the elements of an array.
class JsonValue::ElementIterator
{
public:
    using value_type = JsonValue;                       ///< Element type.
    using difference_type = std::ptrdiff_t;             ///< Distance type.
    using iterator_concept = std::forward_iterator_tag; ///< C++20 iterator concept.
    using iterator_category = std::input_iterator_tag;  ///< Legacy category (elements are returned by value).

    /// Creates an end iterator.
    ElementIterator() noexcept = default;

    /// The current element.
    [[nodiscard]] JsonValue operator*() const noexcept { return JsonValue(m_current); }

    /// Advances to the next element.
    ElementIterator& operator++() noexcept;

    /// Advances to the next element and returns the previous position.
    ElementIterator operator++(int) noexcept
    {
        ElementIterator previous = *this;
        ++*this;
        return previous;
    }

    /// Compares positions.
    [[nodiscard]] friend bool operator==(const ElementIterator& a, const ElementIterator& b) noexcept
    {
        return a.m_remaining == b.m_remaining;
    }

private:
    friend class JsonValue;
    ElementIterator(const yyjson_val* current, usize remaining) noexcept
        : m_current(current)
        , m_remaining(remaining)
    {
    }

    const yyjson_val* m_current = nullptr;
    usize m_remaining = 0;
};

/// Forward iterator over the members of an object.
class JsonValue::MemberIterator
{
public:
    using value_type = JsonMember;                      ///< Element type.
    using difference_type = std::ptrdiff_t;             ///< Distance type.
    using iterator_concept = std::forward_iterator_tag; ///< C++20 iterator concept.
    using iterator_category = std::input_iterator_tag;  ///< Legacy category (members are returned by value).

    /// Creates an end iterator.
    MemberIterator() noexcept = default;

    /// The current member.
    [[nodiscard]] JsonMember operator*() const noexcept;

    /// Advances to the next member.
    MemberIterator& operator++() noexcept;

    /// Advances to the next member and returns the previous position.
    MemberIterator operator++(int) noexcept
    {
        MemberIterator previous = *this;
        ++*this;
        return previous;
    }

    /// Compares positions.
    [[nodiscard]] friend bool operator==(const MemberIterator& a, const MemberIterator& b) noexcept
    {
        return a.m_remaining == b.m_remaining;
    }

private:
    friend class JsonValue;
    MemberIterator(const yyjson_val* key, usize remaining) noexcept
        : m_key(key)
        , m_remaining(remaining)
    {
    }

    const yyjson_val* m_key = nullptr;
    usize m_remaining = 0;
};

/// The elements of an array, for range-based for loops.
class JsonValue::ElementRange
{
public:
    /// First element.
    [[nodiscard]] ElementIterator begin() const noexcept { return m_begin; }
    /// Past the last element.
    [[nodiscard]] ElementIterator end() const noexcept { return {}; }
    /// Number of elements.
    [[nodiscard]] usize size() const noexcept { return m_size; }
    /// True if there are no elements.
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

private:
    friend class JsonValue;
    ElementRange(ElementIterator begin, usize size) noexcept
        : m_begin(begin)
        , m_size(size)
    {
    }

    ElementIterator m_begin;
    usize m_size;
};

/// The members of an object, for range-based for loops.
class JsonValue::MemberRange
{
public:
    /// First member.
    [[nodiscard]] MemberIterator begin() const noexcept { return m_begin; }
    /// Past the last member.
    [[nodiscard]] MemberIterator end() const noexcept { return {}; }
    /// Number of members.
    [[nodiscard]] usize size() const noexcept { return m_size; }
    /// True if there are no members.
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

private:
    friend class JsonValue;
    MemberRange(MemberIterator begin, usize size) noexcept
        : m_begin(begin)
        , m_size(size)
    {
    }

    MemberIterator m_begin;
    usize m_size;
};

/// Owner of a parsed, immutable JSON document. Move-only. Views into it (JsonValue) stay valid until the
/// document is destroyed or assigned to.
class JsonDocument
{
public:
    /// Creates an empty document whose root is absent.
    JsonDocument() noexcept = default;
    ~JsonDocument();

    /// Takes the document of @p other, which becomes empty. Views stay valid.
    JsonDocument(JsonDocument&& other) noexcept;
    /// Frees this document and takes the one of @p other, which becomes empty.
    JsonDocument& operator=(JsonDocument&& other) noexcept;
    JsonDocument(const JsonDocument&) = delete;            ///< Not copyable.
    JsonDocument& operator=(const JsonDocument&) = delete; ///< Not copyable.

    /// Parses @p text (UTF-8, without a byte order mark). The text is not referenced afterwards. Returns
    /// the document, or the position and description of the first problem found.
    [[nodiscard]] static Expected<JsonDocument, JsonError> parse(std::string_view text,
                                                                 const JsonReadOptions& options = {});

    /// Like parse(std::string_view, const JsonReadOptions&), but takes every byte of the document and of
    /// the validation from @p arena instead of the heap. @p arena must outlive the document; a full arena
    /// fails with "out of memory". Typical documents need 2 to 4 times the size of the text; every value
    /// takes 16 bytes, so dense arrays of small numbers need up to 10 times.
    [[nodiscard]] static Expected<JsonDocument, JsonError>
    parse(std::string_view text, LinearAllocator& arena, const JsonReadOptions& options = {});

    /// The root value (absent for an empty document).
    [[nodiscard]] JsonValue root() const noexcept;

    /// True for a default-constructed or moved-from document.
    [[nodiscard]] bool empty() const noexcept { return m_doc == nullptr; }

    /// Writes the root value, see JsonValue::write().
    Expected<void, JsonError> write(std::string& out, const JsonWriteOptions& options = {}) const
    {
        return root().write(out, options);
    }

private:
    explicit JsonDocument(yyjson_doc* doc) noexcept
        : m_doc(doc)
    {
    }

    yyjson_doc* m_doc = nullptr;
};

/// Handle to a value in a JsonMutDocument: two pointers, cheap to copy, valid while the document lives.
/// A default-constructed handle is absent. Containers are filled with add()/push(); each value may be
/// added to one container (or be the root) only once.
class JsonMutValue
{
public:
    /// Creates an absent handle.
    constexpr JsonMutValue() noexcept = default;

    /// The kind of value (JsonType::Absent for an absent handle).
    [[nodiscard]] JsonType type() const noexcept;

    /// True if there is no value (also returned by the builder functions when the arena is full).
    [[nodiscard]] bool isAbsent() const noexcept { return m_value == nullptr; }

    /// Number of elements of an array or members of an object; 0 for anything else.
    [[nodiscard]] usize size() const noexcept;

    /// Appends @p value to this array. Ignored (and the document marked as failed) if either handle is
    /// absent. Asserts that this is an array.
    JsonMutValue& push(JsonMutValue value);

    /// Appends a member to this object, copying @p key. The key must not already be present: duplicate
    /// keys would be written, and the reader rejects them (use set() to replace). Ignored (and the
    /// document marked as failed) if either handle is absent. Asserts that this is an object.
    JsonMutValue& add(std::string_view key, JsonMutValue value);

    /// Replaces the value of the member @p key, or appends the member if there is none. Linear in the
    /// number of members. Asserts that this is an object.
    JsonMutValue& set(std::string_view key, JsonMutValue value);

    /// Removes every member called @p key and returns true if there was one. Asserts that this is an
    /// object.
    bool remove(std::string_view key);

    /// Appends a new empty object member called @p key and returns it.
    JsonMutValue addObject(std::string_view key);

    /// Appends a new empty array member called @p key and returns it.
    JsonMutValue addArray(std::string_view key);

    /// Appends a new empty object to this array and returns it.
    JsonMutValue pushObject();

    /// Appends a new empty array to this array and returns it.
    JsonMutValue pushArray();

    /// The value of the first member called @p key, or an absent handle. Linear in the number of members.
    [[nodiscard]] JsonMutValue find(std::string_view key) const noexcept;

    /// Replaces the contents of @p out with this value as JSON text, reusing its capacity. Fails for an
    /// absent value, non-finite numbers, strings that are not valid UTF-8, and when the document failed()
    /// (the value may be incomplete); @p out is then empty.
    Expected<void, JsonError> write(std::string& out, const JsonWriteOptions& options = {}) const;

    /// Returns this value as compact JSON text for diagnostics ("" if it cannot be written).
    [[nodiscard]] std::string dump() const;

    /// Two handles are equal when they refer to the same value.
    [[nodiscard]] friend bool operator==(const JsonMutValue& a, const JsonMutValue& b) noexcept
    {
        return a.m_value == b.m_value;
    }

private:
    friend class JsonMutDocument;

    JsonMutValue(detail::JsonMutState* state, yyjson_mut_val* value) noexcept
        : m_state(state)
        , m_value(value)
    {
    }

    detail::JsonMutState* m_state = nullptr;
    yyjson_mut_val* m_value = nullptr;
};

/// Builds a JSON document. Strings and keys are copied, so the inputs need not outlive it. Move-only;
/// handles stay valid when the document is moved.
///
/// Memory comes from the heap or, with the arena constructor, from a caller's LinearAllocator. When the
/// arena runs out, builder functions return absent handles, the document remembers the failure
/// (failed()), and write() reports "out of memory".
class JsonMutDocument
{
public:
    /// Creates an empty document on the heap.
    JsonMutDocument();

    /// Creates an empty document whose memory comes from @p arena, which must outlive the document.
    explicit JsonMutDocument(LinearAllocator& arena);

    ~JsonMutDocument();

    /// Takes the document of @p other, which may then only be destroyed or assigned to.
    JsonMutDocument(JsonMutDocument&& other) noexcept;
    /// Frees this document and takes the one of @p other.
    JsonMutDocument& operator=(JsonMutDocument&& other) noexcept;
    JsonMutDocument(const JsonMutDocument&) = delete;            ///< Not copyable.
    JsonMutDocument& operator=(const JsonMutDocument&) = delete; ///< Not copyable.

    /// Creates `null`.
    [[nodiscard]] JsonMutValue makeNull();
    /// Creates `true` or `false`.
    [[nodiscard]] JsonMutValue makeBool(bool value);
    /// Creates a signed integer.
    [[nodiscard]] JsonMutValue makeI64(i64 value);
    /// Creates an unsigned integer.
    [[nodiscard]] JsonMutValue makeU64(u64 value);
    /// Creates a number written in the shortest form that reads back as the same f64.
    [[nodiscard]] JsonMutValue makeF64(f64 value);
    /// Creates a number written in the shortest form that reads back as the same f32 (0.1f is written as
    /// 0.1, not 0.10000000149011612).
    [[nodiscard]] JsonMutValue makeF32(f32 value);
    /// Creates a string holding a copy of @p text.
    [[nodiscard]] JsonMutValue makeString(std::string_view text);
    /// Creates an empty array.
    [[nodiscard]] JsonMutValue makeArray();
    /// Creates an empty object.
    [[nodiscard]] JsonMutValue makeObject();
    /// Creates a deep copy of @p value (from any JsonDocument). An absent @p value gives an absent handle.
    [[nodiscard]] JsonMutValue copy(JsonValue value);

    /// The root value (absent until setRoot()).
    [[nodiscard]] JsonMutValue root() const noexcept;

    /// Makes @p value the root.
    void setRoot(JsonMutValue value) noexcept;

    /// True once a builder function ran out of memory or received an absent handle.
    [[nodiscard]] bool failed() const noexcept;

    /// Writes the root value, see JsonMutValue::write(). Fails with "out of memory" if failed().
    Expected<void, JsonError> write(std::string& out, const JsonWriteOptions& options = {}) const;

private:
    detail::JsonMutState* m_state = nullptr;
};

} // namespace illusion
