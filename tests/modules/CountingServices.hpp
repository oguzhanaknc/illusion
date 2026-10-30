#pragma once

// Counting proxies of the four service interfaces for the module tests. A class that implements a pure
// interface does not compile until it implements all of it, so each proxy covers every function; each
// function counts its calls in its own slot and returns a default value.

#include <illusion/audio/AudioService.hpp>
#include <illusion/input/InputService.hpp>
#include <illusion/physics/PhysicsService.hpp>
#include <illusion/runtime/SceneService.hpp>

#include <algorithm>
#include <array>

namespace illusion::test
{

// Counts the calls of every PhysicsService function and returns default values.
class CountingPhysics final : public PhysicsService
{
public:
    static constexpr usize kFunctions = 27;
    mutable std::array<u32, kFunctions> calls{};

    [[nodiscard]] bool everyFunctionCalledOnce() const noexcept
    {
        return std::ranges::all_of(calls, [](u32 n) { return n == 1; });
    }

    void setGravity(const Vec3&) override { count(0); }
    [[nodiscard]] Vec3 gravity() const override
    {
        count(1);
        return {};
    }
    void setLayersCollide(u32, u32, bool) override { count(2); }
    [[nodiscard]] bool layersCollide(u32, u32) const override
    {
        count(3);
        return {};
    }
    void rebuild(Entity) override { count(4); }
    void setLinearVelocity(Entity, const Vec3&) override { count(5); }
    [[nodiscard]] Vec3 linearVelocity(Entity) const override
    {
        count(6);
        return {};
    }
    void setAngularVelocity(Entity, const Vec3&) override { count(7); }
    [[nodiscard]] Vec3 angularVelocity(Entity) const override
    {
        count(8);
        return {};
    }
    void addForce(Entity, const Vec3&) override { count(9); }
    void addTorque(Entity, const Vec3&) override { count(10); }
    void addImpulse(Entity, const Vec3&) override { count(11); }
    [[nodiscard]] f32 mass(Entity) const override
    {
        count(12);
        return {};
    }
    [[nodiscard]] bool isAwake(Entity) const override
    {
        count(13);
        return {};
    }
    PhysicsMeshHandle createMesh(std::span<const Vec3>, std::span<const u32>) override
    {
        count(14);
        return {};
    }
    PhysicsMeshHandle createConvexHull(std::span<const Vec3>) override
    {
        count(15);
        return {};
    }
    void destroyMesh(PhysicsMeshHandle) override { count(16); }
    [[nodiscard]] bool isMeshValid(PhysicsMeshHandle) const override
    {
        count(17);
        return {};
    }
    void setCharacterVelocity(Entity, const Vec3&) override { count(18); }
    [[nodiscard]] Vec3 characterVelocity(Entity) const override
    {
        count(19);
        return {};
    }
    [[nodiscard]] bool isCharacterGrounded(Entity) const override
    {
        count(20);
        return {};
    }
    [[nodiscard]] f32 jointPosition(Entity) const override
    {
        count(21);
        return {};
    }
    [[nodiscard]] std::span<const Entity> brokenJoints() const override
    {
        count(22);
        return {};
    }
    [[nodiscard]] std::optional<RaycastHit> raycast(const Vec3&, const Vec3&, f32, u32, Entity) const override
    {
        count(23);
        return {};
    }
    [[nodiscard]] std::optional<ShapeCastHit> shapeCast(const Collider&, const Vec3&, const Quat&,
                                                        const Vec3&, f32, u32, Entity) const override
    {
        count(24);
        return {};
    }
    [[nodiscard]] u32 overlap(const Collider&, const Vec3&, const Quat&, std::span<Entity>, u32,
                              Entity) const override
    {
        count(25);
        return {};
    }
    [[nodiscard]] std::span<const ContactEvent> contactEvents() const override
    {
        count(26);
        return {};
    }

private:
    void count(usize function) const noexcept { ++calls[function]; }
};

// Counts the calls of every AudioService function and returns default values.
class CountingAudio final : public AudioService
{
public:
    static constexpr usize kFunctions = 22;
    mutable std::array<u32, kFunctions> calls{};

    [[nodiscard]] bool everyFunctionCalledOnce() const noexcept
    {
        return std::ranges::all_of(calls, [](u32 n) { return n == 1; });
    }

    AudioClipHandle loadClip(std::string_view, AudioClipMode) override
    {
        count(0);
        return {};
    }
    AudioClipHandle requestClip(std::string_view, AudioClipMode) override
    {
        count(1);
        return {};
    }
    [[nodiscard]] AssetState clipState(AudioClipHandle) const override
    {
        count(2);
        return {};
    }
    AudioClipHandle createClip(std::span<const f32>, u32, u32) override
    {
        count(3);
        return {};
    }
    void unloadClip(AudioClipHandle) override { count(4); }
    [[nodiscard]] std::optional<AudioClipInfo> clipInfo(AudioClipHandle) const override
    {
        count(5);
        return {};
    }
    void play(Entity) override { count(6); }
    void stop(Entity) override { count(7); }
    [[nodiscard]] bool isPlaying(Entity) const override
    {
        count(8);
        return {};
    }
    AudioVoiceHandle playOneShot(AudioClipHandle, const AudioPlayParams&) override
    {
        count(9);
        return {};
    }
    void stopVoice(AudioVoiceHandle) override { count(10); }
    [[nodiscard]] bool isVoicePlaying(AudioVoiceHandle) const override
    {
        count(11);
        return {};
    }
    void setMasterVolume(f32) override { count(12); }
    [[nodiscard]] f32 masterVolume() const override
    {
        count(13);
        return {};
    }
    void setBusVolume(AudioBus, f32) override { count(14); }
    [[nodiscard]] f32 busVolume(AudioBus) const override
    {
        count(15);
        return {};
    }
    void setBusPaused(AudioBus, bool) override { count(16); }
    [[nodiscard]] bool isBusPaused(AudioBus) const override
    {
        count(17);
        return {};
    }
    void setBusFilter(AudioBus, const AudioBusFilter&) override { count(18); }
    [[nodiscard]] AudioBusFilter busFilter(AudioBus) const override
    {
        count(19);
        return {};
    }
    void setReverb(const AudioReverbSettings&) override { count(20); }
    [[nodiscard]] AudioReverbSettings reverb() const override
    {
        count(21);
        return {};
    }

private:
    void count(usize function) const noexcept { ++calls[function]; }
};

// Counts the calls of every InputService function and returns default values.
class CountingInput final : public InputService
{
public:
    static constexpr usize kFunctions = 14;
    mutable std::array<u32, kFunctions> calls{};

    [[nodiscard]] bool everyFunctionCalledOnce() const noexcept
    {
        return std::ranges::all_of(calls, [](u32 n) { return n == 1; });
    }

    std::optional<ActionId> addAction(std::string_view, ActionType) override
    {
        count(0);
        return {};
    }
    [[nodiscard]] std::optional<ActionId> findAction(std::string_view) const override
    {
        count(1);
        return {};
    }
    [[nodiscard]] u32 actionCount() const override
    {
        count(2);
        return {};
    }
    [[nodiscard]] std::string_view actionName(ActionId) const override
    {
        count(3);
        return {};
    }
    [[nodiscard]] ActionType actionType(ActionId) const override
    {
        count(4);
        return {};
    }
    [[nodiscard]] std::span<const InputBinding> bindings(ActionId) const override
    {
        count(5);
        return {};
    }
    void setBindings(ActionId, std::span<const InputBinding>) override { count(6); }
    [[nodiscard]] bool isDown(ActionId) const override
    {
        count(7);
        return {};
    }
    [[nodiscard]] bool wasPressed(ActionId) const override
    {
        count(8);
        return {};
    }
    [[nodiscard]] bool wasReleased(ActionId) const override
    {
        count(9);
        return {};
    }
    [[nodiscard]] f32 value(ActionId) const override
    {
        count(10);
        return {};
    }
    [[nodiscard]] Vec2 vector(ActionId) const override
    {
        count(11);
        return {};
    }
    [[nodiscard]] InputDevice lastDevice() const override
    {
        count(12);
        return {};
    }
    [[nodiscard]] GamepadType lastGamepadType() const override
    {
        count(13);
        return {};
    }

private:
    void count(usize function) const noexcept { ++calls[function]; }
};

// Counts the calls of every SceneService function and returns default values.
class CountingScenes final : public SceneService
{
public:
    static constexpr usize kFunctions = 3;
    mutable std::array<u32, kFunctions> calls{};

    [[nodiscard]] bool everyFunctionCalledOnce() const noexcept
    {
        return std::ranges::all_of(calls, [](u32 n) { return n == 1; });
    }

    [[nodiscard]] bool exists(std::string_view) const override
    {
        count(0);
        return {};
    }
    std::optional<u32> instantiate(std::string_view, std::span<Entity>) override
    {
        count(1);
        return {};
    }
    [[nodiscard]] std::string_view lastError() const override
    {
        count(2);
        return {};
    }

private:
    void count(usize function) const noexcept { ++calls[function]; }
};

} // namespace illusion::test
