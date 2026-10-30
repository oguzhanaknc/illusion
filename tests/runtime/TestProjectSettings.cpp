#include "TestSupport.hpp"

#include <illusion/ecs/SettingsFile.hpp>
#include <illusion/ecs/StructRegistry.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/runtime/ProjectSettings.hpp>

#include <doctest/doctest.h>

#include <string>

using namespace illusion;

namespace
{

template <class T>
const StructReflection& reflectionOf()
{
    const StructReflection* type = ComponentRegistry::instance().structs().reflectionOf(typeHash<T>());
    REQUIRE(type != nullptr);
    return *type;
}

template <class T>
std::string settingsFile(const T& value)
{
    Expected<std::string, std::string> text = writeSettingsFile(reflectionOf<T>(), &value);
    REQUIRE(text.hasValue());
    return *text;
}

struct Fixture
{
    Fixture()
    {
        reflectRuntimeStructs();
        REQUIRE(files.mount("settings", directory.path, MountAccess::ReadWrite) == FileError::None);
    }

    test::TempDirectory directory;
    VirtualFileSystem files;
};

} // namespace

TEST_CASE("project settings default to a game that needs no settings files")
{
    const ProjectSettings settings;
    CHECK(settings.physics.gravity.y == doctest::Approx(-9.81f));
    CHECK(settings.physics.layerNames[0] == "Default");
    CHECK(settings.physics.layerNames[1].empty());
    for (const u32 row : settings.physics.collisionMatrix)
    {
        CHECK(row == 0xFFFFu);
    }
    CHECK(settings.audio.busVolumes[4] == doctest::Approx(1.0f));
    CHECK(settings.input.actions.empty());
    CHECK(settings.render.vsync);

    Fixture f;
    ProjectSettings loaded;
    CHECK(loadProjectSettings(f.files, loaded).hasValue()); // no files: the defaults
    CHECK(loaded.physics.stepSeconds == doctest::Approx(1.0 / 60.0));
}

TEST_CASE("project settings round-trip through their settings files")
{
    Fixture f;
    PhysicsSettings physics;
    physics.gravity = Vec3(0.0f, -3.0f, 0.0f);
    physics.layerNames[1] = "Player";
    physics.collisionMatrix[1] = 0x1u;
    AudioSettings audio;
    audio.busVolumes[1] = 0.5f;
    InputSettings input;
    input.actions = AssetRef("assets:/game.input");
    input.gamepadsInBackground = true;
    RenderSettings render;
    render.renderScale = 0.5f;
    REQUIRE(f.files.writeText(kPhysicsSettingsFile, settingsFile(physics)) == FileError::None);
    REQUIRE(f.files.writeText(kAudioSettingsFile, settingsFile(audio)) == FileError::None);
    REQUIRE(f.files.writeText(kInputSettingsFile, settingsFile(input)) == FileError::None);
    REQUIRE(f.files.writeText(kRenderSettingsFile, settingsFile(render)) == FileError::None);

    ProjectSettings settings;
    REQUIRE(loadProjectSettings(f.files, settings).hasValue());
    CHECK(settings.physics.gravity.y == doctest::Approx(-3.0f));
    CHECK(settings.physics.layerNames[1] == "Player");
    CHECK(settings.physics.collisionMatrix[1] == 0x1u);
    CHECK(settings.physics.collisionMatrix[2] == 0xFFFFu);
    CHECK(settings.audio.busVolumes[1] == doctest::Approx(0.5f));
    CHECK(settings.input.actions.path() == "assets:/game.input");
    CHECK(settings.input.gamepadsInBackground);
    CHECK(settings.render.renderScale == doctest::Approx(0.5f));
}

TEST_CASE("a missing field keeps its default and an invalid file names itself")
{
    Fixture f;
    REQUIRE(f.files.writeText(kAudioSettingsFile,
                              R"({"format": "illusion.settings", "type": "illusion.AudioSettings",
                                                     "version": 1, "value": {"maxVoices": 32}})") ==
            FileError::None);
    ProjectSettings settings;
    REQUIRE(loadProjectSettings(f.files, settings).hasValue());
    CHECK(settings.audio.maxVoices == 32);
    CHECK(settings.audio.masterVolume == doctest::Approx(1.0f));

    REQUIRE(f.files.writeText(kRenderSettingsFile,
                              R"({"format": "illusion.settings", "type": "illusion.AudioSettings",
                                                      "version": 1, "value": {}})") == FileError::None);
    const Expected<void, std::string> wrongType = loadProjectSettings(f.files, settings);
    REQUIRE_FALSE(wrongType.hasValue());
    CHECK(wrongType.error().find("settings:/render.json") == 0);
    CHECK(settings.audio.maxVoices == 32); // the files before it were read

    REQUIRE(f.files.writeText(kRenderSettingsFile, "not json") == FileError::None);
    const Expected<void, std::string> broken = loadProjectSettings(f.files, settings);
    REQUIRE_FALSE(broken.hasValue());
    CHECK(broken.error().find("not valid JSON") != std::string::npos);
}

TEST_CASE("project settings reject values outside their ranges")
{
    Fixture f;
    REQUIRE(f.files.writeText(kPhysicsSettingsFile,
                              R"({"format": "illusion.settings", "type": "illusion.PhysicsSettings",
                                                       "version": 1, "value": {"maxStepsPerUpdate": 0}})") ==
            FileError::None);
    ProjectSettings settings;
    const Expected<void, std::string> loaded = loadProjectSettings(f.files, settings);
    REQUIRE_FALSE(loaded.hasValue());
    CHECK(loaded.error().find("maxStepsPerUpdate") != std::string::npos);
}
