// physics_playground: a tower of boxes on the ground, simulated by PhysicsWorld and rendered by the
// SceneRenderer. Impacts make sounds through the AudioWorld: louder for faster impacts, from where they
// happen, with a little room reverb.
//
// Usage: illusion_physics_playground [--frames N] [--screenshot out.png]
//   Space or the gamepad's south button shoots a ball from the camera. R rebuilds the tower.
//   Orbit: left mouse drag or right stick. Escape quits.
//   --screenshot renders the final view offscreen at 1280x720 and saves it as PNG.

#include "common/Screenshot.hpp"

#include <illusion/assets/Environment.hpp>
#include <illusion/audio/AudioWorld.hpp>
#include <illusion/core/EventBus.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/core/Time.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/GamepadSystem.hpp>
#include <illusion/input/InputActions.hpp>
#include <illusion/physics/PhysicsWorld.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/platform/Platform.hpp>
#include <illusion/platform/Window.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/RenderResources.hpp>
#include <illusion/render/SceneRenderer.hpp>
#include <illusion/render/Swapchain.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

using namespace illusion;

namespace
{

struct Assets
{
    MeshHandle cube;
    MeshHandle ball;
    MaterialHandle boxMaterials[3];
    MaterialHandle ballMaterial;
};

MaterialHandle material(RenderResources& resources, const Vec4& color, f32 roughness, f32 metallic = 0.0f)
{
    MaterialDesc desc;
    desc.parameters.baseColorFactor = color;
    desc.parameters.roughnessFactor = roughness;
    desc.parameters.metallicFactor = metallic;
    return resources.createMaterial(desc);
}

// A 6-level pyramid of 0.5 m boxes.
std::vector<Entity> buildTower(World& world, const Assets& assets)
{
    std::vector<Entity> boxes;
    constexpr int kLevels = 6;
    for (int level = 0; level < kLevels; ++level)
    {
        const int count = kLevels - level;
        for (int i = 0; i < count; ++i)
        {
            for (int j = 0; j < count; ++j)
            {
                Transform transform;
                transform.position = Vec3((static_cast<f32>(i) - static_cast<f32>(count - 1) * 0.5f) * 0.52f,
                                          0.25f + static_cast<f32>(level) * 0.5f,
                                          (static_cast<f32>(j) - static_cast<f32>(count - 1) * 0.5f) * 0.52f);
                // The cube mesh and the default collider are 1 m; the Transform's scale shrinks both.
                transform.scale = Vec3(0.5f);
                const MaterialHandle color = assets.boxMaterials[(level + i + j) % 3];
                boxes.push_back(
                    world.create(transform, RigidBody{}, Collider{}, MeshRenderer{assets.cube, 0, color}));
            }
        }
    }
    return boxes;
}

// A short knock: a low thump with a burst of noise, both decaying quickly. Generated, so the sample needs
// no audio files.
std::vector<f32> makeImpactSound(u32 sampleRate)
{
    const u32 frames = sampleRate / 6;
    std::vector<f32> samples(frames);
    u32 noise = 0x12345678u;
    for (u32 i = 0; i < frames; ++i)
    {
        const f32 t = static_cast<f32>(i) / static_cast<f32>(sampleRate);
        noise = noise * 1664525u + 1013904223u;
        const f32 random = static_cast<f32>(noise >> 8) / static_cast<f32>(1u << 24) * 2.0f - 1.0f;
        const f32 thump = std::sin(2.0f * std::numbers::pi_v<f32> * 110.0f * t) * std::exp(-t * 30.0f);
        const f32 click = random * std::exp(-t * 90.0f);
        samples[i] = 0.7f * thump + 0.3f * click;
    }
    return samples;
}

// Plays a knock for each new contact that hits hard enough. Returns the number of sounds started.
u32 playImpacts(AudioWorld& audio, const PhysicsWorld& physics, AudioClipHandle knock)
{
    constexpr f32 kQuietestImpact = 0.8f; // m/s; slower contacts are silent (resting, sliding)
    constexpr f32 kLoudestImpact = 8.0f;
    constexpr u32 kMaxPerFrame = 8; // a collapsing tower touches hundreds of times
    u32 played = 0;
    for (const ContactEvent& event : physics.contactEvents())
    {
        if (event.type != ContactEventType::Begin || event.sensor || event.speed < kQuietestImpact)
        {
            continue;
        }
        AudioPlayParams params;
        params.spatial = true;
        params.position = event.point;
        params.volume = std::min(event.speed / kLoudestImpact, 1.0f);
        // Vary the pitch per pair of bodies so that a pile of boxes does not sound like one.
        params.pitch =
            0.85f + 0.3f * static_cast<f32>((event.a.index * 7u + event.b.index * 13u) % 16u) / 15.0f;
        params.reverbSend = 0.3f;
        params.priority = static_cast<u32>(params.volume * 255.0f);
        if (audio.playOneShot(knock, params).valid() && ++played == kMaxPerFrame)
        {
            break;
        }
    }
    return played;
}

} // namespace

int main(int argc, char** argv)
{
    std::string screenshotPath;
    u64 frameLimit = 0;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--frames" && i + 1 < argc)
        {
            const std::string_view value = argv[++i];
            std::from_chars(value.data(), value.data() + value.size(), frameLimit);
        }
        else if (arg == "--screenshot" && i + 1 < argc)
        {
            screenshotPath = argv[++i];
        }
    }

    EventBus events;
    auto platform = Platform::create();
    if (!platform)
    {
        return 1;
    }
    WindowDesc windowDesc;
    windowDesc.title = "illusion - physics playground";
    auto window = Window::create(*platform, windowDesc, &events);
    auto gamepads = GamepadSystem::create(&events);
    auto device = RenderDevice::create();
    if (!window || !device)
    {
        return 1;
    }
    auto swapchain = Swapchain::create(*device, *window);
    if (!swapchain)
    {
        return 1;
    }
    JobSystem jobs;
    RenderResources resources(*device);
    SceneRendererDesc rendererDesc;
    rendererDesc.colorFormat = swapchain->format();
    auto renderer = SceneRenderer::create(*device, resources, rendererDesc);
    if (!renderer)
    {
        return 1;
    }

    World world;
    PhysicsWorld physics(world, jobs);
    VirtualFileSystem files;
    AudioWorldDesc audioDesc;
    // Screenshot runs are headless (CI): mix without a sound card.
    audioDesc.output = screenshotPath.empty() ? AudioOutput::Device : AudioOutput::Null;
    AudioWorld audio(world, files, audioDesc);
    const std::vector<f32> knockSamples = makeImpactSound(audio.sampleRate());
    const AudioClipHandle knock = audio.createClip(knockSamples, 1, audio.sampleRate());
    AudioReverbSettings room;
    room.roomSize = 0.6f;
    audio.setReverb(room);
    Assets assets;
    assets.cube = resources.createMesh(makeCube(1.0f), "Box");
    assets.ball = resources.createMesh(makeSphere(0.5f, 32, 16), "Ball");
    assets.boxMaterials[0] = material(resources, Vec4(0.85f, 0.35f, 0.1f, 1.0f), 0.6f);
    assets.boxMaterials[1] = material(resources, Vec4(0.9f, 0.8f, 0.6f, 1.0f), 0.7f);
    assets.boxMaterials[2] = material(resources, Vec4(0.2f, 0.45f, 0.8f, 1.0f), 0.5f);
    assets.ballMaterial = material(resources, Vec4(0.9f, 0.9f, 0.9f, 1.0f), 0.2f, 1.0f);

    // Ground: a 40 m plane with a static box collider whose top is at y = 0.
    Transform ground;
    ground.position = Vec3(0.0f, -0.5f, 0.0f);
    RigidBody groundBody;
    groundBody.motionType = MotionType::Static;
    Collider groundCollider;
    groundCollider.halfExtents = Vec3(20.0f, 0.5f, 20.0f);
    world.create(ground, groundBody, groundCollider);
    world.create(Transform{}, MeshRenderer{resources.createMesh(makePlane(40.0f), "Ground"), 0,
                                           material(resources, Vec4(0.45f, 0.45f, 0.45f, 1.0f), 0.9f)});
    std::vector<Entity> tower = buildTower(world, assets);
    std::vector<Entity> balls;

    Transform sun;
    sun.rotation = glm::quatLookAt(glm::normalize(Vec3(-0.4f, -1.0f, -0.6f)), Vec3(0.0f, 1.0f, 0.0f));
    world.create(sun, DirectionalLight{Vec3(1.0f, 0.95f, 0.9f), 3.0f});
    world.create(Transform{}, SkyLight{resources.createEnvironment(makeSkyImage(1024)), 1.0f, true});

    InputActions actions;
    const ActionId orbit = actions.addAction("Orbit", ActionType::Vector2);
    const ActionId shoot = actions.addAction("Shoot", ActionType::Button);
    actions.addBinding(orbit, InputBinding::gamepadStick(GamepadStick::Right));
    actions.addBinding(shoot, InputBinding::key(Key::Space));
    actions.addBinding(shoot, InputBinding::gamepadButton(GamepadButton::South));

    nvrhi::CommandListHandle commands = device->device()->createCommandList();
    FrameClock clock;
    Stopwatch titleTimer;
    const Vec3 target(0.0f, 1.2f, 0.0f);
    f32 yaw = 0.6f;
    f32 pitch = -0.3f;
    constexpr f32 kDistance = 9.0f;
    Transform camera;
    const Entity ears = world.create(Transform{}, AudioListener{});
    u64 frames = 0;
    u64 impacts = 0;
    u64 framesSinceTitle = 0;
    std::vector<const GamepadState*> pads;
    while (!window->shouldClose() && (frameLimit == 0 || frames < frameLimit))
    {
        platform->pollEvents();
        if (gamepads)
        {
            gamepads->update(window->isFocused());
        }
        events.dispatchQueued();
        const FrameTime& frame = clock.tick(monotonicSeconds());
        // Screenshot runs advance by exactly one step per frame, so the result does not depend on timing.
        const f64 dt = frameLimit != 0 ? 1.0 / 60.0 : frame.deltaSeconds;

        pads.clear();
        if (gamepads)
        {
            for (Gamepad* pad : gamepads->gamepads())
            {
                pads.push_back(&pad->state());
            }
        }
        const InputState& input = window->input();
        actions.update(&input, pads);
        if (input.wasPressed(Key::Escape))
        {
            window->setShouldClose(true);
        }
        if (input.isDown(MouseButton::Left))
        {
            yaw -= input.mouseDelta().x * 0.005f;
            pitch -= input.mouseDelta().y * 0.005f;
        }
        const Vec2 stick = actions.vector(orbit);
        yaw -= stick.x * 2.0f * static_cast<f32>(dt);
        pitch = std::clamp(pitch + stick.y * 2.0f * static_cast<f32>(dt), -1.4f, -0.05f);
        camera.position = target + Vec3(std::sin(yaw) * std::cos(pitch), -std::sin(pitch),
                                        std::cos(yaw) * std::cos(pitch)) *
                                       kDistance;
        camera.rotation = glm::quatLookAt(glm::normalize(target - camera.position), Vec3(0.0f, 1.0f, 0.0f));

        // Screenshot runs shoot one ball after a second so that the picture shows the tower falling.
        if (actions.wasPressed(shoot) || (frameLimit != 0 && frames == 60))
        {
            Transform transform = camera;
            transform.scale = Vec3(0.6f);
            RigidBody ballBody;
            ballBody.mass = 1000.0f; // heavier than a few boxes (125 kg each), so it knocks them over
            ballBody.continuousCollision = true;
            Collider ballCollider;
            ballCollider.shape = ColliderShape::Sphere;
            const Entity ball = world.create(transform, ballBody, ballCollider,
                                             MeshRenderer{assets.ball, 0, assets.ballMaterial});
            balls.push_back(ball);
            physics.update(0.0); // create the body now so it can be launched
            physics.setLinearVelocity(ball, camera.forward() * 25.0f);
        }
        if (input.wasPressed(Key::R))
        {
            for (const Entity entity : tower)
            {
                world.destroy(entity);
            }
            for (const Entity entity : balls)
            {
                world.destroy(entity);
            }
            balls.clear();
            tower = buildTower(world, assets);
        }

        physics.update(dt);
        impacts += playImpacts(audio, physics, knock);
        world.get<Transform>(ears) = camera;
        audio.update(dt);

        if (swapchain->beginFrame())
        {
            const UVec2 size = swapchain->size();
            const RenderView view =
                RenderView::fromCamera(camera, Camera{}, static_cast<f32>(size.x) / static_cast<f32>(size.y));
            commands->open();
            renderer->render(commands, swapchain->currentBackBuffer(), world, view, &jobs);
            commands->close();
            device->device()->executeCommandList(commands);
            swapchain->present();
        }
        ++frames;
        ++framesSinceTitle;
        ILL_PROFILE_FRAME();

        if (titleTimer.elapsedSeconds() >= 0.5)
        {
            const f64 fps = static_cast<f64>(framesSinceTitle) / titleTimer.restart();
            framesSinceTitle = 0;
            window->setTitle(std::format("illusion - physics playground - {:.0f} FPS - {} bodies", fps,
                                         physics.bodyCount()));
        }
    }
    device->waitIdle();
    if (!screenshotPath.empty() &&
        !sample::saveScreenshot(*device, resources, world, camera, jobs, screenshotPath))
    {
        ILL_LOG_ERROR("Sample", "cannot save {}", screenshotPath);
        return 1;
    }
    device->waitIdle();
    const u32 errors = device->validationErrorCount();
    ILL_LOG_INFO("Sample", "simulated {} steps over {} frames, {} impact sounds, {} validation error(s)",
                 physics.stepCount(), frames, impacts, errors);
    return errors == 0 ? 0 : 2;
}
