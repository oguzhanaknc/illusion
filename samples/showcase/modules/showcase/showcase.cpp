// The game module of the showcase project. It plays the everything scene through the whole scripting API:
// every function of ModuleContext and of the four host services (PhysicsService, AudioService, InputService
// and SceneService) is called from here, which tools/check_api_coverage.py checks and the engine tests count
// at run time (tests/showcase/TestShowcaseModule.cpp).
//
// What it does, decided only by the frame number and the input so that fixed-step runs repeat exactly:
// - start: a floor and a gem from mesh geometry, the Jump and Move actions, a chime made from numbers, the
//   lamp hum and the music, the mixer settings;
// - update: walks the Player back and forth across the trigger pad until a key or stick moves it (then WASD
//   or the left stick walks and Space or South jumps), lights the lamp and plays a greeting while the Player
//   stands on the pad, opens and closes the Door with its hinge motor, pushes the Ball, Crate, Rock and Drum,
//   slides the drawer, lifts the platform,
//   grows the Ball, sweeps and casts rays, counts contacts and the Trigger's visitors, spawns a lantern
//   prefab, chimes where something lands hard, and switches to moon gravity for a while;
// - stop: unloads the clips.
// Everything that hot reloads must keep lives in the Components showcase.Stats (saved) and showcase.Runtime.

#include <illusion/audio/AudioService.hpp>
#include <illusion/audio/AudioTypes.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/Gamepad.hpp>
#include <illusion/input/InputActions.hpp>
#include <illusion/input/InputService.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsService.hpp>
#include <illusion/render/SceneComponents.hpp>
#include <illusion/runtime/SceneService.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <string_view>
#include <vector>

namespace showcase
{

using namespace illusion;

/// What the game has seen so far; saved with the scene, so a fixed-step run can be compared by its hash.
struct Stats
{
    u32 frames = 0;         ///< Updates so far.
    u32 contacts = 0;       ///< Contacts that began between solid bodies.
    u32 triggerEnters = 0;  ///< Bodies that entered the Trigger.
    u32 jumps = 0;          ///< Jumps of the Player.
    u32 groundedFrames = 0; ///< Updates the Player stood on something.
    u32 spawned = 0;        ///< Entities the module created while playing.
    u32 brokenJoints = 0;   ///< Joints that broke.
    u32 overlaps = 0;       ///< Bodies around the Trigger at the last overlap query.
    f32 rayDistance = 0.0f; ///< Distance from the Ball down to what is under it.
    f32 doorAngle = 0.0f;   ///< Angle of the Door's hinge.
    f32 sweep = 0.0f;       ///< How far a sphere moves forward from the Player before it hits something.
    f32 crateSpeed = 0.0f;  ///< Speed of the Crate.
};

/// Handles and entities of one play session; runtime state, not saved.
struct Runtime
{
    PhysicsMeshHandle floor{};
    PhysicsMeshHandle gem{};
    AudioClipHandle chime{};
    AudioClipHandle hum{};
    AudioClipHandle music{};
    AudioVoiceHandle humVoice{};
    ActionId jump = 0;
    ActionId move = 0;
    Entity ball{};
    Entity player{};
    Entity crate{};
    Entity rock{};
    Entity drum{};
    Entity door{};
    Entity radio{};
    Entity trigger{};
    Entity lamp{};
    Entity voice{};
    Entity drawer{};
    Entity platform{};
    InputDevice device{};
    GamepadType gamepad{};
    u32 awakeFrames = 0;
    f32 walkDirection = -1.0f; ///< -1 away from the camera, +1 towards it.
    bool manual = false;       ///< A key or stick moved the Player: it no longer walks by itself.
    bool onPad = false;        ///< The Player stands on the trigger pad.
    bool humPlaying = false;
    bool radioPlaying = false;
    bool uiPaused = false;
    bool toneReady = false;
};

} // namespace showcase

namespace
{

using namespace illusion;
using showcase::Runtime;
using showcase::Stats;

constexpr u32 kAllLayers = 0xFFFFFFFFu;
constexpr f32 kFloorHalfSize = 10.0f; // the Ground plane of the scene, 20 m wide
constexpr u32 kChimeRate = 22050;
constexpr u32 kChimeFrames = kChimeRate / 4;
constexpr std::string_view kHum = "assets:/audio/hum.wav";
constexpr std::string_view kMusic = "assets:/audio/music.wav";
constexpr f32 kWalkSpeed = 2.0f;      // m/s of the Player when it walks by itself
constexpr f32 kPathNear = 6.0f;       // z of the end of its path nearest the camera
constexpr f32 kPathFar = -0.5f;       // z of the far end, past the trigger pad
constexpr f32 kLampIntensity = 25.0f; // the lamp over the pad, off the pad and on it
constexpr f32 kLampLit = 90.0f;
constexpr u32 kDrawerPeriod = 90;    // frames between pushes of the drawer
constexpr u32 kPlatformPeriod = 120; // frames of each rise and fall of the platform
constexpr f32 kPlatformLow = 0.2f;   // its height at the bottom
constexpr u32 kDoorPeriod = 240;     // frames between the door opening and closing
constexpr std::string_view kLantern = "assets:/prefabs/lantern.prefab";

Quat identity() noexcept
{
    return Quat(1.0f, 0.0f, 0.0f, 0.0f);
}

// The entity with the name @p name, if any.
Entity findNamed(World& world, std::string_view name)
{
    Entity found{};
    world.query<const Name>().each([&](Entity entity, const Name& entityName) {
        if (entityName.value == name)
        {
            found = entity;
        }
    });
    return found;
}

bool loadModule(ModuleContext& context)
{
    reflect<Stats>("showcase.Stats")
        .meta({.displayName = "Showcase stats",
               .icon = "monitoring",
               .description = "What the showcase game saw."})
        .field("frames", &Stats::frames)
        .field("contacts", &Stats::contacts)
        .field("triggerEnters", &Stats::triggerEnters)
        .field("jumps", &Stats::jumps)
        .field("groundedFrames", &Stats::groundedFrames)
        .field("spawned", &Stats::spawned)
        .field("brokenJoints", &Stats::brokenJoints)
        .field("overlaps", &Stats::overlaps)
        .field("rayDistance", &Stats::rayDistance, {.unit = Unit::Meters})
        .field("doorAngle", &Stats::doorAngle, {.unit = Unit::Radians})
        .field("sweep", &Stats::sweep, {.unit = Unit::Meters})
        .field("crateSpeed", &Stats::crateSpeed, {.unit = Unit::MetersPerSecond});
    reflectRuntime<Runtime>("showcase.Runtime", {.displayName = "Showcase runtime"});
    ILL_LOG_INFO("Showcase", "module '{}' {}", context.moduleName(),
                 context.isReloading() ? "reloaded" : "loaded");
    return true;
}

// A 0.25 s bell tone, computed on the Job System.
std::vector<f32> chimeSamples(JobSystem& jobs)
{
    std::vector<f32> samples(kChimeFrames);
    f32* out = samples.data();
    jobs.parallelFor(kChimeFrames, 1024, [out](u32 begin, u32 end) {
        for (u32 i = begin; i < end; ++i)
        {
            const f32 t = static_cast<f32>(i) / static_cast<f32>(kChimeRate);
            const f32 envelope = std::exp(-12.0f * t);
            out[i] = 0.3f * envelope * std::sin(2.0f * std::numbers::pi_v<f32> * 880.0f * t);
        }
    });
    return samples;
}

void startPhysics(World& world, PhysicsService& physics, Runtime& runtime)
{
    physics.setGravity(Vec3(0.0f, -9.81f, 0.0f));
    physics.setLayersCollide(1, 2, true); // the Crate (layer 1) and the Player (layer 2)

    // The floor under the Ground plane: two triangles.
    const std::array<Vec3, 4> corners{
        Vec3(-kFloorHalfSize, 0.0f, -kFloorHalfSize), Vec3(kFloorHalfSize, 0.0f, -kFloorHalfSize),
        Vec3(kFloorHalfSize, 0.0f, kFloorHalfSize), Vec3(-kFloorHalfSize, 0.0f, kFloorHalfSize)};
    const std::array<u32, 6> triangles{0, 2, 1, 0, 3, 2};
    runtime.floor = physics.createMesh(corners, triangles);
    Collider floor;
    floor.shape = ColliderShape::Mesh;
    floor.geometry = runtime.floor;
    world.create(Transform{}, RigidBody{.motionType = MotionType::Static}, floor, Name{"Floor"});

    // An octahedron gem, dropped at frame 20.
    constexpr f32 r = 0.3f;
    const std::array<Vec3, 6> gem{Vec3(r, 0.0f, 0.0f),  Vec3(-r, 0.0f, 0.0f), Vec3(0.0f, r, 0.0f),
                                  Vec3(0.0f, -r, 0.0f), Vec3(0.0f, 0.0f, r),  Vec3(0.0f, 0.0f, -r)};
    runtime.gem = physics.createConvexHull(gem);

    // Geometry that is not needed any more is destroyed.
    const PhysicsMeshHandle scratch = physics.createConvexHull(std::span(gem).first(4));
    const bool created = physics.isMeshValid(scratch);
    physics.destroyMesh(scratch);
    if (!created || physics.isMeshValid(scratch))
    {
        ILL_LOG_WARN("Showcase", "the scratch hull was not created, or outlived destroyMesh()");
    }
}

void startInput(InputService& input, Runtime& runtime)
{
    runtime.jump = input.addAction("Jump", ActionType::Button).value_or(input.findAction("Jump").value_or(0));
    runtime.move =
        input.addAction("Move", ActionType::Vector2).value_or(input.findAction("Move").value_or(0));
    if (input.bindings(runtime.jump).empty())
    {
        const std::array<InputBinding, 2> jump{InputBinding::key(Key::Space),
                                               InputBinding::gamepadButton(GamepadButton::South)};
        input.setBindings(runtime.jump, jump);
    }
    if (input.bindings(runtime.move).empty())
    {
        const std::array<InputBinding, 2> move{InputBinding::keyVector(Key::W, Key::S, Key::A, Key::D),
                                               InputBinding::gamepadStick(GamepadStick::Left)};
        input.setBindings(runtime.move, move);
    }
    for (ActionId action = 0; action < input.actionCount(); ++action)
    {
        ILL_LOG_INFO("Showcase", "action {} ({}) has {} bindings", input.actionName(action),
                     input.actionType(action) == ActionType::Button ? "button" : "value",
                     input.bindings(action).size());
    }
}

void startAudio(JobSystem& jobs, AudioService& audio, Runtime& runtime)
{
    const std::vector<f32> chime = chimeSamples(jobs);
    runtime.chime = audio.createClip(chime, 1, kChimeRate);
    runtime.hum = audio.loadClip(kHum, AudioClipMode::Decode);
    runtime.music = audio.requestClip(kMusic, AudioClipMode::Stream);
    if (const std::optional<AudioClipInfo> info = audio.clipInfo(runtime.chime))
    {
        ILL_LOG_INFO("Showcase", "chime: {} frames, {} bytes", info->frames, info->memoryBytes);
    }
    audio.setMasterVolume(0.9f);
    audio.setBusVolume(AudioBus::Music, 0.6f * audio.busVolume(AudioBus::Music));
    audio.setBusFilter(AudioBus::Ambience, AudioBusFilter{.lowPassCutoff = 3000.0f});
    audio.setReverb(AudioReverbSettings{.roomSize = 0.7f, .damping = 0.3f, .width = 1.0f, .volume = 0.4f});
    ILL_LOG_INFO("Showcase", "mixer: master {}, ambience low-pass {} Hz, reverb room {}",
                 audio.masterVolume(), audio.busFilter(AudioBus::Ambience).lowPassCutoff,
                 audio.reverb().roomSize);
}

void startScenes(SceneService& scenes)
{
    std::array<Entity, 1> none{};
    if (!scenes.instantiate("assets:/prefabs/missing.prefab", none))
    {
        ILL_LOG_INFO("Showcase", "no missing prefab: {}", scenes.lastError());
    }
    if (!scenes.exists(kLantern))
    {
        ILL_LOG_WARN("Showcase", "{} is missing; no lantern will be spawned", kLantern);
    }
}

void startModule(ModuleContext& context)
{
    if (!context.hasWorld() || context.worldRole() != WorldRole::Simulation)
    {
        return;
    }
    World& world = context.world();
    Runtime runtime;
    runtime.ball = findNamed(world, "Ball");
    runtime.player = findNamed(world, "Player");
    runtime.crate = findNamed(world, "Crate");
    runtime.rock = findNamed(world, "Rock");
    runtime.drum = findNamed(world, "Drum");
    runtime.door = findNamed(world, "Door");
    runtime.radio = findNamed(world, "Radio");
    runtime.trigger = findNamed(world, "Trigger");
    runtime.lamp = findNamed(world, "Lamp");
    runtime.voice = findNamed(world, "Voice");
    runtime.drawer = findNamed(world, "Drawer");
    runtime.platform = findNamed(world, "Platform");
    if (PhysicsService* physics = context.service<PhysicsService>())
    {
        startPhysics(world, *physics, runtime);
    }
    if (InputService* input = context.service<InputService>())
    {
        startInput(*input, runtime);
    }
    if (AudioService* audio = context.service<AudioService>())
    {
        startAudio(context.jobs(), *audio, runtime);
    }
    if (SceneService* scenes = context.service<SceneService>())
    {
        startScenes(*scenes);
    }
    world.create(Name{"Showcase"}, Stats{}, runtime);
}

// Walks the Player by itself until Move or Jump is used, then with Move, and jumps with Jump.
void updatePlayer(PhysicsService& physics, const InputService& input, const Vec3& position, Runtime& runtime,
                  Stats& stats, f32 dt)
{
    runtime.device = input.lastDevice();
    runtime.gamepad = input.lastGamepadType();
    const Vec2 move = input.vector(runtime.move);
    const bool pressed = input.wasPressed(runtime.jump);
    const bool held = input.isDown(runtime.jump);
    const f32 strength = input.value(runtime.jump);
    runtime.manual = runtime.manual || pressed || glm::length(move) > 0.0f;
    Vec3 velocity = physics.characterVelocity(runtime.player);
    if (runtime.manual)
    {
        velocity.x = 3.0f * move.x;
        velocity.z = -3.0f * move.y;
    }
    else
    {
        // Back and forth across the trigger pad, turning at the ends of the path.
        if ((runtime.walkDirection < 0.0f && position.z <= kPathFar) ||
            (runtime.walkDirection > 0.0f && position.z >= kPathNear))
        {
            runtime.walkDirection = -runtime.walkDirection;
        }
        velocity.x = 0.0f;
        velocity.z = kWalkSpeed * runtime.walkDirection;
    }
    if (physics.isCharacterGrounded(runtime.player))
    {
        ++stats.groundedFrames;
        velocity.y = 0.0f;
        if (pressed && held && strength > 0.5f)
        {
            velocity.y = 5.0f;
            ++stats.jumps;
        }
    }
    else
    {
        velocity.y += physics.gravity().y * dt;
    }
    if (input.wasReleased(runtime.jump) && velocity.y > 0.0f)
    {
        velocity.y *= 0.5f; // a short hop when Jump is let go early
    }
    physics.setCharacterVelocity(runtime.player, velocity);
}

// Pushes the bodies of the scene at fixed frames.
void updateBodies(World& world, PhysicsService& physics, Runtime& runtime, Stats& stats)
{
    const u32 frame = stats.frames;
    if (frame == 30)
    {
        physics.addImpulse(runtime.ball, physics.mass(runtime.ball) * Vec3(-2.0f, 3.0f, 0.0f));
    }
    if (frame == 45)
    {
        physics.addTorque(runtime.rock, Vec3(0.0f, 40.0f, 0.0f));
    }
    if (frame == 50)
    {
        physics.setAngularVelocity(runtime.drum,
                                   Vec3(0.0f, 0.0f, 3.0f) + physics.angularVelocity(runtime.drum));
    }
    if (frame >= 60 && frame < 90)
    {
        physics.addForce(runtime.crate, Vec3(0.0f, 0.0f, -30.0f));
    }
    if (frame == 90 && world.has<Transform>(runtime.ball))
    {
        // A bigger ball from here on: the scale grows its collider and its mesh alike.
        world.get<Transform>(runtime.ball).scale = Vec3(1.5f);
        physics.rebuild(runtime.ball);
    }
    if (frame % kDrawerPeriod == 0 && world.isAlive(runtime.drawer))
    {
        // Out from under the table and back in again.
        const f32 out = (frame / kDrawerPeriod) % 2 == 0 ? 1.0f : -1.0f;
        physics.addImpulse(runtime.drawer, physics.mass(runtime.drawer) * Vec3(0.0f, 0.0f, 0.9f * out));
    }
    if (Transform* platform = world.tryGet<Transform>(runtime.platform))
    {
        // A kinematic body follows its Transform: up 0.8 m and down again, smoothly.
        const f32 phase = static_cast<f32>(frame % (2 * kPlatformPeriod)) / static_cast<f32>(kPlatformPeriod);
        platform->position.y = kPlatformLow + 0.4f * (1.0f - std::cos(std::numbers::pi_v<f32> * phase));
    }
    if (frame > 0 && frame % kDoorPeriod == 0)
    {
        // The hinge motor opens the door to its limit, then closes it: game code may change motorSpeed any
        // time.
        if (Joint* hinge = world.tryGet<Joint>(runtime.door))
        {
            hinge->motorSpeed = -hinge->motorSpeed;
        }
    }
    if (frame == 200)
    {
        physics.setGravity(Vec3(0.0f, -1.62f, 0.0f)); // moon gravity: the pendulum and the rope slow down
    }
    if (frame == 260)
    {
        physics.setGravity(Vec3(0.0f, -9.81f, 0.0f));
    }
    if (frame == 240 && physics.layersCollide(1, 2))
    {
        physics.setLayersCollide(1, 2, false); // from here on the Player walks through the Crate
    }
    if (physics.isAwake(runtime.crate))
    {
        ++runtime.awakeFrames;
    }
    stats.crateSpeed = glm::length(physics.linearVelocity(runtime.crate));
    stats.doorAngle = physics.jointPosition(runtime.door);
    stats.brokenJoints += static_cast<u32>(physics.brokenJoints().size());
}

// Rays, sweeps and overlaps.
void updateQueries(World& world, const PhysicsService& physics, const Runtime& runtime, Stats& stats)
{
    if (const Transform* ball = world.tryGet<Transform>(runtime.ball))
    {
        const std::optional<RaycastHit> hit =
            physics.raycast(ball->position, Vec3(0.0f, -1.0f, 0.0f), 50.0f, kAllLayers, runtime.ball);
        stats.rayDistance = hit ? hit->distance : -1.0f;
    }
    if (const Transform* player = world.tryGet<Transform>(runtime.player))
    {
        Collider probe;
        probe.shape = ColliderShape::Sphere;
        probe.radius = 0.25f;
        const std::optional<ShapeCastHit> hit =
            physics.shapeCast(probe, player->position + Vec3(0.0f, 0.9f, 0.0f), identity(),
                              Vec3(0.0f, 0.0f, -1.0f), 20.0f, kAllLayers, runtime.player);
        stats.sweep = hit ? hit->distance : -1.0f;
    }
    if (stats.frames % 30 == 0)
    {
        if (const Transform* trigger = world.tryGet<Transform>(runtime.trigger))
        {
            Collider area;
            area.halfExtents = Vec3(2.0f);
            std::array<Entity, 16> around{};
            stats.overlaps =
                physics.overlap(area, trigger->position, identity(), around, kAllLayers, runtime.trigger);
        }
    }
}

// Counts contacts and plays a chime where something lands hard.
void updateContacts(const PhysicsService& physics, AudioService* audio, const Runtime& runtime, Stats& stats)
{
    bool chimed = false;
    for (const ContactEvent& contact : physics.contactEvents())
    {
        if (contact.type != ContactEventType::Begin)
        {
            continue;
        }
        if (contact.sensor)
        {
            if (contact.a == runtime.trigger || contact.b == runtime.trigger)
            {
                ++stats.triggerEnters;
            }
            continue;
        }
        ++stats.contacts;
        if (audio != nullptr && !chimed && contact.speed > 1.0f)
        {
            AudioPlayParams params;
            params.volume = std::min(1.0f, contact.speed / 10.0f);
            params.spatial = true;
            params.position = contact.point;
            static_cast<void>(audio->playOneShot(runtime.chime, params));
            chimed = true;
        }
    }
}

// Lights the lamp over the trigger pad and greets the Player while it stands there.
void updatePad(World& world, AudioService* audio, const Vec3& player, Runtime& runtime)
{
    const Transform* pad = world.tryGet<Transform>(runtime.trigger);
    const Collider* area = world.tryGet<Collider>(runtime.trigger);
    if (pad == nullptr || area == nullptr)
    {
        return;
    }
    const Vec3 offset = glm::abs(player - pad->position);
    const bool onPad = offset.x <= area->halfExtents.x && offset.z <= area->halfExtents.z;
    if (onPad != runtime.onPad)
    {
        runtime.onPad = onPad;
        if (PointLight* light = world.tryGet<PointLight>(runtime.lamp))
        {
            light->intensity = onPad ? kLampLit : kLampIntensity;
        }
        if (onPad && audio != nullptr)
        {
            audio->play(runtime.voice);
        }
    }
}

void updateAudio(AudioService& audio, Runtime& runtime, u32 frame)
{
    runtime.toneReady = runtime.toneReady || audio.clipState(runtime.music) == AssetState::Ready;
    if (frame == 10)
    {
        AudioPlayParams params;
        params.bus = AudioBus::Music;
        params.loop = true;
        params.volume = 0.15f;
        runtime.humVoice = audio.playOneShot(runtime.hum, params); // the lamp hums until frame 200
    }
    if (frame == 30)
    {
        audio.play(runtime.radio);
    }
    if (frame == 100)
    {
        audio.setBusPaused(AudioBus::Ui, true);
    }
    if (frame == 160)
    {
        audio.setBusPaused(AudioBus::Ui, false);
    }
    if (frame == 200)
    {
        audio.stopVoice(runtime.humVoice);
    }
    if (frame == 270)
    {
        audio.stop(runtime.radio);
    }
    runtime.uiPaused = audio.isBusPaused(AudioBus::Ui);
    runtime.humPlaying = audio.isVoicePlaying(runtime.humVoice);
    runtime.radioPlaying = audio.isPlaying(runtime.radio);
}

// Spawns what the frame calls for. These are structural changes, so they come before any reference into
// the World is taken.
u32 spawn(World& world, PhysicsService* physics, SceneService* scenes, const Runtime& runtime, u32 frame)
{
    u32 spawned = 0;
    if (frame == 20 && physics != nullptr)
    {
        Collider gem;
        gem.shape = ColliderShape::ConvexHull;
        gem.geometry = runtime.gem;
        Transform at;
        at.position = Vec3(-1.5f, 3.0f, -1.0f);
        const Entity created = world.create(at, RigidBody{}, gem, Name{"Gem"});
        physics->setLinearVelocity(created, Vec3(-1.0f, 0.0f, 0.0f));
        ++spawned;
    }
    if (frame == 120 && scenes != nullptr)
    {
        std::array<Entity, 8> created{};
        spawned += scenes->instantiate(kLantern, created).value_or(0);
    }
    return spawned;
}

void updateModule(ModuleContext& context, f64 deltaSeconds)
{
    World& world = context.world();
    PhysicsService* physics = context.service<PhysicsService>();
    InputService* input = context.service<InputService>();
    AudioService* audio = context.service<AudioService>();
    SceneService* scenes = context.service<SceneService>();
    Entity self{};
    world.query<const Stats>().each([&self](Entity entity, const Stats&) { self = entity; });
    if (!world.isAlive(self))
    {
        return;
    }
    const u32 spawned =
        spawn(world, physics, scenes, world.get<Runtime>(self), world.get<Stats>(self).frames);
    Stats& stats = world.get<Stats>(self);
    Runtime& runtime = world.get<Runtime>(self);
    stats.spawned += spawned;
    if (physics != nullptr)
    {
        const Transform* player = world.tryGet<Transform>(runtime.player);
        const Vec3 position = player != nullptr ? player->position : Vec3(0.0f);
        if (input != nullptr && player != nullptr)
        {
            updatePlayer(*physics, *input, position, runtime, stats, static_cast<f32>(deltaSeconds));
        }
        updatePad(world, audio, position, runtime);
        updateBodies(world, *physics, runtime, stats);
        updateQueries(world, *physics, runtime, stats);
        updateContacts(*physics, audio, runtime, stats);
    }
    if (audio != nullptr)
    {
        updateAudio(*audio, runtime, stats.frames);
    }
    ++stats.frames;
}

void stopModule(ModuleContext& context)
{
    AudioService* audio = context.service<AudioService>();
    if (audio == nullptr)
    {
        return;
    }
    context.world().query<const Runtime>().each([audio](const Runtime& runtime) {
        audio->unloadClip(runtime.chime);
        audio->unloadClip(runtime.hum);
        audio->unloadClip(runtime.music);
    });
}

constexpr ModuleApi kShowcase =
    makeModuleApi("showcase", "1.0.0",
                  {.load = &loadModule, .start = &startModule, .stop = &stopModule, .update = &updateModule});

} // namespace

ILL_MODULE(showcase, kShowcase)
