#pragma once

// Helpers of the MCP tests: an in-memory transport and a fake ToolProvider with a small, fixed catalogue.

#include <illusion/core/Json.hpp>
#include <illusion/editor/mcp/McpServer.hpp>

#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::test
{

// Messages to the server are queued up front; poll() hands them out, so a running call sees the ones after
// it.
class MemoryTransport final : public editor::mcp::McpTransport
{
public:
    void send(std::string_view line) override { sent.emplace_back(line); }

    std::optional<std::string> poll() override
    {
        if (inbox.empty())
        {
            return std::nullopt;
        }
        std::string line = std::move(inbox.front());
        inbox.pop_front();
        return line;
    }

    std::deque<std::string> inbox;
    std::vector<std::string> sent;
};

// An OpenRPC document like the editor's rpc.discover, with ten commands and one server method.
inline constexpr std::string_view kFakeOpenRpc =
    R"({"openrpc":"1.3.2","info":{"title":"fake","version":"0.2.0"},"methods":[
{"name":"editor.batch","description":"Runs commands in order as one undo step. Example: {\"steps\": []}.","params":[{"name":"steps","required":true,"schema":{"type":"array","items":{"type":"object"}}},{"name":"label","required":false,"schema":{"type":"string"}}],"result":{"name":"result","schema":{"type":"object","properties":{"results":{"type":"array"}}}},"x-illusion":{"version":1,"category":"Editor","permission":"edit","mode":"any","flags":{"readOnly":false,"destructive":false,"idempotent":false,"undoable":false,"async":false}}},
{"name":"editor.list_commands","description":"Lists every command. Example: {}.","params":[],"result":{"name":"result","schema":{"type":"object","properties":{"commands":{"type":"array"}}}},"x-illusion":{"version":1,"category":"Editor","permission":"read","mode":"any","flags":{"readOnly":true,"destructive":false,"idempotent":true,"undoable":false,"async":false}}},
{"name":"editor.wait","description":"Asynchronous: answers after the given number of frames. Example: {\"frames\": 60}.","params":[{"name":"frames","required":true,"schema":{"type":"integer","minimum":1,"maximum":100000}}],"result":{"name":"result","schema":{"type":"object","properties":{"frames":{"type":"integer"}}}},"x-illusion":{"version":1,"category":"Editor","permission":"read","mode":"any","flags":{"readOnly":true,"destructive":false,"idempotent":true,"undoable":false,"async":true}}},
{"name":"entity.create","description":"Creates an entity. Example: {\"name\": \"Board\"}.","params":[{"name":"name","required":false,"schema":{"anyOf":[{"type":"string"},{"type":"null"}]}},{"name":"parent","required":false,"schema":{"type":"string"}},{"name":"dryRun","required":false,"schema":{"type":"boolean"}}],"result":{"name":"result","schema":{"type":"object","properties":{"entity":{"type":"object"}}}},"x-illusion":{"version":1,"category":"Entity","permission":"edit","mode":"any","flags":{"readOnly":false,"destructive":false,"idempotent":false,"undoable":true,"async":false}}},
{"name":"entity.get","description":"Returns an entity. Example: {\"entity\": \"name:Board\"}.","params":[{"name":"entity","required":true,"schema":{"type":"string"}}],"result":{"name":"result","schema":{"type":"object","properties":{"entity":{"type":"object"}}}},"x-illusion":{"version":1,"category":"Entity","permission":"read","mode":"any","flags":{"readOnly":true,"destructive":false,"idempotent":true,"undoable":false,"async":false}}},
{"name":"scene.info","description":"Returns the active scene's state. Example: {}.","params":[],"result":{"name":"result","schema":{"type":"object","properties":{"entities":{"type":"integer"}}}},"x-illusion":{"version":1,"category":"Scene","permission":"read","mode":"any","flags":{"readOnly":true,"destructive":false,"idempotent":true,"undoable":false,"async":false}}},
{"name":"scene.tree","description":"Returns the entity tree. Example: {}.","params":[],"result":{"name":"result","schema":{"type":"object","properties":{"roots":{"type":"array"}}}},"x-illusion":{"version":1,"category":"Scene","permission":"read","mode":"any","flags":{"readOnly":true,"destructive":false,"idempotent":true,"undoable":false,"async":false}}},
{"name":"project.info","description":"Returns the open project. Example: {}.","params":[],"result":{"name":"result","schema":{"type":"object","properties":{"name":{"type":"string"}}}},"x-illusion":{"version":1,"category":"Project","permission":"read","mode":"any","flags":{"readOnly":true,"destructive":false,"idempotent":true,"undoable":false,"async":false}}},
{"name":"component.schema","description":"Returns the JSON Schema of a Component's value. Example: {\"type\": \"illusion.Transform\"}.","params":[{"name":"type","required":true,"schema":{"type":"string"}}],"result":{"name":"result","schema":{"type":"object"}},"x-illusion":{"version":1,"category":"Component","permission":"read","mode":"any","flags":{"readOnly":true,"destructive":false,"idempotent":true,"undoable":false,"async":false}}},
{"name":"log.read","description":"Returns log messages. Example: {\"limit\": 50}.","params":[{"name":"limit","required":false,"schema":{"type":"integer","minimum":1}}],"result":{"name":"result","schema":{"type":"object","properties":{"messages":{"type":"array"}}}},"x-illusion":{"version":1,"category":"Log","permission":"read","mode":"any","flags":{"readOnly":true,"destructive":false,"idempotent":true,"undoable":false,"async":false}}},
{"name":"editor.quit","description":"Ends the editor.","params":[],"result":{"name":"result","schema":{"type":"object"}},"x-illusion":{"permission":"system","server":true}}]})";

// Answers the commands of kFakeOpenRpc without an editor.
class FakeProvider final : public editor::mcp::ToolProvider
{
public:
    FakeProvider()
    {
        Expected<editor::mcp::Catalog, std::string> parsed = editor::mcp::catalogFromOpenRpc(kFakeOpenRpc);
        if (parsed)
        {
            catalogValue = std::move(parsed).value();
        }
    }

    const editor::mcp::Catalog& catalog() override { return catalogValue; }

    u64 catalogRevision() const noexcept override { return revision; }

    editor::mcp::CallResult call(std::string_view method, std::string_view params,
                                 editor::mcp::CallControl& control) override
    {
        calls.emplace_back(method);
        const Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(params);
        const JsonValue args = parsed ? parsed->root() : JsonValue();
        if (method == "editor.wait")
        {
            const u64 frames = args.find("frames").asU64().value_or(1);
            for (u64 frame = 1; frame <= frames; ++frame)
            {
                control.service();
                if (control.cancelled())
                {
                    return {
                        false,
                        R"({"code":1004,"message":"editor.wait was cancelled","data":{"kind":"cancelled"}})"};
                }
                control.progress(static_cast<f64>(frame), static_cast<f64>(frames),
                                 "frame " + std::to_string(frame));
            }
            return {true, R"({"frames":)" + std::to_string(frames) + "}"};
        }
        if (method == "entity.create")
        {
            const JsonValue name = args.find("name");
            if (!name.isAbsent() && !name.isNull() && !name.isString())
            {
                return {
                    false,
                    R"({"code":1202,"message":"'/name' must be a string","data":{"kind":"validation_failed","errors":[{"path":"/name","message":"must be a string"}],"suggestions":[],"retryable":false}})"};
            }
            const std::string text(name.asString().value_or("Entity"));
            return {
                true,
                R"({"entity":{"id":"0000000000000001","name":")" + text + R"(","path":"/)" + text +
                    R"("},"_editor":{"revision":2,"transaction":1,"dryRun":false,"operations":1,"warnings":[]}})"};
        }
        if (method == "entity.get")
        {
            if (args.find("entity").asString() == "name:Board")
            {
                return {
                    true,
                    R"({"entity":{"id":"0000000000000001","name":"Board","path":"/Board"},"components":{}})"};
            }
            return {
                false,
                R"({"code":1200,"message":"entity 'name:Nope' does not exist","data":{"kind":"not_found","errors":[{"path":"/entity","message":"does not exist"}],"suggestions":["name:Board"],"retryable":false}})"};
        }
        if (method == "scene.info")
        {
            return {true, R"({"path":"","entities":1})"};
        }
        if (method == "scene.tree")
        {
            return {true, R"({"roots":[]})"};
        }
        if (method == "project.info")
        {
            return {true, R"({"name":"Fake","root":"/fake"})"};
        }
        if (method == "component.schema")
        {
            if (args.find("type").asString() == "illusion.Transform")
            {
                return {true, R"({"type":"illusion.Transform","schema":{"type":"object"}})"};
            }
            return {
                false,
                R"({"code":1200,"message":"there is no Component type 'illusion.Nope'","data":{"kind":"not_found","errors":[],"suggestions":[],"retryable":false}})"};
        }
        if (method == "log.read")
        {
            return {true, R"({"messages":[]})"};
        }
        if (method == "editor.list_commands")
        {
            return {true, R"({"commands":[]})"};
        }
        if (method == "editor.batch")
        {
            return {true, R"({"results":[]})"};
        }
        if (method == "big.result")
        {
            return {true, result};
        }
        return {false, R"({"code":-32601,"message":"method not found: )" + std::string(method) +
                           R"(","data":{"suggestions":["scene.info"]}})"};
    }

    editor::mcp::Catalog catalogValue;
    u64 revision = 1;
    std::vector<std::string> calls;
    std::string result; // what big.result returns
};

} // namespace illusion::test
