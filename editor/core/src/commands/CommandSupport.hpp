#pragma once

// Helpers shared by the core command modules.

#include <illusion/core/Json.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/editor/EditorStrings.hpp>
#include <illusion/editor/core/CoreCommands.hpp>
#include <illusion/editor/core/EditorContext.hpp>

#include <string>
#include <string_view>

namespace illusion::editor::detail
{

/// Registration functions of the modules (called by registerCoreCommands()).
void registerEditorCommands(CommandRegistry& registry);
void registerProjectCommands(CommandRegistry& registry);
void registerSceneCommands(CommandRegistry& registry);
void registerEntityCommands(CommandRegistry& registry);
void registerComponentCommands(CommandRegistry& registry);
void registerHierarchyCommands(CommandRegistry& registry);
void registerSelectionCommands(CommandRegistry& registry);
void registerViewCommands(CommandRegistry& registry);
void registerHistoryCommands(CommandRegistry& registry);
void registerTransactionCommands(CommandRegistry& registry);
void registerTransformCommands(CommandRegistry& registry);
void registerClipboardCommands(CommandRegistry& registry);
void registerAssetCommands(CommandRegistry& registry);
void registerMaterialCommands(CommandRegistry& registry);
void registerLogCommands(CommandRegistry& registry);
void registerSettingsCommands(CommandRegistry& registry);
void registerUiCommands(CommandRegistry& registry);

/// The text of @p key in the current locale.
std::string localized(text::Localization& localization, text::TextKey key);

/// Parses the JSON of a JsonText parameter at @p pointer.
Expected<JsonDocument, CommandError> parseParam(const JsonText& value, std::string_view pointer);

/// Fails with PreconditionFailed when no project is open.
Expected<void, CommandError> requireProject(CommandContext& context);

/// Matches @p text against @p pattern with `*` (any run) and `?` (one byte).
bool globMatch(std::string_view pattern, std::string_view text) noexcept;

/// Joins a directory path and an entry name: "assets:/" + "a" and "assets:/x" + "a".
std::string joinPath(std::string_view directory, std::string_view name);

/// Facts about the open scene.
SceneInfoResult sceneInfo(EditorContext& editor);

/// The world placement of @p entity derived through its chain of LocalTransforms, as
/// updateTransformHierarchy() derives it (Transforms of children are stale within a command that moved their
/// parents).
Transform worldTransformOf(World& world, Entity entity);

/// Drops the entities that have an ancestor in @p entities: they move with it.
void dropDescendants(World& world, std::vector<Entity>& entities);

/// Resolves the entity references of the params member "/entities", which must name at least one entity.
Expected<std::vector<Entity>, CommandError> resolveEntityList(CommandContext& context,
                                                              const std::vector<std::string>& references);

/// The reflected @p value of @p type as compact JSON (entity fields through @p entities).
Expected<std::string, CommandError> writeReflected(const TypeReflection& type, const void* value,
                                                   const EntityResolver* entities = nullptr);

} // namespace illusion::editor::detail
