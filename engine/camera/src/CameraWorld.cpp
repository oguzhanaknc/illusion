#include <illusion/camera/CameraComponents.hpp>
#include <illusion/camera/CameraDamping.hpp>
#include <illusion/camera/CameraWorld.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/InputActions.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace illusion
{
namespace
{

// The profile of a shake of strength 1 at its source, before the envelope.
constexpr CameraNoiseSettings kShakeProfile{.positionAmplitude = Vec3(0.06f, 0.06f, 0.03f),
                                            .positionFrequency = Vec3(13.0f, 11.0f, 7.0f),
                                            .rotationAmplitude = Vec3(0.025f, 0.02f, 0.015f),
                                            .rotationFrequency = Vec3(9.0f, 8.0f, 6.0f),
                                            .amplitudeGain = 1.0f,
                                            .frequencyGain = 1.0f};

[[nodiscard]] bool finite(Vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] CameraTarget targetOf(const Transform* transform) noexcept
{
    return transform != nullptr ? CameraTarget{transform->position, transform->rotation} : CameraTarget{};
}

// Yaw about +y of the direction @p rotation's -z axis points along: 0 for -z.
[[nodiscard]] f32 facingYaw(const Quat& rotation) noexcept
{
    const Vec3 forward = rotation * Vec3(0.0f, 0.0f, -1.0f);
    if (std::abs(forward.x) < 1e-6f && std::abs(forward.z) < 1e-6f)
    {
        return 0.0f;
    }
    return std::atan2(-forward.x, -forward.z);
}

[[nodiscard]] Vec2 lookInput(const InputActions* actions, const std::string& action) noexcept
{
    if (actions == nullptr || action.empty())
    {
        return Vec2(0.0f);
    }
    const std::optional<ActionId> id = actions->findAction(action);
    return id ? actions->vector(*id) : Vec2(0.0f);
}

[[nodiscard]] f32 axisInput(const InputActions* actions, const std::string& action) noexcept
{
    if (actions == nullptr || action.empty())
    {
        return 0.0f;
    }
    const std::optional<ActionId> id = actions->findAction(action);
    return id ? actions->value(*id) : 0.0f;
}

[[nodiscard]] CameraBlend blendFor(const CameraBrain& brain, Entity from, Entity to,
                                   const VirtualCamera* toCamera) noexcept
{
    for (const CameraBlendOverride& entry : brain.blends)
    {
        if ((entry.from.isNull() || entry.from == from) && (entry.to.isNull() || entry.to == to))
        {
            return entry.blend;
        }
    }
    if (toCamera != nullptr && toCamera->customBlend)
    {
        return toCamera->blendIn;
    }
    return brain.defaultBlend;
}

[[nodiscard]] bool cuts(const CameraBlend& blend) noexcept
{
    return blend.style == CameraBlendStyle::Cut || !(blend.duration > 0.0f);
}

} // namespace

std::string_view cameraErrorMessage(CameraError error) noexcept
{
    switch (error)
    {
    case CameraError::InvalidArgument:
        return "a value is not finite or out of range";
    case CameraError::TooManyShakes:
        return "too many camera shakes run at once";
    }
    return "unknown camera error";
}

struct CameraWorld::Impl
{
    struct Candidate
    {
        Entity entity;
        i32 priority = 0;
        u64 activation = 0;
    };

    struct RunningShake
    {
        CameraShake shake;
        f32 elapsed = 0.0f;
        u32 seed = 0;
    };

    World* world = nullptr;
    CameraWorldDesc desc;
    CameraStatistics statistics;
    u64 update = 0;
    u64 activations = 0;
    u32 shakeSeeds = 0;
    std::vector<Candidate> candidates;
    std::vector<Entity> brains;
    std::vector<Entity> evaluate;
    std::vector<Entity> scratch;
    std::vector<RunningShake> shakes;
    bool shakesLogged = false;
    // Camera::fovY and Camera::nearPlane, found through reflection: engine/render is not linked.
    ComponentId cameraComponent = kInvalidComponentId;
    u32 fovOffset = 0;
    u32 nearOffset = 0;
    u32 registryCount = 0;

    Impl(World& cameraWorld, const CameraWorldDesc& worldDesc)
        : world(&cameraWorld)
        , desc(worldDesc)
    {
        candidates.reserve(desc.reservedCameras);
        brains.reserve(desc.reservedCameras);
        evaluate.reserve(desc.reservedCameras);
        scratch.reserve(desc.reservedCameras);
        shakes.reserve(desc.maxShakes);
    }

    template <class Saved, class State>
    void syncStates()
    {
        scratch.clear();
        world->query<const Saved>().template without<State>().each(
            [this](Entity entity, const Saved&) { scratch.push_back(entity); });
        for (const Entity entity : scratch)
        {
            world->emplace<State>(entity);
        }
        scratch.clear();
        world->query<const State>().template without<Saved>().each(
            [this](Entity entity, const State&) { scratch.push_back(entity); });
        for (const Entity entity : scratch)
        {
            world->remove<State>(entity);
        }
    }

    Expected<void, CameraError> addShake(const CameraShake& shake)
    {
        if (!finite(shake.origin) || !std::isfinite(shake.strength) || shake.strength < 0.0f ||
            !(shake.radius > 0.0f) || !std::isfinite(shake.radius) || !(shake.duration > 0.0f) ||
            !std::isfinite(shake.duration))
        {
            return unexpected(CameraError::InvalidArgument);
        }
        if (shakes.size() >= desc.maxShakes)
        {
            return unexpected(CameraError::TooManyShakes);
        }
        shakes.push_back(RunningShake{shake, 0.0f, ++shakeSeeds});
        return {};
    }

    void fireImpulses(f32 dt)
    {
        world->query<const CameraImpulse, CameraImpulseState, const Transform>().without<Inactive>().each(
            [this, dt](const CameraImpulse& impulse, CameraImpulseState& state, const Transform& transform) {
                bool fire = false;
                if (!state.started)
                {
                    state.started = true;
                    state.untilNext = impulse.interval;
                    fire = impulse.playOnStart;
                }
                else if (impulse.interval > 0.0f)
                {
                    state.untilNext -= dt;
                    if (state.untilNext <= 0.0f)
                    {
                        fire = true;
                        state.untilNext = std::max(state.untilNext + impulse.interval, 0.0f);
                    }
                }
                if (fire && impulse.strength > 0.0f)
                {
                    const Expected<void, CameraError> added =
                        addShake({transform.position, impulse.strength, impulse.radius, impulse.duration});
                    if (!added && !std::exchange(shakesLogged, true))
                    {
                        ILL_LOG_WARN("Camera", "a CameraImpulse cannot shake: {}",
                                     cameraErrorMessage(added.error()));
                    }
                }
            });
    }

    void gatherCandidates()
    {
        candidates.clear();
        statistics.cameras = 0;
        world->query<const VirtualCamera, VirtualCameraState>().each(
            [this](Entity entity, const VirtualCamera& camera, VirtualCameraState& state) {
                ++statistics.cameras;
                const bool capable = camera.enabled && !world->has<Inactive>(entity);
                if (capable && !state.capable)
                {
                    state.activation = ++activations;
                }
                state.capable = capable;
                if (capable)
                {
                    candidates.push_back({entity, camera.priority, state.activation});
                }
            });
    }

    [[nodiscard]] Entity best() const noexcept
    {
        const Candidate* chosen = nullptr;
        for (const Candidate& candidate : candidates)
        {
            if (chosen == nullptr || candidate.priority > chosen->priority ||
                (candidate.priority == chosen->priority && candidate.activation > chosen->activation))
            {
                chosen = &candidate;
            }
        }
        return chosen != nullptr ? chosen->entity : kNullEntity;
    }

    [[nodiscard]] bool hasState(Entity entity) const
    {
        return !entity.isNull() && world->isAlive(entity) && world->has<VirtualCameraState>(entity);
    }

    // Picks the live camera of every brain and starts blends; lists the cameras to evaluate.
    void chooseLive()
    {
        brains.clear();
        evaluate.clear();
        const Entity chosen = best();
        world->query<const CameraBrain, CameraBrainState>().each(
            [this, chosen](Entity entity, const CameraBrain& brain, CameraBrainState& state) {
                brains.push_back(entity);
                if (state.blending && !state.from.isNull() && !hasState(state.from))
                {
                    state.from = kNullEntity;
                    state.fromPose = state.pose;
                }
                if (chosen.isNull())
                {
                    state.live = kNullEntity;
                    state.from = kNullEntity;
                    state.blending = false;
                    return;
                }
                if (chosen != state.live)
                {
                    const Entity previous = state.live;
                    const CameraBlend blend =
                        blendFor(brain, previous, chosen, world->tryGet<VirtualCamera>(chosen));
                    if (previous.isNull() || !state.hasPose || cuts(blend))
                    {
                        state.blending = false;
                        state.from = kNullEntity;
                    }
                    else
                    {
                        state.from = !state.blending && hasState(previous) ? previous : kNullEntity;
                        state.fromPose = state.pose;
                        state.blend = blend;
                        state.elapsed = 0.0f;
                        state.blending = true;
                    }
                    state.live = chosen;
                }
                evaluate.push_back(state.live);
                if (state.blending && !state.from.isNull())
                {
                    evaluate.push_back(state.from);
                }
            });
        std::ranges::sort(evaluate, {}, &Entity::index);
        const auto [first, last] = std::ranges::unique(evaluate);
        evaluate.erase(first, last);
    }

    [[nodiscard]] const Transform* transformOf(Entity entity) const
    {
        return !entity.isNull() && world->isAlive(entity) ? world->tryGet<Transform>(entity) : nullptr;
    }

    void warnOnce(Entity entity, VirtualCameraState& state, std::string_view what)
    {
        if (!std::exchange(state.warned, true))
        {
            ILL_LOG_WARN("Camera", "virtual camera {} has more than one {} stage: only the first is used",
                         entity.index, what);
        }
    }

    [[nodiscard]] CameraPlacement placeDolly(const CameraDolly& dolly, VirtualCameraState& state,
                                             const Transform& own, const CameraTarget& target, f32 dt) const
    {
        const CameraTarget local{
            relativeTo(Transform{target.position, target.rotation, Vec3(1.0f)}, own).position,
            target.rotation};
        const CameraDollySettings settings{dolly.mode, dolly.position, dolly.closed, dolly.damping};
        const CameraPlacement placed =
            stepCameraDolly(settings, state.dolly, {std::span<const CameraPathPoint>(dolly.path), local}, dt);
        const Transform onTrack = combine(own, Transform{placed.position, placed.rotation, Vec3(1.0f)});
        return {onTrack.position, glm::normalize(onTrack.rotation)};
    }

    [[nodiscard]] CameraPlacement placeArm(const CameraSpringArm& arm, VirtualCameraState& state,
                                           const CameraWorldUpdate& input, Entity ignored,
                                           const CameraTarget& target, f32 dt) const
    {
        if (!state.armAnglesSet)
        {
            state.armAngles = Vec2(arm.yaw, arm.pitch);
            state.armAnglesSet = true;
        }
        const Vec2 look = lookInput(input.actions, arm.lookAction) * arm.lookSpeed * dt;
        state.armAngles.x = wrapCameraAngle(state.armAngles.x + look.x);
        state.armAngles.y =
            std::clamp(state.armAngles.y + look.y, std::min(arm.minPitch, arm.maxPitch), arm.maxPitch);
        const f32 base = arm.alignToTarget ? facingYaw(target.rotation) : 0.0f;
        const Quat rotation = glm::angleAxis(base + state.armAngles.x, Vec3(0.0f, 1.0f, 0.0f)) *
                              glm::angleAxis(state.armAngles.y, Vec3(1.0f, 0.0f, 0.0f));
        const CameraSpringArmSettings settings{
            arm.shoulderOffset,  arm.side,        arm.sideDamping,  arm.armLength, arm.collisionRadius,
            arm.collisionLayers, arm.pullInSpeed, arm.recoverySpeed};
        if (input.collision != nullptr)
        {
            input.collision->setIgnored(ignored);
        }
        return stepCameraSpringArm(settings, state.arm, {target, rotation, input.collision}, dt);
    }

    void evaluateCamera(Entity entity, const CameraWorldUpdate& input, f32 dt)
    {
        const VirtualCamera& camera = world->get<VirtualCamera>(entity);
        VirtualCameraState& state = world->get<VirtualCameraState>(entity);
        if (state.evaluated == 0 || state.evaluated + 1 != update)
        {
            state.follow.initialised = false;
            state.arm.initialised = false;
            state.dolly.initialised = false;
            state.lookAt.initialised = false;
            state.composer.initialised = false;
            state.orbit.smoothYaw = state.orbit.yaw;
            state.orbit.smoothPitch = state.orbit.pitch;
            state.orbit.yawVelocity = 0.0f;
            state.orbit.pitchVelocity = 0.0f;
        }
        state.evaluated = update;
        ++statistics.evaluated;

        const Transform ownTransform =
            world->has<Transform>(entity) ? world->get<Transform>(entity) : Transform{};
        const Transform* followTransform = transformOf(camera.follow);
        const Entity followEntity = followTransform != nullptr ? camera.follow : kNullEntity;
        const CameraTarget target = followTransform != nullptr
                                        ? targetOf(followTransform)
                                        : CameraTarget{ownTransform.position, ownTransform.rotation};
        const Transform* lookTransform =
            transformOf(camera.lookAt) != nullptr ? transformOf(camera.lookAt) : followTransform;

        const CameraFollow* follow = world->tryGet<CameraFollow>(entity);
        const CameraOrbit* orbit = world->tryGet<CameraOrbit>(entity);
        const CameraSpringArm* arm = world->tryGet<CameraSpringArm>(entity);
        const CameraDolly* dolly = world->tryGet<CameraDolly>(entity);
        const CameraLookAt* lookAt = world->tryGet<CameraLookAt>(entity);
        const CameraComposer* composer = world->tryGet<CameraComposer>(entity);
        const int bodies = (follow != nullptr) + (orbit != nullptr) + (arm != nullptr) + (dolly != nullptr);
        if (bodies > 1)
        {
            warnOnce(entity, state, "body");
        }
        if (lookAt != nullptr && composer != nullptr)
        {
            warnOnce(entity, state, "aim");
        }

        CameraPlacement placement{ownTransform.position, ownTransform.rotation};
        if (follow != nullptr)
        {
            const CameraFollowSettings settings{follow->offset, follow->space, follow->damping,
                                                follow->rotationDamping};
            placement = stepCameraFollow(settings, state.follow, target, dt);
        }
        else if (orbit != nullptr)
        {
            const CameraOrbitSettings settings{orbit->yaw,         orbit->pitch,       orbit->minPitch,
                                               orbit->maxPitch,    orbit->limitYaw,    orbit->minYaw,
                                               orbit->maxYaw,      orbit->distance,    orbit->minDistance,
                                               orbit->maxDistance, orbit->pivotOffset, orbit->damping};
            const Vec2 look = lookInput(input.actions, orbit->lookAction) * orbit->lookSpeed * dt;
            const f32 zoom = axisInput(input.actions, orbit->zoomAction) * orbit->zoomSpeed * dt;
            placement = stepCameraOrbit(settings, state.orbit, {target, look.x, look.y, zoom}, dt);
        }
        else if (arm != nullptr)
        {
            placement = placeArm(*arm, state, input, followEntity, target, dt);
        }
        else if (dolly != nullptr)
        {
            placement = placeDolly(*dolly, state, ownTransform, target, dt);
        }

        Quat rotation = placement.rotation;
        if (lookTransform != nullptr && (lookAt != nullptr || composer != nullptr))
        {
            if (lookAt != nullptr)
            {
                const CameraAimInput aim{placement.position, placement.rotation,
                                         lookTransform->position + lookAt->targetOffset};
                rotation = stepCameraLookAt({lookAt->damping}, state.lookAt, aim, dt);
            }
            else
            {
                const CameraAimInput aim{placement.position, placement.rotation,
                                         lookTransform->position + composer->targetOffset};
                const CameraComposerSettings settings{composer->screenPosition, composer->deadZone,
                                                      composer->softZone, composer->damping};
                rotation = stepCameraComposer(settings, state.composer, {aim, camera.fovY, input.aspect}, dt);
            }
        }

        CameraPose pose{placement.position, rotation, camera.fovY, camera.nearPlane, camera.dutch};
        if (const CameraNoise* noise = world->tryGet<CameraNoise>(entity))
        {
            const CameraNoiseSettings settings{noise->positionAmplitude, noise->positionFrequency,
                                               noise->rotationAmplitude, noise->rotationFrequency,
                                               noise->amplitudeGain,     noise->frequencyGain};
            state.noise.seed = noise->seed;
            pose = applyCameraNoise(pose, stepCameraNoise(settings, state.noise, dt));
        }
        state.pose = pose;
    }

    [[nodiscard]] CameraPose shaken(const CameraPose& pose) const noexcept
    {
        CameraNoiseOffset total;
        bool any = false;
        for (const RunningShake& running : shakes)
        {
            const CameraShake& shake = running.shake;
            const f32 distance = glm::length(pose.position - shake.origin);
            if (!(distance < shake.radius))
            {
                continue;
            }
            const f32 envelope = 1.0f - std::clamp(running.elapsed / shake.duration, 0.0f, 1.0f);
            CameraNoiseSettings profile = kShakeProfile;
            profile.amplitudeGain = shake.strength * (1.0f - distance / shake.radius) * envelope * envelope;
            const CameraNoiseOffset offset =
                sampleCameraNoise(profile, running.seed, static_cast<f64>(running.elapsed));
            total.position += offset.position;
            total.rotation += offset.rotation;
            any = true;
        }
        return any ? applyCameraNoise(pose, total) : pose;
    }

    void findCameraFields()
    {
        const ComponentRegistry& registry = world->registry();
        if (cameraComponent != kInvalidComponentId || registry.count() == registryCount)
        {
            return;
        }
        registryCount = registry.count();
        const ComponentId id = registry.findByStableName("illusion.Camera");
        const TypeReflection* type = id != kInvalidComponentId ? registry.reflection(id) : nullptr;
        const FieldInfo* fov = type != nullptr ? type->field("fovY") : nullptr;
        const FieldInfo* nearPlane = type != nullptr ? type->field("nearPlane") : nullptr;
        if (fov != nullptr && nearPlane != nullptr && fov->type == FieldType::F32 &&
            nearPlane->type == FieldType::F32)
        {
            cameraComponent = id;
            fovOffset = fov->offset;
            nearOffset = nearPlane->offset;
        }
    }

    void writeCamera(Entity entity, const CameraPose& pose)
    {
        if (Transform* transform = world->tryGet<Transform>(entity))
        {
            transform->position = pose.position;
            transform->rotation = cameraViewRotation(pose);
            const Hierarchy* hierarchy = world->tryGet<Hierarchy>(entity);
            const Transform* parent = hierarchy != nullptr ? transformOf(hierarchy->parent) : nullptr;
            if (parent != nullptr)
            {
                if (LocalTransform* local = world->tryGet<LocalTransform>(entity))
                {
                    *local = LocalTransform::from(relativeTo(*transform, *parent));
                }
            }
            else if (LocalTransform* local = world->tryGet<LocalTransform>(entity))
            {
                *local = LocalTransform::from(*transform);
            }
        }
        findCameraFields();
        if (cameraComponent != kInvalidComponentId)
        {
            if (void* data = world->componentData(entity, cameraComponent))
            {
                auto* bytes = static_cast<std::byte*>(data);
                *reinterpret_cast<f32*>(bytes + fovOffset) = pose.fovY;
                *reinterpret_cast<f32*>(bytes + nearOffset) = pose.nearPlane;
            }
        }
    }

    void finishBrains(f32 dt)
    {
        for (const Entity entity : brains)
        {
            CameraBrainState& state = world->get<CameraBrainState>(entity);
            if (state.live.isNull())
            {
                continue;
            }
            const CameraPose& live = world->get<VirtualCameraState>(state.live).pose;
            CameraPose pose = live;
            state.weight = 1.0f;
            if (state.blending)
            {
                state.elapsed += dt;
                state.weight = cameraBlendWeight(state.blend, state.elapsed);
                const CameraPose& from =
                    state.from.isNull() ? state.fromPose : world->get<VirtualCameraState>(state.from).pose;
                pose = blendCameraPoses(from, live, state.weight);
                if (state.elapsed >= state.blend.duration)
                {
                    state.blending = false;
                    state.from = kNullEntity;
                    state.weight = 1.0f;
                }
            }
            state.pose = shaken(pose);
            state.hasPose = true;
            writeCamera(entity, state.pose);
        }
    }

    void ageShakes(f32 dt)
    {
        for (RunningShake& running : shakes)
        {
            running.elapsed += dt;
        }
        std::erase_if(shakes,
                      [](const RunningShake& running) { return running.elapsed >= running.shake.duration; });
    }
};

CameraWorld::CameraWorld(World& world, const CameraWorldDesc& desc)
    : m_impl(std::make_unique<Impl>(world, desc))
{
}

CameraWorld::~CameraWorld() = default;

void CameraWorld::update(f32 seconds, const CameraWorldUpdate& input)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    const f32 dt = std::isfinite(seconds) ? std::max(seconds, 0.0f) : 0.0f;
    ++impl.update;
    impl.statistics.evaluated = 0;
    impl.syncStates<VirtualCamera, VirtualCameraState>();
    impl.syncStates<CameraBrain, CameraBrainState>();
    impl.syncStates<CameraImpulse, CameraImpulseState>();
    impl.fireImpulses(dt);
    impl.gatherCandidates();
    impl.chooseLive();
    for (const Entity entity : impl.evaluate)
    {
        impl.evaluateCamera(entity, input, dt);
    }
    if (input.collision != nullptr)
    {
        input.collision->setIgnored(kNullEntity);
    }
    impl.finishBrains(dt);
    impl.ageShakes(dt);
    impl.statistics.brains = static_cast<u32>(impl.brains.size());
    impl.statistics.shakes = static_cast<u32>(impl.shakes.size());
}

Expected<void, CameraError> CameraWorld::shake(const CameraShake& shake)
{
    return m_impl->addShake(shake);
}

const CameraStatistics& CameraWorld::statistics() const noexcept
{
    return m_impl->statistics;
}

World& CameraWorld::world() const noexcept
{
    return *m_impl->world;
}

} // namespace illusion
