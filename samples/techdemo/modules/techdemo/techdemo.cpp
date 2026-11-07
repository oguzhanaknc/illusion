// The game module of the tech demo: walk through a forest clearing from dusk to night and back.
//
// - First person: the mouse (or the right stick) looks around, WASD (or the left stick) walks, Shift (or
//   the left stick's button) runs, Space (or South) jumps. The Player is a CharacterController; the Eye
//   camera follows its head with a little bob.
// - Time: the SkyAtmosphere's clock runs a full day in six minutes; hold T (or North) to run it twenty
//   times faster.
// - Night: fireflies come out over the clearing, the lanterns brighten, the crickets sing. The campfire's
//   light flickers and the neon hums all the time.
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

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
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

} // namespace techdemo

namespace
{

using namespace illusion;
using techdemo::Player;

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
    ILL_LOG_INFO("TechDemo", "module '{}' {}", context.moduleName(),
                 context.isReloading() ? "reloaded" : "loaded");
    return true;
}

void startModule(ModuleContext& context)
{
    if (!context.hasWorld() || context.worldRole() != WorldRole::Simulation)
    {
        return;
    }
    World& world = context.world();
    Player player;
    player.body = findNamed(world, "Player");
    player.eye = findNamed(world, "Eye");
    player.sky = findNamed(world, "Sky");
    player.fireLight = findNamed(world, "Fire Light");
    if (const Transform* fire = world.tryGet<Transform>(player.fireLight))
    {
        player.fireBase = fire->position;
    }
    player.fireflies = findNamed(world, "Fireflies");
    player.crickets = findNamed(world, "Crickets");
    constexpr std::array<std::string_view, 3> kLanterns{"Lantern 0 Light", "Lantern 1 Light",
                                                        "Lantern 2 Light"};
    for (u32 i = 0; i < kLanterns.size(); ++i)
    {
        player.lanterns[i] = findNamed(world, kLanterns[i]);
    }
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
    world.create(Name{"Tech Demo"}, player);
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
    const Transform* body = world.tryGet<Transform>(player.body);
    Transform* eye = world.tryGet<Transform>(player.eye);
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
    SkyAtmosphere* sky = world.tryGet<SkyAtmosphere>(player.sky);
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
        if (PointLight* light = world.tryGet<PointLight>(lantern))
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
    if (PointLight* fire = world.tryGet<PointLight>(player.fireLight))
    {
        fire->intensity = kFireIntensity * flicker;
    }
    if (Transform* fire = world.tryGet<Transform>(player.fireLight))
    {
        fire->position =
            player.fireBase + Vec3(0.05f * std::sin(t * 7.3f) + 0.03f * std::sin(t * 17.0f + 0.6f),
                                   0.06f * (flicker - 0.82f),
                                   0.05f * std::sin(t * 5.9f + 2.1f) + 0.03f * std::sin(t * 13.0f));
    }
    for (u32 i = 0; i < player.neonLights.size(); ++i)
    {
        if (PointLight* neon = world.tryGet<PointLight>(player.neonLights[i]))
        {
            neon->intensity =
                kNeonIntensity * (0.9f + 0.1f * std::sin(t * 2.0f + static_cast<f32>(i) * 1.3f));
        }
    }
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
    Player& player = world.get<Player>(self);
    const f32 dt = static_cast<f32>(std::min(deltaSeconds, 0.1));
    player.seconds += dt;
    PhysicsService* physics = context.service<PhysicsService>();
    const InputService* input = context.service<InputService>();
    const Transform* body = world.tryGet<Transform>(player.body);
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
               dt);
    updateLights(world, current);
}

constexpr ModuleApi kTechDemo =
    makeModuleApi("techdemo", "1.0.0", {.load = &loadModule, .start = &startModule, .update = &updateModule});

} // namespace

ILL_MODULE(techdemo, kTechDemo)
