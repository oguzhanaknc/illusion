// The game module of the showcase project. It plays the everything scene through the whole scripting API:
// every function of ModuleContext and of the nine host services (PhysicsService, AudioService, InputService,
// ParticleService, SceneService, StoreService, UiService, SaveService and AnimationService) is called from
// here, which
// tools/check_api_coverage.py checks and the engine tests count at run time
// (tests/showcase/TestShowcaseModule.cpp).
//
// What it does, decided only by the frame number and the input so that fixed-step runs repeat exactly:
// - start: a floor and a gem from mesh geometry, the Jump and Move actions, a chime made from numbers, the
//   lamp hum and the music, the mixer settings;
// - update: walks the Player back and forth across the trigger pad until a key or stick moves it (then WASD
//   or the left stick walks and Space or South jumps), lights the lamp and plays a greeting while the Player
//   stands on the pad, opens and closes the Door with its hinge motor, pushes the Ball, Crate, Rock and Drum,
//   slides the drawer, lifts the platform,
//   grows the Ball, sweeps and casts rays, counts contacts and the Trigger's visitors, spawns a lantern
//   prefab, chimes where something lands hard and raises dust the first time, bursts sparks when the Player
//   steps on the pad, stops and restarts the spark fountain, switches to moon gravity for a while, unlocks
//   achievements, keeps stats and saves the number of runs as a cloud file of the store (or of no store);
//   writes, removes and then loads a saved-game slot, so the load always fails and the World stays as it is
//   (the events and the slot list may differ between runs, so they only go into showcase.Runtime);
//   counts its first 120 frames on the HUD (a string table message with a plural), hides and shows the
//   palette, focuses the Go button for a while, reads the toggle and the slider, counts clicks, and ends the
//   game when `ui.cancel` (Escape, or East on a gamepad) is pressed; plays the Column's animation graph,
//   leaning it with a parameter, bowing it with a trigger and calming it again, and counts its "peak"
//   events;
// - stop: unloads the clips.
// Everything that hot reloads must keep lives in the Components showcase.Stats (saved) and showcase.Runtime.

#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/animation/AnimationService.hpp>
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
#include <illusion/particles/ParticleService.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsService.hpp>
#include <illusion/render/SceneComponents.hpp>
#include <illusion/runtime/SceneService.hpp>
#include <illusion/save/SaveService.hpp>
#include <illusion/store/StoreService.hpp>
#include <illusion/ui/UiService.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
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
    Entity sparks{};
    Entity dust{};
    Entity hud{};
    Entity hudFrames{};
    Entity hudScore{};
    Entity hudPalette{};
    Entity hudButton{};
    Entity hudToggle{};
    Entity hudSlider{};
    Entity column{};
    u32 animationEvents = 0;     ///< "peak" events of the Column's graph seen so far.
    f32 columnTime = 0.0f;       ///< Normalized time of the Column's state at the last update.
    f32 columnLean = 0.0f;       ///< The Column's "lean" parameter as the graph used it.
    bool columnSwings = false;   ///< The Column's graph is in its "swing" state.
    u32 uiEvents = 0;            ///< UI events seen so far.
    u32 uiClicks = 0;            ///< Clicks of the Go button.
    f32 sliderValue = 0.0f;      ///< The HUD slider at the last update.
    f32 buttonWidth = 0.0f;      ///< Width of the Go button at frame 100, in canvas units.
    f32 buttonTransition = 0.0f; ///< Progress of the Go button's transition at frame 225.
    bool toggleOn = false;       ///< The HUD toggle at the last update.
    bool paletteShown = true;    ///< The palette is visible.
    bool buttonFocused = false;  ///< The Go button had the focus at frame 200.
    u32 particlesAlive = 0;      ///< Live particles of every emitter at the last update.
    u32 runs = 0;                ///< Sessions played, from the store's cloud save.
    u32 walkerProgress = 0;      ///< Progress of the walker achievement at frame 100.
    u32 cloudFiles = 0;          ///< Cloud files at frame 280.
    u64 cloudFree = 0;           ///< Free cloud storage at frame 280, in bytes.
    i32 frameStat = 0;           ///< The frames stat read back at frame 200.
    f32 crateStat = 0.0f;        ///< The crate speed stat read back at frame 200.
    bool overlay = false;        ///< The store's overlay covers the game.
    u32 saveEvents = 0;          ///< Saved-game events seen so far.
    u32 saveFailures = 0;        ///< Failed saved-game requests seen so far.
    u32 saveSlots = 0;           ///< Saved-game slots at frame 290.
    bool savesBusy = false;      ///< A saved-game request was pending at frame 290.
    bool started = false;        ///< The started achievement was unlocked at frame 100.
    bool dustRaised = false;     ///< The first hard landing played the dust.
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
constexpr u32 kPadBurst = 40;        // sparks emitted when the Player steps on the pad
constexpr std::string_view kLantern = "assets:/prefabs/lantern.prefab";
constexpr std::string_view kSave = "showcase.save"; // cloud file with the number of runs
constexpr std::string_view kScratch = "showcase.tmp";
constexpr std::string_view kSaveSlot = "showcase"; // saved-game slot, removed again in the same frame
constexpr std::string_view kStartedAchievement = "SHOWCASE_STARTED";
constexpr std::string_view kWalkerAchievement = "SHOWCASE_WALKER";
constexpr u32 kWalkerSteps = 4;     // the walker achievement unlocks after 4 x 60 frames
constexpr u32 kCountedFrames = 120; // the HUD counts the frames up to this one
constexpr std::string_view kFramesKey = "showcase.hud.frames";
constexpr std::string_view kLeanClip = "assets:/models/column.gltf#animation/lean";
constexpr u32 kBowFrame = 30;   // the Column bows (a trigger of its graph)
constexpr u32 kCalmFrame = 150; // and returns to its first state

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

// Tells who plays in which store and counts this run in the cloud save.
void startStore(StoreService& store, Runtime& runtime)
{
    ILL_LOG_INFO("Showcase", "store '{}' ({}), user '{}' ({})", store.storeName(),
                 store.available() ? "signed in" : "offline", store.userName(), store.userId());
    std::array<std::byte, sizeof(u32)> save{};
    const Expected<u64, StoreError> read = store.readCloudFile(kSave, save);
    if (read && *read == save.size())
    {
        std::memcpy(&runtime.runs, save.data(), save.size());
    }
    ++runtime.runs;
    const GamepadOrigin jump{.type = runtime.gamepad, .button = GamepadButton::South};
    if (const std::optional<std::string_view> glyph = store.glyphPath(jump))
    {
        ILL_LOG_INFO("Showcase", "Jump is drawn by {}", *glyph);
    }
}

// Finds the HUD's elements and sets their starting values (the ones the scene already has, so nothing jumps).
void startUi(UiService& ui, Runtime& runtime)
{
    runtime.hud = ui.find({}, "HUD");
    runtime.hudFrames = ui.find(runtime.hud, "Line");
    runtime.hudScore = ui.find(runtime.hud, "Score");
    runtime.hudPalette = ui.find(runtime.hud, "Palette");
    runtime.hudButton = ui.find(runtime.hud, "Button");
    runtime.hudToggle = ui.find(runtime.hud, "Toggle");
    runtime.hudSlider = ui.find(runtime.hud, "Slider");
    static_cast<void>(ui.setTextKey(ui.find(runtime.hud, "Title"), "showcase.hud.title"));
    static_cast<void>(ui.setText(runtime.hudScore, "Score 0120"));
    static_cast<void>(ui.setInteractable(ui.find(runtime.hud, "Locked"), false));
    static_cast<void>(ui.setToggle(runtime.hudToggle, true));
    static_cast<void>(ui.setSliderValue(runtime.hudSlider, 3.5f));
    if (!ui.setLanguage("zz"))
    {
        ILL_LOG_INFO("Showcase", "no locale file for 'zz'; the HUD stays in '{}'", ui.language());
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
    runtime.sparks = findNamed(world, "Sparks");
    runtime.dust = findNamed(world, "Dust");
    runtime.column = findNamed(world, "Column");
    if (Animator* animator = world.tryGet<Animator>(runtime.column))
    {
        animator->playing = true; // the scene holds it still for the editor
    }
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
    if (StoreService* store = context.service<StoreService>())
    {
        startStore(*store, runtime);
    }
    if (UiService* ui = context.service<UiService>())
    {
        startUi(*ui, runtime);
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
void updateContacts(const PhysicsService& physics, AudioService* audio, ParticleService* particles,
                    Runtime& runtime, Stats& stats)
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
        if (particles != nullptr && !runtime.dustRaised && contact.speed > 1.0f)
        {
            runtime.dustRaised = particles->play(runtime.dust); // a cloud by the blocks, once
        }
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
void updatePad(World& world, AudioService* audio, ParticleService* particles, const Vec3& player,
               Runtime& runtime)
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
        if (onPad && particles != nullptr)
        {
            static_cast<void>(particles->emit(runtime.sparks, kPadBurst)); // a shower of sparks to greet it
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

// The spark fountain stops at frame 270 and starts again at 290; the dust is cleared at 285.
void updateParticles(ParticleService& particles, Runtime& runtime, u32 frame)
{
    if (frame == 270 && particles.isPlaying(runtime.sparks))
    {
        particles.stop(runtime.sparks);
    }
    if (frame == 285 && particles.aliveCount(runtime.dust) > 0)
    {
        particles.clear(runtime.dust);
    }
    if (frame == 290)
    {
        particles.play(runtime.sparks);
    }
    runtime.particlesAlive = static_cast<u32>(std::min<u64>(particles.totalAlive(), ~0u));
}

// Achievements on frames 10 and every 60th, stats at 200, the cloud save at 280; the overlay state every
// frame.
void updateStore(StoreService& store, Runtime& runtime, const Stats& stats, u32 frame)
{
    runtime.overlay = store.overlayActive();
    if (frame == 10)
    {
        static_cast<void>(store.unlockAchievement(kStartedAchievement));
    }
    if (frame % 60 == 0 && frame / 60 <= kWalkerSteps)
    {
        static_cast<void>(store.setAchievementProgress(kWalkerAchievement, {frame / 60, kWalkerSteps}));
    }
    if (frame == 30 && !store.available())
    {
        // Only without a store: a showcase run on Steam must not cover the game with the overlay.
        static_cast<void>(store.openOverlay(StoreOverlayPage::Achievements));
    }
    if (frame == 100)
    {
        runtime.started = store.isAchievementUnlocked(kStartedAchievement).valueOr(false);
        runtime.walkerProgress =
            store.achievementProgress(kWalkerAchievement).valueOr(AchievementProgress{}).current;
    }
    if (frame == 200)
    {
        static_cast<void>(store.setStatInt("showcase_frames", static_cast<i32>(stats.frames)));
        static_cast<void>(store.setStatFloat("showcase_crate_speed", stats.crateSpeed));
        runtime.frameStat = store.statInt("showcase_frames").valueOr(0);
        runtime.crateStat = store.statFloat("showcase_crate_speed").valueOr(0.0f);
        static_cast<void>(store.storeStats());
    }
    if (frame == 250)
    {
        static_cast<void>(store.clearAchievement(kStartedAchievement));
    }
    if (frame == 280)
    {
        std::array<std::byte, sizeof(u32)> save{};
        std::memcpy(save.data(), &runtime.runs, save.size());
        static_cast<void>(store.writeCloudFile(kSave, save));
        static_cast<void>(store.writeCloudFile(kScratch, save));
        if (store.cloudFileExists(kScratch))
        {
            static_cast<void>(store.removeCloudFile(kScratch));
        }
        std::array<std::string_view, 8> names{};
        runtime.cloudFiles = store.listCloudFiles(names);
        runtime.cloudFree = store.cloudQuota().valueOr(StoreQuota{}).availableBytes;
    }
}

// Writes the slot at frame 240, then removes it and loads it: requests run in call order, so the load finds
// nothing and fails with NotFound whatever the save directory held, and the World is never replaced. The
// events and the slot list depend on the files and on the save thread's timing, so they are only counted.
void updateSaves(SaveService& saves, Runtime& runtime, const Stats& stats, u32 frame)
{
    std::array<SaveEvent, 8> events{};
    const u32 count = saves.pollEvents(events);
    for (u32 i = 0; i < count && i < events.size(); ++i)
    {
        ++runtime.saveEvents;
        if (events[i].type == SaveEventType::Failed)
        {
            ++runtime.saveFailures;
            ILL_LOG_INFO("Showcase", "slot '{}': {}", events[i].slot, saveErrorName(events[i].error));
        }
    }
    if (frame == 240 && saves.available())
    {
        const SaveMetadata metadata{.title = "Showcase",
                                    .playSeconds = static_cast<f64>(stats.frames) / 60.0,
                                    .gameVersion = 1,
                                    .custom = "assets:/scenes/everything.scene"};
        static_cast<void>(saves.save(kSaveSlot, metadata));
        static_cast<void>(saves.remove(kSaveSlot));
        static_cast<void>(saves.load(kSaveSlot));
    }
    if (frame == 290)
    {
        runtime.savesBusy = saves.busy();
        std::array<SaveSlotInfo, 8> slots{};
        runtime.saveSlots = saves.slots(slots);
        if (const std::optional<SaveSlotInfo> slot = saves.slot(kSaveSlot))
        {
            ILL_LOG_INFO("Showcase", "slot '{}' is still listed ({})", slot->name,
                         saveSlotStateName(slot->state));
        }
    }
}

// The Column's animation graph: a lean that follows the frame, a bow and a return, and its events.
void updateAnimation(AnimationService& animation, Runtime& runtime, u32 frame)
{
    std::array<AnimationEvent, 8> events{};
    const u32 count = animation.pollEvents(events);
    for (u32 i = 0; i < count && i < events.size(); ++i)
    {
        if (events[i].entity == runtime.column && events[i].name == "peak")
        {
            ++runtime.animationEvents;
        }
    }
    const Entity column = runtime.column;
    if (frame == 0)
    {
        // A graph controls the Column, so it cannot be told to play a clip.
        const Expected<void, AnimationError> played = animation.play(column, kLeanClip, 0.25f);
        if (!played)
        {
            ILL_LOG_INFO("Showcase", "Column: {}", animationErrorName(played.error()));
        }
    }
    const f32 lean = 0.5f + 0.5f * std::sin(0.05f * static_cast<f32>(frame));
    static_cast<void>(animation.setFloat(column, "lean", lean));
    if (frame == kBowFrame)
    {
        static_cast<void>(animation.setTrigger(column, "bow"));
        static_cast<void>(animation.setInt(column, "mood", 2));
    }
    static_cast<void>(animation.setBool(column, "calm", frame >= kCalmFrame));
    runtime.columnLean = animation.parameter(column, "lean").valueOr(0.0f);
    runtime.columnTime = animation.normalizedTime(column, "base").valueOr(0.0f);
    runtime.columnSwings = animation.currentState(column, "base").valueOr("") == "swing";
}

// The HUD: the frame counter, the palette, the focus, the values and the events of the last UI update.
void updateUi(UiService& ui, SceneService* scenes, Runtime& runtime, u32 frame)
{
    if (frame <= kCountedFrames)
    {
        const std::array<UiTextArgument, 1> frames{UiTextArgument::number("frames", static_cast<f64>(frame))};
        static_cast<void>(ui.setTextKey(runtime.hudFrames, kFramesKey, frames));
    }
    if (frame == 100)
    {
        ui.invalidateLayout();
        runtime.buttonWidth = ui.rect(runtime.hudButton).value_or(UiRect{}).size().x;
    }
    if (frame == 150 || frame == 160)
    {
        static_cast<void>(ui.setVisible(runtime.hudPalette, frame == 160));
    }
    if (frame == 200)
    {
        runtime.buttonFocused = ui.setFocus(runtime.hudButton) && ui.focused() == runtime.hudButton;
    }
    if (frame == 210)
    {
        static_cast<void>(ui.setFocus({}));
    }
    // The Go button's transition plays to its changed state and back, at rest again before frame 300.
    if (frame == 215 || frame == 225)
    {
        runtime.buttonTransition = ui.transitionProgress(runtime.hudButton).value_or(0.0f);
        static_cast<void>(ui.playTransition(runtime.hudButton, frame == 215));
    }
    runtime.paletteShown = ui.isVisible(runtime.hudPalette);
    runtime.toggleOn = ui.toggle(runtime.hudToggle).value_or(false);
    runtime.sliderValue = ui.sliderValue(runtime.hudSlider).value_or(0.0f);
    std::array<UiEvent, 16> events{};
    const u32 count = std::min<u32>(ui.pollEvents(events), static_cast<u32>(events.size()));
    runtime.uiEvents += count;
    for (const UiEvent& event : std::span(events).first(count))
    {
        if (event.type == UiEventType::Clicked && event.entity == runtime.hudButton)
        {
            ++runtime.uiClicks;
        }
        if (event.type == UiEventType::Cancelled && scenes != nullptr)
        {
            scenes->requestExit();
        }
    }
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
    ParticleService* particles = context.service<ParticleService>();
    StoreService* store = context.service<StoreService>();
    UiService* ui = context.service<UiService>();
    SaveService* saves = context.service<SaveService>();
    AnimationService* animation = context.service<AnimationService>();
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
        updatePad(world, audio, particles, position, runtime);
        updateBodies(world, *physics, runtime, stats);
        updateQueries(world, *physics, runtime, stats);
        updateContacts(*physics, audio, particles, runtime, stats);
    }
    if (audio != nullptr)
    {
        updateAudio(*audio, runtime, stats.frames);
    }
    if (particles != nullptr)
    {
        updateParticles(*particles, runtime, stats.frames);
    }
    if (store != nullptr)
    {
        updateStore(*store, runtime, stats, stats.frames);
    }
    if (saves != nullptr)
    {
        updateSaves(*saves, runtime, stats, stats.frames);
    }
    if (ui != nullptr)
    {
        updateUi(*ui, scenes, runtime, stats.frames);
    }
    if (animation != nullptr)
    {
        updateAnimation(*animation, runtime, stats.frames);
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
