// The AI Components' reflection (ADR 0062): units, hints, asset kinds, manual anchors and scene round trips.

#include <illusion/ai/AiComponents.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>

#include <doctest/doctest.h>

#include <string_view>

using namespace illusion;

namespace
{

const TypeReflection& reflectionOf(std::string_view name)
{
    const ComponentRegistry& registry = ComponentRegistry::instance();
    const TypeReflection* type = registry.reflection(registry.findByStableName(name));
    REQUIRE(type != nullptr);
    return *type;
}

} // namespace

TEST_CASE("the AI Components are reflected with units, hints, asset kinds and manual anchors")
{
    reflectCoreComponents();
    registerAiComponents(ComponentRegistry::instance());
    registerAiComponents(ComponentRegistry::instance());

    const TypeReflection& agent = reflectionOf("illusion.BehaviorAgent");
    CHECK(agent.meta.docsAnchor == "manual/ai.md#agents");
    CHECK(agent.field("tree")->meta.assetKind == AssetKind::BehaviorTree);
    CHECK(agent.field("tickInterval")->meta.unit == Unit::Seconds);
    CHECK(agent.field("blackboard")->type == FieldType::Array);

    const TypeReflection& perception = reflectionOf("illusion.AiPerception");
    CHECK(perception.meta.docsAnchor == "manual/ai.md#perception");
    CHECK(perception.field("sightRadius")->meta.unit == Unit::Meters);
    CHECK(perception.field("sightAngle")->meta.hint == FieldHint::Angle);
    CHECK(perception.field("sightAngle")->meta.displayUnit == Unit::Degrees);
    CHECK(perception.field("occluderMask")->meta.hint == FieldHint::LayerMask);
    CHECK(perception.field("memorySeconds")->meta.unit == Unit::Seconds);

    const TypeReflection& stimulus = reflectionOf("illusion.AiStimulus");
    CHECK(stimulus.meta.docsAnchor == "manual/ai.md#stimuli");
    CHECK(stimulus.field("team")->meta.max == static_cast<f64>(kMaxAiTeams - 1));
    CHECK(reflectionOf("illusion.AiAgentState").meta.transient);
    for (const std::string_view name :
         {"illusion.BehaviorAgent", "illusion.AiPerception", "illusion.AiStimulus"})
    {
        CAPTURE(name);
        const TypeReflection& type = reflectionOf(name);
        CHECK(type.meta.category == "AI");
        CHECK_FALSE(type.meta.icon.empty());
        CHECK_FALSE(type.meta.description.empty());
        CHECK_FALSE(type.meta.transient);
    }
}

TEST_CASE("AI Components with non-default values survive a scene round trip")
{
    reflectCoreComponents();
    registerAiComponents(ComponentRegistry::instance());
    World world;
    const Entity other = world.create(Transform{});
    BehaviorAgent agent;
    agent.tree = AssetRef("game:/ai/guard.behavior");
    agent.enabled = false;
    agent.tickInterval = 0.25f;
    agent.blackboard.push_back(BlackboardOverride{
        .key = "alert", .number = 3.0f, .vector = Vec3(1.0f, 2.0f, 3.0f), .entity = other});
    AiPerception perception;
    perception.sightRadius = 22.0f;
    perception.sightAngle = 1.5f;
    perception.eyeHeight = 1.2f;
    perception.occluderMask = 0b101u;
    perception.hearing = 0.5f;
    perception.memorySeconds = 9.0f;
    perception.detectTeams = 0b10u;
    perception.targetKey = "target";
    perception.positionKey = "lastSeen";
    const AiStimulus stimulus{.team = 4, .offset = Vec3(0.0f, 1.5f, 0.0f), .visible = false};
    world.create(Transform{}, agent, perception, stimulus);
    assignMissingGuids(world);
    const SceneSaveResult saved = saveScene(world);
    REQUIRE(saved.ok());

    World loaded;
    const SceneLoadResult result = loadScene(loaded, saved.json);
    REQUIRE(result.ok());
    REQUIRE(result.entities.size() == 2);
    const Entity entity =
        loaded.has<BehaviorAgent>(result.entities[0]) ? result.entities[0] : result.entities[1];
    const Entity target = entity == result.entities[0] ? result.entities[1] : result.entities[0];
    const BehaviorAgent& a = loaded.get<BehaviorAgent>(entity);
    CHECK(a.tree == agent.tree);
    CHECK_FALSE(a.enabled);
    CHECK(a.tickInterval == 0.25f);
    REQUIRE(a.blackboard.size() == 1);
    CHECK(a.blackboard[0].key == "alert");
    CHECK(a.blackboard[0].number == 3.0f);
    CHECK(a.blackboard[0].vector == Vec3(1.0f, 2.0f, 3.0f));
    CHECK(a.blackboard[0].entity == target);
    const AiPerception& p = loaded.get<AiPerception>(entity);
    CHECK(p.sightRadius == 22.0f);
    CHECK(p.sightAngle == 1.5f);
    CHECK(p.eyeHeight == 1.2f);
    CHECK(p.occluderMask == 0b101u);
    CHECK(p.hearing == 0.5f);
    CHECK(p.memorySeconds == 9.0f);
    CHECK(p.detectTeams == 0b10u);
    CHECK(p.targetKey == "target");
    CHECK(p.positionKey == "lastSeen");
    const AiStimulus& s = loaded.get<AiStimulus>(entity);
    CHECK(s.team == 4);
    CHECK(s.offset == Vec3(0.0f, 1.5f, 0.0f));
    CHECK_FALSE(s.visible);
}
