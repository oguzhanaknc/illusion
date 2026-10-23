#include <illusion/editor/rpc/RpcProtocol.hpp>

namespace illusion::editor::rpc
{
namespace
{

std::string compact(const JsonMutDocument& doc)
{
    std::string text;
    if (!doc.write(text, JsonWriteOptions{.pretty = false}))
    {
        return "null";
    }
    return text;
}

std::string jsonString(std::string_view text)
{
    JsonMutDocument doc;
    doc.setRoot(doc.makeString(text));
    return compact(doc);
}

} // namespace

Expected<Request, std::string> parseRequest(std::string_view line)
{
    Expected<JsonDocument, JsonError> parsed =
        JsonDocument::parse(line, JsonReadOptions{.maxBytes = kMaxMessageSize});
    if (!parsed)
    {
        return unexpected(errorResponse({}, static_cast<i32>(ProtocolError::ParseError),
                                        "parse error: " + parsed.error().message));
    }
    Request request;
    request.document = std::move(parsed).value();
    const JsonValue root = request.document.root();
    if (root.isArray())
    {
        return unexpected(errorResponse({}, static_cast<i32>(ProtocolError::InvalidRequest),
                                        "batch arrays are not supported; send one request per line or use "
                                        "editor.batch"));
    }
    if (!root.isObject())
    {
        return unexpected(errorResponse({}, static_cast<i32>(ProtocolError::InvalidRequest),
                                        "a request must be a JSON object"));
    }
    const JsonValue id = root.find("id");
    if (!id.isAbsent())
    {
        if (!id.isString() && !id.isNumber() && !id.isNull())
        {
            return unexpected(errorResponse({}, static_cast<i32>(ProtocolError::InvalidRequest),
                                            "id must be a string, a number or null"));
        }
        request.id = id.dump();
    }
    // Notifications never get a reply, not even an error.
    const auto fail = [&](ProtocolError code, std::string_view message) -> Expected<Request, std::string> {
        if (request.notification())
        {
            return unexpected(std::string());
        }
        return unexpected(errorResponse(request.id, static_cast<i32>(code), message));
    };
    if (root.find("jsonrpc").asString() != std::optional<std::string_view>("2.0"))
    {
        return fail(ProtocolError::InvalidRequest, "jsonrpc must be \"2.0\"");
    }
    const std::optional<std::string_view> method = root.find("method").asString();
    if (!method || method->empty())
    {
        return fail(ProtocolError::InvalidRequest, "method must be a non-empty string");
    }
    request.method = *method;
    request.params = root.find("params");
    if (!request.params.isAbsent() && !request.params.isObject())
    {
        if (request.notification())
        {
            return unexpected(std::string());
        }
        return unexpected(errorResponse(
            request.id, static_cast<i32>(ProtocolError::InvalidParams),
            "params must be an object (named parameters)",
            R"({"kind":"invalid_params","errors":[{"path":"","message":"must be an object"}],"suggestions":[],"retryable":false})"));
    }
    return request;
}

std::string resultResponse(std::string_view id, std::string_view result)
{
    std::string out = R"({"jsonrpc":"2.0","id":)";
    out += id.empty() ? std::string_view("null") : id;
    out += R"(,"result":)";
    out += result;
    out += '}';
    return out;
}

std::string errorResponse(std::string_view id, i32 code, std::string_view message, std::string_view data)
{
    std::string out = R"({"jsonrpc":"2.0","id":)";
    out += id.empty() ? std::string_view("null") : id;
    out += R"(,"error":{"code":)";
    out += std::to_string(code);
    out += R"(,"message":)";
    out += jsonString(message);
    if (!data.empty())
    {
        out += R"(,"data":)";
        out += data;
    }
    out += "}}";
    return out;
}

std::string errorResponse(std::string_view id, const CommandError& error)
{
    JsonMutDocument doc;
    doc.setRoot(writeCommandError(doc, error));
    std::string out = R"({"jsonrpc":"2.0","id":)";
    out += id.empty() ? std::string_view("null") : id;
    out += R"(,"error":)";
    out += compact(doc);
    out += '}';
    return out;
}

std::string notification(std::string_view method, std::string_view params)
{
    std::string out = R"({"jsonrpc":"2.0","method":)";
    out += jsonString(method);
    out += R"(,"params":)";
    out += params;
    out += '}';
    return out;
}

bool constantTimeEqual(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size())
    {
        return false;
    }
    unsigned char difference = 0;
    for (usize i = 0; i < a.size(); ++i)
    {
        difference |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return difference == 0;
}

} // namespace illusion::editor::rpc
