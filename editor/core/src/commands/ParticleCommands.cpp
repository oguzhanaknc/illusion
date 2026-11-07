// The particles.* commands: play, stop, clear and burst the particle emitters of the active World, and count
// their particles (ADR 0047). Particles are runtime state, not scene content, so these commands are not
// undoable and do not go through WorldEditor; in the edit World they drive the preview that EditorContext
// advances every frame, in the play World the game's effects.

#include "CommandSupport.hpp"

#include <illusion/editor/EditorStrings.hpp>
#include <illusion/particles/ParticleComponents.hpp>
#include <illusion/particles/ParticleWorld.hpp>

namespace illusion::editor::detail
{

namespace
{

namespace strings = illusion::editor::strings;

// The emitter named by @p reference in the active World.
Expected<Entity, CommandError> emitterEntity(CommandContext& context, std::string_view reference)
{
    Expected<Entity, CommandError> entity = context.entity(reference, "/entity");
    if (!entity)
    {
        return entity;
    }
    if (!context.world().has<ParticleEmitter>(*entity))
    {
        return unexpected(
            CommandError::make(ErrorKind::PreconditionFailed, "the entity has no illusion.ParticleEmitter"));
    }
    return entity;
}

ParticlesResult stateOf(ParticleWorld& particles, Entity entity, u32 emitted = 0)
{
    return ParticlesResult{particles.aliveCount(entity), particles.isPlaying(entity), emitted};
}

// Runs @p action on the emitter of the params, unless this is a dry run.
template <class Action>
Expected<ParticlesResult, CommandError> onEmitter(CommandContext& context, std::string_view reference,
                                                  Action action)
{
    Expected<Entity, CommandError> entity = emitterEntity(context, reference);
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    // ParticleWorld keeps no state of its own about an emitter: any instance over the World acts on its pool.
    ParticleWorld particles(context.world());
    if (!context.dryRun())
    {
        action(particles, *entity);
    }
    return stateOf(particles, *entity);
}

Expected<ParticlesResult, CommandError> particlesPlay(CommandContext& context, const ParticlesParams& params)
{
    return onEmitter(context, params.entity,
                     [](ParticleWorld& particles, Entity entity) { particles.play(entity); });
}

Expected<ParticlesResult, CommandError> particlesStop(CommandContext& context, const ParticlesParams& params)
{
    return onEmitter(context, params.entity,
                     [](ParticleWorld& particles, Entity entity) { particles.stop(entity); });
}

Expected<ParticlesResult, CommandError> particlesClear(CommandContext& context, const ParticlesParams& params)
{
    return onEmitter(context, params.entity, [](ParticleWorld& particles, Entity entity) {
        static_cast<void>(particles.clear(entity));
    });
}

Expected<ParticlesResult, CommandError> particlesEmit(CommandContext& context,
                                                      const ParticlesEmitParams& params)
{
    if (params.count == 0 || params.count > kMaxParticlesPerEmitter)
    {
        return unexpected(CommandError::invalid("/count", "must be between 1 and 65536"));
    }
    u32 emitted = 0;
    Expected<ParticlesResult, CommandError> result =
        onEmitter(context, params.entity, [&emitted, &params](ParticleWorld& particles, Entity entity) {
            emitted = particles.emit(entity, params.count);
        });
    if (result)
    {
        result->emitted = emitted;
    }
    return result;
}

Expected<ParticlesStatsResult, CommandError> particlesStats(CommandContext& context,
                                                            const ParticlesStatsParams& params)
{
    World& world = context.world();
    ParticleWorld particles(world);
    ParticlesStatsResult result;
    const auto count = [&](Entity entity) {
        ++result.emitters;
        result.playing += particles.isPlaying(entity) ? 1u : 0u;
        result.alive += particles.aliveCount(entity);
    };
    if (params.entity)
    {
        Expected<Entity, CommandError> entity = emitterEntity(context, *params.entity);
        if (!entity)
        {
            return unexpected(std::move(entity).error());
        }
        count(*entity);
        return result;
    }
    world.query<const ParticleEmitter>().each([&](Entity entity, const ParticleEmitter&) { count(entity); });
    return result;
}

} // namespace

void registerParticleCommands(CommandRegistry& registry)
{
    registry.add<&particlesPlay>({
        .name = "particles.play",
        .title = strings::kCommandParticlesPlayTitle,
        .description = strings::kCommandParticlesPlayDescription,
        .agentSummary =
            "Starts a particle emitter again from the beginning: its particles are cleared, its burst "
            "fires and a timed effect runs for its duration again. Acts on the play World while "
            "playing (the game's effects), otherwise on the editor's preview. Not undoable: "
            "particles are runtime state. Example: {\"entity\": \"/Sparks\"}.",
        .category = "Particles",
        .icon = "play_circle",
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
        .palette = true,
    });
    registry.add<&particlesStop>({
        .name = "particles.stop",
        .title = strings::kCommandParticlesStopTitle,
        .description = strings::kCommandParticlesStopDescription,
        .agentSummary = "Stops a particle emitter emitting; its live particles finish their lives. Example: "
                        "{\"entity\": \"/Sparks\"}.",
        .category = "Particles",
        .icon = "stop_circle",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
        .palette = true,
    });
    registry.add<&particlesClear>({
        .name = "particles.clear",
        .title = strings::kCommandParticlesClearTitle,
        .description = strings::kCommandParticlesClearDescription,
        .agentSummary = "Removes the live particles of an emitter at once; it keeps emitting if it plays. "
                        "Example: {\"entity\": \"/Dust\"}.",
        .category = "Particles",
        .icon = "clear_all",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&particlesEmit>({
        .name = "particles.emit",
        .title = strings::kCommandParticlesEmitTitle,
        .description = strings::kCommandParticlesEmitDescription,
        .agentSummary =
            "Emits a burst of particles now from an emitter, where it stands, whether it plays or "
            "not; emitted says how many fit below its maxParticles. Example: "
            "{\"entity\": \"/Sparks\", \"count\": 40}.",
        .category = "Particles",
        .icon = "auto_awesome",
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&particlesStats>({
        .name = "particles.stats",
        .title = strings::kCommandParticlesStatsTitle,
        .description = strings::kCommandParticlesStatsDescription,
        .agentSummary =
            "Counts the particle emitters of the active World, those that emit, and their live "
            "particles; with entity, only that emitter. Example: {} or {\"entity\": \"/Sparks\"}.",
        .category = "Particles",
        .icon = "monitoring",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Read,
    });
}

} // namespace illusion::editor::detail
