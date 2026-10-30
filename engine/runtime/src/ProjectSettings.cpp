#include <illusion/audio/AudioComponents.hpp>
#include <illusion/ecs/SettingsFile.hpp>
#include <illusion/ecs/StructRegistry.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/runtime/ProjectSettings.hpp>

#include <format>

namespace illusion
{

static_assert(kProjectPhysicsLayers == kMaxPhysicsLayers,
              "PhysicsSettings needs one entry per physics layer");
static_assert(kProjectAudioBuses == kAudioBusCount, "AudioSettings needs one volume per audio bus");

namespace
{

template <class T>
Expected<void, std::string> loadFile(const VirtualFileSystem& files, std::string_view path, T& value,
                                     std::string& text)
{
    if (!files.exists(path))
    {
        return {};
    }
    const StructReflection* type = ComponentRegistry::instance().structs().reflectionOf(typeHash<T>());
    if (type == nullptr)
    {
        return unexpected(
            std::format("{}: the settings type is not reflected (call reflectRuntimeStructs())", path));
    }
    if (const FileError error = files.readText(path, text); error != FileError::None)
    {
        return unexpected(std::format("{}: cannot read the file ({})", path, fileErrorName(error)));
    }
    if (Expected<void, std::string> read = readSettingsFile(*type, text, &value); !read)
    {
        return unexpected(std::format("{}: {}", path, read.error()));
    }
    return {};
}

} // namespace

void reflectRuntimeStructs(ComponentRegistry& registry)
{
    StructRegistry& structs = registry.structs();
    reflectStruct<PhysicsSettings>("illusion.PhysicsSettings", 1, structs)
        .meta({.displayName = "Physics", .docsAnchor = "manual/runtime.md#project-settings"})
        .field("gravity", &PhysicsSettings::gravity, {.description = "Gravity in m/s²."})
        .field("stepSeconds", &PhysicsSettings::stepSeconds,
               {.description = "Fixed simulation step.", .unit = Unit::Seconds, .min = 0.001, .max = 0.1})
        .field("maxStepsPerUpdate", &PhysicsSettings::maxStepsPerUpdate,
               {.description = "Steps per frame at most; longer frames slow the simulation down.",
                .min = 1.0,
                .max = 16.0})
        .field("layerNames", &PhysicsSettings::layerNames, {.description = "Names of the collision layers."})
        .field("collisionMatrix", &PhysicsSettings::collisionMatrix,
               {.description = "Per layer, the layers it collides with (both layers must agree).",
                .hint = FieldHint::LayerMask});
    reflectStruct<AudioSettings>("illusion.AudioSettings", 1, structs)
        .meta({.displayName = "Audio", .docsAnchor = "manual/runtime.md#project-settings"})
        .field("masterVolume", &AudioSettings::masterVolume,
               {.min = 0.0, .softMax = 2.0, .hint = FieldHint::Slider})
        .field("busVolumes", &AudioSettings::busVolumes,
               {.description = "Per bus: Sfx, Music, Ambience, Dialogue, Ui.", .min = 0.0, .softMax = 2.0})
        .field("maxVoices", &AudioSettings::maxVoices, {.min = 1.0, .max = 4096.0})
        .field("speedOfSound", &AudioSettings::speedOfSound, {.unit = Unit::MetersPerSecond, .min = 1.0});
    reflectStruct<InputSettings>("illusion.InputSettings", 1, structs)
        .meta({.displayName = "Input", .docsAnchor = "manual/runtime.md#project-settings"})
        .field("actions", &InputSettings::actions, {.description = "The input actions asset (.input)."})
        .field("gamepadsInBackground", &InputSettings::gamepadsInBackground,
               {.description = "Gamepads drive the game without the focus."});
    reflectStruct<RenderSettings>("illusion.RenderSettings", 1, structs)
        .meta({.displayName = "Rendering", .docsAnchor = "manual/runtime.md#project-settings"})
        .field("vsync", &RenderSettings::vsync)
        .field("maxFramesPerSecond", &RenderSettings::maxFramesPerSecond, {.description = "0 for no limit."})
        .field("renderScale", &RenderSettings::renderScale,
               {.min = 0.25, .max = 2.0, .hint = FieldHint::Slider});
}

void reflectRuntimeStructs()
{
    reflectRuntimeStructs(ComponentRegistry::instance());
}

Expected<void, std::string> loadProjectSettings(const VirtualFileSystem& files, ProjectSettings& settings)
{
    std::string text;
    if (auto read = loadFile(files, kPhysicsSettingsFile, settings.physics, text); !read)
    {
        return read;
    }
    if (auto read = loadFile(files, kAudioSettingsFile, settings.audio, text); !read)
    {
        return read;
    }
    if (auto read = loadFile(files, kInputSettingsFile, settings.input, text); !read)
    {
        return read;
    }
    return loadFile(files, kRenderSettingsFile, settings.render, text);
}

} // namespace illusion
