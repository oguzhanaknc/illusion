#include "rpc/RpcTestSupport.hpp"

#include <illusion/editor/client/RpcClient.hpp>
#include <illusion/platform/Process.hpp>

#include <fstream>
#include <thread>

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::editor::rpc;

namespace
{

// A socket path short enough for sun_path, unique per test.
std::filesystem::path socketIn(const test::TempDirectory& directory)
{
    return defaultSocketPath(directory.path / "project");
}

} // namespace

TEST_CASE("in-memory stream pairs carry bytes both ways and report closing")
{
    auto [a, b] = makeStreamPair();
    REQUIRE(a->write("hello"));
    std::vector<char> buffer(16);
    usize count = 0;
    REQUIRE(b->read(buffer, count, std::chrono::milliseconds(1000)) == ReadStatus::Data);
    CHECK(std::string_view(buffer.data(), count) == "hello");
    CHECK(b->read(buffer, count, std::chrono::milliseconds(1)) == ReadStatus::Timeout);
    REQUIRE(b->write("back"));
    a->close();
    // What was written before the close is still delivered to the other end.
    CHECK(b->read(buffer, count, std::chrono::milliseconds(1000)) == ReadStatus::Closed);
    CHECK(a->read(buffer, count, std::chrono::milliseconds(1000)) == ReadStatus::Closed);
    CHECK_FALSE(b->write("late"));
}

TEST_CASE("defaultSocketPath stays in the project when it fits and hashes long paths elsewhere")
{
    const std::filesystem::path shortRoot = utf8Path("/p");
    CHECK(defaultSocketPath(shortRoot) == shortRoot / ".illusion" / "run" / "editor.sock");
    const std::filesystem::path longRoot = utf8Path("/" + std::string(200, 'x'));
    const std::filesystem::path hashed = defaultSocketPath(longRoot);
    const std::u8string text = hashed.u8string();
    CHECK(text.size() <= kMaxSocketPath);
    CHECK(hashed.filename().u8string().starts_with(u8"illusion-"));
    CHECK(defaultSocketPath(longRoot) == hashed);
}

TEST_CASE("a socket listener accepts clients that exchange bytes")
{
    test::TempDirectory directory;
    const std::filesystem::path path = socketIn(directory);
    std::filesystem::create_directories(path.parent_path());
    Expected<SocketListener, std::string> listener = SocketListener::listen(path);
    REQUIRE_MESSAGE(listener.hasValue(), (listener ? std::string() : listener.error()));
    CHECK(listener->path() == path);
    CHECK(listener->accept(std::chrono::milliseconds(1)) == nullptr);

    Expected<std::unique_ptr<RpcStream>, std::string> client = connectSocket(path);
    REQUIRE_MESSAGE(client.hasValue(), (client ? std::string() : client.error()));
    std::unique_ptr<RpcStream> server = listener->accept(test::kRpcPatience);
    REQUIRE(server != nullptr);
    REQUIRE((*client)->write("ping\n"));
    std::vector<char> buffer(16);
    usize count = 0;
    REQUIRE(server->read(buffer, count, test::kRpcPatience) == ReadStatus::Data);
    CHECK(std::string_view(buffer.data(), count) == "ping\n");
    (*client)->close();
    CHECK(server->read(buffer, count, test::kRpcPatience) == ReadStatus::Closed);

    // close() from another thread ends a blocked accept().
    std::thread closer([&] { listener->close(); });
    CHECK(listener->accept(std::chrono::milliseconds::max()) == nullptr);
    closer.join();

    CHECK_FALSE(connectSocket(directory.path / "missing.sock").hasValue());
    CHECK_FALSE(SocketListener::listen(utf8Path("/" + std::string(200, 'x'))).hasValue());
}

TEST_CASE("the run directory and the endpoint file are restricted to the current user")
{
    test::TempDirectory directory;
    const std::filesystem::path file = endpointFilePath(directory.path);
    EndpointInfo info{currentProcessId(), std::string(kProtocolVersion), "1.2.3", "unix", "/tmp/x.sock",
                      makeToken()};
    REQUIRE(writeEndpointFile(file, info).hasValue());
    CHECK(isOwnerOnly(file.parent_path()));
    CHECK(isOwnerOnly(file));
    Expected<EndpointInfo, std::string> read = readEndpointFile(file);
    REQUIRE_MESSAGE(read.hasValue(), (read ? std::string() : read.error()));
    CHECK(read->pid == info.pid);
    CHECK(read->token == info.token);
    CHECK(read->address == "/tmp/x.sock");

    // Writing again replaces the file atomically.
    info.address = "/tmp/y.sock";
    REQUIRE(writeEndpointFile(file, info).hasValue());
    CHECK(readEndpointFile(file)->address == "/tmp/y.sock");

    // In an existing directory (next to a socket in /tmp, say) only the file is restricted.
    const std::filesystem::path beside = directory.path / "editor.sock.endpoint.json";
    REQUIRE(writeEndpointFile(beside, info).hasValue());
    CHECK(isOwnerOnly(beside));
    CHECK_FALSE(isOwnerOnly(directory.path));

    const std::filesystem::path open = directory.path / "open.txt";
    std::ofstream(open) << "x";
    CHECK_FALSE(isOwnerOnly(open));
    CHECK(restrictToOwner(open));
    CHECK(isOwnerOnly(open));
}

TEST_CASE("an endpoint file of a finished editor is reported as stale")
{
    test::TempDirectory directory;
    ProcessOptions options;
    options.executable = utf8Path(ILLUSION_EDITOR_EXECUTABLE);
    options.arguments = {"--help-is-not-an-option"};
    Expected<Process, std::string> process = Process::spawn(options);
    REQUIRE(process.hasValue());
    const u64 pid = process->id();
    process->wait(test::kRpcPatience);
    *process = Process();
    const std::filesystem::path file = endpointFilePath(directory.path);
    REQUIRE(writeEndpointFile(file, EndpointInfo{pid, "0.1.0", "x", "unix", "/tmp/s", "t"}).hasValue());
    Expected<EndpointInfo, std::string> read = readEndpointFile(file);
    REQUIRE_FALSE(read.hasValue());
    CHECK(read.error().find("stale") != std::string::npos);
    CHECK_FALSE(readEndpointFile(directory.path / "missing.json").hasValue());
}

TEST_CASE("tokens are 64 hex digits and differ")
{
    const std::string a = makeToken();
    const std::string b = makeToken();
    CHECK(a.size() == 64);
    CHECK(a.find_first_not_of("0123456789abcdef") == std::string::npos);
    CHECK(a != b);
}

TEST_CASE("a socket client authenticates with the server's token and runs commands")
{
    test::TempDirectory directory;
    test::RpcHarness rpc;
    const std::filesystem::path path = socketIn(directory);
    std::filesystem::create_directories(path.parent_path());
    Expected<SocketListener, std::string> listener = SocketListener::listen(path);
    REQUIRE(listener.hasValue());
    rpc.server->listen(std::move(listener).value());

    // The client blocks in call(); the test thread runs the server's queue meanwhile.
    std::atomic<bool> done{false};
    std::string result;
    std::string failure;
    std::thread caller([&] {
        Expected<client::RpcClient, std::string> connection =
            client::RpcClient::connect(path, rpc.server->token(), "socket-test");
        if (!connection)
        {
            failure = connection.error();
        }
        else if (Expected<std::string, client::RpcError> info = connection->call("scene.info"))
        {
            result = *info;
        }
        else
        {
            failure = info.error().message;
        }
        Expected<client::RpcClient, std::string> wrong = client::RpcClient::connect(path, "bad", "x");
        if (wrong)
        {
            failure = "a wrong token was accepted";
        }
        done.store(true);
    });
    while (!done.load())
    {
        rpc.server->waitForWork(std::chrono::milliseconds(5));
        rpc.server->pump();
    }
    caller.join();
    CHECK(failure.empty());
    CHECK(test::parseJson(result).root().find("entities").isInteger());
}
