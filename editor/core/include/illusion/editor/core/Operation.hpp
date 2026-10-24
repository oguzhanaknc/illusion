#pragma once

/// @file
/// The reversible primitive operations that editor transactions are made of. Undo, redo, the crash
/// journal and recovery all replay them.

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/ecs/EntityGuid.hpp>

#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace illusion
{
class JsonMutDocument;
class JsonMutValue;
class JsonValue;
} // namespace illusion

namespace illusion::editor
{

/// One Component of one entity before and after a change, as compact JSON in the scene v2 value encoding
/// (entity fields are ids). std::nullopt means the entity did not have the Component: a change from nullopt
/// adds it, a change to nullopt removes it.
struct ComponentChange
{
    EntityGuid entity;                 ///< The entity.
    std::string type;                  ///< Stable name of the Component, e.g. "illusion.Transform".
    std::optional<std::string> before; ///< The value before.
    std::optional<std::string> after;  ///< The value after.
};

/// One entity of an EntityLifetime: its id and every reflected Component it has.
struct EntityRecord
{
    EntityGuid guid;        ///< The id.
    std::string components; ///< A compact JSON object: stable Component name -> value.
};

/// Entities that appear (spawn) or disappear (despawn) as a whole. Parents come before their children.
struct EntityLifetime
{
    std::vector<EntityRecord> entities; ///< The entities with their values.
    bool spawn = true;                  ///< True: they are created; false: they are destroyed.
};

/// A change of a project file or directory. Contents live in the session's blob store in
/// `.illusion/trash`, so both directions can be replayed.
struct FileChange
{
    /// What changes.
    enum class Kind : u8
    {
        Replace, ///< The item at `path` goes from blob `before` to blob `after` (nullopt: no item).
        Move,    ///< The item at `path` moves to `target`.
    };

    Kind kind = Kind::Replace;         ///< What changes.
    std::string path;                  ///< Virtual path of the item.
    std::string target;                ///< Move: the new virtual path.
    std::optional<std::string> before; ///< Replace: blob of the item before.
    std::optional<std::string> after;  ///< Replace: blob of the item after.
};

/// A setting of the open scene (its "settings" section) before and after a change, as compact JSON values of
/// the reflected struct with the data version they are saved with. std::nullopt means the scene did not have
/// the setting.
struct SettingsChange
{
    std::string type;                   ///< Stable name of the settings struct, e.g. "game.Difficulty".
    std::optional<std::string> before;  ///< The value before.
    std::optional<std::string> after;   ///< The value after.
    std::optional<u32> beforeVersion{}; ///< Data version of the value before (nullopt: none recorded).
    std::optional<u32> afterVersion{};  ///< Data version of the value after (nullopt: none recorded).
};

/// One primitive operation.
using Operation = std::variant<ComponentChange, EntityLifetime, FileChange, SettingsChange>;

/// The operation that undoes @p operation.
[[nodiscard]] Operation inverted(const Operation& operation);

/// Approximate memory used by @p operation in bytes (for the undo budget).
[[nodiscard]] usize operationBytes(const Operation& operation) noexcept;

/// Describes the first thing that both @p a and @p b touch, or returns std::nullopt when they are
/// independent, so that applying or reverting either one leaves the other valid. They overlap when they
/// change the same Component of an entity, when one creates or destroys an entity that the other changes or
/// refers to in a value, or when they touch the same file or setting.
[[nodiscard]] std::optional<std::string> findOverlap(std::span<const Operation> a,
                                                     std::span<const Operation> b);

/// Writes @p operation as a JSON object (for the crash journal).
[[nodiscard]] JsonMutValue writeOperation(JsonMutDocument& doc, const Operation& operation);

/// Reads an operation written by writeOperation(); the error says what is wrong.
[[nodiscard]] Expected<Operation, std::string> readOperation(JsonValue json);

} // namespace illusion::editor
