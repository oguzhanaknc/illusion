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
    bool async = false;       ///< Runs over several frames with progress (from M7j).
    bool needsGpu = false;    ///< Fails with Unsupported without a GPU.
};

/// When a command may run.
enum class CommandMode : u8
{
    Edit, ///< While editing (not while playing).
    Play, ///< Only while playing.
    Any,  ///< Always.
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

/// A registered command.
struct CommandDescriptor
{
    /// Runs the handler: @p params points to the params struct, @p result to a default result struct.
    using Invoke = Expected<void, CommandError> (*)(CommandContext& context, const void* params,
                                                    void* result);

    CommandInfo info;                         ///< Static description (strings owned by the registry).
    const StructReflection* params = nullptr; ///< Reflection of the params (nullptr: NoParams).
    const StructReflection* result = nullptr; ///< Reflection of the result (nullptr: NoResult).
    u64 paramsType = 0;                       ///< typeHash() of the params struct.
    u64 resultType = 0;                       ///< typeHash() of the result struct.
    Invoke invoke = nullptr;                  ///< The handler.
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

    /// The command named @p name, or nullptr.
    [[nodiscard]] const CommandDescriptor* find(std::string_view name) const;

    /// Every command, sorted by name. Pointers stay valid while the registry lives.
    [[nodiscard]] const std::vector<const CommandDescriptor*>& commands() const noexcept { return m_sorted; }

    /// Up to three registered names close to @p name, best first.
    [[nodiscard]] std::vector<std::string> suggest(std::string_view name) const;

private:
    struct Entry
    {
        CommandDescriptor descriptor;
        std::string name, summary, category, icon, shortcut; // owned copies of the info strings
    };

    void addDescriptor(const CommandDescriptor& descriptor);

    std::map<std::string, Entry, std::less<>> m_entries;
    std::vector<const CommandDescriptor*> m_sorted;
};

} // namespace illusion::editor
