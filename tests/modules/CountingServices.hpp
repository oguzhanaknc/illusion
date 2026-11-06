#pragma once

// Counting proxies of the four service interfaces for the module tests. A class that implements a pure
// interface does not compile until it implements all of it, so each proxy covers every function; each
// function counts its calls in its own slot. A proxy made with a real service forwards every call to it;
// one made without returns default values.

#include <illusion/audio/AudioService.hpp>
#include <illusion/input/InputService.hpp>
#include <illusion/physics/PhysicsService.hpp>
#include <illusion/runtime/SceneService.hpp>

#include <algorithm>
#include <array>
#include <type_traits>

namespace illusion::test
{

// The call counters and the forwarding of one proxy of @p Service with @p Functions functions.
template <class Service, usize Functions>
class CountingProxy : public Service
{
public:
    static constexpr usize kFunctions = Functions;
    mutable std::array<u32, kFunctions> calls{};

    [[nodiscard]] bool everyFunctionCalledOnce() const noexcept
    {
        return std::ranges::all_of(calls, [](u32 n) { return n == 1; });
    }

    [[nodiscard]] bool everyFunctionCalled() const noexcept
    {
        return std::ranges::all_of(calls, [](u32 n) { return n > 0; });
    }

protected:
    explicit CountingProxy(Service* inner) noexcept
        : m_inner(inner)
    {
    }

    // Counts a call of function @p index, then runs @p call on the real service (if any).
    template <class R, class F>
    R forward(usize index, F&& call) const
    {
        ++calls[index];
        if constexpr (std::is_void_v<R>)
        {
            if (m_inner != nullptr)
            {
                call(*m_inner);
            }
        }
        else
        {
            return m_inner != nullptr ? call(*m_inner) : R{};
        }
    }

private:
    Service* m_inner;
};

// Counts the calls of every PhysicsService function.
class CountingPhysics final : public CountingProxy<PhysicsService, 27>
{
public:
    explicit CountingPhysics(PhysicsService* inner = nullptr) noexcept
        : CountingProxy(inner)
    {
    }

    void setGravity(const Vec3& g) override
    {
        forward<void>(0, [&](PhysicsService& s) { s.setGravity(g); });
    }
    [[nodiscard]] Vec3 gravity() const override
    {
        return forward<Vec3>(1, [&](PhysicsService& s) { return s.gravity(); });
    }
    void setLayersCollide(u32 a, u32 b, bool collide) override
    {
        forward<void>(2, [&](PhysicsService& s) { s.setLayersCollide(a, b, collide); });
    }
    [[nodiscard]] bool layersCollide(u32 a, u32 b) const override
    {
        return forward<bool>(3, [&](PhysicsService& s) { return s.layersCollide(a, b); });
    }
    void rebuild(Entity e) override
    {
        forward<void>(4, [&](PhysicsService& s) { s.rebuild(e); });
    }
    void setLinearVelocity(Entity e, const Vec3& v) override
    {
        forward<void>(5, [&](PhysicsService& s) { s.setLinearVelocity(e, v); });
    }
    [[nodiscard]] Vec3 linearVelocity(Entity e) const override
    {
        return forward<Vec3>(6, [&](PhysicsService& s) { return s.linearVelocity(e); });
    }
    void setAngularVelocity(Entity e, const Vec3& v) override
    {
        forward<void>(7, [&](PhysicsService& s) { s.setAngularVelocity(e, v); });
    }
    [[nodiscard]] Vec3 angularVelocity(Entity e) const override
    {
        return forward<Vec3>(8, [&](PhysicsService& s) { return s.angularVelocity(e); });
    }
    void addForce(Entity e, const Vec3& v) override
    {
        forward<void>(9, [&](PhysicsService& s) { s.addForce(e, v); });
    }
    void addTorque(Entity e, const Vec3& v) override
    {
        forward<void>(10, [&](PhysicsService& s) { s.addTorque(e, v); });
    }
    void addImpulse(Entity e, const Vec3& v) override
    {
        forward<void>(11, [&](PhysicsService& s) { s.addImpulse(e, v); });
    }
    [[nodiscard]] f32 mass(Entity e) const override
    {
        return forward<f32>(12, [&](PhysicsService& s) { return s.mass(e); });
    }
    [[nodiscard]] bool isAwake(Entity e) const override
    {
        return forward<bool>(13, [&](PhysicsService& s) { return s.isAwake(e); });
    }
    PhysicsMeshHandle createMesh(std::span<const Vec3> vertices, std::span<const u32> indices) override
    {
        return forward<PhysicsMeshHandle>(14,
                                          [&](PhysicsService& s) { return s.createMesh(vertices, indices); });
    }
    PhysicsMeshHandle createConvexHull(std::span<const Vec3> points) override
    {
        return forward<PhysicsMeshHandle>(15, [&](PhysicsService& s) { return s.createConvexHull(points); });
    }
    void destroyMesh(PhysicsMeshHandle mesh) override
    {
        forward<void>(16, [&](PhysicsService& s) { s.destroyMesh(mesh); });
    }
    [[nodiscard]] bool isMeshValid(PhysicsMeshHandle mesh) const override
    {
        return forward<bool>(17, [&](PhysicsService& s) { return s.isMeshValid(mesh); });
    }
    void setCharacterVelocity(Entity e, const Vec3& v) override
    {
        forward<void>(18, [&](PhysicsService& s) { s.setCharacterVelocity(e, v); });
    }
    [[nodiscard]] Vec3 characterVelocity(Entity e) const override
    {
        return forward<Vec3>(19, [&](PhysicsService& s) { return s.characterVelocity(e); });
    }
    [[nodiscard]] bool isCharacterGrounded(Entity e) const override
    {
        return forward<bool>(20, [&](PhysicsService& s) { return s.isCharacterGrounded(e); });
    }
    [[nodiscard]] f32 jointPosition(Entity e) const override
    {
        return forward<f32>(21, [&](PhysicsService& s) { return s.jointPosition(e); });
    }
    [[nodiscard]] std::span<const Entity> brokenJoints() const override
    {
        return forward<std::span<const Entity>>(22, [&](PhysicsService& s) { return s.brokenJoints(); });
    }
    [[nodiscard]] std::optional<RaycastHit> raycast(const Vec3& origin, const Vec3& direction,
                                                    f32 maxDistance, u32 layerMask,
                                                    Entity ignore) const override
    {
        return forward<std::optional<RaycastHit>>(23, [&](PhysicsService& s) {
            return s.raycast(origin, direction, maxDistance, layerMask, ignore);
        });
    }
    [[nodiscard]] std::optional<ShapeCastHit> shapeCast(const Collider& shape, const Vec3& position,
                                                        const Quat& rotation, const Vec3& direction,
                                                        f32 maxDistance, u32 layerMask,
                                                        Entity ignore) const override
    {
        return forward<std::optional<ShapeCastHit>>(24, [&](PhysicsService& s) {
            return s.shapeCast(shape, position, rotation, direction, maxDistance, layerMask, ignore);
        });
    }
    [[nodiscard]] u32 overlap(const Collider& shape, const Vec3& position, const Quat& rotation,
                              std::span<Entity> out, u32 layerMask, Entity ignore) const override
    {
        return forward<u32>(25, [&](PhysicsService& s) {
            return s.overlap(shape, position, rotation, out, layerMask, ignore);
        });
    }
    [[nodiscard]] std::span<const ContactEvent> contactEvents() const override
    {
        return forward<std::span<const ContactEvent>>(26,
                                                      [&](PhysicsService& s) { return s.contactEvents(); });
    }
};

// Counts the calls of every AudioService function.
class CountingAudio final : public CountingProxy<AudioService, 22>
{
public:
    explicit CountingAudio(AudioService* inner = nullptr) noexcept
        : CountingProxy(inner)
    {
    }

    AudioClipHandle loadClip(std::string_view path, AudioClipMode mode) override
    {
        return forward<AudioClipHandle>(0, [&](AudioService& s) { return s.loadClip(path, mode); });
    }
    AudioClipHandle requestClip(std::string_view path, AudioClipMode mode) override
    {
        return forward<AudioClipHandle>(1, [&](AudioService& s) { return s.requestClip(path, mode); });
    }
    [[nodiscard]] AssetState clipState(AudioClipHandle clip) const override
    {
        return forward<AssetState>(2, [&](AudioService& s) { return s.clipState(clip); });
    }
    AudioClipHandle createClip(std::span<const f32> samples, u32 channels, u32 sampleRate) override
    {
        return forward<AudioClipHandle>(
            3, [&](AudioService& s) { return s.createClip(samples, channels, sampleRate); });
    }
    void unloadClip(AudioClipHandle clip) override
    {
        forward<void>(4, [&](AudioService& s) { s.unloadClip(clip); });
    }
    [[nodiscard]] std::optional<AudioClipInfo> clipInfo(AudioClipHandle clip) const override
    {
        return forward<std::optional<AudioClipInfo>>(5, [&](AudioService& s) { return s.clipInfo(clip); });
    }
    void play(Entity e) override
    {
        forward<void>(6, [&](AudioService& s) { s.play(e); });
    }
    void stop(Entity e) override
    {
        forward<void>(7, [&](AudioService& s) { s.stop(e); });
    }
    [[nodiscard]] bool isPlaying(Entity e) const override
    {
        return forward<bool>(8, [&](AudioService& s) { return s.isPlaying(e); });
    }
    AudioVoiceHandle playOneShot(AudioClipHandle clip, const AudioPlayParams& params) override
    {
        return forward<AudioVoiceHandle>(9, [&](AudioService& s) { return s.playOneShot(clip, params); });
    }
    void stopVoice(AudioVoiceHandle voice) override
    {
        forward<void>(10, [&](AudioService& s) { s.stopVoice(voice); });
    }
    [[nodiscard]] bool isVoicePlaying(AudioVoiceHandle voice) const override
    {
        return forward<bool>(11, [&](AudioService& s) { return s.isVoicePlaying(voice); });
    }
    void setMasterVolume(f32 volume) override
    {
        forward<void>(12, [&](AudioService& s) { s.setMasterVolume(volume); });
    }
    [[nodiscard]] f32 masterVolume() const override
    {
        return forward<f32>(13, [&](AudioService& s) { return s.masterVolume(); });
    }
    void setBusVolume(AudioBus bus, f32 volume) override
    {
        forward<void>(14, [&](AudioService& s) { s.setBusVolume(bus, volume); });
    }
    [[nodiscard]] f32 busVolume(AudioBus bus) const override
    {
        return forward<f32>(15, [&](AudioService& s) { return s.busVolume(bus); });
    }
    void setBusPaused(AudioBus bus, bool paused) override
    {
        forward<void>(16, [&](AudioService& s) { s.setBusPaused(bus, paused); });
    }
    [[nodiscard]] bool isBusPaused(AudioBus bus) const override
    {
        return forward<bool>(17, [&](AudioService& s) { return s.isBusPaused(bus); });
    }
    void setBusFilter(AudioBus bus, const AudioBusFilter& filter) override
    {
        forward<void>(18, [&](AudioService& s) { s.setBusFilter(bus, filter); });
    }
    [[nodiscard]] AudioBusFilter busFilter(AudioBus bus) const override
    {
        return forward<AudioBusFilter>(19, [&](AudioService& s) { return s.busFilter(bus); });
    }
    void setReverb(const AudioReverbSettings& settings) override
    {
        forward<void>(20, [&](AudioService& s) { s.setReverb(settings); });
    }
    [[nodiscard]] AudioReverbSettings reverb() const override
    {
        return forward<AudioReverbSettings>(21, [&](AudioService& s) { return s.reverb(); });
    }
};

// Counts the calls of every InputService function.
class CountingInput final : public CountingProxy<InputService, 14>
{
public:
    explicit CountingInput(InputService* inner = nullptr) noexcept
        : CountingProxy(inner)
    {
    }

    std::optional<ActionId> addAction(std::string_view name, ActionType type) override
    {
        return forward<std::optional<ActionId>>(0, [&](InputService& s) { return s.addAction(name, type); });
    }
    [[nodiscard]] std::optional<ActionId> findAction(std::string_view name) const override
    {
        return forward<std::optional<ActionId>>(1, [&](InputService& s) { return s.findAction(name); });
    }
    [[nodiscard]] u32 actionCount() const override
    {
        return forward<u32>(2, [&](InputService& s) { return s.actionCount(); });
    }
    [[nodiscard]] std::string_view actionName(ActionId action) const override
    {
        return forward<std::string_view>(3, [&](InputService& s) { return s.actionName(action); });
    }
    [[nodiscard]] ActionType actionType(ActionId action) const override
    {
        return forward<ActionType>(4, [&](InputService& s) { return s.actionType(action); });
    }
    [[nodiscard]] std::span<const InputBinding> bindings(ActionId action) const override
    {
        return forward<std::span<const InputBinding>>(5, [&](InputService& s) { return s.bindings(action); });
    }
    void setBindings(ActionId action, std::span<const InputBinding> bindings) override
    {
        forward<void>(6, [&](InputService& s) { s.setBindings(action, bindings); });
    }
    [[nodiscard]] bool isDown(ActionId action) const override
    {
        return forward<bool>(7, [&](InputService& s) { return s.isDown(action); });
    }
    [[nodiscard]] bool wasPressed(ActionId action) const override
    {
        return forward<bool>(8, [&](InputService& s) { return s.wasPressed(action); });
    }
    [[nodiscard]] bool wasReleased(ActionId action) const override
    {
        return forward<bool>(9, [&](InputService& s) { return s.wasReleased(action); });
    }
    [[nodiscard]] f32 value(ActionId action) const override
    {
        return forward<f32>(10, [&](InputService& s) { return s.value(action); });
    }
    [[nodiscard]] Vec2 vector(ActionId action) const override
    {
        return forward<Vec2>(11, [&](InputService& s) { return s.vector(action); });
    }
    [[nodiscard]] InputDevice lastDevice() const override
    {
        return forward<InputDevice>(12, [&](InputService& s) { return s.lastDevice(); });
    }
    [[nodiscard]] GamepadType lastGamepadType() const override
    {
        return forward<GamepadType>(13, [&](InputService& s) { return s.lastGamepadType(); });
    }
};

// Counts the calls of every SceneService function.
class CountingScenes final : public CountingProxy<SceneService, 3>
{
public:
    explicit CountingScenes(SceneService* inner = nullptr) noexcept
        : CountingProxy(inner)
    {
    }

    [[nodiscard]] bool exists(std::string_view path) const override
    {
        return forward<bool>(0, [&](SceneService& s) { return s.exists(path); });
    }
    std::optional<u32> instantiate(std::string_view path, std::span<Entity> created) override
    {
        return forward<std::optional<u32>>(1, [&](SceneService& s) { return s.instantiate(path, created); });
    }
    [[nodiscard]] std::string_view lastError() const override
    {
        return forward<std::string_view>(2, [&](SceneService& s) { return s.lastError(); });
    }
};

} // namespace illusion::test
