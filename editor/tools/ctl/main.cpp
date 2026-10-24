// illusion_ctl: command-line client of the editor protocol (ADR 0037) and runner of transcripts (ADR 0038).
//
//     illusion_ctl <connection> call <method> [<params JSON>]
//     illusion_ctl <connection> batch <requests.jsonl> [--keep-going]
//     illusion_ctl <connection> subscribe [<topic>...] [--count <n>]
//     illusion_ctl <connection> dump-catalog
//     illusion_ctl --launch <editor> [--editor-arg <arg>]... replay <transcript.jsonl> [--checks
//     <checks.jsonl>]
//                  [--var <name>=<JSON>]... [--gpu] [--temp-workspace] [--keep]
//                  [--via-mcp <illusion_mcp> [--era legacy|modern]]
//     illusion_ctl --launch <editor> [--editor-arg <arg>]... check <checks.jsonl> [...]
//
// --via-mcp runs every step as an MCP tool call through the bridge, which launches the editor (ADR 0039).
//
// <connection> is one of --endpoint <file>, --project <dir> (its endpoint file), --socket <path> --token <t>,
// or --launch <editor> [--editor-arg <arg>]... (starts a headless editor over stdio).
// Exit codes: 0 success, 1 a call or check failed, 2 usage error, 3 no connection.

#include <illusion/core/Json.hpp>
#include <illusion/editor/client/McpClient.hpp>
#include <illusion/editor/client/RpcClient.hpp>
#include <illusion/editor/client/Transcript.hpp>
#include <illusion/editor/rpc/RpcStream.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using namespace illusion;
using namespace illusion::editor;

namespace
{

constexpr std::string_view kUsage =
    "usage: illusion_ctl <connection> call <method> [<params JSON>]\n"
    "       illusion_ctl <connection> batch <requests.jsonl> [--keep-going]\n"
    "       illusion_ctl <connection> subscribe [<topic>...] [--count <n>]\n"
    "       illusion_ctl <connection> dump-catalog\n"
    "       illusion_ctl --launch <editor> [--editor-arg <arg>]... replay <transcript.jsonl>\n"
    "                    [--checks <checks.jsonl>] [--var <name>=<JSON>]... [--gpu] [--temp-workspace] "
    "[--keep]\n"
    "                    [--via-mcp <illusion_mcp> [--era legacy|modern]]\n"
    "       illusion_ctl --launch <editor> [--editor-arg <arg>]... check <checks.jsonl> [same options]\n"
    "connection: --endpoint <file> | --project <dir> | --socket <path> --token <token>\n"
    "            | --launch <editor> [--editor-arg <arg>]...   (also: --client <name>)\n";

void print(FILE* stream, std::string_view text)
{
    std::fwrite(text.data(), 1, text.size(), stream);
    std::fputc('\n', stream);
    std::fflush(stream);
}

int usage(std::string_view message)
{
    std::fprintf(stderr, "illusion_ctl: %.*s\n%.*s", static_cast<int>(message.size()), message.data(),
                 static_cast<int>(kUsage.size()), kUsage.data());
    return 2;
}

std::string pretty(std::string_view json)
{
    Expected<JsonDocument, JsonError> doc = JsonDocument::parse(json);
    std::string out;
    if (!doc || !doc->write(out))
    {
        return std::string(json);
    }
    return out;
}

// C stdio instead of iostreams: GCC 13 reports a false -Wnull-dereference inside <streambuf> at -O3.
std::optional<std::string> readFile(const std::filesystem::path& path)
{
#if defined(_WIN32)
    std::FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0)
    {
        file = nullptr;
    }
#else
    std::FILE* file = std::fopen(path.c_str(), "rb");
#endif
    if (file == nullptr)
    {
        return std::nullopt;
    }
    std::string text;
    char buffer[16 * 1024];
    usize count = 0;
    while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        text.append(buffer, count);
    }
    const bool failed = std::ferror(file) != 0;
    std::fclose(file);
    return failed ? std::nullopt : std::optional<std::string>(std::move(text));
}

struct Options
{
    std::filesystem::path endpoint;
    std::filesystem::path socket;
    std::string token;
    std::filesystem::path editor;
    std::vector<std::string> editorArguments;
    std::string client = "illusion_ctl";
    std::vector<std::string> command; // the command and its arguments
};

// A temporary directory removed on destruction unless kept.
struct TempWorkspace
{
    std::filesystem::path path;
    bool keep = false;

    TempWorkspace()
    {
        std::error_code code;
        const auto unique = std::random_device{}();
        path = std::filesystem::temp_directory_path(code) /
               utf8Path("illusion-acceptance-" + std::to_string(unique));
        std::filesystem::remove_all(path, code);
        std::filesystem::create_directories(path, code);
    }
    ~TempWorkspace()
    {
        if (!keep)
        {
            std::error_code code;
            std::filesystem::remove_all(path, code);
        }
    }
    TempWorkspace(const TempWorkspace&) = delete;
    TempWorkspace& operator=(const TempWorkspace&) = delete;
};

std::string pathJson(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    JsonMutDocument doc;
    doc.setRoot(doc.makeString(std::string_view(reinterpret_cast<const char*>(text.data()), text.size())));
    std::string out;
    (void)doc.write(out, JsonWriteOptions{.pretty = false});
    return out;
}

Expected<client::RpcClient, std::string> connect(const Options& options)
{
    if (!options.editor.empty())
    {
        return client::RpcClient::launch(client::LaunchOptions{options.editor, options.editorArguments});
    }
    if (!options.socket.empty())
    {
        return client::RpcClient::connect(options.socket, options.token, options.client);
    }
    if (!options.endpoint.empty())
    {
        return client::RpcClient::connectEndpoint(options.endpoint, options.client);
    }
    return unexpected(std::string("no connection: pass --endpoint, --project, --socket or --launch"));
}

int printCall(client::RpcClient& connection, std::string_view method, std::string_view params)
{
    Expected<std::string, client::RpcError> result = connection.call(method, params);
    if (!result)
    {
        print(stderr, result.error().json.empty() ? result.error().message : pretty(result.error().json));
        return 1;
    }
    print(stdout, pretty(*result));
    return 0;
}

int runTranscripts(const Options& options)
{
    const std::vector<std::string>& args = options.command;
    if (options.editor.empty())
    {
        return usage(args[0] + " needs --launch <editor>");
    }
    if (args.size() < 2)
    {
        return usage(args[0] + " needs a file");
    }
    std::vector<std::filesystem::path> files = {utf8Path(args[1])};
    client::RunnerOptions runner;
    runner.launch = client::LaunchOptions{options.editor, options.editorArguments};
    bool tempWorkspace = false;
    bool keep = false;
    std::filesystem::path bridge;
    bool modern = false;
    for (usize i = 2; i < args.size(); ++i)
    {
        const std::string_view arg = args[i];
        if (arg == "--via-mcp" && i + 1 < args.size())
        {
            bridge = utf8Path(args[++i]);
        }
        else if (arg == "--era" && i + 1 < args.size())
        {
            const std::string_view era = args[++i];
            if (era != "legacy" && era != "modern")
            {
                return usage("--era takes legacy or modern");
            }
            modern = era == "modern";
        }
        else if (arg == "--checks" && i + 1 < args.size())
        {
            files.push_back(utf8Path(args[++i]));
        }
        else if (arg == "--var" && i + 1 < args.size())
        {
            const std::string& assignment = args[++i];
            const usize equals = assignment.find('=');
            if (equals == std::string::npos || equals == 0)
            {
                return usage("--var takes <name>=<JSON>");
            }
            runner.variables[assignment.substr(0, equals)] = assignment.substr(equals + 1);
        }
        else if (arg == "--gpu")
        {
            runner.gpu = true;
        }
        else if (arg == "--temp-workspace")
        {
            tempWorkspace = true;
        }
        else if (arg == "--keep")
        {
            keep = true;
        }
        else
        {
            return usage("unknown argument '" + std::string(arg) + "'");
        }
    }
    std::optional<TempWorkspace> workspace;
    if (tempWorkspace)
    {
        workspace.emplace();
        workspace->keep = keep;
        runner.launch.arguments.push_back("--workspace");
        const std::u8string text = workspace->path.u8string();
        runner.launch.arguments.emplace_back(reinterpret_cast<const char*>(text.data()), text.size());
        runner.variables["workspace"] = pathJson(workspace->path);
    }
    runner.log = [](std::string_view line) {
        print(stdout, line);
    };
    if (!bridge.empty())
    {
        runner.connect = [bridge, modern](const client::LaunchOptions& launch)
            -> Expected<std::unique_ptr<client::CallChannel>, std::string> {
            client::McpLaunchOptions options;
            options.bridge = bridge;
            options.modern = modern;
            options.workingDirectory = launch.workingDirectory;
            const std::u8string editor = launch.editor.u8string();
            options.arguments = {"--launch", "--editor",
                                 std::string(reinterpret_cast<const char*>(editor.data()), editor.size())};
            for (const std::string& argument : launch.arguments)
            {
                options.arguments.emplace_back("--editor-arg");
                options.arguments.push_back(argument);
            }
            Expected<client::McpClient, std::string> started = client::McpClient::launch(options);
            if (!started)
            {
                return unexpected(std::move(started).error());
            }
            return std::unique_ptr<client::CallChannel>(
                std::make_unique<client::McpClient>(std::move(started).value()));
        };
    }

    client::TranscriptRunner transcripts(std::move(runner));
    bool passed = true;
    for (const std::filesystem::path& file : files)
    {
        const std::optional<std::string> text = readFile(file);
        const std::u8string name = file.filename().u8string();
        const std::string fileName(reinterpret_cast<const char*>(name.data()), name.size());
        if (!text)
        {
            print(stderr, "cannot read " + fileName);
            return 2;
        }
        print(stdout, "== " + fileName);
        const client::RunReport report = transcripts.run(*text, fileName);
        for (const client::StepFailure& failure : report.failures)
        {
            print(stderr, fileName + ":" + std::to_string(failure.line) + ": " +
                              (failure.check.empty() ? std::string() : failure.check + ": ") +
                              failure.description + ": " + failure.reason);
            if (!failure.actual.empty())
            {
                print(stderr, "  reply: " + failure.actual.substr(0, 2000));
            }
        }
        std::string summary = fileName + ": " + std::to_string(report.steps) + " steps, " +
                              std::to_string(report.failures.size()) + " failed";
        if (!report.passedChecks.empty() || !report.failedChecks.empty())
        {
            summary += "; checks passed:";
            for (const std::string& check : report.passedChecks)
            {
                summary += " " + check;
            }
            summary += "; failed:";
            for (const std::string& check : report.failedChecks)
            {
                summary += " " + check;
            }
        }
        print(stdout, summary);
        if (!report.passed())
        {
            passed = false;
            // Checks run even after a failed transcript; a broken transcript leaves nothing worth checking.
            if (args[0] == "replay" && file == files.front())
            {
                break;
            }
        }
    }
    const std::optional<i32> exit = transcripts.stop();
    if (exit && *exit != 0)
    {
        print(stderr, "the editor exited with code " + std::to_string(*exit));
        passed = false;
    }
    if (workspace && (!passed || keep))
    {
        workspace->keep = true;
        const std::u8string text = workspace->path.u8string();
        print(stderr,
              "workspace kept at " + std::string(reinterpret_cast<const char*>(text.data()), text.size()));
    }
    return passed ? 0 : 1;
}

int run(const std::vector<std::string>& args)
{
    Options options;
    usize i = 0;
    for (; i < args.size() && args[i].starts_with("--"); ++i)
    {
        const std::string_view arg = args[i];
        if (i + 1 >= args.size())
        {
            return usage(std::string(arg) + " needs a value");
        }
        const std::string& value = args[++i];
        if (arg == "--endpoint")
        {
            options.endpoint = utf8Path(value);
        }
        else if (arg == "--project")
        {
            options.endpoint = rpc::endpointFilePath(utf8Path(value));
        }
        else if (arg == "--socket")
        {
            options.socket = utf8Path(value);
        }
        else if (arg == "--token")
        {
            options.token = value;
        }
        else if (arg == "--launch")
        {
            options.editor = utf8Path(value);
        }
        else if (arg == "--editor-arg")
        {
            options.editorArguments.push_back(value);
        }
        else if (arg == "--client")
        {
            options.client = value;
        }
        else
        {
            return usage("unknown option '" + std::string(arg) + "'");
        }
    }
    options.command.assign(args.begin() + static_cast<std::ptrdiff_t>(i), args.end());
    if (options.command.empty())
    {
        return usage("no command");
    }
    const std::string& command = options.command[0];
    if (command == "replay" || command == "check")
    {
        return runTranscripts(options);
    }

    Expected<client::RpcClient, std::string> connection = connect(options);
    if (!connection)
    {
        print(stderr, "illusion_ctl: " + connection.error());
        return 3;
    }
    const std::vector<std::string>& rest = options.command;
    if (command == "call")
    {
        if (rest.size() < 2 || rest.size() > 3)
        {
            return usage("call takes <method> [<params JSON>]");
        }
        return printCall(*connection, rest[1], rest.size() == 3 ? std::string_view(rest[2]) : "{}");
    }
    if (command == "dump-catalog")
    {
        return printCall(*connection, "rpc.discover", "{}");
    }
    if (command == "batch")
    {
        if (rest.size() < 2)
        {
            return usage("batch takes <requests.jsonl>");
        }
        const bool keepGoing = rest.size() > 2 && rest[2] == "--keep-going";
        const std::optional<std::string> text = readFile(utf8Path(rest[1]));
        if (!text)
        {
            return usage("cannot read " + rest[1]);
        }
        int status = 0;
        for (usize begin = 0; begin < text->size();)
        {
            usize end = text->find('\n', begin);
            end = end == std::string::npos ? text->size() : end;
            const std::string_view line = std::string_view(*text).substr(begin, end - begin);
            begin = end + 1;
            Expected<JsonDocument, JsonError> request = JsonDocument::parse(line);
            if (!request || !request->root().isObject())
            {
                continue;
            }
            const std::string method(request->root().find("method").asString().value_or(""));
            const JsonValue params = request->root().find("params");
            Expected<std::string, client::RpcError> result =
                connection->call(method, params.isAbsent() ? "{}" : params.dump());
            print(stdout, result                        ? *result
                          : result.error().json.empty() ? result.error().message
                                                        : result.error().json);
            if (!result)
            {
                status = 1;
                if (!keepGoing)
                {
                    break;
                }
            }
        }
        return status;
    }
    if (command == "subscribe")
    {
        u64 count = 0;
        JsonMutDocument doc;
        JsonMutValue params = doc.makeObject();
        JsonMutValue topics = params.addArray("topics");
        for (usize j = 1; j < rest.size(); ++j)
        {
            if (rest[j] == "--count" && j + 1 < rest.size())
            {
                count = std::stoull(rest[++j]);
            }
            else
            {
                topics.push(doc.makeString(rest[j]));
            }
        }
        doc.setRoot(params);
        std::string text;
        (void)doc.write(text, JsonWriteOptions{.pretty = false});
        if (Expected<std::string, client::RpcError> subscribed = connection->call("editor.subscribe", text);
            !subscribed)
        {
            print(stderr, subscribed.error().message);
            return 1;
        }
        for (u64 received = 0; count == 0 || received < count; ++received)
        {
            std::optional<std::string> event = connection->nextNotification(std::chrono::milliseconds::max());
            if (!event)
            {
                break;
            }
            print(stdout, *event);
        }
        return 0;
    }
    return usage("unknown command '" + command + "'");
}

} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv)
{
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
    {
        const std::u8string text = std::filesystem::path(argv[i]).u8string();
        args.emplace_back(reinterpret_cast<const char*>(text.data()), text.size());
    }
    return run(args);
}
#else
int main(int argc, char** argv)
{
    return run(std::vector<std::string>(argv + 1, argv + argc));
}
#endif
