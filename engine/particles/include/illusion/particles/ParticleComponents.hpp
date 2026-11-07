#pragma once

/// @file
/// Particle effects (ADR 0047): the saved ParticleEmitter Component and the runtime ParticlePool that
/// ParticleWorld fills.

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/core/Random.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/ecs/Reflection.hpp>

#include <array>
#include <memory>
#include <span>

namespace illusion
{

/// The largest ParticleEmitter::maxParticles.
inline constexpr u32 kMaxParticlesPerEmitter = 65536;

/// Spread angle in radians that covers every direction: particles leave an emitter this wide anywhere.
inline constexpr f32 kParticleFullSpread = 3.14159265f;

/// Where an emitter creates particles, in its local space.
enum class EmitterShape : u8
{
    Point,  ///< At the emitter's origin.
    Sphere, ///< Anywhere inside a sphere of ParticleEmitter::radius.
    Box,    ///< Anywhere inside a box of half size ParticleEmitter::boxExtents.
};

/// How particles are blended into the image.
enum class ParticleBlend : u8
{
    Additive, ///< Light is added: fire, sparks, magic. Order does not matter.
    Alpha,    ///< Covers what is behind by its alpha: smoke, dust.
};

/// The space particles live in.
enum class ParticleSpace : u8
{
    World, ///< Particles stay where they were born when the emitter moves (smoke, trails).
    Local, ///< Particles move with the emitter (a glow around a torch).
};

/// Where an emitter's particles are simulated.
enum class ParticleSimulation : u8
{
    Cpu, ///< ParticleWorld on the Job System: deterministic, readable by code (ParticlePool), up to a few
         ///< hundred thousand particles in all.
    Gpu, ///< Compute shaders in the renderer: up to kMaxParticlesPerEmitter per emitter at a fraction of the
         ///< cost, but only where a view draws them; the CPU sees a live count a few frames late.
};

/// Emits and simulates particles on the Entity (ParticleWorld) and draws them (SceneRenderer). Every field
/// is saved; the particles themselves are runtime state in ParticlePool.
struct ParticleEmitter
{
    u32 maxParticles = 256;  ///< Live particles at most (1 to kMaxParticlesPerEmitter).
    f32 rate = 10.0f;        ///< Particles per second while emitting.
    u32 burst = 0;           ///< Particles emitted at once when the emitter starts.
    bool loop = true;        ///< Emit for ever; otherwise for `duration` seconds.
    f32 duration = 5.0f;     ///< Seconds of emission when not looping.
    bool playOnStart = true; ///< Start emitting on the first update; otherwise wait for play().
    f32 lifetimeMin = 1.0f;  ///< Seconds a particle lives, at least.
    f32 lifetimeMax = 1.0f;  ///< Seconds a particle lives, at most.
    f32 speedMin = 1.0f;     ///< Initial speed in m/s, at least.
    f32 speedMax = 1.0f;     ///< Initial speed in m/s, at most.
    EmitterShape shape = EmitterShape::Point; ///< Where particles are born.
    f32 radius = 0.5f;                        ///< Sphere: radius in m.
    Vec3 boxExtents{0.5f};                    ///< Box: half size in m.
    f32 spread = kParticleFullSpread; ///< Half angle in radians around local +Y of the initial directions.
    f32 gravityFactor = 0.0f;         ///< Multiplies the world's gravity (0: none, 1: full, negative: rises).
    f32 drag = 0.0f;                  ///< Fraction of the velocity lost per second.
    f32 sizeStart = 0.1f;             ///< Width in m at birth.
    f32 sizeEnd = 0.1f;               ///< Width in m at death.
    Vec4 colorStart{1.0f};            ///< Linear colour and alpha at birth (HDR: may exceed 1).
    Vec4 colorEnd{1.0f, 1.0f, 1.0f, 0.0f};         ///< Linear colour and alpha at death.
    ParticleBlend blend = ParticleBlend::Additive; ///< How particles are blended.
    ParticleSpace space = ParticleSpace::World;    ///< The space particles live in.
    AssetRef texture{};                            ///< Sprite texture (none: a soft round dot).
    u32 seed = 1; ///< Seed of the emitter's random numbers: equal seeds give equal particles.
    ParticleSimulation simulation = ParticleSimulation::Cpu; ///< Where the particles are simulated.
};

/// The live particles of a ParticleEmitter, in structure-of-arrays form (runtime state, not saved).
/// Particles [0, count) are alive; positions and velocities are in world space or in the emitter's space
/// (ParticleEmitter::space).
struct ParticlePool
{
    u32 capacity = 0;     ///< Slots in the arrays.
    u32 count = 0;        ///< Live particles.
    f64 elapsed = 0.0;    ///< Seconds since the emitter started.
    f64 pending = 0.0;    ///< Particles owed by the emission rate, less than one.
    bool playing = false; ///< Emitting (or waiting to start); false after stop() or the duration.
    bool started = false; ///< The burst was emitted.
    /// @name GPU emitters (ParticleSimulation::Gpu)
    /// Each ParticleWorld::update() publishes one step: its births and seconds, numbered by `gpuStep`. Every
    /// renderer that draws the emitter simulates each new step on the GPU (the step seeds its random
    /// numbers, so all views agree) and writes the live count it reads back into `count`.
    /// @{
    u32 gpuStep = 0;       ///< Steps published so far.
    u32 gpuBirths = 0;     ///< Particles born in the last step.
    f32 gpuSeconds = 0.0f; ///< Seconds of the last step.
    u32 gpuQueued = 0;     ///< Births asked for by emit() since, published with the next step.
    Vec3 gpuGravity{0.0f}; ///< Gravity of the last step, in the emitter's space for local emitters.
    u32 gpuGeneration = 0; ///< Changes on play() and clear(): the GPU particles start afresh.
    /// @}
    Random random{};               ///< The emitter's random numbers.
    std::unique_ptr<f32[]> data{}; ///< capacity * kFloatsPerParticle floats; see the accessors.

    /// Floats stored per particle: position (3), velocity (3), age and lifetime.
    static constexpr u32 kFloatsPerParticle = 8;

    /// An empty pool.
    ParticlePool() noexcept = default;
    /// Copies the live particles (a play session starts from a copy of the edited World).
    ParticlePool(const ParticlePool& other);
    /// @copydoc ParticlePool(const ParticlePool&)
    ParticlePool& operator=(const ParticlePool& other);
    ParticlePool(ParticlePool&&) noexcept = default;            ///< Takes the particles of @p other.
    ParticlePool& operator=(ParticlePool&&) noexcept = default; ///< Takes the particles of @p other.
    ~ParticlePool() = default;                                  ///< Frees the particles.

    /// X, Y and Z of the positions, each `capacity` long.
    [[nodiscard]] std::span<f32> positions(u32 axis) noexcept { return array(axis); }
    /// X, Y and Z of the velocities.
    [[nodiscard]] std::span<f32> velocities(u32 axis) noexcept { return array(3 + axis); }
    /// Seconds each particle has lived.
    [[nodiscard]] std::span<f32> ages() noexcept { return array(6); }
    /// Seconds each particle lives in all.
    [[nodiscard]] std::span<f32> lifetimes() noexcept { return array(7); }
    /// @copydoc positions
    [[nodiscard]] std::span<const f32> positions(u32 axis) const noexcept { return array(axis); }
    /// @copydoc velocities
    [[nodiscard]] std::span<const f32> velocities(u32 axis) const noexcept { return array(3 + axis); }
    /// @copydoc ages
    [[nodiscard]] std::span<const f32> ages() const noexcept { return array(6); }
    /// @copydoc lifetimes
    [[nodiscard]] std::span<const f32> lifetimes() const noexcept { return array(7); }

    /// Position of particle @p index.
    [[nodiscard]] Vec3 position(u32 index) const noexcept;
    /// Age of particle @p index as a fraction of its lifetime, in [0, 1].
    [[nodiscard]] f32 lifeFraction(u32 index) const noexcept;

private:
    [[nodiscard]] std::span<f32> array(u32 which) const noexcept
    {
        return {data.get() + static_cast<usize>(which) * capacity, capacity};
    }
};

/// The colour and alpha of a particle of @p emitter that has lived @p lifeFraction of its life.
[[nodiscard]] Vec4 particleColor(const ParticleEmitter& emitter, f32 lifeFraction) noexcept;

/// The width of a particle of @p emitter that has lived @p lifeFraction of its life.
[[nodiscard]] f32 particleSize(const ParticleEmitter& emitter, f32 lifeFraction) noexcept;

/// Reflects ParticleEmitter ("illusion.ParticleEmitter") with editor metadata and the runtime ParticlePool
/// ("illusion.ParticlePool", transient).
void reflectParticleComponents(ComponentRegistry& registry = ComponentRegistry::instance());

} // namespace illusion

/// Names of EmitterShape values in scene files.
template <>
struct illusion::EnumReflection<illusion::EmitterShape>
{
    /// Value names.
    static constexpr std::array kValues{illusion::EnumValue{"Point", 0}, illusion::EnumValue{"Sphere", 1},
                                        illusion::EnumValue{"Box", 2}};
};

/// Names of ParticleBlend values in scene files.
template <>
struct illusion::EnumReflection<illusion::ParticleBlend>
{
    /// Value names.
    static constexpr std::array kValues{illusion::EnumValue{"Additive", 0}, illusion::EnumValue{"Alpha", 1}};
};

/// Names of ParticleSimulation values in scene files.
template <>
struct illusion::EnumReflection<illusion::ParticleSimulation>
{
    /// Value names.
    static constexpr std::array kValues{illusion::EnumValue{"Cpu", 0}, illusion::EnumValue{"Gpu", 1}};
};

/// Names of ParticleSpace values in scene files.
template <>
struct illusion::EnumReflection<illusion::ParticleSpace>
{
    /// Value names.
    static constexpr std::array kValues{illusion::EnumValue{"World", 0}, illusion::EnumValue{"Local", 1}};
};
