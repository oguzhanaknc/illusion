#pragma once

/// @file
/// Validated, reversible edits of the edited World and the project files. Every edit is recorded as
/// primitive operations into the open transaction.

#include <illusion/core/Expected.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/editor/core/CommandError.hpp>
#include <illusion/editor/core/Operation.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{
class GuidGenerator;
class JsonValue;
class VirtualFileSystem;
class World;
} // namespace illusion

namespace illusion::editor
{

class ChangeJournal;
class EntityIndex;

/// What WorldEditor::create() makes.
struct CreateEntityOptions
{
    std::string name{};         ///< Name ("" for none).
    Entity parent{};            ///< Parent (null: a root).
    std::optional<u32> index{}; ///< Position among the siblings (nullopt: last; only with a parent or a
                                ///< Hierarchy root).
    JsonValue components{};     ///< Optional JSON object: stable Component name -> value.
    EntityGuid guid{};          ///< Id to use (null: a new one).
};

/// Applies edits to one World (and, with setFiles(), to project files) and records them.
///
/// Every edit validates first and changes nothing when it fails. While a transaction is open (begin()),
/// each edit appends the operations that redo it; commit() hands them over and rollback() undoes them.
/// Edits made without a transaction are applied but not recorded. Components without reflection cannot be
/// recorded: entities are snapshot with every reflected Component (runtime ones included), so undoing the
/// destruction of an entity loses only Components that have no reflection.
///
/// The Components `illusion.EntityGuid` and `illusion.Hierarchy` cannot be added, removed or set
/// directly: ids are fixed, and the hierarchy changes through setParent() and reorder().
class WorldEditor : NonCopyable
{
public:
    /// Edits @p world; @p index must index it. All references must outlive the editor.
    WorldEditor(World& world, EntityIndex& index, GuidGenerator& guids, ChangeJournal& changes);

    /// Enables file edits: paths are on @p files, and replaced or removed contents are kept as blobs in
    /// @p blobs (a directory). @p blobPrefix is prepended to blob names, e.g. the session id.
    void setFiles(VirtualFileSystem* files, std::filesystem::path blobs, std::string blobPrefix);

    /// Opens a transaction. Transactions do not nest.
    void begin();

    /// True while a transaction is open.
    [[nodiscard]] bool recording() const noexcept { return m_recording; }

    /// Number of operations recorded in the open transaction.
    [[nodiscard]] usize pendingOperations() const noexcept { return m_operations.size(); }

    /// Closes the transaction and returns its operations in the order they were applied.
    [[nodiscard]] std::vector<Operation> commit();

    /// Closes the transaction and undoes its operations.
    void rollback();

    /// Creates an entity with a new id (or @p options.guid), a Name, the given Components and a parent.
    [[nodiscard]] Expected<Entity, CommandError> create(const CreateEntityOptions& options);

    /// Destroys @p root and its descendants. Entity fields elsewhere that refer to them become null (and
    /// come back on undo). Returns the number of destroyed entities.
    [[nodiscard]] Expected<u32, CommandError> destroy(Entity root);

    /// Copies @p root and its descendants with new ids, right after @p root among its siblings. Entity
    /// fields that refer into the copied subtree refer to the copies. Returns the copy of @p root.
    [[nodiscard]] Expected<Entity, CommandError> duplicate(Entity root);

    /// Sets the Name of @p entity; an empty name removes it.
    [[nodiscard]] Expected<void, CommandError> rename(Entity entity, std::string_view name);

    /// Adds or removes the Disabled tag (see illusion::setEnabled()).
    [[nodiscard]] Expected<void, CommandError> setEnabled(Entity entity, bool enabled);

    /// Adds Component @p type with its default value, then @p value (a JSON object) if it is given.
    /// Components the type requires are added first. Returns the stable names of the added Components.
    [[nodiscard]] Expected<std::vector<std::string>, CommandError>
    addComponent(Entity entity, ComponentId type, JsonValue value = {});

    /// Removes Component @p type. Fails if another Component of the entity requires it.
    [[nodiscard]] Expected<void, CommandError> removeComponent(Entity entity, ComponentId type);

    /// Reads @p value into Component @p type: Merge keeps fields that @p value does not mention (a merge
    /// patch), Replace resets them. Returns true if the value changed.
    [[nodiscard]] Expected<bool, CommandError> setComponent(Entity entity, ComponentId type, JsonValue value,
                                                            JsonReadMode mode = JsonReadMode::Merge);

    /// Sets the field at @p path ("position/x", "points/2") of Component @p type. Returns true if the
    /// value changed.
    [[nodiscard]] Expected<bool, CommandError> setField(Entity entity, ComponentId type,
                                                        std::string_view path, JsonValue value);

    /// Gives Component @p type its default value. Returns true if the value changed.
    [[nodiscard]] Expected<bool, CommandError> resetComponent(Entity entity, ComponentId type);

    /// Moves @p entity under @p parent (null: to the roots) at @p index (see illusion::setParent()).
    [[nodiscard]] Expected<void, CommandError> setParent(Entity entity, Entity parent, ParentMode mode,
                                                         u32 index = kLastSibling);

    /// Moves @p entity to @p index among its siblings.
    [[nodiscard]] Expected<void, CommandError> reorder(Entity entity, u32 index);

    /// Writes a UTF-8 text file. Returns true if it was created, false if it replaced a file.
    [[nodiscard]] Expected<bool, CommandError> writeFile(std::string_view path, std::string_view text);

    /// Moves or renames a file or directory.
    [[nodiscard]] Expected<void, CommandError> moveFile(std::string_view from, std::string_view to);

    /// Removes a file or a directory with everything in it.
    [[nodiscard]] Expected<void, CommandError> deleteFile(std::string_view path);

    /// Creates a directory and any missing parents.
    [[nodiscard]] Expected<void, CommandError> createDirectory(std::string_view path);

    /// Applies @p operation (undo, redo, journal recovery), recording it if a transaction is open.
    /// Returns an Internal error if the World or the files do not match the operation.
    [[nodiscard]] Expected<void, CommandError> apply(const Operation& operation);

    /// The value of Component @p type of @p entity as compact JSON (entity fields as ids), or std::nullopt
    /// if the entity does not have it.
    [[nodiscard]] std::optional<std::string> capture(Entity entity, ComponentId type) const;

    /// The edited World.
    [[nodiscard]] World& world() noexcept { return *m_world; }

private:
    struct Watch;

    void record(Operation operation);
    [[nodiscard]] Expected<void, CommandError> checkEditable(Entity entity, ComponentId type) const;
    [[nodiscard]] EntityRecord snapshot(Entity entity) const;
    [[nodiscard]] Expected<void, CommandError> applyComponent(const ComponentChange& change);
    [[nodiscard]] Expected<void, CommandError> applyLifetime(const EntityLifetime& lifetime);
    [[nodiscard]] Expected<void, CommandError> applyFile(const FileChange& change);
    [[nodiscard]] Expected<std::optional<std::string>, CommandError> storeBlob(std::string_view path);
    [[nodiscard]] Expected<void, CommandError> createParents(std::string_view path);
    void watch(std::vector<Watch>& watches, Entity entity, ComponentId type) const;
    void finish(std::vector<Watch>& watches);
    void watchSiblings(std::vector<Watch>& watches, Entity parent);
    void subtreeOf(Entity root, std::vector<Entity>& out);
    [[nodiscard]] bool hasEntityFields(ComponentId type);

    World* m_world;
    EntityIndex* m_index;
    GuidGenerator* m_guids;
    ChangeJournal* m_changes;
    VirtualFileSystem* m_files = nullptr;
    std::filesystem::path m_blobs;
    std::string m_blobPrefix;
    u64 m_blobCounter = 0;
    bool m_recording = false;
    std::vector<Operation> m_operations;
    std::vector<i8> m_entityFieldCache; // per ComponentId: -1 unknown, 0 no Entity fields, 1 has some
};

} // namespace illusion::editor
