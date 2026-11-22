#pragma once

// Counting proxies of the service interfaces for the module tests. A class that implements a pure
// interface does not compile until it implements all of it, so each proxy covers every function; each
// function counts its calls in its own slot. A proxy made with a real service forwards every call to it;
// one made without returns default values.

#include <illusion/animation/AnimationService.hpp>
#include <illusion/audio/AudioService.hpp>
#include <illusion/input/InputService.hpp>
#include <illusion/nav/NavService.hpp>
#include <illusion/particles/ParticleService.hpp>
#include <illusion/physics/PhysicsService.hpp>
#include <illusion/runtime/SceneService.hpp>
#include <illusion/save/SaveService.hpp>
#include <illusion/store/StoreService.hpp>
#include <illusion/ui/UiService.hpp>

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

// Counts the calls of every ParticleService function.
class CountingParticles final : public CountingProxy<ParticleService, 7>
{
public:
    explicit CountingParticles(ParticleService* inner = nullptr) noexcept
        : CountingProxy(inner)
    {
    }

    bool play(Entity e) override
    {
        return forward<bool>(0, [&](ParticleService& s) { return s.play(e); });
    }
    bool stop(Entity e) override
    {
        return forward<bool>(1, [&](ParticleService& s) { return s.stop(e); });
    }
    bool clear(Entity e) override
    {
        return forward<bool>(2, [&](ParticleService& s) { return s.clear(e); });
    }
    u32 emit(Entity e, u32 count) override
    {
        return forward<u32>(3, [&](ParticleService& s) { return s.emit(e, count); });
    }
    [[nodiscard]] u32 aliveCount(Entity e) const override
    {
        return forward<u32>(4, [&](ParticleService& s) { return s.aliveCount(e); });
    }
    [[nodiscard]] bool isPlaying(Entity e) const override
    {
        return forward<bool>(5, [&](ParticleService& s) { return s.isPlaying(e); });
    }
    [[nodiscard]] u64 totalAlive() const override
    {
        return forward<u64>(6, [&](ParticleService& s) { return s.totalAlive(); });
    }
};

// Counts the calls of every SceneService function.
class CountingScenes final : public CountingProxy<SceneService, 4>
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
    void requestExit() override
    {
        forward<void>(3, [&](SceneService& s) { s.requestExit(); });
    }
};

// Counts the calls of every StoreService function.
class CountingStore final : public CountingProxy<StoreService, 23>
{
public:
    explicit CountingStore(StoreService* inner = nullptr) noexcept
        : CountingProxy(inner)
    {
    }

    using Void = Expected<void, StoreError>;

    [[nodiscard]] bool available() const override
    {
        return forward<bool>(0, [&](StoreService& s) { return s.available(); });
    }
    [[nodiscard]] std::string_view storeName() const override
    {
        return forward<std::string_view>(1, [&](StoreService& s) { return s.storeName(); });
    }
    [[nodiscard]] std::string_view userName() const override
    {
        return forward<std::string_view>(2, [&](StoreService& s) { return s.userName(); });
    }
    [[nodiscard]] u64 userId() const override
    {
        return forward<u64>(3, [&](StoreService& s) { return s.userId(); });
    }
    Void unlockAchievement(std::string_view name) override
    {
        return forward<Void>(4, [&](StoreService& s) { return s.unlockAchievement(name); });
    }
    Void clearAchievement(std::string_view name) override
    {
        return forward<Void>(5, [&](StoreService& s) { return s.clearAchievement(name); });
    }
    [[nodiscard]] Expected<bool, StoreError> isAchievementUnlocked(std::string_view name) const override
    {
        return forward<Expected<bool, StoreError>>(
            6, [&](StoreService& s) { return s.isAchievementUnlocked(name); });
    }
    Void setAchievementProgress(std::string_view name, AchievementProgress progress) override
    {
        return forward<Void>(7, [&](StoreService& s) { return s.setAchievementProgress(name, progress); });
    }
    [[nodiscard]] Expected<AchievementProgress, StoreError>
    achievementProgress(std::string_view name) const override
    {
        return forward<Expected<AchievementProgress, StoreError>>(
            8, [&](StoreService& s) { return s.achievementProgress(name); });
    }
    Void setStatInt(std::string_view name, i32 value) override
    {
        return forward<Void>(9, [&](StoreService& s) { return s.setStatInt(name, value); });
    }
    [[nodiscard]] Expected<i32, StoreError> statInt(std::string_view name) const override
    {
        return forward<Expected<i32, StoreError>>(10, [&](StoreService& s) { return s.statInt(name); });
    }
    Void setStatFloat(std::string_view name, f32 value) override
    {
        return forward<Void>(11, [&](StoreService& s) { return s.setStatFloat(name, value); });
    }
    [[nodiscard]] Expected<f32, StoreError> statFloat(std::string_view name) const override
    {
        return forward<Expected<f32, StoreError>>(12, [&](StoreService& s) { return s.statFloat(name); });
    }
    Void storeStats() override
    {
        return forward<Void>(13, [&](StoreService& s) { return s.storeStats(); });
    }
    Void writeCloudFile(std::string_view name, std::span<const std::byte> data) override
    {
        return forward<Void>(14, [&](StoreService& s) { return s.writeCloudFile(name, data); });
    }
    Expected<u64, StoreError> readCloudFile(std::string_view name, std::span<std::byte> out) override
    {
        return forward<Expected<u64, StoreError>>(
            15, [&](StoreService& s) { return s.readCloudFile(name, out); });
    }
    Void removeCloudFile(std::string_view name) override
    {
        return forward<Void>(16, [&](StoreService& s) { return s.removeCloudFile(name); });
    }
    u32 listCloudFiles(std::span<std::string_view> out) override
    {
        return forward<u32>(17, [&](StoreService& s) { return s.listCloudFiles(out); });
    }
    [[nodiscard]] bool cloudFileExists(std::string_view name) const override
    {
        return forward<bool>(18, [&](StoreService& s) { return s.cloudFileExists(name); });
    }
    [[nodiscard]] Expected<StoreQuota, StoreError> cloudQuota() const override
    {
        return forward<Expected<StoreQuota, StoreError>>(19, [&](StoreService& s) { return s.cloudQuota(); });
    }
    [[nodiscard]] bool overlayActive() const override
    {
        return forward<bool>(20, [&](StoreService& s) { return s.overlayActive(); });
    }
    bool openOverlay(StoreOverlayPage page) override
    {
        return forward<bool>(21, [&](StoreService& s) { return s.openOverlay(page); });
    }
    [[nodiscard]] std::optional<std::string_view> glyphPath(GamepadOrigin origin) const override
    {
        return forward<std::optional<std::string_view>>(22,
                                                        [&](StoreService& s) { return s.glyphPath(origin); });
    }
};

// Counts the calls of every UiService function.
class CountingUi final : public CountingProxy<UiService, 19>
{
public:
    explicit CountingUi(UiService* inner = nullptr) noexcept
        : CountingProxy(inner)
    {
    }

    [[nodiscard]] Entity find(Entity root, std::string_view name) const override
    {
        return forward<Entity>(0, [&](UiService& s) { return s.find(root, name); });
    }
    bool setText(Entity e, std::string_view utf8) override
    {
        return forward<bool>(1, [&](UiService& s) { return s.setText(e, utf8); });
    }
    bool setTextKey(Entity e, std::string_view key, std::span<const UiTextArgument> arguments) override
    {
        return forward<bool>(2, [&](UiService& s) { return s.setTextKey(e, key, arguments); });
    }
    bool setVisible(Entity e, bool visible) override
    {
        return forward<bool>(3, [&](UiService& s) { return s.setVisible(e, visible); });
    }
    [[nodiscard]] bool isVisible(Entity e) const override
    {
        return forward<bool>(4, [&](UiService& s) { return s.isVisible(e); });
    }
    bool setInteractable(Entity e, bool enabled) override
    {
        return forward<bool>(5, [&](UiService& s) { return s.setInteractable(e, enabled); });
    }
    bool setToggle(Entity e, bool on) override
    {
        return forward<bool>(6, [&](UiService& s) { return s.setToggle(e, on); });
    }
    [[nodiscard]] std::optional<bool> toggle(Entity e) const override
    {
        return forward<std::optional<bool>>(7, [&](UiService& s) { return s.toggle(e); });
    }
    bool setSliderValue(Entity e, f32 value) override
    {
        return forward<bool>(8, [&](UiService& s) { return s.setSliderValue(e, value); });
    }
    [[nodiscard]] std::optional<f32> sliderValue(Entity e) const override
    {
        return forward<std::optional<f32>>(9, [&](UiService& s) { return s.sliderValue(e); });
    }
    bool setFocus(Entity e) override
    {
        return forward<bool>(10, [&](UiService& s) { return s.setFocus(e); });
    }
    [[nodiscard]] Entity focused() const override
    {
        return forward<Entity>(11, [&](UiService& s) { return s.focused(); });
    }
    [[nodiscard]] std::optional<UiRect> rect(Entity e) const override
    {
        return forward<std::optional<UiRect>>(12, [&](UiService& s) { return s.rect(e); });
    }
    void invalidateLayout() override
    {
        forward<void>(13, [&](UiService& s) { s.invalidateLayout(); });
    }
    bool playTransition(Entity e, bool towardsChanged) override
    {
        return forward<bool>(14, [&](UiService& s) { return s.playTransition(e, towardsChanged); });
    }
    [[nodiscard]] std::optional<f32> transitionProgress(Entity e) const override
    {
        return forward<std::optional<f32>>(15, [&](UiService& s) { return s.transitionProgress(e); });
    }
    u32 pollEvents(std::span<UiEvent> events) const override
    {
        return forward<u32>(16, [&](UiService& s) { return s.pollEvents(events); });
    }
    bool setLanguage(std::string_view locale) override
    {
        return forward<bool>(17, [&](UiService& s) { return s.setLanguage(locale); });
    }
    [[nodiscard]] std::string_view language() const override
    {
        return forward<std::string_view>(18, [&](UiService& s) { return s.language(); });
    }
};

// Counts the calls of every SaveService function.
class CountingSave final : public CountingProxy<SaveService, 8>
{
public:
    explicit CountingSave(SaveService* inner = nullptr) noexcept
        : CountingProxy(inner)
    {
    }

    using Void = Expected<void, SaveError>;

    [[nodiscard]] bool available() const override
    {
        return forward<bool>(0, [&](SaveService& s) { return s.available(); });
    }
    Void save(std::string_view slot, const SaveMetadata& metadata) override
    {
        return forward<Void>(1, [&](SaveService& s) { return s.save(slot, metadata); });
    }
    Void load(std::string_view slot) override
    {
        return forward<Void>(2, [&](SaveService& s) { return s.load(slot); });
    }
    Void remove(std::string_view slot) override
    {
        return forward<Void>(3, [&](SaveService& s) { return s.remove(slot); });
    }
    [[nodiscard]] bool busy() const override
    {
        return forward<bool>(4, [&](SaveService& s) { return s.busy(); });
    }
    u32 slots(std::span<SaveSlotInfo> out) override
    {
        return forward<u32>(5, [&](SaveService& s) { return s.slots(out); });
    }
    [[nodiscard]] std::optional<SaveSlotInfo> slot(std::string_view slot) const override
    {
        return forward<std::optional<SaveSlotInfo>>(6, [&](SaveService& s) { return s.slot(slot); });
    }
    u32 pollEvents(std::span<SaveEvent> out) override
    {
        return forward<u32>(7, [&](SaveService& s) { return s.pollEvents(out); });
    }
};

// Counts the calls of every AnimationService function.
class CountingAnimation final : public CountingProxy<AnimationService, 14>
{
public:
    explicit CountingAnimation(AnimationService* inner = nullptr) noexcept
        : CountingProxy(inner)
    {
    }

    using Void = Expected<void, AnimationError>;

    Void setFloat(Entity e, std::string_view name, f32 value) override
    {
        return forward<Void>(0, [&](AnimationService& s) { return s.setFloat(e, name, value); });
    }
    Void setInt(Entity e, std::string_view name, i32 value) override
    {
        return forward<Void>(1, [&](AnimationService& s) { return s.setInt(e, name, value); });
    }
    Void setBool(Entity e, std::string_view name, bool value) override
    {
        return forward<Void>(2, [&](AnimationService& s) { return s.setBool(e, name, value); });
    }
    Void setTrigger(Entity e, std::string_view name) override
    {
        return forward<Void>(3, [&](AnimationService& s) { return s.setTrigger(e, name); });
    }
    [[nodiscard]] Expected<f32, AnimationError> parameter(Entity e, std::string_view name) const override
    {
        return forward<Expected<f32, AnimationError>>(
            4, [&](AnimationService& s) { return s.parameter(e, name); });
    }
    [[nodiscard]] Expected<std::string_view, AnimationError>
    currentState(Entity e, std::string_view layer) const override
    {
        return forward<Expected<std::string_view, AnimationError>>(
            5, [&](AnimationService& s) { return s.currentState(e, layer); });
    }
    [[nodiscard]] Expected<f32, AnimationError> normalizedTime(Entity e,
                                                               std::string_view layer) const override
    {
        return forward<Expected<f32, AnimationError>>(
            6, [&](AnimationService& s) { return s.normalizedTime(e, layer); });
    }
    Void play(Entity e, std::string_view clip, f32 fadeSeconds) override
    {
        return forward<Void>(7, [&](AnimationService& s) { return s.play(e, clip, fadeSeconds); });
    }
    u32 pollEvents(std::span<AnimationEvent> out) override
    {
        return forward<u32>(8, [&](AnimationService& s) { return s.pollEvents(out); });
    }
    [[nodiscard]] Expected<Mat4, AnimationError> jointTransform(Entity e,
                                                                std::string_view joint) const override
    {
        return forward<Expected<Mat4, AnimationError>>(
            9, [&](AnimationService& s) { return s.jointTransform(e, joint); });
    }
    Void setLookAtTarget(Entity e, const Vec3& point) override
    {
        return forward<Void>(10, [&](AnimationService& s) { return s.setLookAtTarget(e, point); });
    }
    Void setIkTarget(Entity e, std::string_view joint, const Vec3& point) override
    {
        return forward<Void>(11, [&](AnimationService& s) { return s.setIkTarget(e, joint, point); });
    }
    Void setIkWeight(Entity e, std::string_view joint, f32 weight) override
    {
        return forward<Void>(12, [&](AnimationService& s) { return s.setIkWeight(e, joint, weight); });
    }
    Void setRagdoll(Entity e, bool active) override
    {
        return forward<Void>(13, [&](AnimationService& s) { return s.setRagdoll(e, active); });
    }
};

// Counts the calls of every NavService function.
class CountingNav final : public CountingProxy<NavService, 10>
{
public:
    explicit CountingNav(NavService* inner = nullptr) noexcept
        : CountingProxy(inner)
    {
    }

    using Void = Expected<void, NavError>;

    Expected<NavPathResult, NavError> findPath(const Vec3& from, const Vec3& to, std::span<Vec3> out,
                                               const NavQueryFilter& filter) override
    {
        return forward<Expected<NavPathResult, NavError>>(
            0, [&](NavService& s) { return s.findPath(from, to, out, filter); });
    }
    [[nodiscard]] Expected<Vec3, NavError> nearestPoint(const Vec3& position,
                                                        const NavQueryFilter& filter) override
    {
        return forward<Expected<Vec3, NavError>>(
            1, [&](NavService& s) { return s.nearestPoint(position, filter); });
    }
    [[nodiscard]] Expected<NavRaycastHit, NavError> raycast(const Vec3& from, const Vec3& to,
                                                            const NavQueryFilter& filter) override
    {
        return forward<Expected<NavRaycastHit, NavError>>(
            2, [&](NavService& s) { return s.raycast(from, to, filter); });
    }
    [[nodiscard]] Expected<Vec3, NavError> randomPoint(const Vec3& center, f32 radius, u64 seed,
                                                       const NavQueryFilter& filter) override
    {
        return forward<Expected<Vec3, NavError>>(
            3, [&](NavService& s) { return s.randomPoint(center, radius, seed, filter); });
    }
    Void setDestination(Entity e, const Vec3& destination) override
    {
        return forward<Void>(4, [&](NavService& s) { return s.setDestination(e, destination); });
    }
    Void stop(Entity e) override
    {
        return forward<Void>(5, [&](NavService& s) { return s.stop(e); });
    }
    Void warp(Entity e, const Vec3& position) override
    {
        return forward<Void>(6, [&](NavService& s) { return s.warp(e, position); });
    }
    [[nodiscard]] Expected<NavAgentStatus, NavError> status(Entity e) const override
    {
        return forward<Expected<NavAgentStatus, NavError>>(7, [&](NavService& s) { return s.status(e); });
    }
    [[nodiscard]] Expected<f32, NavError> remainingDistance(Entity e) const override
    {
        return forward<Expected<f32, NavError>>(8, [&](NavService& s) { return s.remainingDistance(e); });
    }
    u32 pollEvents(std::span<NavEvent> out) override
    {
        return forward<u32>(9, [&](NavService& s) { return s.pollEvents(out); });
    }
};

} // namespace illusion::test
