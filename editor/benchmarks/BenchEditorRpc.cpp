#include "Budget.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/editor/core/EditorContext.hpp>
#include <illusion/editor/rpc/RpcProtocol.hpp>
#include <illusion/editor/rpc/RpcServer.hpp>
#include <illusion/editor/rpc/RpcStream.hpp>

#include <string>
#include <vector>

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::editor::rpc;

// Requests through the whole server on an in-memory connection, 64 pipelined at a time, per request: framing
// and parsing on the I/O thread, the queue, the command (selection.get) on this thread, the reply through the
// writer thread, and reading it back. Budget about 9x the 1.1 us measured on a desktop (Release, MSVC); see
// Budget.hpp.
ILL_BENCHMARK("rpc/roundtrip", 10'000.0)
{
    constexpr usize kPipelined = 64;
    EditorContext editor(EditorContextOptions{.seed = 1, .crashJournal = false});
    RpcServer server(editor);
    auto [serverEnd, clientEnd] = makeStreamPair();
    const u32 connection = server.addConnection(std::move(serverEnd), true, "bench");
    ILL_CHECK(connection != 0, "cannot connect the benchmark client");
    std::string requests;
    for (usize i = 0; i < kPipelined; ++i)
    {
        requests += R"({"jsonrpc":"2.0","id":)" + std::to_string(i + 1) +
                    R"(,"method":"selection.get","params":{}})" + "\n";
    }
    std::vector<char> buffer(64 * 1024);
    LineFramer framer;
    bench.batch(kPipelined).unit("request").run("selection.get over an in-memory connection", [&] {
        clientEnd->write(requests);
        usize replies = 0;
        usize ran = 0;
        while (replies < kPipelined)
        {
            if (ran < kPipelined)
            {
                server.waitForWork(std::chrono::milliseconds(100));
                ran += server.pump();
            }
            usize count = 0;
            if (clientEnd->read(buffer, count, std::chrono::milliseconds(ran < kPipelined ? 0 : 1000)) ==
                ReadStatus::Data)
            {
                framer.feed(std::string_view(buffer.data(), count), [&](std::string_view) { ++replies; });
            }
        }
    });
}

// Splitting a stream of 64 KiB reads into lines of typical request size, per byte. Budget about 12x the
// 0.04 ns measured on a desktop.
ILL_BENCHMARK("rpc/framing", 0.5)
{
    std::string stream;
    const std::string line =
        R"({"jsonrpc":"2.0","id":7,"method":"component.set","params":{"entity":"/Board","type":"illusion.Transform","value":{"position":[1,2,3]}}})"
        "\n";
    while (stream.size() < 64 * 1024)
    {
        stream += line;
    }
    LineFramer framer;
    usize lines = 0;
    bench.batch(stream.size()).unit("byte").run("NDJSON framing", [&] {
        framer.feed(stream, [&](std::string_view) { ++lines; });
    });
    ankerl::nanobench::doNotOptimizeAway(lines);
}
