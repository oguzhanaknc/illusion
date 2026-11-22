#include <illusion/ai/AiComponents.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/StructRegistry.hpp>

#include <string_view>

namespace illusion
{
namespace
{

constexpr std::string_view kRequiresTransform[] = {"illusion.Transform"};
constexpr f64 kMaxTeam = static_cast<f64>(kMaxAiTeams - 1);
constexpr f64 kFullTurn = 6.283185307179586;

} // namespace

void registerAiComponents(ComponentRegistry& registry)
{
    reflectStruct<BlackboardOverride>("illusion.BlackboardOverride", 1, registry.structs())
        .meta({.displayName = "Blackboard value", .docsAnchor = "manual/ai.md#the-blackboard"})
        .field("key", &BlackboardOverride::key, {.description = "Name of a key of the tree."})
        .field("number", &BlackboardOverride::number,
               {.description = "Bool (non-zero is on), Int (rounded) and Float keys."})
        .field("vector", &BlackboardOverride::vector, {.description = "Vector3 keys.", .unit = Unit::Meters})
        .field("entity", &BlackboardOverride::entity, {.description = "Entity keys."});

    reflect<BehaviorAgent>("illusion.BehaviorAgent", 1, registry)
        .meta({.displayName = "Behavior agent",
               .category = "AI",
               .icon = "psychology",
               .description = "Runs a behaviour tree that moves the Entity's NavAgent and asks game code for "
                              "actions.",
               .docsAnchor = "manual/ai.md#agents",
               .requiredComponents = kRequiresTransform})
        .field("tree", &BehaviorAgent::tree,
               {.description = "The .behavior file it runs.", .assetKind = AssetKind::BehaviorTree})
        .field("enabled", &BehaviorAgent::enabled, {.description = "Tick the tree while the game runs."})
        .field(
            "tickInterval", &BehaviorAgent::tickInterval,
            {.description = "Time between ticks; 0 ticks every update.", .unit = Unit::Seconds, .min = 0.0})
        .field("blackboard", &BehaviorAgent::blackboard,
               {.description = "Starting values that replace the tree's defaults."});

    reflect<AiPerception>("illusion.AiPerception", 1, registry)
        .meta(
            {.displayName = "AI perception",
             .category = "AI",
             .icon = "visibility",
             .description = "Sees and hears AiStimulus Entities and noises, and remembers them for a while.",
             .docsAnchor = "manual/ai.md#perception",
             .requiredComponents = kRequiresTransform})
        .field("sightRadius", &AiPerception::sightRadius,
               {.description = "How far it sees; 0 is blind.",
                .category = "Sight",
                .unit = Unit::Meters,
                .min = 0.0})
        .field("sightAngle", &AiPerception::sightAngle,
               {.description = "Full angle of the sight cone about the Entity's +z axis.",
                .category = "Sight",
                .unit = Unit::Radians,
                .displayUnit = Unit::Degrees,
                .min = 0.0,
                .max = kFullTurn,
                .hint = FieldHint::Angle})
        .field("eyeHeight", &AiPerception::eyeHeight,
               {.description = "Height of the eye above the Entity's position.",
                .category = "Sight",
                .unit = Unit::Meters})
        .field("occluderMask", &AiPerception::occluderMask,
               {.description = "Physics layers that block sight.",
                .category = "Sight",
                .hint = FieldHint::LayerMask})
        .field("hearing", &AiPerception::hearing,
               {.description = "Multiplier of the radius of noises; 0 is deaf.",
                .category = "Hearing",
                .min = 0.0})
        .field("memorySeconds", &AiPerception::memorySeconds,
               {.description = "How long it remembers what it no longer senses.",
                .unit = Unit::Seconds,
                .min = 0.0})
        .field("detectTeams", &AiPerception::detectTeams,
               {.description = "Teams it notices (bit i = team i).", .advanced = true})
        .field("targetKey", &AiPerception::targetKey,
               {.description = "Entity key of the tree set to the chosen target; empty for none.",
                .category = "Blackboard"})
        .field("positionKey", &AiPerception::positionKey,
               {.description =
                    "Vector3 key of the tree set to the target's last known position; empty for none.",
                .category = "Blackboard"});

    reflect<AiStimulus>("illusion.AiStimulus", 1, registry)
        .meta({.displayName = "AI stimulus",
               .category = "AI",
               .icon = "radar",
               .description = "Something AiPerception Entities can see.",
               .docsAnchor = "manual/ai.md#stimuli",
               .requiredComponents = kRequiresTransform})
        .field("team", &AiStimulus::team, {.description = "Its team.", .min = 0.0, .max = kMaxTeam})
        .field("offset", &AiStimulus::offset,
               {.description = "Point looked at, relative to the Entity's position.", .unit = Unit::Meters})
        .field("visible", &AiStimulus::visible, {.description = "Can be seen now."});

    reflectRuntime<AiAgentState>("illusion.AiAgentState", {.displayName = "AI agent state"}, registry);
}

} // namespace illusion
