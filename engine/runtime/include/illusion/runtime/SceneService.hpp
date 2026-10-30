#pragma once

/// @file
/// The scene part of the scripting API: loading scene files into the World a module runs on.
///
/// @code
/// if (illusion::SceneService* scenes = context.service<illusion::SceneService>())
/// {
///     std::array<illusion::Entity, 64> created;
///     if (const std::optional<illusion::u32> count = scenes->instantiate("game:/level1.scene", created))
///     {
///         // the first min(*count, 64) entities are in created
///     }
/// }
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/ecs/Entity.hpp>

#include <optional>
#include <span>
#include <string_view>

/// @defgroup scripting_api Scripting API
/// The host service interfaces that shared game modules call (ADR 0041): PhysicsService, AudioService,
/// InputService and SceneService. Each is an abstract class without data members that the host
/// implements over an engine library; modules find them with `context.service<T>()`.

namespace illusion
{

/// Scene files of one Simulation World, implemented by the host (SceneLoaderService over a World and a
/// VirtualFileSystem) and found by modules with `context.service<SceneService>()`. The scene is read and
/// its entities are created by host code. Adding a function changes the module API version.
/// @ingroup scripting_api
class SceneService : NonCopyable
{
public:
    /// True if a file exists at @p virtualPath.
    [[nodiscard]] virtual bool exists(std::string_view virtualPath) const = 0;
    /// Loads the scene file at @p virtualPath into the World with new EntityGuids, so the same file can be
    /// instantiated many times. Writes the created entities in file order into @p created (as many as fit)
    /// and returns how many were created; nullopt if the file cannot be read or is not a valid scene, with
    /// the reason in lastError().
    virtual std::optional<u32> instantiate(std::string_view virtualPath, std::span<Entity> created) = 0;
    /// Why the last instantiate() failed (empty after a success); valid until the next call.
    [[nodiscard]] virtual std::string_view lastError() const = 0;

protected:
    SceneService() = default;
    virtual ~SceneService() = default;
};

} // namespace illusion
