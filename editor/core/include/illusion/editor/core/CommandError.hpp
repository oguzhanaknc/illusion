#pragma once

/// @file
/// Errors of editor commands: a kind with a stable code, a message, the invalid values and suggestions.

#include <illusion/core/Base.hpp>
#include <illusion/ecs/ComponentJson.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace illusion
{
class JsonMutDocument;
class JsonMutValue;
} // namespace illusion

namespace illusion::editor
{

/// Why a command failed. The values are the application error codes of the editor protocol (positive,
/// outside the JSON-RPC and MCP reserved ranges); they never change.
enum class ErrorKind : u16
{
    Unauthenticated = 1000,    ///< The client has not authenticated.
    PermissionDenied = 1001,   ///< The permission mode does not allow the command.
    Busy = 1002,               ///< The editor cannot run the command now; try again.
    Timeout = 1003,            ///< The command did not finish in time.
    Cancelled = 1004,          ///< The command was cancelled.
    WrongMode = 1100,          ///< The command is not available in the current mode (edit or play).
    NotFound = 1200,           ///< A command, entity, Component, file or other named thing does not exist.
    AlreadyExists = 1201,      ///< The thing to create exists.
    ValidationFailed = 1202,   ///< The parameters are invalid; `errors` lists each problem.
    Conflict = 1203,           ///< The state changed since the client read it (expectRevision).
    Ambiguous = 1204,          ///< A reference names more than one entity.
    PreconditionFailed = 1205, ///< The current state does not allow the command, e.g. unsaved changes.
    IoError = 1300,            ///< Reading or writing a file failed.
    BuildFailed = 1301,        ///< Building a module failed.
    ModuleError = 1302,        ///< A module could not be loaded.
    Unsupported = 1400,        ///< The command cannot run here, e.g. without a GPU.
    LimitExceeded = 1401,      ///< A size or count limit was exceeded.
    Internal = 1500,           ///< A bug in the editor; the journal has the details.
};

/// Stable snake_case name of @p kind, e.g. "not_found".
[[nodiscard]] std::string_view errorKindName(ErrorKind kind) noexcept;

/// The error of a failed command. Messages are English: they are written for logs and AI agents.
struct CommandError
{
    ErrorKind kind = ErrorKind::Internal;   ///< What went wrong.
    std::string message;                    ///< One sentence, e.g. "entity '/Game/Board' does not exist".
    std::vector<ValueError> errors{};       ///< Invalid parameters, each with a JSON Pointer into the params.
    std::vector<std::string> suggestions{}; ///< Close matches of a misspelt name, best first.
    bool retryable = false;                 ///< True if the same call may succeed later.

    /// An error of @p kind with @p message.
    [[nodiscard]] static CommandError make(ErrorKind kind, std::string message);

    /// A ValidationFailed error for the value at @p pointer in the params.
    [[nodiscard]] static CommandError invalid(std::string pointer, std::string message);
};

/// Returns @p error with @p prefix (a JSON Pointer such as "/value") prepended to the pointers of its errors,
/// for errors about a value that sits at @p prefix in the params.
[[nodiscard]] CommandError withPointerPrefix(CommandError error, std::string_view prefix);

/// Writes @p error as a JSON-RPC error object: `{code, message, data: {kind, errors, suggestions,
/// retryable}}`.
[[nodiscard]] JsonMutValue writeCommandError(JsonMutDocument& doc, const CommandError& error);

/// Up to @p limit of @p candidates that are close to @p name, best first (fuzzy matching).
[[nodiscard]] std::vector<std::string>
closestMatches(std::string_view name, const std::vector<std::string_view>& candidates, usize limit = 3);

} // namespace illusion::editor
