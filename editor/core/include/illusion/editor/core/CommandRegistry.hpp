#pragma once

/// @file
/// Typed editor commands: descriptors with reflected parameter and result structs, and their registry.

#include <illusion/core/Assert.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/TypeName.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/ecs/StructRegistry.hpp>
#include <illusion/editor/core/CommandError.hpp>
#include <illusion/text/StringTable.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace illusion::editor
{

class CommandContext;

/// Behaviour flags of a command.
struct CommandFlags
{
    bool readOnly = false;    ///< Changes nothing.
    bool destructive = false; ///< Removes or overwrites data (even if undoable).
    bool idempotent = false;  ///< Running it twice has the effect of running it once.
    bool undoable = false;    ///< Runs in a transaction that becomes an undo step.
    bool async = false;       ///< Runs over several frames with progress (registered with addAsync()).
    bool needsGpu = false;    ///< Fails with Unsupported without a GPU.
    /// Replaces the scene, saves it or moves through the undo history, so it cannot run while a transaction
    /// is open (EditorContext::beginTransaction()).
    bool outsideTransactions = false;
};

/// When a command may run, and which World it acts on while a play session runs (ADR 0042).
enum class CommandMode : u8
{
    Edit, ///< Always; it acts on the edit World (or on no World), also while playing.
    Play, ///< Only while playing (it controls the play session); it acts on the edit World.
    /// Always; while playing it acts on the play World, unless its params say `"world": "edit"`.
    Any,
};

/// What a command may touch; permission modes allow or ask by class (from M7j).
enum class PermissionClass : u8
{
    Read,        ///< Reads state.
    Edit,        ///< Changes the scene or editor state (undoable).
    Destructive, ///< Deletes data or history.
    Files,       ///< Writes, moves or deletes project files.
    Build,       ///< Builds or loads code.
    System,      ///< Changes the editor process, preferences or automation.
};

/// Stable lower-case name of @p mode, e.g. "edit".
[[nodiscard]] std::string_view commandModeName(CommandMode mode) noexcept;

/// Stable lower-case name of @p permission, e.g. "files".
[[nodiscard]] std::string_view permissionClassName(PermissionClass permission) noexcept;

/// Static description of a command, given at registration.
struct CommandInfo
{
    std::string_view name{};              ///< `domain.verb`, snake_case verbs, e.g. "entity.create".
    u32 version = 1;                      ///< Increases when params or result change incompatibly.
    text::TextKey title{};                ///< Localized title for menus and the palette.
    text::TextKey description{};          ///< Localized one-sentence description (tooltips).
    std::string_view agentSummary{};      ///< English summary for AI agents, with an example call.
    std::string_view category{};          ///< Group in menus and the catalogue, e.g. "Entity".
    std::string_view icon{};              ///< Material Symbols icon name.
    CommandFlags flags{};                 ///< Behaviour.
    CommandMode mode = CommandMode::Edit; ///< When it may run.
    PermissionClass permission = PermissionClass::Read; ///< What it may touch.
    bool palette = false;                               ///< Listed in the command palette.
    std::string_view defaultShortcut{};                 ///< Default shortcut, e.g. "Ctrl+Z" ("" for none).
};

/// Parameters of a command that takes none.
struct NoParams
{
};

/// Result of a command that returns nothing but `_editor`.
struct NoResult
{
};

/// Progress of an async command, reported to clients (`rpc.progress`).
struct CommandProgress
{
    f64 done = 0.0;      ///< Work done, in units of `total`.
    f64 total = 0.0;     ///< Work in all (0: unknown).
    std::string message; ///< What it does now, in English ("" for nothing to say).
};

/// The running part of an async command, without its result type (see CommandTask).
class CommandTaskBase : NonCopyable
{
public:
    virtual ~CommandTaskBase() = default;

    /// Advances the task by one frame. Returns true once it finished and wrote its result to @p result
    /// (the command's default-constructed result struct).
    [[nodiscard]] virtual Expected<bool, CommandError> advance(CommandContext& context,
                                                               CommandProgress& progress, void* result) = 0;
};

/// The running part of an async command whose result is @p R. Its handler validates the params and returns
/// a task, which the editor advances once per frame on its main thread until it finishes. Destroying the
/// task cancels the command, so the editor must be consistent after every frame of it.
template <class R>
class CommandTask : public CommandTaskBase
{
public:
    /// Advances the task by one frame: returns the result once finished, std::nullopt while it runs (after
    /// updating @p progress), or the error that ends it.
    [[nodiscard]] virtual Expected<std::optional<R>, CommandError> update(CommandContext& context,
                                                                          CommandProgress& progress) = 0;

    /// Calls update() and stores a finished result in @p result.
    [[nodiscard]] Expected<bool, CommandError> advance(CommandContext& context, CommandProgress& progress,
                                                       void* result) final
    {
        Expected<std::optional<R>, CommandError> out = update(context, progress);
        if (!out)
        {
            return unexpected(std::move(out).error());
        }
        if (!out->has_value())
        {
            return false;
        }
        *static_cast<R*>(result) = std::move(**out);
        return true;
    }
};

struct CommandDescriptor;

/// Decides which commands a caller may run, e.g. the permission mode of an RPC client. EditorContext asks it
/// before every command of a call that names it (CommandOptions::gate), also before each step of
/// `editor.batch`.
class CommandGate
{
public:
    virtual ~CommandGate() = default;

    /// Returns the error (usually PermissionDenied) if @p command must not run.
    [[nodiscard]] virtual Expected<void, CommandError> check(const CommandDescriptor& command) const = 0;
};

/// A registered command.
struct CommandDescriptor
{
    /// Runs the handler: @p params points to the params struct, @p result to a default result struct.
    using Invoke = Expected<void, CommandError> (*)(CommandContext& context, const void* params,
                                                    void* result);
    /// Starts an async command: validates @p params and returns its task.
    using Start = Expected<std::unique_ptr<CommandTaskBase>, CommandError> (*)(CommandContext& context,
                                                                               const void* params);

    CommandInfo info;                         ///< Static description (strings owned by the registry).
    const StructReflection* params = nullptr; ///< Reflection of the params (nullptr: NoParams).
    const StructReflection* result = nullptr; ///< Reflection of the result (nullptr: NoResult).
    u64 paramsType = 0;                       ///< typeHash() of the params struct.
    u64 resultType = 0;                       ///< typeHash() of the result struct.
    Invoke invoke = nullptr;                  ///< The handler (nullptr for async commands).
    Start start = nullptr;                    ///< Starts an async command (nullptr for the others).
};

namespace detail
{

/// The params and result types of a command handler type.
template <class F>
struct CommandTraits;

/// The params and result types of `Expected<R, CommandError> (*)(CommandContext&, const P&)`.
template <class P, class R>
struct CommandTraits<Expected<R, CommandError> (*)(CommandContext&, const P&)>
{
    using Params = P; ///< The params struct.
    using Result = R; ///< The result struct.
};

/// The params and result types of an async command handler type.
template <class F>
struct AsyncCommandTraits;

/// The params and result types of
/// `Expected<std::unique_ptr<CommandTask<R>>, CommandError> (*)(CommandContext&, const P&)`.
template <class P, class R>
struct AsyncCommandTraits<Expected<std::unique_ptr<CommandTask<R>>, CommandError> (*)(CommandContext&,
                                                                                      const P&)>
{
    using Params = P; ///< The params struct.
    using Result = R; ///< The result struct.
};

} // namespace detail

/// Every command of the editor, by name.
///
/// A handler is a function `Expected<Result, CommandError> fn(CommandContext&, const Params&)`. Params and
/// Result are structs reflected with reflectStruct() (or NoParams/NoResult); their reflections generate the
/// JSON Schemas of the command and convert JSON calls. Menus, shortcuts, the palette, panels, RPC and MCP
/// all run commands through EditorContext, which validates and wraps them in a transaction.
class CommandRegistry : NonCopyable
{
public:
    /// Registers handler @p Fn under @p info.name. The params and result structs must be reflected in
    /// @p structs; names must be unique.
    template <auto Fn>
    void add(const CommandInfo& info, const StructRegistry& structs = ComponentRegistry::instance().structs())
    {
        using Traits = detail::CommandTraits<decltype(Fn)>;
        using P = typename Traits::Params;
        using R = typename Traits::Result;
        ILL_ASSERT(!info.flags.async, "async command {} must be registered with addAsync()", info.name);
        CommandDescriptor descriptor = describe<P, R>(info, structs);
        descriptor.invoke = [](CommandContext& context, const void* params,
                               void* result) -> Expected<void, CommandError> {
            Expected<R, CommandError> out = Fn(context, *static_cast<const P*>(params));
            if (!out)
            {
                return unexpected(std::move(out).error());
            }
            *static_cast<R*>(result) = std::move(out).value();
            return {};
        };
        addDescriptor(descriptor);
    }

    /// Registers the async command @p Fn under @p info.name and sets CommandFlags::async. @p Fn is a function
    /// `Expected<std::unique_ptr<CommandTask<Result>>, CommandError> fn(CommandContext&, const Params&)` that
    /// validates the params and returns the task, which copies what it needs from them (they live only during
    /// the call). Async commands cannot be undoable.
    template <auto Fn>
    void addAsync(const CommandInfo& info,
                  const StructRegistry& structs = ComponentRegistry::instance().structs())
    {
        using Traits = detail::AsyncCommandTraits<decltype(Fn)>;
        using P = typename Traits::Params;
        using R = typename Traits::Result;
        ILL_ASSERT(!info.flags.undoable, "async command {} cannot be undoable", info.name);
        CommandInfo asyncInfo = info;
        asyncInfo.flags.async = true;
        CommandDescriptor descriptor = describe<P, R>(asyncInfo, structs);
        descriptor.start =
            [](CommandContext& context,
               const void* params) -> Expected<std::unique_ptr<CommandTaskBase>, CommandError> {
            Expected<std::unique_ptr<CommandTask<R>>, CommandError> task =
                Fn(context, *static_cast<const P*>(params));
            if (!task)
            {
                return unexpected(std::move(task).error());
            }
            ILL_ASSERT(task.value() != nullptr, "the handler of an async command returned no task");
            return std::unique_ptr<CommandTaskBase>(std::move(task).value());
        };
        addDescriptor(descriptor);
    }

    /// The command named @p name, or nullptr.
    [[nodiscard]] const CommandDescriptor* find(std::string_view name) const;

    /// Every command, sorted by name. Pointers stay valid while the registry lives.
    [[nodiscard]] const std::vector<const CommandDescriptor*>& commands() const noexcept { return m_sorted; }

    /// Up to three registered names close to @p name, best first.
    [[nodiscard]] std::vector<std::string> suggest(std::string_view name) const;

    /// Increases whenever a command is registered (EditorContext::endFrame() then posts `commands.changed`).
    [[nodiscard]] u64 revision() const noexcept { return m_revision; }

private:
    template <class P, class R>
    static CommandDescriptor describe(const CommandInfo& info, const StructRegistry& structs)
    {
        CommandDescriptor descriptor;
        descriptor.info = info;
        descriptor.paramsType = typeHash<P>();
        descriptor.resultType = typeHash<R>();
        if constexpr (!std::is_same_v<P, NoParams>)
        {
            descriptor.params = structs.reflectionOf(typeHash<P>());
            ILL_ASSERT(descriptor.params != nullptr, "params of command {} are not reflected", info.name);
        }
        if constexpr (!std::is_same_v<R, NoResult>)
        {
            descriptor.result = structs.reflectionOf(typeHash<R>());
            ILL_ASSERT(descriptor.result != nullptr, "result of command {} is not reflected", info.name);
        }
        return descriptor;
    }

    struct Entry
    {
        CommandDescriptor descriptor;
        std::string name, summary, category, icon, shortcut; // owned copies of the info strings
    };

    void addDescriptor(const CommandDescriptor& descriptor);

    std::map<std::string, Entry, std::less<>> m_entries;
    std::vector<const CommandDescriptor*> m_sorted;
    u64 m_revision = 0;
};

} // namespace illusion::editor
