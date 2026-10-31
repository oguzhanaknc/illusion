#pragma once

/// @file
/// Child processes: spawning without a shell, stdio pipes drained by reader threads into bounded buffers,
/// exit codes, timeouts and termination.

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>

#include <chrono>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace illusion
{

/// An output stream of a child process.
enum class ProcessStream : u8
{
    Output, ///< Standard output.
    Error,  ///< Standard error.
};

/// Exit code reported for a process ended by Process::terminate() (on every platform).
inline constexpr i32 kTerminatedExitCode = -9;

/// How to start a child process.
struct ProcessOptions
{
    /// The program: a path to an executable file (relative paths are relative to the current directory).
    /// No shell runs and `PATH` is not searched; use findExecutable() for that.
    std::filesystem::path executable;
    std::vector<std::string> arguments{};     ///< UTF-8 arguments, passed exactly (no shell, no globbing).
    std::filesystem::path workingDirectory{}; ///< Directory to start in (empty: the current directory).
    /// Variables added to (or replacing those of) the environment of this process, as UTF-8 name and value.
    std::vector<std::pair<std::string, std::string>> environment{};
    bool pipeInput = false;          ///< Open a pipe for writeInput(); otherwise stdin reads the null device.
    bool captureOutput = true;       ///< Capture stdout for read(); otherwise the child shares this one's.
    bool captureError = true;        ///< Capture stderr for read(); otherwise the child shares this one's.
    usize bufferCapacity = 1u << 20; ///< Bytes buffered per captured stream; a full buffer blocks the child.
    /// Let the child run on its own, e.g. a code editor that should outlive this process: its stdin, stdout
    /// and stderr use the null device (pipeInput and the capture options are ignored), and destroying the
    /// Process neither terminates nor waits for it. poll() and wait() then report nothing and terminate()
    /// does nothing; on POSIX systems a thread reaps the child when it exits.
    bool detached = false;
};

/// A running (or finished) child process.
///
/// Reader threads drain the captured streams into bounded buffers, so a chatty child never blocks on a full
/// pipe while the buffer has room; read them with read() or readLine(). Destroying a Process that still runs
/// terminates it. Methods may be called from any one thread at a time; reading the two streams from two
/// threads is allowed.
///
/// On POSIX systems the first spawn() sets `SIGPIPE` to ignored if it had the default action, so that
/// writing to a child that closed its stdin fails instead of ending this process.
class Process
{
public:
    /// A Process that was not started.
    Process() noexcept;
    /// Takes over @p other, which becomes not started.
    Process(Process&& other) noexcept;
    /// Terminates the current process (if running) and takes over @p other.
    Process& operator=(Process&& other) noexcept;
    Process(const Process&) = delete;            ///< Not copyable.
    Process& operator=(const Process&) = delete; ///< Not copyable.
    /// Terminates the process if it still runs and waits for it.
    ~Process();

    /// Starts @p options.executable. The error describes why it could not start.
    [[nodiscard]] static Expected<Process, std::string> spawn(const ProcessOptions& options);

    /// True if the process was started (it may have finished since).
    [[nodiscard]] bool valid() const noexcept { return m_impl != nullptr; }

    /// The operating system's process id (0 if not started).
    [[nodiscard]] u64 id() const noexcept;

    /// Writes @p bytes to the child's stdin (ProcessOptions::pipeInput), blocking until all are written.
    /// Returns false if there is no input pipe or the child closed it.
    bool writeInput(std::string_view bytes);

    /// Closes the child's stdin, so it reads end of file.
    void closeInput() noexcept;

    /// Appends up to @p maxBytes buffered bytes of @p stream to @p out without blocking; returns how many.
    usize read(ProcessStream stream, std::string& out, usize maxBytes = std::numeric_limits<usize>::max());

    /// Waits up to @p timeout for a complete line of @p stream and moves it (without the line break and a
    /// trailing `\r`) to @p line. At end of file the last unterminated line is returned too. Returns false on
    /// timeout, or at end of file when nothing is left.
    bool readLine(ProcessStream stream, std::string& line, std::chrono::milliseconds timeout);

    /// True once @p stream reached end of file and its buffer is empty (always true if not captured).
    [[nodiscard]] bool drained(ProcessStream stream) const;

    /// The exit code if the process has finished (a process ended by signal N reports -N).
    [[nodiscard]] std::optional<i32> poll();

    /// Waits up to @p timeout for the process to finish; returns its exit code, or nothing on timeout.
    std::optional<i32> wait(std::chrono::milliseconds timeout = std::chrono::milliseconds::max());

    /// Forcibly ends the process (SIGKILL or TerminateProcess); it then exits with kTerminatedExitCode.
    /// Returns false if it had already finished or was not started.
    bool terminate() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// Finds @p name (e.g. "cmake") in the directories of `PATH`, trying the `PATHEXT` extensions on Windows.
/// A @p name with a directory part is returned as is if it exists.
[[nodiscard]] std::optional<std::filesystem::path> findExecutable(std::string_view name);

/// True if a process with id @p pid exists (it may belong to another user).
[[nodiscard]] bool isProcessRunning(u64 pid) noexcept;

/// The id of this process.
[[nodiscard]] u64 currentProcessId() noexcept;

/// Quotes @p arguments into a Windows command line that CommandLineToArgvW() and the C runtime split back
/// into exactly the same arguments (used by Process on Windows; exposed for tests).
[[nodiscard]] std::string windowsCommandLine(const std::vector<std::string>& arguments);

} // namespace illusion
