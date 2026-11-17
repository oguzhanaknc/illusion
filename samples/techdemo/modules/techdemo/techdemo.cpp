// The game module of the tech demo: walk through a forest clearing from dusk to night and back.
//
// - First person: the mouse (or the right stick) looks around, WASD (or the left stick) walks, Shift (or
//   the left stick's button) runs, Space (or South) jumps. The Player is a CharacterController; the Eye
//   camera follows its head with a little bob.
// - Time: the SkyAtmosphere's clock runs a full day in six minutes; hold T (or North) to run it twenty
//   times faster.
// - Night: fireflies come out over the clearing, the lanterns brighten, the crickets sing. The campfire's
//   light flickers and the neon hums all the time.
// - UI: the HUD and the pause menu are the scene assets:/scenes/ui.scene, instantiated at the start. Escape
//   (or East) opens and closes the menu: Resume, Save and Load, the master volume, the language (English or
//   Turkish) and Quit. While it shows, the clock stops and the modal canvas keeps the walker's actions from
//   the input.
// - Saved games: the Player, the Sky, the orbs and the lanterns carry illusion.Saved; Save writes them to one
//   slot and Load (disabled while there is no slot) puts them back. A load makes new entities, so the module
//   finds the ones it keeps handles of again.
// Hot reload keeps the state in the techdemo.Player Component.

#include <illusion/audio/AudioService.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/InputActions.hpp>
#include <illusion/input/InputService.hpp>
#include <illusion/particles/ParticleService.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsService.hpp>
#include <illusion/render/SceneComponents.hpp>
#include <illusion/runtime/SceneService.hpp>
#include <illusion/save/SaveService.hpp>
#include <illusion/ui/UiService.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <span>
#include <string_view>

namespace techdemo
{

using namespace illusion;

/// The walker's view and the clock; runtime state, kept across hot reloads.
struct Player
{
    f32 yaw = 0.0f;                      ///< Radians about +y; 0 looks along -z.
    f32 pitch = -0.05f;                  ///< Radians up (+) or down (-).
    f32 bob = 0.0f;                      ///< Phase of the head bob.
    f32 hoursPerSecond = 24.0f / 360.0f; ///< A day in six minutes.
    f32 seconds = 0.0f;                  ///< Since the start, for flicker and pulse.
    bool night = false;                  ///< Fireflies, lanterns and crickets are on.
    bool grounded = false;               ///< The terrain's collider is ready: props were set free.
    Entity body{};
    Entity eye{};
    Entity sky{};
    Entity fireLight{};
    Vec3 fireBase{0.0f}; ///< Where the fire light sits; it dances around this point.
    Entity fireflies{};
    Entity crickets{};
    std::array<Entity, 3> lanterns{};
    std::array<Entity, 4> neonLights{};
    ActionId move = 0;
    ActionId look = 0;
    ActionId lookStick = 0;
    ActionId jump = 0;
    ActionId sprint = 0;
    ActionId fastTime = 0;
};

/// What the pause menu asked SaveService for last.
enum class RequestKind : u8
{
    None,
    Save,
    Load,
};

/// The elements of the game UI the module drives, and the frame-time readout; runtime state.
struct Hud
{
    Entity frameTime{};
    Entity menu{};
    Entity resume{};
    Entity volume{};
    Entity volumeLabel{};
    Entity language{};
    Entity quit{};
    Entity save{};
    Entity load{};
    Entity status{};
    RequestKind requested = RequestKind::None; ///< The save or load whose event is awaited.
    f64 sampleSeconds = 0.0;                   ///< Since the readout was last written.
    u32 sampleFrames = 0;                      ///< Updates since then.
    bool readoutShown = false;
};

} // namespace techdemo

namespace
{

using namespace illusion;
using techdemo::Hud;
using techdemo::Player;
using techdemo::RequestKind;

constexpr std::string_view kUiScene = "assets:/scenes/ui.scene";
constexpr std::string_view kSlot = "slot1"; // the demo keeps one saved game
constexpr f64 kReadoutSeconds = 0.5;        // how often the frame-time readout changes
constexpr f32 kEyeHeight = 1.65f;
constexpr f32 kWalkSpeed = 3.2f;
constexpr f32 kRunSpeed = 6.5f;
constexpr f32 kJumpSpeed = 5.0f;
constexpr f32 kMouseSensitivity = 0.0022f; // radians per pixel
constexpr f32 kStickTurnRate = 2.6f;       // radians per second at full tilt
constexpr f32 kMaxPitch = 1.45f;
constexpr f32 kFireIntensity = 40.0f;
constexpr f32 kLanternDay = 1.5f;
constexpr f32 kLanternNight = 7.0f;
constexpr f32 kNeonIntensity = 26.0f;

// The Component of an entity that the scene may lack: the module also runs on the UI scene alone.
template <typename T>
T* component(World& world, Entity entity) noexcept
{
    return world.isAlive(entity) ? world.tryGet<T>(entity) : nullptr;
}

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

ActionId action(InputService& input, std::string_view name, ActionType type,
                std::span<const InputBinding> bindings)
{
    const ActionId id = input.addAction(name, type).value_or(input.findAction(name).value_or(0));
    if (input.bindings(id).empty())
    {
        input.setBindings(id, bindings);
    }
    return id;
}

bool loadModule(ModuleContext& context)
{
    reflectRuntime<Player>("techdemo.Player", {.displayName = "Tech demo player"});
    reflectRuntime<Hud>("techdemo.Hud", {.displayName = "Tech demo HUD"});
    ILL_LOG_INFO("TechDemo", "module '{}' {}", context.moduleName(),
                 context.isReloading() ? "reloaded" : "loaded");
    return true;
}

// Instantiates the HUD and the pause menu (unless the scene already has them) and finds what the module
// drives; a World without UI gets an empty Hud.
Hud startUi(ModuleContext& context)
{
    Hud hud;
    UiService* ui = context.service<UiService>();
    if (ui == nullptr)
    {
        return hud;
    }
    SceneService* scenes = context.service<SceneService>();
    if (scenes != nullptr && ui->find({}, "Pause Menu").isNull() &&
        !scenes->instantiate(kUiScene, std::span<Entity>{}).has_value())
    {
        ILL_LOG_WARN("TechDemo", "no game UI: {}", scenes->lastError());
    }
    hud.frameTime = ui->find({}, "Frame Time");
    hud.menu = ui->find({}, "Pause Menu");
    hud.resume = ui->find(hud.menu, "Resume");
    hud.volume = ui->find(hud.menu, "Volume");
    hud.volumeLabel = ui->find(hud.menu, "Volume Label");
    hud.language = ui->find(hud.menu, "Language");
    hud.quit = ui->find(hud.menu, "Quit");
    hud.save = ui->find(hud.menu, "Save");
    hud.load = ui->find(hud.menu, "Load");
    hud.status = ui->find(hud.menu, "Status");
    return hud;
}

// The entities that saved games replace: a load makes new ones, so these handles are looked up again.
void findSaved(World& world, Player& player)
{
    player.body = findNamed(world, "Player");
    player.sky = findNamed(world, "Sky");
    constexpr std::array<std::string_view, 3> kLanterns{"Lantern 0 Light", "Lantern 1 Light",
                                                        "Lantern 2 Light"};
    for (u32 i = 0; i < kLanterns.size(); ++i)
    {
        player.lanterns[i] = findNamed(world, kLanterns[i]);
    }
}

void startModule(ModuleContext& context)
{
    if (!context.hasWorld() || context.worldRole() != WorldRole::Simulation)
    {
        return;
    }
    World& world = context.world();
    Player player;
    findSaved(world, player);
    player.eye = findNamed(world, "Eye");
    player.fireLight = findNamed(world, "Fire Light");
    if (const Transform* fire = component<Transform>(world, player.fireLight))
    {
        player.fireBase = fire->position;
    }
    player.fireflies = findNamed(world, "Fireflies");
    player.crickets = findNamed(world, "Crickets");
    constexpr std::array<std::string_view, 4> kNeon{"Neon Gate Light", "Neon Hoop 0 Light",
                                                    "Neon Hoop 1 Light", "Neon Hoop 2 Light"};
    for (u32 i = 0; i < kNeon.size(); ++i)
    {
        player.neonLights[i] = findNamed(world, kNeon[i]);
    }
    if (InputService* input = context.service<InputService>())
    {
        const std::array move{InputBinding::keyVector(Key::W, Key::S, Key::A, Key::D),
                              InputBinding::gamepadStick(GamepadStick::Left)};
        const std::array look{InputBinding::mouseMotion(kMouseSensitivity)};
        const std::array lookStick{InputBinding::gamepadStick(GamepadStick::Right)};
        const std::array jump{InputBinding::key(Key::Space),
                              InputBinding::gamepadButton(GamepadButton::South)};
        const std::array sprint{InputBinding::key(Key::LeftShift),
                                InputBinding::gamepadButton(GamepadButton::LeftStick)};
        const std::array fastTime{InputBinding::key(Key::T),
                                  InputBinding::gamepadButton(GamepadButton::North)};
        player.move = action(*input, "Move", ActionType::Vector2, move);
        player.look = action(*input, "Look", ActionType::Delta, look);
        player.lookStick = action(*input, "Look Stick", ActionType::Vector2, lookStick);
        player.jump = action(*input, "Jump", ActionType::Button, jump);
        player.sprint = action(*input, "Sprint", ActionType::Button, sprint);
        player.fastTime = action(*input, "Fast Time", ActionType::Button, fastTime);
    }
    world.create(Name{"Tech Demo"}, player, startUi(context));
}

void updateWalk(PhysicsService& physics, const InputService& input, Player& player, f32 dt)
{
    const Vec2 mouse = input.vector(player.look);
    const Vec2 stick = input.vector(player.lookStick);
    player.yaw -= mouse.x + stick.x * kStickTurnRate * dt;
    player.pitch = std::clamp(player.pitch - mouse.y + stick.y * kStickTurnRate * dt, -kMaxPitch, kMaxPitch);

    const Vec2 move = input.vector(player.move);
    const Vec3 forward(-std::sin(player.yaw), 0.0f, -std::cos(player.yaw));
    const Vec3 right(std::cos(player.yaw), 0.0f, -std::sin(player.yaw));
    const f32 speed = input.isDown(player.sprint) ? kRunSpeed : kWalkSpeed;
    const Vec3 walk = (forward * move.y + right * move.x) * speed;
    Vec3 velocity = physics.characterVelocity(player.body);
    velocity.x = walk.x;
    velocity.z = walk.z;
    // The terrain's collider is cooked in the background: hold still until there is ground below.
    if (!player.grounded)
    {
        physics.setCharacterVelocity(player.body, Vec3(0.0f));
        return;
    }
    if (physics.isCharacterGrounded(player.body))
    {
        velocity.y = input.wasPressed(player.jump) ? kJumpSpeed : 0.0f;
        player.bob += glm::length(Vec2(walk.x, walk.z)) * dt * 2.2f;
    }
    else
    {
        velocity.y += physics.gravity().y * dt;
    }
    physics.setCharacterVelocity(player.body, velocity);
}

// Once the terrain's collider exists, the orbs and lanterns (kinematic in the scene, so that they wait for
// the ground) become dynamic bodies that the player can push.
void releaseProps(World& world, PhysicsService& physics, const Vec3& position, Player& player)
{
    if (player.grounded || !physics.raycast(position + Vec3(0.0f, 2.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f),
                                            500.0f, 0xFFFFFFFFu, player.body))
    {
        return;
    }
    player.grounded = true;
    world.query<const Name, RigidBody>().each([&](Entity entity, const Name& name, RigidBody& body) {
        if (body.motionType == MotionType::Kinematic &&
            (name.value.starts_with("Orb ") || name.value.starts_with("Lantern ")))
        {
            body.motionType = MotionType::Dynamic;
            physics.rebuild(entity);
        }
    });
}

void updateEye(World& world, Player& player)
{
    const Transform* body = component<Transform>(world, player.body);
    Transform* eye = component<Transform>(world, player.eye);
    if (body == nullptr || eye == nullptr)
    {
        return;
    }
    const f32 bob = 0.035f * std::sin(player.bob * 2.0f);
    eye->position = body->position + Vec3(0.0f, kEyeHeight + bob, 0.0f);
    eye->rotation = glm::angleAxis(player.yaw, Vec3(0.0f, 1.0f, 0.0f)) *
                    glm::angleAxis(player.pitch, Vec3(1.0f, 0.0f, 0.0f));
}

// The clock, and what changes with night: fireflies, lanterns and crickets.
void updateTime(World& world, const InputService* input, ParticleService* particles, AudioService* audio,
                Player& player, f32 dt)
{
    SkyAtmosphere* sky = component<SkyAtmosphere>(world, player.sky);
    if (sky == nullptr)
    {
        return;
    }
    const f32 speed = input != nullptr && input->isDown(player.fastTime) ? 20.0f : 1.0f;
    sky->timeOfDay = std::fmod(sky->timeOfDay + player.hoursPerSecond * speed * dt, 24.0f);
    const f32 sunHeight = sky->sunDirection().y;
    // Night comes a little after sunset and leaves a little before sunrise.
    const bool night = player.night ? sunHeight < 0.02f : sunHeight < -0.04f;
    if (night != player.night)
    {
        player.night = night;
        ILL_LOG_INFO("TechDemo", "{} at {:.1f} h", night ? "night falls" : "day breaks", sky->timeOfDay);
        if (particles != nullptr)
        {
            if (night)
            {
                particles->play(player.fireflies);
            }
            else
            {
                particles->stop(player.fireflies);
            }
        }
        if (audio != nullptr)
        {
            if (night)
            {
                audio->play(player.crickets);
            }
            else
            {
                audio->stop(player.crickets);
            }
        }
    }
    const f32 dusk = glm::smoothstep(0.15f, -0.1f, sunHeight);
    for (const Entity lantern : player.lanterns)
    {
        if (PointLight* light = component<PointLight>(world, lantern))
        {
            light->intensity = kLanternDay + (kLanternNight - kLanternDay) * dusk;
        }
    }
}

// The campfire flickers and its light dances with the flames, so the shadows it casts move; the neon hums.
void updateLights(World& world, Player& player)
{
    const f32 t = player.seconds;
    const f32 flicker = 0.82f + 0.1f * std::sin(t * 11.0f) + 0.05f * std::sin(t * 23.0f + 1.7f) +
                        0.03f * std::sin(t * 41.0f + 0.4f);
    if (PointLight* fire = component<PointLight>(world, player.fireLight))
    {
        fire->intensity = kFireIntensity * flicker;
    }
    if (Transform* fire = component<Transform>(world, player.fireLight))
    {
        fire->position =
            player.fireBase + Vec3(0.05f * std::sin(t * 7.3f) + 0.03f * std::sin(t * 17.0f + 0.6f),
                                   0.06f * (flicker - 0.82f),
                                   0.05f * std::sin(t * 5.9f + 2.1f) + 0.03f * std::sin(t * 13.0f));
    }
    for (u32 i = 0; i < player.neonLights.size(); ++i)
    {
        if (PointLight* neon = component<PointLight>(world, player.neonLights[i]))
        {
            neon->intensity =
                kNeonIntensity * (0.9f + 0.1f * std::sin(t * 2.0f + static_cast<f32>(i) * 1.3f));
        }
    }
}

void showVolume(UiService& ui, const Hud& hud, f32 volume)
{
    const std::array percent{UiTextArgument::number("percent", std::round(static_cast<f64>(volume) * 100.0))};
    static_cast<void>(ui.setTextKey(hud.volumeLabel, "techdemo.menu.volume", percent));
}

// Save needs a save directory; Load also a slot that can be read.
void updateSaveItems(UiService& ui, const SaveService* saves, const Hud& hud)
{
    const bool available = saves != nullptr && saves->available();
    static_cast<void>(ui.setInteractable(hud.save, available));
    const std::optional<SaveSlotInfo> slot = available ? saves->slot(kSlot) : std::nullopt;
    const bool loadable = slot && (slot->state == SaveSlotState::Ok || slot->state == SaveSlotState::Backup);
    static_cast<void>(ui.setInteractable(hud.load, loadable));
}

void showMenu(UiService& ui, const AudioService* audio, const SaveService* saves, const Hud& hud, bool show)
{
    static_cast<void>(ui.setVisible(hud.menu, show));
    if (show)
    {
        static_cast<void>(ui.setText(hud.status, {}));
        updateSaveItems(ui, saves, hud);
        const f32 volume = audio != nullptr ? audio->masterVolume() : 1.0f;
        static_cast<void>(ui.setSliderValue(hud.volume, volume));
        showVolume(ui, hud, volume);
        static_cast<void>(ui.setFocus(hud.resume));
    }
}

// The results of the menu's save and load requests.
void updateSaves(World& world, UiService& ui, SaveService& saves, Hud& hud, Player& player)
{
    std::array<SaveEvent, 8> events{};
    const u32 count = std::min<u32>(saves.pollEvents(events), static_cast<u32>(events.size()));
    for (const SaveEvent& event : std::span(events).first(count))
    {
        if (event.slot != kSlot)
        {
            continue;
        }
        std::string_view key;
        switch (event.type)
        {
        case SaveEventType::Saved:
            key = "techdemo.menu.saved";
            break;
        case SaveEventType::Loaded:
        case SaveEventType::LoadedBackup:
            // The saved entities are new ones; props that were still waiting for the ground wait again.
            findSaved(world, player);
            player.grounded = false;
            key = event.type == SaveEventType::Loaded ? "techdemo.menu.loaded" : "techdemo.menu.loadedBackup";
            break;
        case SaveEventType::Failed:
            key =
                hud.requested == RequestKind::Load ? "techdemo.menu.loadFailed" : "techdemo.menu.saveFailed";
            break;
        case SaveEventType::Removed:
            break;
        }
        if (!key.empty())
        {
            static_cast<void>(ui.setTextKey(hud.status, key, {}));
        }
        hud.requested = RequestKind::None;
    }
    if (count > 0)
    {
        updateSaveItems(ui, &saves, hud);
    }
}

// The events of the pause menu, and the frame-time readout. Returns whether the menu shows.
bool updateUi(ModuleContext& context, Hud& hud, Player& player, f64 deltaSeconds)
{
    UiService* ui = context.service<UiService>();
    if (ui == nullptr || hud.menu.isNull())
    {
        return false;
    }
    AudioService* audio = context.service<AudioService>();
    SaveService* saves = context.service<SaveService>();
    if (saves != nullptr)
    {
        updateSaves(context.world(), *ui, *saves, hud, player);
    }
    std::array<UiEvent, 16> events{};
    const u32 count = std::min<u32>(ui->pollEvents(events), static_cast<u32>(events.size()));
    for (const UiEvent& event : std::span(events).first(count))
    {
        if (event.type == UiEventType::Cancelled)
        {
            showMenu(*ui, audio, saves, hud, !ui->isVisible(hud.menu));
        }
        else if (event.type == UiEventType::Clicked && event.entity == hud.resume)
        {
            showMenu(*ui, audio, saves, hud, false);
        }
        else if (event.type == UiEventType::Clicked && event.entity == hud.save && saves != nullptr)
        {
            hud.requested = RequestKind::Save;
            static_cast<void>(saves->save(kSlot, {.playSeconds = static_cast<f64>(player.seconds)}));
        }
        else if (event.type == UiEventType::Clicked && event.entity == hud.load && saves != nullptr)
        {
            hud.requested = RequestKind::Load;
            static_cast<void>(saves->load(kSlot));
        }
        else if (event.type == UiEventType::Clicked && event.entity == hud.language)
        {
            static_cast<void>(ui->setLanguage(ui->language() == "tr" ? "en" : "tr"));
        }
        else if (event.type == UiEventType::Clicked && event.entity == hud.quit)
        {
            if (SceneService* scenes = context.service<SceneService>())
            {
                scenes->requestExit();
            }
        }
        else if (event.type == UiEventType::ValueChanged && event.entity == hud.volume)
        {
            if (audio != nullptr)
            {
                audio->setMasterVolume(event.value);
            }
            showVolume(*ui, hud, event.value);
        }
    }
    const bool open = ui->isVisible(hud.menu);
    // A menu shown in this update is laid out at its end; the focus can go to Resume only afterwards.
    if (open && ui->focused().isNull())
    {
        static_cast<void>(ui->setFocus(hud.resume));
    }

    hud.sampleSeconds += std::max(deltaSeconds, 0.0);
    ++hud.sampleFrames;
    if (hud.sampleSeconds >= kReadoutSeconds || !hud.readoutShown)
    {
        const f64 frameSeconds = hud.sampleSeconds / static_cast<f64>(hud.sampleFrames);
        const std::array readout{
            UiTextArgument::number("fps", frameSeconds > 0.0 ? std::round(1.0 / frameSeconds) : 0.0),
            UiTextArgument::number("ms", std::round(frameSeconds * 10000.0) / 10.0)};
        static_cast<void>(ui->setTextKey(hud.frameTime, "techdemo.hud.frameTime", readout));
        hud.sampleSeconds = 0.0;
        hud.sampleFrames = 0;
        hud.readoutShown = true;
    }
    return open;
}

void updateModule(ModuleContext& context, f64 deltaSeconds)
{
    World& world = context.world();
    Entity self{};
    world.query<const Player>().each([&self](Entity entity, const Player&) { self = entity; });
    if (!world.isAlive(self))
    {
        return;
    }
    // A copy: the UI may add Components to its elements.
    const Hud* stored = component<Hud>(world, self);
    Hud hud = stored != nullptr ? *stored : Hud{};
    // Also a copy, for the same reason.
    Player walker = world.get<Player>(self);
    const bool paused = updateUi(context, hud, walker, deltaSeconds);
    if (Hud* written = component<Hud>(world, self))
    {
        *written = hud;
    }
    world.get<Player>(self) = walker;

    Player& player = world.get<Player>(self);
    const f32 dt = static_cast<f32>(std::min(deltaSeconds, 0.1));
    // The pause menu stops the clock; the walker reads no input meanwhile, so it stands still.
    const f32 clock = paused ? 0.0f : dt;
    player.seconds += clock;
    PhysicsService* physics = context.service<PhysicsService>();
    const InputService* input = context.service<InputService>();
    const Transform* body = component<Transform>(world, player.body);
    if (physics != nullptr && body != nullptr)
    {
        // A copy: rebuilding bodies may change the World's structure.
        const Vec3 position = body->position;
        releaseProps(world, *physics, position, player);
        if (input != nullptr)
        {
            updateWalk(*physics, *input, world.get<Player>(self), dt);
        }
    }
    Player& current = world.get<Player>(self);
    updateEye(world, current);
    updateTime(world, input, context.service<ParticleService>(), context.service<AudioService>(), current,
               clock);
    updateLights(world, current);
}

constexpr ModuleApi kTechDemo =
    makeModuleApi("techdemo", "1.0.0", {.load = &loadModule, .start = &startModule, .update = &updateModule});

} // namespace

ILL_MODULE(techdemo, kTechDemo)
