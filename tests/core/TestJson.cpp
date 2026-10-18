#include "TestSupport.hpp"

#include <illusion/core/Json.hpp>
#include <illusion/core/Memory.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

using namespace illusion;
using namespace std::string_literals;
using namespace std::string_view_literals;

static_assert(std::ranges::forward_range<JsonValue::ElementRange>);
static_assert(std::ranges::forward_range<JsonValue::MemberRange>);
static_assert(std::is_nothrow_move_constructible_v<JsonDocument>);
static_assert(std::is_nothrow_move_constructible_v<JsonMutDocument>);
static_assert(!std::is_copy_constructible_v<JsonDocument>);
static_assert(sizeof(JsonValue) == sizeof(void*));

namespace
{

JsonDocument parseOrFail(std::string_view text, const JsonReadOptions& options = {})
{
    auto parsed = JsonDocument::parse(text, options);
    INFO(text);
    if (!parsed)
    {
        FAIL_CHECK(parsed.error().message);
        return {};
    }
    return std::move(parsed).value();
}

JsonError parseError(std::string_view text, const JsonReadOptions& options = {})
{
    auto parsed = JsonDocument::parse(text, options);
    INFO(text);
    REQUIRE_FALSE(parsed.hasValue());
    return std::move(parsed).error();
}

std::string written(const JsonValue& value, const JsonWriteOptions& options = {})
{
    std::string out;
    const auto result = value.write(out, options);
    REQUIRE(result.hasValue());
    return out;
}

std::string written(const JsonMutDocument& doc, const JsonWriteOptions& options = {})
{
    std::string out;
    const auto result = doc.write(out, options);
    INFO((result ? std::string() : result.error().message));
    REQUIRE(result.hasValue());
    return out;
}

std::string nested(char open, char close, usize depth)
{
    return std::string(depth, open) + std::string(depth, close);
}

} // namespace

// --- Reading ---------------------------------------------------------------------------------------------

TEST_CASE("JSON values report their types and typed contents")
{
    const JsonDocument doc = parseOrFail(
        R"({"n": null, "t": true, "f": false, "i": -12, "u": 7, "r": 2.5, "s": "text", "a": [], "o": {}})");
    const JsonValue root = doc.root();
    REQUIRE(root.isObject());
    CHECK(root.size() == 9);

    CHECK(root.find("n").type() == JsonType::Null);
    CHECK(root.find("n").isNull());
    CHECK(root.find("t").asBool() == true);
    CHECK(root.find("f").asBool() == false);
    CHECK(root.find("i").type() == JsonType::Integer);
    CHECK(root.find("i").asI64() == -12);
    CHECK(root.find("u").asU64() == 7u);
    CHECK(root.find("u").asI64() == 7);
    CHECK(root.find("r").type() == JsonType::Real);
    CHECK(root.find("r").isNumber());
    CHECK(root.find("r").asF64() == 2.5);
    CHECK(root.find("s").asString() == "text"sv);
    CHECK(root.find("a").isArray());
    CHECK(root.find("o").isObject());

    // Mismatched accessors return nullopt.
    CHECK_FALSE(root.find("s").asI64().has_value());
    CHECK_FALSE(root.find("i").asString().has_value());
    CHECK_FALSE(root.find("r").asI64().has_value());
    CHECK_FALSE(root.find("t").asF64().has_value());
    CHECK_FALSE(root.find("n").asBool().has_value());
    CHECK_FALSE(root.find("i").asU64().has_value()); // negative
    CHECK(root.find("u").asF64() == 7.0);            // integers convert
    CHECK(root.find("s").size() == 0);               // not a container
}

TEST_CASE("JSON integers keep their exact 64-bit value")
{
    const JsonDocument doc = parseOrFail("[9007199254740993, 18446744073709551615, -9223372036854775808, "
                                         "9223372036854775807, 9223372036854775808, "
                                         "18446744073709551616]");
    const JsonValue root = doc.root();
    CHECK(root[0].asU64() == 9007199254740993ull); // 2^53 + 1 is not representable as f64
    CHECK(root[0].asI64() == 9007199254740993ll);
    CHECK(root[1].asU64() == std::numeric_limits<u64>::max());
    CHECK_FALSE(root[1].asI64().has_value());
    CHECK(root[2].asI64() == std::numeric_limits<i64>::min());
    CHECK_FALSE(root[2].asU64().has_value());
    CHECK(root[3].asI64() == std::numeric_limits<i64>::max());
    CHECK_FALSE(root[4].asI64().has_value());
    CHECK(root[4].asU64() == 9223372036854775808ull);
    // Beyond u64 a number is no longer an integer.
    CHECK(root[5].type() == JsonType::Real);
    CHECK(root[5].asF64() == 18446744073709551616.0);

    CHECK(written(root, {.pretty = false}) ==
          "[9007199254740993,18446744073709551615,-9223372036854775808,9223372036854775807,"
          "9223372036854775808,18446744073709552000.0]");
}

TEST_CASE("JSON strings are decoded and may contain null characters")
{
    const JsonDocument doc = parseOrFail(R"(["a\u0000b", "ç😀", "tab\there", "\"\\\/"])");
    const JsonValue root = doc.root();
    CHECK(root[0].asString() == std::string_view("a\0b", 3));
    CHECK(root[1].asString() == "\xC3\xA7\xF0\x9F\x98\x80"sv);
    CHECK(root[2].asString() == "tab\there"sv);
    CHECK(root[3].asString() == "\"\\/"sv);
}

TEST_CASE("missing JSON values are absent and chain safely")
{
    const JsonDocument doc = parseOrFail(R"({"list": [1, 2], "child": {"x": 1}})");
    const JsonValue root = doc.root();
    CHECK(root.find("nothing").isAbsent());
    CHECK(root.find("nothing").type() == JsonType::Absent);
    CHECK(root.find("list")[2].isAbsent());
    CHECK(root.find("nothing").find("deeper")[3].isAbsent());
    CHECK(root[0].isAbsent()); // an object is not an array
    CHECK(root.find("list").find("x").isAbsent());
    CHECK(root.find("nothing").size() == 0);
    CHECK_FALSE(root.find("nothing").asI64().has_value());
    CHECK(JsonValue().isAbsent());
    CHECK(JsonDocument().root().isAbsent());
    CHECK(JsonDocument().empty());
    CHECK(root.find("nothing").elements().empty());
    CHECK(root.find("list").members().empty());
}

TEST_CASE("JSON arrays and objects iterate in file order")
{
    const JsonDocument doc = parseOrFail(R"({"z": [3, [4, 5], {"k": 6}, 7], "a": 1, "m": {"y": 2, "b": 3}})");
    const JsonValue root = doc.root();

    std::vector<std::string_view> keys;
    for (const JsonMember member : root.members())
    {
        keys.push_back(member.key);
    }
    CHECK(keys == std::vector<std::string_view>{"z", "a", "m"});

    const JsonValue list = root.find("z");
    CHECK(list.elements().size() == 4);
    std::vector<JsonType> types;
    for (const JsonValue element : list.elements())
    {
        types.push_back(element.type());
    }
    CHECK(types ==
          std::vector<JsonType>{JsonType::Integer, JsonType::Array, JsonType::Object, JsonType::Integer});
    CHECK(list[3].asI64() == 7);
    CHECK(list[1][1].asI64() == 5);
    CHECK(list[2].find("k").asI64() == 6);

    auto it = root.find("m").members().begin();
    CHECK((*it).key == "y");
    const auto previous = it++;
    CHECK((*previous).key == "y");
    CHECK((*it).key == "b");
    CHECK((*it).value.asI64() == 3);
    ++it;
    CHECK(it == root.find("m").members().end());

    // Views stay valid when the document is moved.
    JsonDocument owner = parseOrFail("[1, 2, 3]");
    const JsonValue second = owner.root()[1];
    JsonDocument moved = std::move(owner);
    CHECK(second.asI64() == 2);
    CHECK(moved.root()[1] == second);
    CHECK(owner.empty());
}

TEST_CASE("JSON Pointer resolves the RFC 6901 examples")
{
    const JsonDocument doc = parseOrFail(R"({
        "foo": ["bar", "baz"], "": 0, "a/b": 1, "c%d": 2, "e^f": 3, "g|h": 4, "i\\j": 5, "k\"l": 6, " ": 7, "m~n": 8
    })");
    const JsonValue root = doc.root();
    CHECK(root.pointer("") == root);
    CHECK(root.pointer("/foo") == root.find("foo"));
    CHECK(root.pointer("/foo/0").asString() == "bar"sv);
    CHECK(root.pointer("/foo/1").asString() == "baz"sv);
    CHECK(root.pointer("/").asI64() == 0);
    CHECK(root.pointer("/a~1b").asI64() == 1);
    CHECK(root.pointer("/c%d").asI64() == 2);
    CHECK(root.pointer("/e^f").asI64() == 3);
    CHECK(root.pointer("/g|h").asI64() == 4);
    CHECK(root.pointer("/i\\j").asI64() == 5);
    CHECK(root.pointer("/k\"l").asI64() == 6);
    CHECK(root.pointer("/ ").asI64() == 7);
    CHECK(root.pointer("/m~0n").asI64() == 8);
}

TEST_CASE("malformed or unresolvable JSON Pointers are absent")
{
    const JsonDocument doc = parseOrFail(R"({"foo": ["bar", {"x": [10, 20]}], "~1": 1, "01": 2})");
    const JsonValue root = doc.root();
    CHECK(root.pointer("/foo/1/x/1").asI64() == 20);
    CHECK(root.pointer("/~01").asI64() == 1); // "~01" is "~1", not "/"
    CHECK(root.pointer("/01").asI64() == 2);  // leading zeros are fine for object keys
    for (const std::string_view pointer :
         {"foo"sv, "/foo/-"sv, "/foo/01"sv, "/foo/2"sv, "/foo/-1"sv, "/foo/1a"sv, "/foo/0/x"sv, "/missing"sv,
          "/m~2n"sv, "/m~"sv, "/foo/"sv, "/foo/99999999999999999999999"sv, "//"sv})
    {
        INFO(pointer);
        CHECK(root.pointer(pointer).isAbsent());
    }
    CHECK(JsonValue().pointer("").isAbsent());
}

TEST_CASE("JSON parsing rejects invalid documents")
{
    // Truncation, syntax that is not standard JSON, NaN and Infinity, bad numbers, bad strings and trailing
    // content.
    const std::string_view invalid[] = {
        ""sv,
        "   "sv,
        "[1, 2"sv,
        "{\"a\": "sv,
        "{\"a\" 1}"sv,
        "\"unterminated"sv,
        "tru"sv,
        "[1,]"sv,
        "{\"a\": 1,}"sv,
        "/* comment */ 1"sv,
        "1 // comment"sv,
        "{'a': 1}"sv,
        "{a: 1}"sv,
        "NaN"sv,
        "[nan]"sv,
        "Infinity"sv,
        "[-Infinity]"sv,
        "[inf]"sv,
        "1e999"sv,
        "01"sv,
        "+1"sv,
        ".5"sv,
        "1."sv,
        "0x10"sv,
        "\"bad \\x escape\""sv,
        "\"control \x01 character\""sv,
        "\"lone \\ud800 surrogate\""sv,
        "1 2"sv,
        "{} x"sv,
        "[] []"sv,
        "\xEF\xBB\xBF{}"sv, // byte order mark
    };
    for (const std::string_view text : invalid)
    {
        const JsonError error = parseError(text);
        CHECK_FALSE(error.message.empty());
        CHECK(error.line >= 1);
        CHECK(error.column >= 1);
        CHECK(error.offset <= text.size());
    }
}

TEST_CASE("JSON parsing rejects invalid UTF-8")
{
    const std::string_view invalid[] = {
        "\"\xFF\""sv,             // never valid
        "\"\xC0\xAF\""sv,         // overlong '/'
        "\"\xC3\""sv,             // truncated sequence
        "\"\xED\xA0\x80\""sv,     // UTF-16 surrogate encoded in UTF-8
        "\"\xF4\x90\x80\x80\""sv, // above U+10FFFF
        "\"\x80\""sv,             // lone continuation byte
    };
    for (const std::string_view text : invalid)
    {
        const JsonError error = parseError(text);
        CHECK(error.offset >= 1);
    }
    CHECK(parseOrFail("\"\xF4\x8F\xBF\xBF\"").root().asString()->size() == 4); // U+10FFFF is fine
}

TEST_CASE("JSON parse errors report the line and column of the problem")
{
    const JsonError error = parseError("{\n  \"a\": 1,\n  \"b\": ?\n}");
    CHECK(error.line == 3);
    CHECK(error.column == 8);
    CHECK(error.offset == 19);

    const JsonError truncated = parseError("[1,\n2");
    CHECK(truncated.line == 2);
}

TEST_CASE("JSON parsing enforces the depth limit without recursion")
{
    CHECK(parseOrFail(nested('[', ']', 128)).root().isArray());
    const JsonError tooDeep = parseError(nested('[', ']', 129));
    CHECK(tooDeep.offset == 128); // the bracket that opens level 129
    CHECK(tooDeep.line == 1);
    CHECK(tooDeep.column == 129);
    CHECK(tooDeep.message == "nesting is deeper than 128 levels");

    // A depth far beyond any call stack is rejected cleanly.
    CHECK(parseError(nested('[', ']', 100000)).offset == 128);

    JsonReadOptions shallow;
    shallow.maxDepth = 2;
    CHECK(parseOrFail(R"({"a": [1]})", shallow).root().isObject());
    CHECK(parseError(R"({"a": [{}]})", shallow).offset == 7);
    CHECK(parseError(R"({"a": {"b": {}}})", shallow).offset == 12);
    shallow.maxDepth = 0;
    CHECK(parseOrFail("42", shallow).root().asI64() == 42);
    CHECK(parseError("[]", shallow).offset == 0);
    shallow.maxDepth = 200;
    CHECK(parseOrFail(nested('[', ']', 200), shallow).root().isArray());
    CHECK(parseError(nested('[', ']', 201), shallow).offset == 200);
}

TEST_CASE("the JSON depth limit ignores brackets in strings")
{
    JsonReadOptions options;
    options.maxDepth = 2;
    const JsonDocument doc = parseOrFail(R"([{"[[[{": "]]]}}{{{", "\"[[[": "\\[[["}])", options);
    CHECK(doc.root()[0].find("[[[{").asString() == "]]]}}{{{"sv);
    CHECK(doc.root()[0].find("\"[[[").asString() == "\\[[["sv);
}

TEST_CASE("the JSON depth limit is checked before the document is built")
{
    // A megabyte of brackets in a small arena fails on depth, not on memory: no DOM was built.
    LinearAllocator arena(64 * 1024);
    const auto deep = JsonDocument::parse(std::string(1024 * 1024, '['), arena);
    REQUIRE_FALSE(deep.hasValue());
    CHECK(deep.error().message == "nesting is deeper than 128 levels");
    CHECK(deep.error().offset == 128);
    CHECK(arena.used() == 0);

    // Even when the text before the bracket does not fit the arena.
    LinearAllocator tiny(64);
    const auto wide = JsonDocument::parse("[" + std::string(4096, ' ') + nested('[', ']', 200) + "]", tiny);
    REQUIRE_FALSE(wide.hasValue());
    CHECK(wide.error().message == "nesting is deeper than 128 levels");
}

TEST_CASE("a JSON syntax error before the depth limit is reported first")
{
    JsonReadOptions options;
    options.maxDepth = 3;
    const JsonError syntax = parseError("[1 2, [[[[]]]]]", options);
    CHECK(syntax.offset == 3);
    CHECK(syntax.message != "nesting is deeper than 3 levels");

    // An error after the bracket that breaks the limit comes second.
    const JsonError depth = parseError("[[[[ x ]]]]", options);
    CHECK(depth.offset == 3);
    CHECK(depth.message == "nesting is deeper than 3 levels");

    // With depth 0, a complete scalar before the bracket is fine up to that point.
    options.maxDepth = 0;
    CHECK(parseError("1 []", options).offset == 2);
    CHECK(parseError("  []", options).offset == 2);
}

TEST_CASE("JSON parsing enforces the size limit")
{
    JsonReadOptions small;
    small.maxBytes = 8;
    CHECK(parseOrFail("[1, 2]", small).root().size() == 2);
    const JsonError error = parseError("[1, 2, 3, 4]", small);
    CHECK(error.offset == 8);
    CHECK(error.message == "the document is larger than 8 bytes");

    // The limit itself is allowed.
    small.maxBytes = 6;
    CHECK(parseOrFail("[1, 2]", small).root().size() == 2);
    CHECK(parseError("[1, 22]", small).offset == 6);
}

TEST_CASE("duplicate JSON keys are rejected at the position of the repeated key")
{
    const JsonError error = parseError("{\n  \"a\": 1,\n  \"b\": 2,\n  \"a\": 3\n}");
    CHECK(error.message == "duplicate key \"a\"");
    CHECK(error.line == 4);
    CHECK(error.column == 3);

    // In a large object (sorted check), in nested objects, and the earliest duplicate wins.
    std::string large = "{";
    for (int i = 0; i < 100; ++i)
    {
        large += "\"key" + std::to_string(i) + "\": " + std::to_string(i) + ", ";
    }
    large += "\"key42\": 0, \"key7\": 0}";
    const JsonError largeError = parseError(large);
    CHECK(largeError.message == "duplicate key \"key42\"");
    CHECK(largeError.offset == large.find("\"key42\": 0"));

    const std::string inner = R"({"x": {"k": 1, "k": 2}, "x": 1})";
    CHECK(parseError(inner).offset == inner.find("\"k\": 2"));
    const std::string outer = R"({"a": 1, "a": {"k": 1, "k": 2}})";
    CHECK(parseError(outer).offset == outer.find("\"a\": {"));
    const std::string inArray = R"([{"q": 1}, [{"r": 1, "s": [], "r": 2}]])";
    CHECK(parseError(inArray).offset == inArray.find("\"r\": 2"));
    CHECK(parseError(R"({"a": 1, "a": 2})").message == "duplicate key \"a\""); // compared decoded

    // Keys may repeat in different objects.
    CHECK(parseOrFail(R"([{"a": 1}, {"a": 2}, {"b": {"a": 3}}])").root().size() == 3);
}

TEST_CASE("duplicate JSON key messages escape and shorten the key")
{
    CHECK(parseError(R"({"q\"\\\n\u0001": 1, "q\"\\\n\u0001": 2})").message ==
          R"(duplicate key "q\"\\\n\u0001")");

    const std::string key(100, 'k');
    const JsonError error = parseError("{\"" + key + "\": 1, \"" + key + "\": 2}");
    CHECK(error.message == "duplicate key \"" + std::string(64, 'k') + "...\"");
}

TEST_CASE("duplicate JSON keys can be allowed explicitly")
{
    JsonReadOptions options;
    options.allowDuplicateKeys = true;
    const JsonDocument doc = parseOrFail(R"({"a": 1, "a": 2})", options);
    CHECK(doc.root().size() == 2);
    CHECK(doc.root().find("a").asI64() == 1);
    CHECK(doc.root().pointer("/a").asI64() == 1);
}

TEST_CASE("JSON documents parse into a caller arena")
{
    LinearAllocator arena(64 * 1024);
    std::string text = "{";
    for (int i = 0; i < 40; ++i)
    {
        text += "\"k" + std::to_string(i) + "\": [" + std::to_string(i) + ", \"text\"], ";
    }
    text += "\"end\": true}";
    {
        auto parsed = JsonDocument::parse(text, arena);
        REQUIRE(parsed.hasValue());
        CHECK(arena.used() > text.size());
        CHECK(parsed->root().find("k39")[0].asI64() == 39);
        CHECK(parsed->root().find("end").asBool() == true);
    }
    // Duplicate detection of large objects also takes its scratch memory from the arena.
    arena.reset();
    const std::string duplicate = text.substr(0, text.size() - 1) + ", \"k3\": 0}";
    CHECK(JsonDocument::parse(duplicate, arena).error().message == "duplicate key \"k3\"");

    LinearAllocator tiny(64);
    const auto failed = JsonDocument::parse(text, tiny);
    REQUIRE_FALSE(failed.hasValue());
    CHECK(failed.error().message == "out of memory");
}

TEST_CASE("JSON documents parse into an arena without the heap")
{
    // A large object (sorted duplicate check) and nesting deeper than any inline stack.
    std::string text = "{";
    for (int i = 0; i < 40; ++i)
    {
        text += "\"k" + std::to_string(i) + "\": " + std::to_string(i) + ", ";
    }
    text += "\"deep\": " + nested('[', ']', 300) + "}";
    JsonReadOptions options;
    options.maxDepth = 400;
    LinearAllocator arena(256 * 1024);
    for (int round = 0; round < 3; ++round)
    {
        arena.reset();
        test::ScopedAllocationCounter allocations;
        {
            auto parsed = JsonDocument::parse(text, arena, options);
            REQUIRE(parsed.hasValue());
            CHECK(parsed->root().find("k39").asI64() == 39);
        }
        CHECK(allocations.count() == 0);
    }
}

TEST_CASE("arena-backed JSON documents can be destroyed after the arena is reset")
{
    LinearAllocator arena(64 * 1024);
    auto parsed = JsonDocument::parse(R"({"a": [1, 2, 3]})", arena);
    REQUIRE(parsed.hasValue());
    JsonDocument doc = std::move(parsed).value();
    JsonDocument assigned = JsonDocument::parse("[4]", arena).value();
    JsonMutDocument built(arena);
    built.setRoot(built.makeArray());
    JsonMutDocument builtAssigned(arena);

    // Reuse every byte of the arena, so that reading the old documents would find garbage.
    arena.reset();
    void* all = arena.allocate(arena.capacity(), 1);
    REQUIRE(all != nullptr);
    std::memset(all, 0xCD, arena.capacity());

    // Assigning and destroying must not touch the arena.
    assigned = JsonDocument();
    builtAssigned = JsonMutDocument();
    CHECK(assigned.empty());
    CHECK(builtAssigned.root().isAbsent());
}

// --- Writing ---------------------------------------------------------------------------------------------

TEST_CASE("compact JSON output has no whitespace")
{
    const JsonDocument doc =
        parseOrFail(R"( { "a" : 1 , "b" : [ true , null , { } , [ ] ] , "c" : "x y" } )");
    CHECK(written(doc.root(), {.pretty = false}) == R"({"a":1,"b":[true,null,{},[]],"c":"x y"})");
    CHECK(doc.root().dump() == R"({"a":1,"b":[true,null,{},[]],"c":"x y"})");
    CHECK(doc.root().find("b").dump() == "[true,null,{},[]]");
    CHECK(JsonValue().dump().empty());
}

TEST_CASE("pretty JSON output is canonical")
{
    const JsonDocument doc = parseOrFail(
        R"({"name": "box", "size": [1.0, 2.5, -0.0], "empty": [], "none": {}, "rows": [[1, 2], [3]], "mixed": [1, {"a": null}], "flag": false})");
    CHECK(written(doc.root()) == R"({
  "name": "box",
  "size": [1.0, 2.5, -0.0],
  "empty": [],
  "none": {},
  "rows": [
    [1, 2],
    [3]
  ],
  "mixed": [
    1,
    {
      "a": null
    }
  ],
  "flag": false
}
)");
    CHECK(written(parseOrFail("[[]]").root()) == "[\n  []\n]\n");
    CHECK(written(parseOrFail("7").root()) == "7\n");
    CHECK(written(parseOrFail("\"s\"").root()) == "\"s\"\n");
}

TEST_CASE("JSON numbers are written in their shortest round-trip form")
{
    // yyjson's format: fixed notation up to 1e21, exponents otherwise; integers without a fraction.
    const JsonDocument doc =
        parseOrFail("[0.1, 1e-7, 1e30, 123456789012345680000, 1e21, 3.0, -2.5e-300, 0, -0, 1E2]");
    const std::string text = written(doc.root(), {.pretty = false});
    CHECK(text == "[0.1,1e-7,1e30,123456789012345680000.0,1e21,3.0,-2.5e-300,0,0,100.0]");
    const JsonDocument again = parseOrFail(text);
    for (usize i = 0; i < doc.root().size(); ++i)
    {
        CHECK(again.root()[i].asF64() == doc.root()[i].asF64());
    }
}

TEST_CASE("JSON strings are escaped like yyjson escapes them")
{
    JsonMutDocument doc;
    doc.setRoot(doc.makeString("quote \" backslash \\ slash / newline \n tab \t cr \r bs \b ff \f nul "s +
                               '\0' + " bell \x07 del \x7F unicode \xC3\xA7 \xF0\x9F\x98\x80"));
    CHECK(written(doc, {.pretty = false}) == "\"quote \\\" backslash \\\\ slash / newline \\n tab \\t cr \\r "
                                             "bs \\b ff \\f nul \\u0000 bell \\u0007 "
                                             "del \x7F unicode \xC3\xA7 \xF0\x9F\x98\x80\"");
}

TEST_CASE("sortKeys orders members by key bytes at every level")
{
    const JsonDocument doc =
        parseOrFail(R"({"b": {"z": 1, "a": 2}, "a": [{"y": 1, "x": 2}], "B": 0, "": 1, "aa": 3})");
    CHECK(written(doc.root(), {.pretty = false, .sortKeys = true}) ==
          R"({"":1,"B":0,"a":[{"x":2,"y":1}],"aa":3,"b":{"a":2,"z":1}})");
    CHECK(written(doc.root(), {.pretty = true, .sortKeys = true}) == R"({
  "": 1,
  "B": 0,
  "a": [
    {
      "x": 2,
      "y": 1
    }
  ],
  "aa": 3,
  "b": {
    "a": 2,
    "z": 1
  }
}
)");
    // File order is kept without sortKeys.
    CHECK(written(doc.root(), {.pretty = false}) ==
          R"({"b":{"z":1,"a":2},"a":[{"y":1,"x":2}],"B":0,"":1,"aa":3})");
}

TEST_CASE("sortKeys handles more open members than its inline storage")
{
    // 150 members, the last of which is an object with another 150: 300 sorted members are open at once.
    const auto keyOf = [](int i) {
        std::string key = std::to_string(i);
        return std::string(3 - key.size(), '0') + key;
    };
    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    doc.setRoot(root);
    for (int i = 149; i > 0; --i)
    {
        root.add(keyOf(i), doc.makeI64(i));
    }
    JsonMutValue inner = root.addObject("000");
    for (int i = 149; i >= 0; --i)
    {
        inner.add(keyOf(i), doc.makeI64(-i));
    }

    std::string expected = "{\"000\":{";
    for (int i = 0; i < 150; ++i)
    {
        expected += (i == 0 ? "\"" : ",\"") + keyOf(i) + "\":" + std::to_string(-i);
    }
    expected += "}";
    for (int i = 1; i < 150; ++i)
    {
        expected += ",\"" + keyOf(i) + "\":" + std::to_string(i);
    }
    expected += "}";
    CHECK(written(doc, {.pretty = false, .sortKeys = true}) == expected);
}

TEST_CASE("writing very deep JSON does not recurse")
{
    JsonReadOptions options;
    options.maxDepth = 5000;
    const std::string text = nested('[', ']', 5000);
    const JsonDocument doc = parseOrFail(text, options);
    CHECK(written(doc.root(), {.pretty = false}) == text);
    const std::string pretty = written(doc.root());
    CHECK(pretty.size() > text.size());
}

TEST_CASE("writing JSON reuses the output buffer without allocating")
{
    std::string text = "{\"entities\": [";
    for (int i = 0; i < 200; ++i)
    {
        text += std::string(i == 0 ? "" : ", ") +
                R"({"position": [1.5, 2.25, -3.0], "name": "entity", "id": )" + std::to_string(i) + "}";
    }
    text += "]}";
    const JsonDocument doc = parseOrFail(text);
    std::string out;
    REQUIRE(doc.write(out).hasValue());
    REQUIRE(doc.write(out, {.pretty = true, .sortKeys = true}).hasValue()); // warm up
    {
        test::ScopedAllocationCounter allocations;
        for (int i = 0; i < 3; ++i)
        {
            REQUIRE(doc.write(out).hasValue());
            REQUIRE(doc.write(out, {.pretty = false}).hasValue());
            REQUIRE(doc.write(out, {.pretty = true, .sortKeys = true}).hasValue());
        }
        CHECK(allocations.count() == 0);
    }
}

TEST_CASE("writing an absent JSON value fails and clears the output")
{
    std::string out = "old";
    const auto result = JsonValue().write(out);
    REQUIRE_FALSE(result.hasValue());
    CHECK(out.empty());
    CHECK(result.error().message == "cannot write an absent value");
    CHECK(result.error().line == 0);
}

// --- Building --------------------------------------------------------------------------------------------

TEST_CASE("JSON documents are built from values, arrays and objects")
{
    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    doc.setRoot(root);
    root.add("null", doc.makeNull())
        .add("bool", doc.makeBool(true))
        .add("i64", doc.makeI64(std::numeric_limits<i64>::min()))
        .add("u64", doc.makeU64(std::numeric_limits<u64>::max()))
        .add("f64", doc.makeF64(0.1))
        .add("f32", doc.makeF32(0.1f))
        .add("text", doc.makeString("hello"));
    JsonMutValue list = root.addArray("list");
    list.push(doc.makeI64(1)).push(doc.makeI64(2));
    list.pushObject().add("inner", doc.makeBool(false));
    list.pushArray();
    root.addObject("empty");

    CHECK(root.type() == JsonType::Object);
    CHECK(root.size() == 9);
    CHECK(list.size() == 4);
    CHECK(root.find("list") == list);
    CHECK(root.find("missing").isAbsent());
    CHECK(doc.root() == root);
    CHECK_FALSE(doc.failed());
    CHECK(written(doc) == R"({
  "null": null,
  "bool": true,
  "i64": -9223372036854775808,
  "u64": 18446744073709551615,
  "f64": 0.1,
  "f32": 0.1,
  "text": "hello",
  "list": [
    1,
    2,
    {
      "inner": false
    },
    []
  ],
  "empty": {}
}
)");
}

TEST_CASE("JSON builder strings and keys are copied")
{
    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    doc.setRoot(root);
    {
        std::string key = "temporary key";
        std::string value = "temporary value";
        root.add(key, doc.makeString(value));
        key.assign(key.size(), 'x');
        value.assign(value.size(), 'y');
    }
    CHECK(root.dump() == R"({"temporary key":"temporary value"})");
}

TEST_CASE("empty string views work as JSON strings and keys")
{
    // A default-constructed std::string_view has null data.
    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    doc.setRoot(root);
    root.add(std::string_view(), doc.makeString(std::string_view()));
    CHECK_FALSE(doc.failed());
    CHECK(root.find(std::string_view()) == root.find(""));
    CHECK(root.dump() == R"({"":""})");
    root.set(std::string_view(), doc.makeI64(1));
    CHECK(root.dump() == R"({"":1})");
    CHECK(root.remove(std::string_view()));

    const JsonDocument parsed = parseOrFail(R"({"": 5})");
    CHECK(parsed.root().find(std::string_view()).asI64() == 5);
    CHECK_FALSE(JsonDocument::parse(std::string_view()).hasValue());
}

TEST_CASE("set() replaces members and remove() deletes them")
{
    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    doc.setRoot(root);
    root.set("a", doc.makeI64(1)).set("b", doc.makeI64(2)).set("a", doc.makeI64(3));
    CHECK(root.dump() == R"({"a":3,"b":2})");
    CHECK(root.remove("a"));
    CHECK_FALSE(root.remove("a"));
    CHECK(root.dump() == R"({"b":2})");
}

TEST_CASE("deep copies of parsed values can be edited and written")
{
    const JsonDocument source = parseOrFail(R"({"keep": [1, {"x": "y"}], "drop": true})");
    JsonMutDocument doc;
    JsonMutValue copy = doc.copy(source.root());
    doc.setRoot(copy);
    copy.remove("drop");
    copy.find("keep").push(doc.copy(source.root().find("drop")));
    CHECK(copy.dump() == R"({"keep":[1,{"x":"y"},true]})");
    CHECK(doc.copy(JsonValue()).isAbsent());
}

TEST_CASE("JSON mutable handles stay valid when the document moves")
{
    JsonMutDocument doc;
    JsonMutValue array = doc.makeArray();
    doc.setRoot(array);
    JsonMutDocument moved = std::move(doc);
    array.push(moved.makeI64(5));
    CHECK(written(moved, {.pretty = false}) == "[5]");
    JsonMutDocument assigned;
    assigned = std::move(moved);
    CHECK(written(assigned, {.pretty = false}) == "[5]");
}

TEST_CASE("writing non-finite numbers or invalid UTF-8 fails")
{
    for (const f64 bad : {std::numeric_limits<f64>::infinity(), -std::numeric_limits<f64>::infinity(),
                          std::numeric_limits<f64>::quiet_NaN()})
    {
        JsonMutDocument doc;
        JsonMutValue array = doc.makeArray();
        doc.setRoot(array);
        array.push(doc.makeI64(1)).push(doc.makeF64(bad));
        std::string out = "old";
        const auto result = doc.write(out);
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error().message == "cannot write a non-finite number");
        CHECK(out.empty());
    }
    JsonMutDocument doc;
    JsonMutValue object = doc.makeObject();
    doc.setRoot(object);
    object.add("bad\xFF", doc.makeNull());
    std::string out;
    CHECK(doc.write(out).error().message == "cannot write a string that is not valid UTF-8");
    CHECK(object.dump().empty());
}

TEST_CASE("a JSON document without a root cannot be written")
{
    JsonMutDocument doc;
    std::string out;
    CHECK(doc.write(out).error().message == "the document has no root value");
}

TEST_CASE("an absent JSON handle passed to a builder marks the document as failed")
{
    const auto failsWith = [](auto&& build) {
        JsonMutDocument doc;
        JsonMutValue array = doc.makeArray();
        JsonMutValue object = doc.makeObject();
        doc.setRoot(object);
        object.add("list", array);
        CHECK_FALSE(doc.failed());
        build(doc, array, object);
        CHECK(doc.failed());
        std::string out;
        CHECK(doc.write(out).error().message.find("incomplete") != std::string::npos);
    };
    failsWith([](JsonMutDocument&, JsonMutValue& array, JsonMutValue&) { array.push(JsonMutValue()); });
    failsWith([](JsonMutDocument&, JsonMutValue&, JsonMutValue& object) { object.add("x", JsonMutValue()); });
    failsWith([](JsonMutDocument&, JsonMutValue&, JsonMutValue& object) { object.set("x", JsonMutValue()); });
    failsWith([](JsonMutDocument& doc, JsonMutValue&, JsonMutValue&) { doc.setRoot(JsonMutValue()); });
    failsWith(
        [](JsonMutDocument& doc, JsonMutValue&, JsonMutValue&) { JsonMutValue().push(doc.makeNull()); });
}

TEST_CASE("arena-backed JSON documents fail softly when the arena is full")
{
    LinearAllocator arena(16 * 1024);
    JsonMutDocument doc(arena);
    JsonMutValue array = doc.makeArray();
    doc.setRoot(array);
    for (int i = 0; i < 100; ++i)
    {
        array.push(doc.makeString("small"));
    }
    CHECK_FALSE(doc.failed());
    CHECK(arena.used() > 0);
    CHECK(array.size() == 100);

    for (int i = 0; i < 10000 && !doc.failed(); ++i)
    {
        array.push(doc.makeString(std::string(64, 'x')));
    }
    CHECK(doc.failed());
    std::string out;
    const auto result = doc.write(out);
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().message.find("incomplete") != std::string::npos);
    CHECK_FALSE(array.write(out).hasValue());

    LinearAllocator none(8);
    JsonMutDocument broken(none);
    CHECK(broken.failed());
    CHECK(broken.makeObject().isAbsent());
}

TEST_CASE("arena-backed JSON documents build and write without the heap")
{
    LinearAllocator arena(256 * 1024);
    std::string out;
    out.reserve(64 * 1024);
    for (int round = 0; round < 3; ++round)
    {
        arena.reset();
        test::ScopedAllocationCounter allocations;
        {
            JsonMutDocument doc(arena);
            JsonMutValue root = doc.makeObject();
            doc.setRoot(root);
            JsonMutValue entities = root.addArray("entities");
            for (int i = 0; i < 100; ++i)
            {
                JsonMutValue entity = entities.pushObject();
                entity.add("id", doc.makeI64(i)).add("name", doc.makeString("crate"));
                entity.addArray("position")
                    .push(doc.makeF32(1.5f))
                    .push(doc.makeF32(-2.0f))
                    .push(doc.makeF32(0.25f));
            }
            REQUIRE(doc.write(out).hasValue());
        }
        CHECK(allocations.count() == 0);
    }
    CHECK(out.find("\"position\": [1.5, -2.0, 0.25]") != std::string::npos);
}

#if ILL_ASSERTS_ENABLED
TEST_CASE("JSON builder calls on the wrong kind of value assert")
{
    test::ScopedAssertCatcher catcher;
    JsonMutDocument doc;
    JsonMutValue array = doc.makeArray();
    JsonMutValue object = doc.makeObject();
    CHECK_THROWS_AS(array.add("key", doc.makeNull()), test::AssertionFailure);
    CHECK_THROWS_AS(object.push(doc.makeNull()), test::AssertionFailure);
    CHECK_THROWS_AS((void)array.remove("key"), test::AssertionFailure);
}

TEST_CASE("JSON values from another document assert")
{
    test::ScopedAssertCatcher catcher;
    JsonMutDocument doc;
    JsonMutValue array = doc.makeArray();
    JsonMutValue object = doc.makeObject();
    JsonMutDocument other;
    CHECK_THROWS_AS(array.push(other.makeNull()), test::AssertionFailure);
    CHECK_THROWS_AS(object.add("key", other.makeNull()), test::AssertionFailure);
    CHECK_THROWS_AS(object.set("key", other.makeNull()), test::AssertionFailure);
    CHECK_THROWS_AS(doc.setRoot(other.makeArray()), test::AssertionFailure);
    CHECK(object.size() == 0);
    CHECK(array.size() == 0);
}
#endif

TEST_CASE("JSON type names are lowercase words")
{
    CHECK(jsonTypeName(JsonType::Absent) == "absent");
    CHECK(jsonTypeName(JsonType::Object) == "object");
    CHECK(jsonTypeName(JsonType::Integer) == "integer");
}
