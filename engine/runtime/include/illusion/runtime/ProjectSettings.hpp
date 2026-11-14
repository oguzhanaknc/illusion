#pragma once

/// @file
/// The project settings a game runs with: physics, audio, input, rendering and the window, each a reflected
/// struct saved in its own settings file (SettingsFile.hpp) on the project's `settings:` mount. A missing
/// file keeps the defaults, so a new project needs none of them.
///
/// @code
/// illusion::reflectRuntimeStructs();
/// illusion::ProjectSettings settings;
/// if (auto loaded = illusion::loadProjectSettings(files, settings); !loaded)
/// {
///     ILL_LOG_ERROR("runtime", "{}", loaded.error());
/// }
/// @endcode

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/platform/Window.hpp>

#include <array>
#include <string>
#include <string_view>

namespace illusion
{

class ComponentRegistry;
class VirtualFileSystem;

/// Number of physics layers in PhysicsSettings (kMaxPhysicsLayers of the physics library).
inline constexpr u32 kProjectPhysicsLayers = 16;

/// Number of audio buses in AudioSettings (kAudioBusCount of the audio library).
inline constexpr u32 kProjectAudioBuses = 5;

/// Physics of the game (`settings:/physics.json`, type "illusion.PhysicsSettings").
struct PhysicsSettings
{
    Vec3 gravity{0.0f, -9.81f, 0.0f}; ///< Gravity in m/s².
    f64 stepSeconds = 1.0 / 60.0;     ///< Fixed simulation step in seconds.
    u32 maxStepsPerUpdate = 4;        ///< Steps per frame at most; longer frames slow the simulation down.
    /// Names of the collision layers, for editors (Collider::layer and RigidBody::layer are indices).
    std::array<std::string, kProjectPhysicsLayers> layerNames{"Default"};
    /// Bit b of entry a: layers a and b may collide. Layers collide only when both entries agree, so the
    /// matrix acts as if symmetric. Every layer collides with every layer by default.
    std::array<u32, kProjectPhysicsLayers> collisionMatrix = [] {
        std::array<u32, kProjectPhysicsLayers> all{};
        all.fill((1u << kProjectPhysicsLayers) - 1u);
        return all;
    }();
};

/// Audio of the game (`settings:/audio.json`, type "illusion.AudioSettings").
struct AudioSettings
{
    f32 masterVolume = 1.0f; ///< Linear gain of the whole mix.
    /// Linear gain per bus, in AudioBus order: Sfx, Music, Ambience, Dialogue, Ui.
    std::array<f32, kProjectAudioBuses> busVolumes{1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    u32 maxVoices = 128;       ///< Sounds that can play at the same time.
    f32 speedOfSound = 343.3f; ///< In m/s, for the doppler effect.
};

/// Input of the game (`settings:/input.json`, type "illusion.InputSettings").
struct InputSettings
{
    /// The input actions asset (`.input`, InputActionsAsset.hpp) the game starts with; empty for none.
    AssetRef actions{};
    /// Gamepads drive the game even while its window or view does not have the focus.
    bool gamepadsInBackground = false;
};

/// Rendering of the game (`settings:/render.json`, type "illusion.RenderSettings"), applied by the host
/// that presents the game: the player, or the editor's Game view.
struct RenderSettings
{
    bool vsync = true;          ///< Wait for the display's refresh.
    u32 maxFramesPerSecond = 0; ///< Frame rate limit; 0 for none.
    f32 renderScale = 1.0f;     ///< Resolution of the 3D view relative to the window, in [0.25, 2].
};

/// The game's window (`settings:/window.json`, type "illusion.WindowSettings"), applied by the player; the
/// editor's Game view ignores it.
struct WindowSettings
{
    u32 width = 1280;                       ///< Width of a Windowed or Fullscreen window, in pixels.
    u32 height = 720;                       ///< Height of a Windowed or Fullscreen window, in pixels.
    WindowMode mode = WindowMode::Windowed; ///< Windowed, Borderless or Fullscreen.
    bool resizable = true;                  ///< The player can resize a Windowed window.
};

/// Every project settings struct.
struct ProjectSettings
{
    PhysicsSettings physics{}; ///< `settings:/physics.json`.
    AudioSettings audio{};     ///< `settings:/audio.json`.
    InputSettings input{};     ///< `settings:/input.json`.
    RenderSettings render{};   ///< `settings:/render.json`.
    WindowSettings window{};   ///< `settings:/window.json`.
};

/// File of PhysicsSettings.
inline constexpr std::string_view kPhysicsSettingsFile = "settings:/physics.json";
/// File of AudioSettings.
inline constexpr std::string_view kAudioSettingsFile = "settings:/audio.json";
/// File of InputSettings.
inline constexpr std::string_view kInputSettingsFile = "settings:/input.json";
/// File of RenderSettings.
inline constexpr std::string_view kRenderSettingsFile = "settings:/render.json";
/// File of WindowSettings.
inline constexpr std::string_view kWindowSettingsFile = "settings:/window.json";

/// Reflects PhysicsSettings, AudioSettings, InputSettings, RenderSettings and WindowSettings as structs of
/// @p registry (as "illusion.PhysicsSettings", ...), so that settings files and editors can read and write
/// them. Call it at startup, before loadProjectSettings().
void reflectRuntimeStructs(ComponentRegistry& registry);

/// reflectRuntimeStructs() on ComponentRegistry::instance().
void reflectRuntimeStructs();

/// Reads the project settings files that exist on @p files into @p settings; the others keep their values.
/// Fails, starting with the file, on the first file that cannot be read or holds invalid settings
/// (readSettingsFile()); the settings read before it are kept. Needs reflectRuntimeStructs() on
/// ComponentRegistry::instance().
[[nodiscard]] Expected<void, std::string> loadProjectSettings(const VirtualFileSystem& files,
                                                              ProjectSettings& settings);

} // namespace illusion

/// Names of WindowMode values in settings files.
template <>
struct illusion::EnumReflection<illusion::WindowMode>
{
    /// Value names.
    static constexpr std::array kValues{illusion::EnumValue{"Windowed", 0},
                                        illusion::EnumValue{"Borderless", 1},
                                        illusion::EnumValue{"Fullscreen", 2}};
};
