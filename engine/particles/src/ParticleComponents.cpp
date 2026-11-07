#include <illusion/particles/ParticleComponents.hpp>

#include <algorithm>
#include <utility>

namespace illusion
{

namespace
{

constexpr std::string_view kRequiresTransform[] = {"illusion.Transform"};
constexpr f64 kFullSpread = static_cast<f64>(kParticleFullSpread);

} // namespace

ParticlePool::ParticlePool(const ParticlePool& other)
    : capacity(other.capacity)
    , count(other.count)
    , elapsed(other.elapsed)
    , pending(other.pending)
    , playing(other.playing)
    , started(other.started)
    , gpuStep(other.gpuStep)
    , gpuBirths(other.gpuBirths)
    , gpuSeconds(other.gpuSeconds)
    , gpuQueued(other.gpuQueued)
    , gpuGravity(other.gpuGravity)
    , gpuGeneration(other.gpuGeneration)
    , random(other.random)
{
    if (other.data)
    {
        const usize floats = static_cast<usize>(capacity) * kFloatsPerParticle;
        data = std::make_unique<f32[]>(floats);
        std::copy_n(other.data.get(), floats, data.get());
    }
}

ParticlePool& ParticlePool::operator=(const ParticlePool& other)
{
    if (this != &other)
    {
        ParticlePool copy(other);
        *this = std::move(copy);
    }
    return *this;
}

Vec3 ParticlePool::position(u32 index) const noexcept
{
    return Vec3(positions(0)[index], positions(1)[index], positions(2)[index]);
}

f32 ParticlePool::lifeFraction(u32 index) const noexcept
{
    return std::clamp(ages()[index] / lifetimes()[index], 0.0f, 1.0f);
}

Vec4 particleColor(const ParticleEmitter& emitter, f32 lifeFraction) noexcept
{
    const f32 t = std::clamp(lifeFraction, 0.0f, 1.0f);
    return emitter.colorStart + (emitter.colorEnd - emitter.colorStart) * t;
}

f32 particleSize(const ParticleEmitter& emitter, f32 lifeFraction) noexcept
{
    const f32 t = std::clamp(lifeFraction, 0.0f, 1.0f);
    return emitter.sizeStart + (emitter.sizeEnd - emitter.sizeStart) * t;
}

void reflectParticleComponents(ComponentRegistry& registry)
{
    reflect<ParticleEmitter>("illusion.ParticleEmitter", 1, registry)
        .meta({.displayName = "Particle emitter",
               .category = "Effects",
               .icon = "auto_awesome",
               .description = "Emits particles: sparks, smoke, dust, magic.",
               .docsAnchor = "manual/particles.md#emitters",
               .requiredComponents = kRequiresTransform})
        .field("maxParticles", &ParticleEmitter::maxParticles,
               {.description = "Live particles at most.",
                .category = "Emission",
                .min = 1.0,
                .max = static_cast<f64>(kMaxParticlesPerEmitter)})
        .field(
            "rate", &ParticleEmitter::rate,
            {.description = "Particles per second.", .category = "Emission", .min = 0.0, .softMax = 1000.0})
        .field("burst", &ParticleEmitter::burst,
               {.description = "Particles emitted at once on start.",
                .category = "Emission",
                .max = static_cast<f64>(kMaxParticlesPerEmitter)})
        .field("loop", &ParticleEmitter::loop, {.category = "Emission"})
        .field("duration", &ParticleEmitter::duration,
               {.description = "Seconds of emission when not looping.",
                .category = "Emission",
                .unit = Unit::Seconds,
                .min = 0.0})
        .field("playOnStart", &ParticleEmitter::playOnStart, {.category = "Emission"})
        .field("lifetimeMin", &ParticleEmitter::lifetimeMin,
               {.category = "Particles", .unit = Unit::Seconds, .min = 0.001, .softMax = 10.0})
        .field("lifetimeMax", &ParticleEmitter::lifetimeMax,
               {.category = "Particles", .unit = Unit::Seconds, .min = 0.001, .softMax = 10.0})
        .field("speedMin", &ParticleEmitter::speedMin,
               {.category = "Particles", .unit = Unit::MetersPerSecond, .softMin = 0.0, .softMax = 20.0})
        .field("speedMax", &ParticleEmitter::speedMax,
               {.category = "Particles", .unit = Unit::MetersPerSecond, .softMin = 0.0, .softMax = 20.0})
        .field("shape", &ParticleEmitter::shape, {.category = "Shape"})
        .field("radius", &ParticleEmitter::radius, {.category = "Shape", .unit = Unit::Meters, .min = 0.0})
        .field("boxExtents", &ParticleEmitter::boxExtents,
               {.category = "Shape", .unit = Unit::Meters, .min = 0.0})
        .field("spread", &ParticleEmitter::spread,
               {.description = "Half angle of the directions around local +Y.",
                .category = "Shape",
                .unit = Unit::Radians,
                .displayUnit = Unit::Degrees,
                .min = 0.0,
                .max = kFullSpread,
                .hint = FieldHint::Angle})
        .field("gravityFactor", &ParticleEmitter::gravityFactor,
               {.category = "Motion", .softMin = -2.0, .softMax = 2.0})
        .field("drag", &ParticleEmitter::drag,
               {.description = "Fraction of the velocity lost per second.",
                .category = "Motion",
                .min = 0.0,
                .softMax = 10.0})
        .field("sizeStart", &ParticleEmitter::sizeStart,
               {.category = "Look", .unit = Unit::Meters, .min = 0.0, .softMax = 5.0})
        .field("sizeEnd", &ParticleEmitter::sizeEnd,
               {.category = "Look", .unit = Unit::Meters, .min = 0.0, .softMax = 5.0})
        .field("colorStart", &ParticleEmitter::colorStart,
               {.category = "Look", .min = 0.0, .hint = FieldHint::HdrColor})
        .field("colorEnd", &ParticleEmitter::colorEnd,
               {.category = "Look", .min = 0.0, .hint = FieldHint::HdrColor})
        .field("blend", &ParticleEmitter::blend, {.category = "Look"})
        .field("space", &ParticleEmitter::space, {.category = "Motion"})
        .field("texture", &ParticleEmitter::texture,
               {.description = "Sprite texture; none draws a soft round dot.",
                .category = "Look",
                .assetKind = AssetKind::Texture})
        .field("seed", &ParticleEmitter::seed,
               {.description = "Equal seeds give equal particles.", .category = "Emission"})
        .field("simulation", &ParticleEmitter::simulation,
               {.description =
                    "Cpu: deterministic and readable by code; Gpu: compute shaders, for very large emitters.",
                .category = "Emission"});
    reflectRuntime<ParticlePool>("illusion.ParticlePool", {.displayName = "Particle pool"}, registry);
}

} // namespace illusion
