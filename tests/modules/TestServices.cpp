// The host adapters behind the scripting API: each forwards to its engine library (ADR 0041).

#include "TestSupport.hpp"
#include "physics/PhysicsTestSupport.hpp"

#include <illusion/audio/AudioWorldService.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/InputActionsService.hpp>
#include <illusion/physics/PhysicsWorldService.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/runtime/SceneLoaderService.hpp>

#include <doctest/doctest.h>

#include <array>
#include <vector>

using namespace illusion;

TEST_CASE("PhysicsWorldService forwards to its PhysicsWorld")
{
    JobSystem jobs{JobSystemDesc{2, 256}};
    World world;
    PhysicsWorld physics(world, jobs);
    PhysicsWorldService service(physics);
    PhysicsService& api = service;
    CHECK(&service.physics() == &physics);

    api.setGravity(Vec3(0.0f, -1.0f, 0.0f));
    CHECK(physics.gravity() == Vec3(0.0f, -1.0f, 0.0f));
    api.setLayersCollide(1, 2, false);
    CHECK_FALSE(physics.layersCollide(1, 2));
    CHECK_FALSE(api.layersCollide(1, 2));

    const std::array<Vec3, 3> points{Vec3(0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)};
    const std::array<u32, 3> indices{0, 1, 2};
    const PhysicsMeshHandle mesh = api.createMesh(points, indices);
    CHECK(physics.isMeshValid(mesh));
    api.destroyMesh(mesh);
    CHECK_FALSE(api.isMeshValid(mesh));

    const Entity ground = test::createGround(world);
    const Entity a =
        world.create(test::at(Vec3(0.0f, 2.0f, 0.0f)), test::body(MotionType::Dynamic), test::sphere(0.5f));
    const Entity b =
        world.create(test::at(Vec3(3.0f, 2.0f, 0.0f)), test::body(MotionType::Dynamic), test::sphere(0.5f));
    physics.update(test::kStep);
    api.setLinearVelocity(a, Vec3(1.0f, 0.0f, 0.0f));
    CHECK(physics.linearVelocity(a) == Vec3(1.0f, 0.0f, 0.0f));
    CHECK(api.mass(a) == doctest::Approx(physics.mass(a)));

    const std::optional<RaycastHit> hit =
        api.raycast(Vec3(0.0f, 10.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 20.0f, 0xFFFFFFFFu, kNullEntity);
    REQUIRE(hit);
    CHECK(hit->entity == a);

    // overlap() writes as many entities as fit and reports how many there are.
    Collider all = test::box(Vec3(10.0f));
    std::array<Entity, 1> one{};
    CHECK(api.overlap(all, Vec3(0.0f), Quat(1.0f, 0.0f, 0.0f, 0.0f), one, 0xFFFFFFFFu, kNullEntity) == 3);
    CHECK((one[0] == a || one[0] == b || one[0] == ground));
    std::array<Entity, 4> four{};
    CHECK(api.overlap(all, Vec3(0.0f), Quat(1.0f, 0.0f, 0.0f, 0.0f), four, 0xFFFFFFFFu, ground) == 2);
}

TEST_CASE("AudioWorldService forwards to its AudioWorld")
{
    VirtualFileSystem files;
    World world;
    AudioWorldDesc desc;
    desc.output = AudioOutput::Offline;
    desc.maxVoices = 4;
    AudioWorld audio(world, files, desc);
    AudioWorldService service(audio);
    AudioService& api = service;
    CHECK(&service.audio() == &audio);

    const std::vector<f32> samples(480, 0.25f);
    const AudioClipHandle clip = api.createClip(samples, 1, audio.sampleRate());
    CHECK(api.clipState(clip) == AssetState::Ready);
    REQUIRE(api.clipInfo(clip));
    CHECK(api.clipInfo(clip)->channels == 1);
    AudioPlayParams params;
    params.loop = true;
    const AudioVoiceHandle voice = api.playOneShot(clip, params);
    CHECK(api.isVoicePlaying(voice));
    api.stopVoice(voice);
    CHECK_FALSE(audio.isPlaying(voice));

    api.setMasterVolume(0.5f);
    CHECK(audio.masterVolume() == 0.5f);
    api.setBusPaused(AudioBus::Music, true);
    CHECK(api.isBusPaused(AudioBus::Music));
    AudioReverbSettings reverb;
    reverb.roomSize = 0.75f;
    api.setReverb(reverb);
    CHECK(api.reverb().roomSize == 0.75f);
    api.unloadClip(clip);
    CHECK(api.clipState(clip) == AssetState::Failed);
    CHECK(api.loadClip("missing:/clip.wav", AudioClipMode::Decode).valid() == false);
}

TEST_CASE("InputActionsService adds actions once and forwards rebinding and state")
{
    InputActions actions;
    InputActionsService service(actions);
    InputService& api = service;
    CHECK(&service.actions() == &actions);

    const std::optional<ActionId> jump = api.addAction("Jump", ActionType::Button);
    REQUIRE(jump);
    CHECK(api.addAction("Jump", ActionType::Button) == jump); // existing action of the same type
    CHECK_FALSE(api.addAction("Jump", ActionType::Axis));     // the name is taken by another type
    CHECK(api.actionCount() == 1);
    CHECK(api.findAction("Jump") == jump);
    CHECK(api.actionName(*jump) == "Jump");
    CHECK(api.actionType(*jump) == ActionType::Button);

    const std::array<InputBinding, 1> space{InputBinding::key(Key::Space)};
    api.setBindings(*jump, space);
    REQUIRE(api.bindings(*jump).size() == 1);
    CHECK(api.bindings(*jump)[0] == space[0]);

    InputState keyboard;
    keyboard.setKey(Key::Space, true);
    actions.update(&keyboard, {});
    CHECK(api.isDown(*jump));
    CHECK(api.wasPressed(*jump));
    CHECK_FALSE(api.wasReleased(*jump));
    CHECK(api.value(*jump) == 1.0f);
    CHECK(api.lastDevice() == InputDevice::KeyboardMouse);
}

TEST_CASE("SceneLoaderService instantiates scene files with new ids and reports failures")
{
    reflectCoreComponents();
    test::TempDirectory directory;
    VirtualFileSystem files;
    REQUIRE(files.mount("game", directory.path, MountAccess::ReadWrite) == FileError::None);
    {
        World source;
        source.create(Transform{}, Name{"Crate"}, EntityGuid{1});
        source.create(Transform{}, Name{"Lamp"}, EntityGuid{2});
        const SceneSaveResult saved = saveScene(source);
        REQUIRE(saved.ok());
        REQUIRE(files.writeText("game:/level.scene", saved.json) == FileError::None);
        REQUIRE(files.writeText("game:/broken.scene", "{ not json") == FileError::None);
    }

    World world;
    SceneLoaderService service(world, files);
    SceneService& api = service;
    CHECK(api.exists("game:/level.scene"));
    CHECK_FALSE(api.exists("game:/missing.scene"));

    std::array<Entity, 1> first{};
    CHECK(api.instantiate("game:/level.scene", first) == 2); // writes as many as fit
    CHECK(api.lastError().empty());
    std::array<Entity, 4> second{};
    CHECK(api.instantiate("game:/level.scene", second) == 2); // the same file again, with new ids
    CHECK(world.aliveCount() == 4);
    CHECK(world.get<EntityGuid>(first[0]).value != world.get<EntityGuid>(second[0]).value);

    CHECK_FALSE(api.instantiate("game:/missing.scene", second));
    CHECK(api.lastError().find("missing.scene") != std::string_view::npos);
    CHECK_FALSE(api.instantiate("game:/broken.scene", second));
    CHECK(api.lastError().find("broken.scene") != std::string_view::npos);
    CHECK(world.aliveCount() == 4);
}
