#pragma once

/// @file
/// Replayable transcripts and checks: JSON Lines files of editor calls with captures and matchers, run
/// against a launched editor (ADR 0038).
///
/// Each line is one step:
///
///     {"call": "entity.create", "params": {"name": "Board"}, "capture": {"board": "/entity/id"},
///      "expect": {"result": {"entity": {"name": "Board"}}}}
///     {"call": "entity.get", "params": {"entity": "${board}"}, "expect": {"error": {"code": 1200}}}
///     {"restart": {"arguments": ["--project", "${root}"]}}
///     {"check": "C4", "title": "Persistence", "call": "scene.info", "expect": {"result": {"hash":
///     "${hash}"}}}
///
/// `${name}` in params, expectations and restart arguments is replaced by a captured value (a string that is
/// exactly `${name}` becomes the captured JSON value; elsewhere the text is substituted). Expectations are
/// matchers (see matchJson()). A step with `"requires": "gpu"` expects `otherwise` (by default error 1400)
/// when the editor runs without a GPU; nothing is skipped. `{"frames": n}` steps call `play.step`.

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/editor/client/RpcClient.hpp>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::editor::client
{

/// Checks @p actual against the matcher @p expected. Returns nothing on a match, otherwise where and why it
/// differs, e.g. "/entity/name: expected \"Board\", got \"Wall\"".
///
/// Matchers: an object matches an object that has (at least) its members, each matching; an array matches an
/// array of the same length element by element; other values match equal values (numbers by value). Objects
/// with one `$` member are operators: `{"$approx": n}` or `{"$approx": [n, tolerance]}` (default 1e-4),
/// `{"$regex": "..."}` (whole string), `{"$length": n}` or `{"$length": {"min": a, "max": b}}` (arrays,
/// objects, strings), `{"$exists": bool}` (inside an object: the member is present or absent), `{"$oneOf":
/// [m...]}` and `{"$contains": m}` (an array element matches m).
[[nodiscard]] std::optional<std::string> matchJson(JsonValue expected, JsonValue actual,
                                                   const std::string& pointer = {});

/// One step that did not go as expected.
struct StepFailure
{
    usize line = 0;          ///< 1-based line in the file.
    std::string check{};     ///< Check id of the step ("" outside checks).
    std::string description; ///< The step, e.g. "entity.create".
    std::string reason;      ///< What differed.
    std::string actual{};    ///< The reply (compact JSON), if any.
};

/// The outcome of running a file.
struct RunReport
{
    usize steps = 0;                       ///< Steps run.
    std::vector<StepFailure> failures;     ///< Failed steps, in order.
    std::vector<std::string> passedChecks; ///< Ids of checks whose steps all passed.
    std::vector<std::string> failedChecks; ///< Ids of checks with a failed step.

    /// True if no step failed.
    [[nodiscard]] bool passed() const noexcept { return failures.empty(); }
};

/// Settings of a TranscriptRunner.
struct RunnerOptions
{
    LaunchOptions launch;                         ///< How to start (and restart) the editor.
    std::map<std::string, std::string> variables; ///< Initial variables (name -> JSON value text).
    bool gpu = false;                             ///< The editor has a GPU (`requires: "gpu"` steps run).
    std::function<void(std::string_view)> log{};  ///< Receives a line per step (optional).
    /// Starts the editor and connects to it (empty: RpcClient::launch()). illusion_ctl passes one that starts
    /// illusion_mcp and runs every step as an MCP tool call (`--via-mcp`).
    std::function<Expected<std::unique_ptr<CallChannel>, std::string>(const LaunchOptions&)> connect{};
};

/// Runs transcripts and checks against an editor it launches.
class TranscriptRunner
{
public:
    /// A runner; nothing starts until run().
    explicit TranscriptRunner(RunnerOptions options);

    /// Runs the steps of @p jsonl (named @p name in messages), launching the editor first if needed.
    /// Outside checks the run stops at the first failure (later steps depend on it); within a checks file
    /// a failed check skips to the next check.
    [[nodiscard]] RunReport run(std::string_view jsonl, std::string_view name);

    /// The variables captured so far (name -> JSON value text).
    [[nodiscard]] const std::map<std::string, std::string>& variables() const noexcept { return m_variables; }

    /// Asks the editor to quit and waits for it; returns its exit code.
    std::optional<i32> stop();

private:
    Expected<void, std::string> ensureStarted(const std::vector<std::string>& extra);

    RunnerOptions m_options;
    std::map<std::string, std::string> m_variables;
    std::vector<std::string> m_restartArguments;
    std::unique_ptr<CallChannel> m_client;
};

} // namespace illusion::editor::client
