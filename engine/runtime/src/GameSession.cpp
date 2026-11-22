#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/animation/AnimationWorld.hpp>
#include <illusion/animation/AnimationWorldService.hpp>
#include <illusion/audio/AudioWorldService.hpp>
#include <illusion/core/Assert.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Wind.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/Gamepad.hpp>
#include <illusion/input/GamepadSystem.hpp>
#include <illusion/input/InputActions.hpp>
#include <illusion/input/InputActionsAsset.hpp>
#include <illusion/input/InputActionsService.hpp>
#include <illusion/input/InputStrings.hpp>
#include <illusion/modules/ModuleHost.hpp>
#include <illusion/nav/NavComponents.hpp>
#include <illusion/nav/NavWorld.hpp>
#include <illusion/particles/ParticleWorld.hpp>
#include <illusion/particles/ParticleWorldService.hpp>
#include <illusion/physics/PhysicsAssetResolver.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsWorld.hpp>
#include <illusion/physics/PhysicsWorldService.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/platform/Input.hpp>
#include <illusion/render/RenderAssets.hpp>
#include <illusion/runtime/GameLocalization.hpp>
#include <illusion/runtime/GameSession.hpp>
#include <illusion/runtime/SceneLoaderService.hpp>
#include <illusion/save/SaveService.hpp>
#include <illusion/save/SaveSystemService.hpp>
#include <illusion/store/StoreService.hpp>
#include <illusion/text/LocaleFile.hpp>
#include <illusion/text/Localization.hpp>
#include <illusion/text/MessageFormat.hpp>
#include <illusion/ui/UiWidgets.hpp>
#include <illusion/ui/UiWorld.hpp>
#include <illusion/ui/UiWorldService.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace illusion
{
namespace
{

constexpr usize kReservedGamepads = 8;
// While a modal canvas is shown or a text field has the focus, only the actions with this prefix get input.
constexpr std::string_view kUiActionPrefix = "ui.";
// Repeat of the held editing keys of a text field, in seconds: first after the delay, then at the interval.
constexpr f64 kKeyRepeatDelay = 0.4;
constexpr f64 kKeyRepeatInterval = 0.05;

// Presses of a key with repeat while it is held.
struct KeyRepeat
{
    f64 wait = 0.0;

    [[nodiscard]] bool update(const InputState& input, Key key, f64 deltaSeconds) noexcept
    {
        if (input.wasPressed(key))
        {
            wait = kKeyRepeatDelay;
            return true;
        }
        if (!input.isDown(key))
        {
            return false;
        }
        wait -= deltaSeconds;
        if (wait > 0.0)
        {
            return false;
        }
        wait = std::max(wait + kKeyRepeatInterval, 0.0);
        return true;
    }
};

// The labels of UiGlyph: the first binding of the action for the device used last, in the session's language.
class ActionGlyphs final : public UiGlyphSource
{
public:
    ActionGlyphs(const InputActions& actions, const text::Localization& localization) noexcept
        : m_actions(actions)
        , m_localization(localization)
    {
    }

    [[nodiscard]] std::string_view actionLabel(std::string_view action) override
    {
        m_label.clear();
        const std::optional<ActionId> id = m_actions.findAction(action);
        if (!id)
        {
            return {};
        }
        const bool gamepad = m_actions.lastDevice() == InputDevice::Gamepad;
        for (const InputBinding& binding : m_actions.bindings(*id))
        {
            if (binding.isGamepad() == gamepad)
            {
                m_label = glyphLabel(binding, gamepadFamily(m_actions.lastGamepadType()), m_localization);
                break;
            }
        }
        return m_label;
    }

    [[nodiscard]] u64 version() const noexcept override { return m_version; }

    // Counts a change of the device used last, the gamepad family, the bindings or the language.
    void refresh() noexcept
    {
        const InputDevice device = m_actions.lastDevice();
        const GamepadFamily family = gamepadFamily(m_actions.lastGamepadType());
        const u64 bindings = m_actions.bindingsVersion();
        if (device != m_device || family != m_family || bindings != m_bindings ||
            !(m_localization.locale() == m_locale))
        {
            m_device = device;
            m_family = family;
            m_bindings = bindings;
            m_locale = m_localization.locale();
            ++m_version;
        }
    }

private:
    const InputActions& m_actions;
    const text::Localization& m_localization;
    std::string m_label;
    u64 m_version = 0;
    InputDevice m_device = InputDevice::KeyboardMouse;
    GamepadFamily m_family = GamepadFamily::Generic;
    u64 m_bindings = 0;
    text::LocaleId m_locale{};
};

// Adds the actions the UI reads, with their default bindings, when @p actions lacks them.
void addMissingUiActions(InputActions& actions)
{
    if (!actions.findAction(kUiNavigateAction))
    {
        const ActionId navigate = actions.addAction(kUiNavigateAction, ActionType::Vector2);
        const std::array bindings{InputBinding::keyVector(Key::Up, Key::Down, Key::Left, Key::Right),
                                  InputBinding::keyVector(Key::W, Key::S, Key::A, Key::D),
                                  InputBinding::gamepadStick(GamepadStick::Left),
                                  InputBinding::gamepadStick(GamepadStick::DPad)};
        actions.setBindings(navigate, bindings);
    }
    if (!actions.findAction(kUiSubmitAction))
    {
        const ActionId submit = actions.addAction(kUiSubmitAction, ActionType::Button);
        const std::array bindings{InputBinding::key(Key::Enter), InputBinding::key(Key::Space),
                                  InputBinding::gamepadButton(GamepadButton::South)};
        actions.setBindings(submit, bindings);
    }
    if (!actions.findAction(kUiCancelAction))
    {
        const ActionId cancel = actions.addAction(kUiCancelAction, ActionType::Button);
        const std::array bindings{InputBinding::key(Key::Escape),
                                  InputBinding::gamepadButton(GamepadButton::East)};
        actions.setBindings(cancel, bindings);
    }
}

PhysicsWorldDesc physicsDesc(const PhysicsSettings& settings) noexcept
{
    PhysicsWorldDesc desc;
    desc.gravity = settings.gravity;
    desc.stepSeconds = settings.stepSeconds;
    desc.maxStepsPerUpdate = settings.maxStepsPerUpdate;
    return desc;
}

AudioWorldDesc audioDesc(const GameSessionDesc& session) noexcept
{
    AudioWorldDesc desc;
    desc.output = session.audioOutput;
    desc.maxVoices = session.settings.audio.maxVoices;
    desc.speedOfSound = session.settings.audio.speedOfSound;
    desc.streamer = session.streamer;
    return desc;
}

// The generator of the EntityGuids made while running: seeded with fixed steps, else none (the shared one).
std::optional<GuidGenerator> seededGenerator(const GameSessionDesc& session) noexcept
{
    if (session.fixedStep)
    {
        return std::optional<GuidGenerator>(std::in_place, session.seed);
    }
    return std::nullopt;
}

// The store that saved games are mirrored to (ADR 0058), or null.
StoreService* saveCloud(const GameSessionDesc& session)
{
    const bool mirrored = session.store != nullptr && session.storeCloudIsRemote &&
                          session.settings.save.cloud && session.store->available();
    return mirrored ? session.store : nullptr;
}

// Moves the Entities whose pose the last Animation step wrote by their root motion (ADR 0060): a character
// gets it as its horizontal velocity for @p step seconds, keeping its vertical velocity; any other Entity
// is moved, through its LocalTransform when it has a parent. Both turn about +y by the yaw.
void applyRootMotion(World& world, PhysicsWorld& physics, u64 frame, f64 step)
{
    if (!(step > 0.0))
    {
        return;
    }
    const auto seconds = static_cast<f32>(step);
    world.query<const AnimationPose, Transform>().without<Inactive>().each(
        [&](Entity entity, const AnimationPose& pose, Transform& transform) {
            if (pose.frame != frame || (pose.rootTranslation == Vec3(0.0f) && pose.rootYaw == 0.0f))
            {
                return;
            }
            const Quat turn = glm::angleAxis(pose.rootYaw, Vec3(0.0f, 1.0f, 0.0f));
            transform.rotation = glm::normalize(turn * transform.rotation);
            if (const NavAgent* agent = world.tryGet<NavAgent>(entity);
                agent != nullptr &&
                (agent->drive == NavDrive::Character || agent->drive == NavDrive::Transform))
            {
                return; // navigation moves it
            }
            if (world.tryGet<CharacterController>(entity) != nullptr)
            {
                const Vec3 velocity = physics.characterVelocity(entity);
                physics.setCharacterVelocity(entity, Vec3(pose.rootTranslation.x / seconds, velocity.y,
                                                          pose.rootTranslation.z / seconds));
                return;
            }
            transform.position += pose.rootTranslation;
            const Hierarchy* hierarchy = world.tryGet<Hierarchy>(entity);
            LocalTransform* local = world.tryGet<LocalTransform>(entity);
            if (hierarchy == nullptr || local == nullptr || !world.isAlive(hierarchy->parent))
            {
                return; // a root: updateTransformHierarchy() copies Transform into LocalTransform
            }
            const Transform* parent = world.tryGet<Transform>(hierarchy->parent);
            *local = LocalTransform::from(relativeTo(transform, parent != nullptr ? *parent : Transform{}));
        });
}

// Hands the crowd's output to physics and animation (ADR 0062): Character agents get the horizontal part of
// their desired velocity, keeping their vertical velocity, and speed parameters follow the agents' speeds.
void applyNavigation(World& world, PhysicsWorld& physics, AnimationService* animation)
{
    ILL_PROFILE_SCOPE();
    world.query<const NavAgent, const NavAgentState>().without<Inactive>().each(
        [&](Entity entity, const NavAgent& agent, const NavAgentState& state) {
            if (agent.drive == NavDrive::Character && world.has<CharacterController>(entity))
            {
                const Vec3 velocity = physics.characterVelocity(entity);
                physics.setCharacterVelocity(
                    entity,
                    state.link.isNull() ? Vec3(state.velocity.x, velocity.y, state.velocity.z) : Vec3(0.0f));
            }
            if (animation != nullptr && !agent.speedParameter.empty() && world.has<Animator>(entity))
            {
                static_cast<void>(animation->setFloat(entity, agent.speedParameter, state.speed));
            }
        });
}

// Places the Entities attached to joints (BoneAttachment) by their parents' Transforms after root motion
// moved them, so that physics drives their bodies to where the sockets are in this update.
void followSockets(World& world)
{
    world.query<const BoneAttachment, const Hierarchy, const LocalTransform, Transform>()
        .without<Inactive>()
        .each([&world](const BoneAttachment&, const Hierarchy& hierarchy, const LocalTransform& local,
                       Transform& transform) {
            const Transform* parent =
                world.isAlive(hierarchy.parent) ? world.tryGet<Transform>(hierarchy.parent) : nullptr;
            if (parent != nullptr)
            {
                transform = combine(*parent, local.asTransform());
            }
        });
}

// True if @p entity is @p character, one of its ancestors or one of its descendants: foot placement's rays
// pass through the character's own bodies.
[[nodiscard]] bool partOf(const World& world, Entity entity, Entity character)
{
    constexpr int kMaxDepth = 64;
    const auto below = [&world](Entity child, Entity ancestor) {
        Entity current = child;
        for (int depth = 0; depth < kMaxDepth && world.isAlive(current); ++depth)
        {
            if (current == ancestor)
            {
                return true;
            }
            const Hierarchy* hierarchy = world.tryGet<Hierarchy>(current);
            if (hierarchy == nullptr)
            {
                return false;
            }
            current = hierarchy->parent;
        }
        return false;
    };
    return below(entity, character) || below(character, entity);
}

// The body that carries @p character: the nearest of it and its ancestors with a CharacterController or a
// RigidBody, else the Entity itself.
[[nodiscard]] Entity carrierOf(const World& world, Entity character)
{
    constexpr int kMaxDepth = 64;
    Entity current = character;
    for (int depth = 0; depth < kMaxDepth && world.isAlive(current); ++depth)
    {
        if (world.has<CharacterController>(current) || world.has<RigidBody>(current))
        {
            return current;
        }
        const Hierarchy* hierarchy = world.tryGet<Hierarchy>(current);
        if (hierarchy == nullptr)
        {
            break;
        }
        current = hierarchy->parent;
    }
    return character;
}

// The lowest height of @p entity's collision shape, from a sphere around it; nullopt for meshes and hulls.
[[nodiscard]] std::optional<f32> bottomOf(const World& world, Entity entity)
{
    const Collider* collider = world.tryGet<Collider>(entity);
    const Transform* transform = world.tryGet<Transform>(entity);
    if (collider == nullptr || transform == nullptr)
    {
        return std::nullopt;
    }
    f32 radius = 0.0f;
    switch (collider->shape)
    {
    case ColliderShape::Box:
        radius = glm::length(collider->halfExtents);
        break;
    case ColliderShape::Sphere:
        radius = collider->radius;
        break;
    case ColliderShape::Capsule:
    case ColliderShape::Cylinder:
        radius = std::hypot(collider->halfHeight + collider->radius, collider->radius);
        break;
    case ColliderShape::ConvexHull:
    case ColliderShape::Mesh:
        return std::nullopt;
    }
    const Vec3 scale = glm::abs(transform->scale);
    const Vec3 center = transform->position + transform->rotation * (collider->offset * transform->scale);
    return center.y - radius * std::max({scale.x, scale.y, scale.z});
}

// The first hit straight down from @p origin within @p length that is not part of @p character. The body
// that carries the character is ignored; a ray that meets another of its bodies (a socket's) goes on below
// it.
std::optional<RaycastHit> groundBelow(const World& world, const PhysicsWorld& physics, Entity character,
                                      Vec3 origin, f32 length, u32 layerMask)
{
    constexpr int kMaxSkips = 8;
    constexpr f32 kSkipDistance = 1e-3f;
    const Entity carrier = carrierOf(world, character);
    for (int i = 0; i < kMaxSkips && length > 0.0f; ++i)
    {
        std::optional<RaycastHit> hit =
            physics.raycast(origin, Vec3(0.0f, -1.0f, 0.0f), length, layerMask, carrier);
        if (!hit || !partOf(world, hit->entity, character))
        {
            return hit;
        }
        const std::optional<f32> bottom = bottomOf(world, hit->entity);
        if (!bottom)
        {
            return std::nullopt;
        }
        const f32 below = std::min(origin.y - hit->distance, *bottom) - kSkipDistance;
        length -= origin.y - below;
        origin.y = below;
    }
    return std::nullopt;
}

// Foot placement (ADR 0061), before the Animation step: below each foot's animated position of the last
// update, the ground gives the foot its lift and tilt; the pelvis drops by the largest lowering, all of them
// following at FootPlacement::speed. A foot without ground eases its IK off.
void placeFeet(World& world, const PhysicsWorld& physics, f64 step)
{
    ILL_PROFILE_SCOPE();
    const auto seconds = static_cast<f32>(step);
    world.query<const FootPlacement, FootPlacementPose, const Transform>().without<Inactive>().each(
        [&](Entity entity, const FootPlacement& settings, FootPlacementPose& pose,
            const Transform& transform) {
            if (pose.frame == 0)
            {
                return; // no update has posed the feet yet
            }
            const f32 rate = settings.speed * seconds;
            const f32 follow = settings.speed > 0.0f && std::isfinite(rate) ? 1.0f - std::exp(-rate) : 1.0f;
            const f32 maxStep = std::isfinite(settings.maxStep) ? std::max(settings.maxStep, 0.0f) : 0.0f;
            const f32 footHeight = std::isfinite(settings.footHeight) ? settings.footHeight : 0.0f;
            const f32 rayLength =
                std::isfinite(settings.rayLength) ? std::max(settings.rayLength, 0.0f) : 0.0f;
            f32 drop = 0.0f;
            for (u32 f = 0; f < pose.footCount && f < FootPlacementPose::kMaxFeet; ++f)
            {
                FootContact& foot = pose.feet[f];
                const Vec3 origin(foot.animated.x, pose.pelvis.y, foot.animated.z);
                const std::optional<RaycastHit> hit =
                    groundBelow(world, physics, entity, origin, rayLength, settings.layerMask);
                f32 lift = 0.0f;
                f32 weight = 0.0f;
                Quat tilt{1.0f, 0.0f, 0.0f, 0.0f};
                if (hit)
                {
                    // A foot keeps its animated height above the Entity's origin, at least footHeight.
                    const f32 above = std::max(foot.animated.y - transform.position.y, footHeight);
                    lift = std::clamp(hit->point.y + above - foot.animated.y, -maxStep, maxStep);
                    weight = 1.0f;
                    const f32 normalLength = glm::length(hit->normal);
                    const Vec3 normal =
                        normalLength > 0.0f ? hit->normal / normalLength : Vec3(0.0f, 1.0f, 0.0f);
                    const f32 cosine = normal.y;
                    if (cosine > -0.999f && std::isfinite(cosine))
                    {
                        const Vec3 axis = glm::cross(Vec3(0.0f, 1.0f, 0.0f), normal);
                        tilt = glm::normalize(Quat(1.0f + cosine, axis.x, axis.y, axis.z));
                    }
                    drop = std::min(drop, lift);
                }
                foot.lift += (lift - foot.lift) * follow;
                foot.weight += (weight - foot.weight) * follow;
                const Quat from = glm::dot(foot.tilt, tilt) < 0.0f ? -foot.tilt : foot.tilt;
                foot.tilt = glm::normalize(from * (1.0f - follow) + tilt * follow);
            }
            pose.pelvisOffset += (drop - pose.pelvisOffset) * follow;
        });
}

} // namespace

struct GameSession::Impl
{
    explicit Impl(const GameSessionDesc& sessionDesc)
        : desc(sessionDesc)
        , seededGuids(seededGenerator(sessionDesc))
        , physics(world, *sessionDesc.jobs, physicsDesc(sessionDesc.settings.physics))
        , audio(world, *sessionDesc.files, audioDesc(sessionDesc))
        , particles(world)
        , navigation(world, sessionDesc.streamer)
        , ui(world)
        , physicsService(physics)
        , audioService(audio)
        , particleService(particles)
        , inputService(actions)
        , sceneService(world, *sessionDesc.files, &guids())
        , uiService(world, ui, localization)
        , saveService(sessionDesc.saves, saveCloud(sessionDesc))
    {
        gamepads.reserve(kReservedGamepads);
        ui.setAssets(sessionDesc.uiAssets);
        ui.setLocalization(&localization);
        ui.setGlyphs(&glyphs);
    }

    [[nodiscard]] GuidGenerator& guids() noexcept
    {
        return seededGuids ? *seededGuids : GuidGenerator::shared();
    }

    void applySettings()
    {
        const PhysicsSettings& physicsSettings = desc.settings.physics;
        for (u32 a = 0; a < kProjectPhysicsLayers; ++a)
        {
            for (u32 b = a; b < kProjectPhysicsLayers; ++b)
            {
                const bool collide = ((physicsSettings.collisionMatrix[a] >> b) & 1u) != 0 &&
                                     ((physicsSettings.collisionMatrix[b] >> a) & 1u) != 0;
                physics.setLayersCollide(a, b, collide);
            }
        }
        audio.setMasterVolume(desc.settings.audio.masterVolume);
        for (u32 bus = 0; bus < kProjectAudioBuses; ++bus)
        {
            audio.setBusVolume(static_cast<AudioBus>(bus), desc.settings.audio.busVolumes[bus]);
        }
    }

    void updateInput()
    {
        const bool gamepadsReach = focus || desc.settings.input.gamepadsInBackground;
        gamepads.clear();
        if (desc.gamepads != nullptr && gamepadsReach)
        {
            for (const Gamepad* gamepad : desc.gamepads->gamepads())
            {
                gamepads.push_back(&gamepad->state());
            }
        }
        // The modal state and the focus of the last UI update hold for the modules of this one.
        const std::string_view prefix = ui.modalActive() || typing() ? kUiActionPrefix : std::string_view();
        if (actions.exclusivePrefix() != prefix)
        {
            actions.setExclusivePrefix(prefix);
        }
        actions.update(focus ? &input : nullptr, gamepads);
    }

    // True when the last UI update left a UiTextField with the focus.
    [[nodiscard]] bool typing() const noexcept
    {
        const Entity focused = ui.focused();
        return !focused.isNull() && world.isAlive(focused) && world.has<UiTextField>(focused);
    }

    // What the UI reads this update: the cursor, primary button and wheel with the focus, and the ui.*
    // actions; while a text field has the focus, the typed text and editing keys instead of the keyboard's
    // part of ui.navigate and ui.submit (see GameSession).
    [[nodiscard]] UiInput uiInput(f64 deltaSeconds) noexcept
    {
        UiInput result;
        result.deltaSeconds = deltaSeconds;
        // A pointer set by a tool reaches the UI with or without the input focus.
        if (uiPointer || focus)
        {
            result.pointerPixels = uiPointer ? uiPointer->pixels : input.mousePosition() * pointerScale;
            const Vec2 size(uiFrame.targetPixels);
            result.pointerPresent = result.pointerPixels.x >= 0.0f && result.pointerPixels.y >= 0.0f &&
                                    result.pointerPixels.x < size.x && result.pointerPixels.y < size.y;
            result.pointerDown = uiPointer ? uiPointer->down : input.isDown(MouseButton::Left);
            result.wheel = uiPointer ? 0.0f : input.scrollDelta().y;
        }
        const bool editing = typing();
        const bool keys = editing && focus;
        if (const std::optional<ActionId> navigate = actions.findAction(kUiNavigateAction))
        {
            result.navigate = editing ? actions.gamepadVector(*navigate) : actions.vector(*navigate);
            if (keys && input.isDown(Key::Up) != input.isDown(Key::Down))
            {
                result.navigate.y = input.isDown(Key::Up) ? 1.0f : -1.0f;
            }
        }
        if (const std::optional<ActionId> submit = actions.findAction(kUiSubmitAction))
        {
            result.submitDown =
                editing ? actions.gamepadVector(*submit).x > 0.0f ||
                              (keys && (input.isDown(Key::Enter) || input.isDown(Key::KeypadEnter)))
                        : actions.isDown(*submit);
        }
        const f64 elapsed = std::isfinite(deltaSeconds) ? std::max(deltaSeconds, 0.0) : 0.0;
        if (keys)
        {
            result.text = input.text();
            result.caretLeft = repeats[0].update(input, Key::Left, elapsed);
            result.caretRight = repeats[1].update(input, Key::Right, elapsed);
            result.backspace = repeats[2].update(input, Key::Backspace, elapsed);
            result.deleteForward = repeats[3].update(input, Key::Delete, elapsed);
            result.caretHome = input.wasPressed(Key::Home);
            result.caretEnd = input.wasPressed(Key::End);
        }
        if (const std::optional<ActionId> cancel = actions.findAction(kUiCancelAction))
        {
            result.cancelPressed = actions.wasPressed(*cancel);
        }
        return result;
    }

    GameSessionDesc desc;
    std::optional<GuidGenerator> seededGuids; // fixedStep: ids of Entities created while running
    GameStringTable strings;                  // viewed by localization
    text::Localization localization;
    World world;
    PhysicsWorld physics;
    AudioWorld audio;
    ParticleWorld particles;
    NavWorld navigation;
    UiWorld ui;
    UiFrame uiFrame{};
    Vec2 pointerScale{1.0f};
    std::optional<GameUiPointer> uiPointer;
    InputActions actions;
    InputState input;
    ActionGlyphs glyphs{actions, localization};
    std::array<KeyRepeat, 4> repeats{}; // Left, Right, Backspace, Delete of a focused text field
    HierarchyIndex hierarchy;
    PhysicsWorldService physicsService;
    AudioWorldService audioService;
    ParticleWorldService particleService;
    InputActionsService inputService;
    SceneLoaderService sceneService;
    UiWorldService uiService;
    SaveSystemService saveService;
    std::unique_ptr<AnimationWorld> animation;
    std::unique_ptr<AnimationWorldService> animationService;
    std::unique_ptr<PhysicsAssetResolver> physicsAssets;
    std::unique_ptr<RenderAssetResolver> renderAssets;
    std::vector<const GamepadState*> gamepads;
    u64 updates = 0;
    f64 time = 0.0;
    bool started = false;
    bool focus = true;
};

Expected<std::unique_ptr<GameSession>, std::string> GameSession::create(const GameSessionDesc& desc)
{
    ILL_ASSERT(desc.jobs != nullptr, "GameSessionDesc::jobs is required");
    ILL_ASSERT(desc.files != nullptr, "GameSessionDesc::files is required");
    auto impl = std::make_unique<Impl>(desc);
    if (!desc.settings.input.actions.empty())
    {
        if (Expected<void, std::string> loaded =
                loadInputActions(*desc.files, desc.settings.input.actions.path(), impl->actions);
            !loaded)
        {
            return unexpected(std::move(loaded.error()));
        }
    }
    addMissingUiActions(impl->actions);
    impl->applySettings();
    loadGameLocalization(*desc.files, impl->localization, impl->strings);
    if (desc.streamer != nullptr)
    {
        impl->physicsAssets = std::make_unique<PhysicsAssetResolver>(impl->physics, *desc.streamer);
        impl->animation = std::make_unique<AnimationWorld>(impl->world, *desc.streamer);
        impl->animationService = std::make_unique<AnimationWorldService>(*impl->animation);
    }
    if (desc.renderAssets != nullptr)
    {
        impl->renderAssets = std::make_unique<RenderAssetResolver>(*desc.renderAssets);
    }
    return std::unique_ptr<GameSession>(new GameSession(std::move(impl)));
}

GameSession::GameSession(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}

GameSession::~GameSession()
{
    stop();
}

World& GameSession::world() noexcept
{
    return m_impl->world;
}

const World& GameSession::world() const noexcept
{
    return m_impl->world;
}

void GameSession::start()
{
    Impl& impl = *m_impl;
    if (impl.started)
    {
        return;
    }
    impl.started = true;
    if (impl.desc.modules != nullptr && impl.desc.mode == GameSessionMode::Play)
    {
        ServiceSet services;
        services.provide<PhysicsService>(impl.physicsService);
        services.provide<AudioService>(impl.audioService);
        services.provide<ParticleService>(impl.particleService);
        services.provide<InputService>(impl.inputService);
        services.provide<SceneService>(impl.sceneService);
        services.provide<UiService>(impl.uiService);
        if (impl.animationService)
        {
            services.provide<AnimationService>(*impl.animationService);
        }
        if (impl.desc.store != nullptr)
        {
            services.provide<StoreService>(*impl.desc.store);
        }
        if (impl.desc.saves != nullptr)
        {
            services.provide<SaveService>(impl.saveService);
        }
        impl.desc.modules->attachWorld(impl.world, WorldRole::Simulation, services);
    }
}

void GameSession::stop()
{
    Impl& impl = *m_impl;
    if (!impl.started)
    {
        return;
    }
    impl.started = false;
    if (impl.desc.modules != nullptr && impl.desc.mode == GameSessionMode::Play)
    {
        impl.desc.modules->detachWorld(impl.world);
    }
    impl.saveService.cancel();
}

bool GameSession::isStarted() const noexcept
{
    return m_impl->started;
}

void GameSession::update(f64 deltaSeconds)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    const f64 step = impl.desc.fixedStep ? impl.desc.settings.physics.stepSeconds : deltaSeconds;
    if (impl.desc.mode == GameSessionMode::Play)
    {
        impl.updateInput();
        if (impl.started && impl.desc.modules != nullptr)
        {
            impl.desc.modules->update(impl.world, step);
        }
    }
    assignMissingGuids(impl.world, impl.guids());
    if (impl.desc.mode == GameSessionMode::Play && impl.started && impl.desc.saves != nullptr)
    {
        impl.saveService.update(impl.world, &impl.guids(), impl.desc.fixedStep);
    }
    if (impl.desc.fixedStep)
    {
        impl.navigation.waitIdle(impl.desc.jobs);
    }
    impl.navigation.update(static_cast<f32>(step), impl.desc.jobs, impl.started);
    if (impl.started)
    {
        applyNavigation(impl.world, impl.physics, impl.animationService.get());
    }
    if (impl.animation)
    {
        if (impl.started)
        {
            placeFeet(impl.world, impl.physics, step);
        }
        impl.animation->update(static_cast<f32>(step), impl.desc.jobs, impl.started);
        if (impl.started)
        {
            applyRootMotion(impl.world, impl.physics, impl.animation->frame(), step);
            followSockets(impl.world);
        }
    }
    impl.physics.update(step);
    updateTransformHierarchy(impl.world, impl.hierarchy, impl.desc.jobs);
    impl.localization.applyPendingLocale();
    impl.glyphs.refresh();
    if (impl.desc.mode == GameSessionMode::Play)
    {
        impl.ui.update(impl.uiFrame, impl.uiInput(step));
    }
    else
    {
        impl.ui.update(impl.uiFrame);
    }
    // The wind blows before particles and the renderer read it.
    advanceWind(impl.world, static_cast<f32>(step));
    // Particles fall with the physics gravity, which game code may change.
    impl.particles.setGravity(impl.physics.gravity());
    impl.particles.update(static_cast<f32>(step), impl.desc.jobs);
    impl.audio.update(step);
    if (impl.physicsAssets)
    {
        impl.physicsAssets->update();
    }
    if (impl.renderAssets)
    {
        impl.renderAssets->update(impl.world, impl.desc.jobs);
    }
    impl.input.beginFrame();
    ++impl.updates;
    impl.time += step;
}

InputState& GameSession::keyboardMouse() noexcept
{
    return m_impl->input;
}

void GameSession::setInputFocus(bool focus) noexcept
{
    m_impl->focus = focus;
}

bool GameSession::hasInputFocus() const noexcept
{
    return m_impl->focus;
}

InputActions& GameSession::actions() noexcept
{
    return m_impl->actions;
}

PhysicsWorld& GameSession::physics() noexcept
{
    return m_impl->physics;
}

ParticleWorld& GameSession::particles() noexcept
{
    return m_impl->particles;
}

AnimationWorld* GameSession::animation() noexcept
{
    return m_impl->animation.get();
}

NavWorld& GameSession::navigation() noexcept
{
    return m_impl->navigation;
}

AudioWorld& GameSession::audio() noexcept
{
    return m_impl->audio;
}

void GameSession::setUiFrame(const UiFrame& frame, Vec2 pointerScale) noexcept
{
    m_impl->uiFrame = frame;
    m_impl->pointerScale = pointerScale;
}

UiFrame GameSession::uiFrame() const noexcept
{
    return m_impl->uiFrame;
}

void GameSession::setUiPointer(const std::optional<GameUiPointer>& pointer) noexcept
{
    m_impl->uiPointer = pointer;
}

UiWorld& GameSession::ui() noexcept
{
    return m_impl->ui;
}

SaveService* GameSession::saves() noexcept
{
    return m_impl->desc.saves != nullptr ? &m_impl->saveService : nullptr;
}

text::Localization& GameSession::localization() noexcept
{
    return m_impl->localization;
}

GameSessionMode GameSession::mode() const noexcept
{
    return m_impl->desc.mode;
}

const ProjectSettings& GameSession::settings() const noexcept
{
    return m_impl->desc.settings;
}

bool GameSession::exitRequested() const noexcept
{
    return m_impl->sceneService.exitRequested();
}

u64 GameSession::updateCount() const noexcept
{
    return m_impl->updates;
}

f64 GameSession::time() const noexcept
{
    return m_impl->time;
}

} // namespace illusion
