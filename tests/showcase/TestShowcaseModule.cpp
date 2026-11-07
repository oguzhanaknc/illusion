// The showcase module (samples/showcase/modules/showcase) on the showcase scene: it reaches every function of
// the scripting API through the host's services, and fixed-step sessions with the same input repeat exactly.

#include "TestSupport.hpp"
#include "modules/CountingServices.hpp"

#include <illusion/audio/AudioComponents.hpp>
#include <illusion/audio/AudioWorld.hpp>
#include <illusion/audio/AudioWorldService.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/InputActions.hpp>
#include <illusion/input/InputActionsService.hpp>
#include <illusion/modules/ModuleHost.hpp>
#include <illusion/particles/ParticleComponents.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsWorld.hpp>
#include <illusion/physics/PhysicsWorldService.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/platform/Input.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/runtime/GameSession.hpp>
#include <illusion/runtime/ProjectSettings.hpp>
#include <illusion/runtime/SceneLoaderService.hpp>

#include <doctest/doctest.h>

#include <array>
#include <span>
#include <string>

using namespace illusion;

namespace
{

constexpr f64 kStep = 1.0 / 60.0;
constexpr int kFrames = 300;
constexpr std::string_view kScene = "assets:/scenes/everything.scene";

void reflectAll()
{
    reflectCoreComponents();
    reflectPhysicsComponents();
    reflectAudioComponents();
    reflectParticleComponents();
    reflectRenderComponents();
    reflectRuntimeStructs();
}

// The showcase project's assets, mounted read-only.
void mountShowcase(VirtualFileSystem& files)
{
    REQUIRE(files.mount("assets", utf8Path(ILLUSION_SAMPLES_DIR) / "showcase" / "assets",
                        MountAccess::ReadOnly) == FileError::None);
}

// Instantiates the everything scene into @p world, with the same EntityGuids every time.
void loadShowcaseScene(World& world, const VirtualFileSystem& files)
{
    GuidGenerator guids(1);
    SceneLoaderService scenes(world, files, &guids);
    std::array<Entity, 1> first{};
    const std::optional<u32> count = scenes.instantiate(kScene, first);
    INFO(scenes.lastError());
    REQUIRE(count.has_value());
    REQUIRE(*count > 30);
}

// The u32 field @p field of the module's showcase.Stats in @p world, read through reflection.
u32 statsField(const World& world, std::string_view field)
{
    const ComponentRegistry& registry = world.registry();
    const ComponentId id = registry.findByStableName("showcase.Stats");
    REQUIRE(id != kInvalidComponentId);
    const TypeReflection* type = registry.reflection(id);
    REQUIRE(type != nullptr);
    std::vector<Entity> entities;
    world.entities(entities);
    for (const Entity entity : entities)
    {
        if (const void* data = world.componentData(entity, id))
        {
            JsonMutDocument doc;
            Expected<JsonMutValue, ValueError> written = writeValue(doc, *type, data);
            REQUIRE(written.hasValue());
            doc.setRoot(*written);
            std::string text;
            REQUIRE(doc.write(text).hasValue());
            const Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(text);
            REQUIRE(parsed.hasValue());
            return static_cast<u32>(parsed->root().find(field).asU64().value_or(0));
        }
    }
    FAIL("no showcase.Stats in the World");
    return 0;
}

// The reflected state of a World: every entity (prefab members too) in id order, hashed.
u64 stateHash(const World& world)
{
    SceneSaveOptions options;
    options.order = SceneOrder::ById;
    options.prefabMode = PrefabSaveMode::Expanded;
    const SceneSaveResult saved = saveScene(world, options);
    REQUIRE_MESSAGE(saved.ok(), saved.error);
    return xxhash64(std::as_bytes(std::span(saved.json.data(), saved.json.size())));
}

struct SessionRun
{
    u64 hash = 0;
    u32 frames = 0;
    u32 jumps = 0;
    u32 spawned = 0;
};

// A fixed-step game session of the showcase scene with the showcase module for kFrames updates. With
// @p input, the player holds D for a second and taps Space twice.
SessionRun runSession(bool input)
{
    reflectAll();
    test::TempDirectory directory;
    VirtualFileSystem files;
    mountShowcase(files);
    JobSystem jobs{JobSystemDesc{2, 1024}};
    ModuleHostDesc hostDesc;
    hostDesc.shadowDirectory = directory.path / "shadow";
    ModuleHost modules(jobs, hostDesc);
    const std::optional<std::string> loadError = modules.load(utf8Path(SHOWCASE_MODULE));
    REQUIRE_MESSAGE(!loadError, loadError.value_or(""));

    GameSessionDesc desc;
    desc.jobs = &jobs;
    desc.files = &files;
    desc.modules = &modules;
    desc.audioOutput = AudioOutput::Offline;
    desc.fixedStep = true;
    desc.seed = 7;
    Expected<std::unique_ptr<GameSession>, std::string> created = GameSession::create(desc);
    REQUIRE(created.hasValue());
    GameSession& session = **created;
    loadShowcaseScene(session.world(), files);
    session.start();
    for (int frame = 0; frame < kFrames; ++frame)
    {
        if (input)
        {
            auto& keys = session.keyboardMouse();
            keys.setKey(Key::D, frame >= 60 && frame < 120);
            keys.setKey(Key::Space, (frame >= 40 && frame < 50) || (frame >= 180 && frame < 185));
        }
        session.update(kStep);
    }
    SessionRun run;
    run.hash = stateHash(session.world());
    run.frames = statsField(session.world(), "frames");
    run.jumps = statsField(session.world(), "jumps");
    run.spawned = statsField(session.world(), "spawned");
    return run;
}

} // namespace

TEST_CASE("the showcase module calls every function of the scripting API on the showcase scene")
{
    reflectAll();
    test::TempDirectory directory;
    VirtualFileSystem files;
    mountShowcase(files);
    JobSystem jobs{JobSystemDesc{2, 1024}};
    World world; // outlives the host, as in a game
    loadShowcaseScene(world, files);

    PhysicsWorld physics(world, jobs);
    AudioWorldDesc audioDesc;
    audioDesc.output = AudioOutput::Offline;
    AudioWorld audio(world, files, audioDesc);
    InputActions actions;
    PhysicsWorldService physicsService(physics);
    AudioWorldService audioService(audio);
    InputActionsService inputService(actions);
    SceneLoaderService sceneService(world, files);
    test::CountingPhysics countingPhysics(&physicsService);
    test::CountingAudio countingAudio(&audioService);
    test::CountingInput countingInput(&inputService);
    test::CountingScenes countingScenes(&sceneService);
    ServiceSet services;
    services.provide<PhysicsService>(countingPhysics);
    services.provide<AudioService>(countingAudio);
    services.provide<InputService>(countingInput);
    services.provide<SceneService>(countingScenes);

    ModuleHostDesc hostDesc;
    hostDesc.shadowDirectory = directory.path / "shadow";
    ModuleHost modules(jobs, hostDesc);
    modules.attachWorld(world, WorldRole::Simulation, std::move(services));
    const std::optional<std::string> loadError = modules.load(utf8Path(SHOWCASE_MODULE));
    REQUIRE_MESSAGE(!loadError, loadError.value_or(""));
    for (int frame = 0; frame < kFrames; ++frame)
    {
        modules.update(world, kStep);
        physics.update(kStep);
        audio.update(kStep);
    }
    CHECK(modules.detachWorld(world)); // runs stop(), which unloads the clips

    const auto uncalled = [](std::span<const u32> calls) {
        std::string list;
        for (usize i = 0; i < calls.size(); ++i)
        {
            if (calls[i] == 0)
            {
                list += std::to_string(i) + " ";
            }
        }
        return list;
    };
    INFO("uncalled physics functions: " << uncalled(countingPhysics.calls));
    INFO("uncalled audio functions: " << uncalled(countingAudio.calls));
    INFO("uncalled input functions: " << uncalled(countingInput.calls));
    INFO("uncalled scene functions: " << uncalled(countingScenes.calls));
    CHECK(countingPhysics.everyFunctionCalled());
    CHECK(countingAudio.everyFunctionCalled());
    CHECK(countingInput.everyFunctionCalled());
    CHECK(countingScenes.everyFunctionCalled());

    // The game ran on the real services: it counted its frames, met contacts and spawned a gem and a lantern.
    CHECK(statsField(world, "frames") == kFrames);
    CHECK(statsField(world, "contacts") > 0);
    CHECK(statsField(world, "groundedFrames") > 0);
    CHECK(statsField(world, "spawned") >= 2);
}

TEST_CASE("fixed-step showcase sessions with the same input end in the same state after 300 frames")
{
    const SessionRun first = runSession(true);
    CHECK(first.frames == kFrames);
    CHECK(first.jumps == 2);
    CHECK(first.spawned >= 2);
    const SessionRun second = runSession(true);
    CHECK(second.hash == first.hash);
    const SessionRun idle = runSession(false);
    CHECK(idle.jumps == 0);
    CHECK(idle.hash != first.hash);
}
