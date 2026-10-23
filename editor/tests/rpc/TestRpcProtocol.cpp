#include "rpc/RpcTestSupport.hpp"

#include <illusion/editor/rpc/RpcProtocol.hpp>

#include <random>

using namespace illusion;
using namespace illusion::editor::rpc;

namespace
{

std::vector<std::string> frame(LineFramer& framer, std::string_view bytes, bool* ok = nullptr)
{
    std::vector<std::string> lines;
    const bool fed = framer.feed(bytes, [&](std::string_view line) { lines.emplace_back(line); });
    if (ok != nullptr)
    {
        *ok = fed;
    }
    return lines;
}

i64 errorCode(const std::string& response)
{
    const JsonDocument doc = test::parseJson(response);
    return doc.root().find("error").find("code").asI64().value_or(0);
}

} // namespace

TEST_CASE("the framer joins split reads and splits several messages per read")
{
    LineFramer framer;
    CHECK(frame(framer, R"({"a":)").empty());
    CHECK(framer.pending() == 5);
    CHECK(frame(framer, "1}\n{\"b\":2}\r\n\n\n{\"c\"") ==
          std::vector<std::string>{R"({"a":1})", R"({"b":2})"});
    CHECK(frame(framer, ":3}\n") == std::vector<std::string>{R"({"c":3})"});
    CHECK(framer.pending() == 0);
    CHECK_FALSE(framer.overflowed());
}

TEST_CASE("the framer rejects a line over the limit, also when it arrives in pieces")
{
    LineFramer whole(8);
    bool ok = true;
    CHECK(frame(whole, "12345678\n", &ok).size() == 1);
    CHECK(ok);
    CHECK(frame(whole, "123456789\n", &ok).empty());
    CHECK_FALSE(ok);
    CHECK(whole.overflowed());
    CHECK(frame(whole, "1\n", &ok).empty());
    CHECK_FALSE(ok);

    LineFramer pieces(8);
    CHECK(frame(pieces, "12345", &ok).empty());
    CHECK(ok);
    CHECK(frame(pieces, "6789", &ok).empty());
    CHECK_FALSE(ok);
}

TEST_CASE("parseRequest accepts requests with string, numeric and null ids and notifications")
{
    Expected<Request, std::string> numeric =
        parseRequest(R"({"jsonrpc":"2.0","id":7,"method":"scene.info"})");
    REQUIRE(numeric.hasValue());
    CHECK(numeric->id == "7");
    CHECK(numeric->method == "scene.info");
    CHECK(numeric->params.isAbsent());

    Expected<Request, std::string> text =
        parseRequest(R"({"jsonrpc":"2.0","id":"c-1","method":"entity.get","params":{"entity":"/A"}})");
    REQUIRE(text.hasValue());
    CHECK(text->id == R"("c-1")");
    CHECK(text->params.find("entity").asString() == "/A");

    Expected<Request, std::string> null = parseRequest(R"({"jsonrpc":"2.0","id":null,"method":"x"})");
    REQUIRE(null.hasValue());
    CHECK(null->id == "null");
    CHECK_FALSE(null->notification());

    Expected<Request, std::string> notification = parseRequest(R"({"jsonrpc":"2.0","method":"x"})");
    REQUIRE(notification.hasValue());
    CHECK(notification->notification());
}

TEST_CASE("parseRequest answers malformed messages with the JSON-RPC error codes")
{
    CHECK(errorCode(parseRequest("{nope").error()) == -32700);
    CHECK(errorCode(parseRequest("\"\xff\xfe\"").error()) == -32700);
    // Invalid UTF-8 inside a string.
    CHECK(errorCode(parseRequest("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"a\xc3\"}").error()) == -32700);
    CHECK(errorCode(parseRequest("5").error()) == -32600);
    CHECK(errorCode(parseRequest(R"({"jsonrpc":"1.0","id":1,"method":"x"})").error()) == -32600);
    CHECK(errorCode(parseRequest(R"({"jsonrpc":"2.0","id":1})").error()) == -32600);
    CHECK(errorCode(parseRequest(R"({"jsonrpc":"2.0","id":1,"method":""})").error()) == -32600);
    CHECK(errorCode(parseRequest(R"({"jsonrpc":"2.0","id":[1],"method":"x"})").error()) == -32600);
    CHECK(errorCode(parseRequest(R"({"jsonrpc":"2.0","id":1,"method":"x","params":[1]})").error()) == -32602);

    const std::string batch = parseRequest(R"([{"jsonrpc":"2.0","id":1,"method":"x"}])").error();
    CHECK(errorCode(batch) == -32600);
    CHECK(batch.find("editor.batch") != std::string::npos);

    // The error carries the request's id when it could be read.
    const JsonDocument withId =
        test::parseJson(parseRequest(R"({"jsonrpc":"1.0","id":"q","method":"x"})").error());
    CHECK(withId.root().find("id").asString() == "q");
    const JsonDocument parse = test::parseJson(parseRequest("{").error());
    CHECK(parse.root().find("id").isNull());
}

TEST_CASE("invalid notifications get no reply at all")
{
    CHECK(parseRequest(R"({"jsonrpc":"1.0","method":"x"})").error().empty());
    CHECK(parseRequest(R"({"jsonrpc":"2.0","method":"x","params":5})").error().empty());
    CHECK(parseRequest(R"({"jsonrpc":"2.0"})").error().empty());
}

TEST_CASE("responses are compact JSON-RPC objects")
{
    CHECK(resultResponse("3", R"({"a":1})") == R"({"jsonrpc":"2.0","id":3,"result":{"a":1}})");
    CHECK(resultResponse("", "{}") == R"({"jsonrpc":"2.0","id":null,"result":{}})");
    CHECK(errorResponse(R"("x")", -32601, "no \"such\" method") ==
          R"({"jsonrpc":"2.0","id":"x","error":{"code":-32601,"message":"no \"such\" method"}})");
    const JsonDocument command = test::parseJson(
        errorResponse("1", editor::CommandError::make(editor::ErrorKind::NotFound, "missing")));
    CHECK(command.root().find("error").find("code").asI64() == 1200);
    CHECK(command.root().find("error").find("data").find("kind").asString() == "not_found");
    CHECK(notification("event", R"({"topic":"t"})") ==
          R"({"jsonrpc":"2.0","method":"event","params":{"topic":"t"}})");
}

TEST_CASE("constantTimeEqual compares tokens")
{
    CHECK(constantTimeEqual("abc", "abc"));
    CHECK_FALSE(constantTimeEqual("abc", "abd"));
    CHECK_FALSE(constantTimeEqual("abc", "abcd"));
    CHECK(constantTimeEqual("", ""));
}

TEST_CASE("parseRequest survives 100k mutated messages and every error it returns is valid JSON")
{
    const std::vector<std::string> seeds = {
        R"({"jsonrpc":"2.0","id":7,"method":"entity.create","params":{"name":"Board","components":{"illusion.Transform":{"position":[4.5,9.5,0]}},"clientToken":"c1"}})",
        R"({"jsonrpc":"2.0","method":"rpc.cancel","params":{"id":"x"}})",
        R"([{"jsonrpc":"2.0","id":"a","method":"scene.info"}])",
    };
    std::mt19937 random(1234);
    usize requests = 0;
    for (usize i = 0; i < 100000; ++i)
    {
        std::string text = seeds[i % seeds.size()];
        const usize edits = 1 + random() % 4;
        for (usize e = 0; e < edits && !text.empty(); ++e)
        {
            const usize at = random() % text.size();
            switch (random() % 4)
            {
            case 0:
                text[at] = static_cast<char>(random() % 256);
                break;
            case 1:
                text.erase(at, 1 + random() % 8);
                break;
            case 2:
                text.insert(at, 1, "{}[]\":,\\0123e-"[random() % 14]);
                break;
            default:
                text.resize(at);
                break;
            }
        }
        Expected<Request, std::string> parsed = parseRequest(text);
        if (parsed)
        {
            ++requests;
            CHECK_FALSE(parsed->method.empty());
        }
        else if (!parsed.error().empty())
        {
            const Expected<JsonDocument, JsonError> error = JsonDocument::parse(parsed.error());
            REQUIRE(error.hasValue());
            CHECK(error->root().find("error").find("code").isInteger());
        }
    }
    CHECK(requests > 0);
}
