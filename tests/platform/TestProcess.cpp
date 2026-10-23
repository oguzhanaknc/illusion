#include "TestSupport.hpp"

#include <illusion/platform/FileSystem.hpp>
#include <illusion/platform/Process.hpp>

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace illusion;
using namespace std::chrono_literals;

namespace
{

// Bounds every wait so that a broken implementation fails instead of hanging the test run.
constexpr auto kPatience = 60s;

std::filesystem::path helper()
{
    return utf8Path(PROCESS_HELPER);
}

Process start(std::vector<std::string> arguments, ProcessOptions options = {})
{
    options.executable = helper();
    options.arguments = std::move(arguments);
    Expected<Process, std::string> process = Process::spawn(options);
    REQUIRE_MESSAGE(static_cast<bool>(process), (process ? std::string() : process.error()));
    return std::move(process).value();
}

std::vector<std::string> lines(Process& process, ProcessStream stream = ProcessStream::Output)
{
    std::vector<std::string> out;
    std::string line;
    while (process.readLine(stream, line, kPatience))
    {
        out.push_back(line);
    }
    return out;
}

std::string hex(std::string_view text)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    for (const char c : text)
    {
        const auto byte = static_cast<unsigned char>(c);
        out += kDigits[byte >> 4];
        out += kDigits[byte & 15];
    }
    return out;
}

} // namespace

TEST_CASE("windowsCommandLine quotes exactly the arguments that need it")
{
    CHECK(windowsCommandLine({"plain", "two words", ""}) == "plain \"two words\" \"\"");
    CHECK(windowsCommandLine({R"(say "hi")"}) == R"("say \"hi\"")");
    CHECK(windowsCommandLine({R"(C:\dir\)", R"(C:\dir\ x)"}) == R"(C:\dir\ "C:\dir\ x")");
    CHECK(windowsCommandLine({R"(a\\"b)"}) == R"("a\\\\\"b")");
    CHECK(windowsCommandLine({R"(trailing\\)", "tab\there"}) == "trailing\\\\ \"tab\there\"");
}

TEST_CASE("a child process receives its arguments exactly, including spaces, quotes and Unicode")
{
    const std::vector<std::string> arguments = {
        "plain",    "two words",       "",     R"(quote " inside)",      R"(back\slash\)",
        R"(\"\\")", "ünïcødé 漢字 🎮", "a\tb", "--flag=value with space"};
    std::vector<std::string> call = {"args"};
    call.insert(call.end(), arguments.begin(), arguments.end());
    Process process = start(call);
    const std::vector<std::string> printed = lines(process);
    REQUIRE(printed.size() == arguments.size());
    for (usize i = 0; i < arguments.size(); ++i)
    {
        CHECK(printed[i] == hex(arguments[i]));
    }
    CHECK(process.wait(kPatience) == 0);
}

TEST_CASE("exit codes are reported by poll and wait")
{
    Process ok = start({"exit", "0"});
    CHECK(ok.wait(kPatience) == 0);
    CHECK(ok.poll() == 0);

    Process failed = start({"exit", "42"});
    CHECK(failed.wait(kPatience) == 42);
    CHECK_FALSE(failed.terminate());
}

TEST_CASE("wait times out while the child runs, and terminate ends it")
{
    Process process = start({"hang"});
    CHECK(process.id() != 0);
    CHECK_FALSE(process.poll().has_value());
    CHECK_FALSE(process.wait(10ms).has_value());
    CHECK(process.terminate());
    CHECK(process.wait(kPatience) == kTerminatedExitCode);
    CHECK_FALSE(process.terminate());
}

TEST_CASE("destroying a running process terminates it")
{
    Process process = start({"hang"});
    Process moved = std::move(process);
    CHECK_FALSE(process.valid());
    CHECK(moved.valid());
    // Leaving the scope must not hang.
}

TEST_CASE("added environment variables reach the child and replace inherited ones")
{
    ProcessOptions options;
    options.environment = {{"ILLUSION_TEST_VARIABLE", "välue with spaces"}, {"PATH", "replaced"}};
    Process process = start({"env", "ILLUSION_TEST_VARIABLE"}, options);
    CHECK(lines(process) == std::vector<std::string>{"välue with spaces"});
    CHECK(process.wait(kPatience) == 0);

    Process path = start({"env", "PATH"}, options);
    CHECK(lines(path) == std::vector<std::string>{"replaced"});

    Process unset = start({"env", "ILLUSION_TEST_VARIABLE"});
    CHECK(lines(unset) == std::vector<std::string>{"<unset>"});
}

TEST_CASE("the child starts in the given working directory")
{
    test::TempDirectory temp;
    const std::filesystem::path directory = temp.path / utf8Path("çalışma dizini");
    std::filesystem::create_directories(directory);
    ProcessOptions options;
    options.workingDirectory = directory;
    Process process = start({"cwd"}, options);
    const std::vector<std::string> printed = lines(process);
    REQUIRE(printed.size() == 1);
    std::string reported;
    for (usize i = 0; i + 1 < printed[0].size(); i += 2)
    {
        reported += static_cast<char>(std::stoi(printed[0].substr(i, 2), nullptr, 16));
    }
    CHECK(std::filesystem::equivalent(utf8Path(reported), directory));
}

TEST_CASE("stdin is piped to the child and closing it ends the child's input")
{
    ProcessOptions options;
    options.pipeInput = true;
    Process process = start({"cat"}, options);
    CHECK(process.writeInput("first line\nsecond"));
    CHECK(process.writeInput(" line\r\n"));
    std::string line;
    REQUIRE(process.readLine(ProcessStream::Output, line, kPatience));
    CHECK(line == "first line");
    REQUIRE(process.readLine(ProcessStream::Output, line, kPatience));
    CHECK(line == "second line");
    process.closeInput();
    CHECK_FALSE(process.writeInput("after close"));
    CHECK(process.wait(kPatience) == 0);
    CHECK_FALSE(process.readLine(ProcessStream::Output, line, kPatience));
    CHECK(process.drained(ProcessStream::Output));
}

TEST_CASE("large output passes through a small buffer without losing bytes")
{
    ProcessOptions options;
    options.bufferCapacity = 4096;
    constexpr usize kBytes = 3 * 1024 * 1024 + 5;
    Process process = start({"spam", std::to_string(kBytes)}, options);
    std::string output;
    while (!process.drained(ProcessStream::Output))
    {
        std::string line;
        if (process.readLine(ProcessStream::Output, line, kPatience))
        {
            output += line;
            output += '\n';
        }
    }
    // The last line is cut short and has no line break of its own.
    CHECK(output.size() == kBytes + 1);
    CHECK(output.starts_with("abcdefghijklmnopqrstuvwxyz\nabc"));
    CHECK(process.wait(kPatience) == 0);
    CHECK(lines(process, ProcessStream::Error) == std::vector<std::string>{"done"});
}

TEST_CASE("read takes the buffered bytes without blocking")
{
    Process process = start({"spam", "100"});
    CHECK(process.wait(kPatience) == 0);
    std::string output;
    while (!process.drained(ProcessStream::Output))
    {
        std::string line;
        if (process.readLine(ProcessStream::Output, line, kPatience))
        {
            output += line + "\n";
        }
    }
    CHECK(output.size() == 101);
    std::string rest;
    CHECK(process.read(ProcessStream::Output, rest) == 0);
}

TEST_CASE("an uncaptured stream reads as drained")
{
    ProcessOptions options;
    options.captureError = false;
    Process process = start({"exit", "3"}, options);
    CHECK(process.drained(ProcessStream::Error));
    CHECK(process.wait(kPatience) == 3);
}

TEST_CASE("spawning a missing executable fails with a message")
{
    ProcessOptions options;
    options.executable = utf8Path("does/not/exist/program");
    Expected<Process, std::string> process = Process::spawn(options);
    REQUIRE_FALSE(static_cast<bool>(process));
    CHECK(process.error().find("does not exist") != std::string::npos);

    Process empty;
    CHECK_FALSE(empty.valid());
    CHECK_FALSE(empty.poll().has_value());
    CHECK_FALSE(empty.writeInput("x"));
}

TEST_CASE("isProcessRunning sees live processes and not finished ones")
{
    CHECK(isProcessRunning(currentProcessId()));
    Process process = start({"exit", "0"});
    const u64 pid = process.id();
    CHECK(process.wait(kPatience) == 0);
    process = Process();
    CHECK_FALSE(isProcessRunning(pid));
}

TEST_CASE("findExecutable searches PATH and accepts paths")
{
    CHECK(findExecutable(PROCESS_HELPER).has_value());
    CHECK_FALSE(findExecutable("illusion-no-such-program-anywhere").has_value());
#if defined(ILL_PLATFORM_WINDOWS)
    CHECK(findExecutable("cmd").has_value());
#else
    CHECK(findExecutable("sh").has_value());
#endif
}
