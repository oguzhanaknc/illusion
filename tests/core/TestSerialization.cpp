#include <illusion/core/Hash.hpp>
#include <illusion/core/Serialization.hpp>

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace illusion;

namespace
{

struct Sample
{
    u32 a;
    f32 b;
    u64 c;
};

} // namespace

TEST_CASE("binary writer and reader round-trip values, strings and vectors")
{
    std::vector<std::byte> bytes;
    BinaryWriter writer(bytes);
    writer.write(u8{7});
    writer.write(-12345);
    writer.write(Sample{1, 2.5f, 3});
    writer.writeString("hello, world");
    writer.writeString("");
    const std::vector<u32> numbers = {1, 2, 3, 0xFFFFFFFF};
    writer.writeVector(std::span<const u32>(numbers));
    CHECK(writer.size() == bytes.size());

    BinaryReader reader(bytes);
    u8 small = 0;
    int negative = 0;
    Sample sample{};
    std::string text;
    std::string empty = "x";
    std::vector<u32> readNumbers;
    CHECK(reader.read(small));
    CHECK(reader.read(negative));
    CHECK(reader.read(sample));
    CHECK(reader.readString(text));
    CHECK(reader.readString(empty));
    CHECK(reader.readVector(readNumbers));
    CHECK(reader.ok());
    CHECK(reader.remaining() == 0);
    CHECK(reader.position() == bytes.size());
    CHECK(small == 7);
    CHECK(negative == -12345);
    CHECK(sample.c == 3);
    CHECK(sample.b == 2.5f);
    CHECK(text == "hello, world");
    CHECK(empty.empty());
    CHECK(readNumbers == numbers);
}

TEST_CASE("binary reader failures are sticky and never read out of bounds")
{
    std::vector<std::byte> bytes;
    BinaryWriter writer(bytes);
    writer.write(u32{5});
    writer.writeString("abcdef");

    BinaryReader reader{std::span(bytes).first(6)}; // the string is truncated
    u32 value = 0;
    std::string text;
    CHECK(reader.read(value));
    CHECK_FALSE(reader.readString(text));
    CHECK_FALSE(reader.ok());
    CHECK_FALSE(reader.read(value)); // stays failed even though 2 bytes remain
    CHECK(reader.view(1).empty());

    // A corrupted element count must not allocate gigabytes.
    std::vector<std::byte> huge;
    BinaryWriter hugeWriter(huge);
    hugeWriter.write(u64{1} << 40);
    BinaryReader hugeReader(huge);
    std::vector<u64> out;
    CHECK_FALSE(hugeReader.readVector(out));
    CHECK(out.empty());
}

TEST_CASE("byte hashing matches string hashing and can be continued")
{
    const std::string text = "illusion engine";
    const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
    CHECK(fnv1a64(bytes) == fnv1a64(std::string_view(text)));
    const u64 first = fnv1a64(bytes.first(5));
    CHECK(fnv1a64(bytes.subspan(5), first) == fnv1a64(bytes));
    CHECK(fnv1a64(std::span<const std::byte>{}) == 0xcbf29ce484222325ull);
}
