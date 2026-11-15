#pragma once

/// @file
/// The World section of a saved game (docs/adr/0058-saved-games.md): captureSave() writes every Entity with
/// the Saved tag and all its descendants as scene JSON; applySave() replaces the World's saved entities with
/// those of a capture in one loadScene() call. Both run on the thread that owns the World.
///
/// @code
/// const auto captured = illusion::captureSave(world);
/// // ... later, after the game loaded the level the save was made in:
/// const auto applied = illusion::applySave(world, captured->world);
/// if (!applied)
/// {
///     ILL_LOG_ERROR("Game", "{}", applied.error().message);
/// }
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/save/SaveFile.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

class GuidGenerator;
class World;

/// Why captureSave() or applySave() failed.
struct SaveFailure
{
    SaveError error = SaveError::InvalidScene; ///< The reason.
    std::string message;                       ///< Details in English, for logs.
};

/// Options of captureSave().
struct SaveCaptureOptions
{
    GuidGenerator* guids = nullptr; ///< Makes missing ids; nullptr uses GuidGenerator::shared().
};

/// The World section written by captureSave().
struct SaveCapture
{
    std::string world; ///< Scene JSON.
    u32 entities = 0;  ///< Number of saved entities.
};

/// What applySave() did.
struct SaveApplied
{
    std::vector<Entity> entities;      ///< The created entities, in file order.
    u32 replaced = 0;                  ///< Number of saved entities destroyed.
    std::vector<std::string> warnings; ///< Migrated types, dropped Components and fields (also logged).
};

/// Fills @p out (replacing its contents) with the World's saved set: every Entity with the Saved tag and all
/// its descendants, each once.
void collectSavedEntities(const World& world, std::vector<Entity>& out);

/// Writes the saved set of @p world as scene JSON (PrefabSaveMode::Expanded, SceneOrder::Hierarchy, Entity
/// fields that refer to entities outside it as their ids), after giving every Entity without an EntityGuid
/// one. Fails with InvalidScene, for example when a floating-point field is not finite.
[[nodiscard]] Expected<SaveCapture, SaveFailure> captureSave(World& world,
                                                             const SaveCaptureOptions& options = {});

/// Replaces the saved set of @p world with the entities of @p json (a World section from captureSave()): an
/// Entity in both is recreated with the saved values, one only in the World is destroyed, one only in the
/// file is created. References to entities outside the file are found by id, or become null. Components of
/// types this program does not know are dropped with a warning. Fails with InvalidScene, changing nothing.
[[nodiscard]] Expected<SaveApplied, SaveFailure> applySave(World& world, std::string_view json);

} // namespace illusion
