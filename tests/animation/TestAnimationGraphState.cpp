#include "TestSupport.hpp"

#include <illusion/animation/AnimationGraphState.hpp>
#include <illusion/core/Memory.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace illusion;

namespace
{

constexpr f32 kPi = std::numbers::pi_v<f32>;
constexpr std::string_view kClipPrefix = "game:/rig.glb#animation/";

// A graph file around @p parameters and @p layers (JSON array contents); "@name" stands for a clip path.
std::string graphJson(std::string_view parameters, std::string_view layers, std::string_view extra = "")
{
    std::string text = R"({"format": "illusion.animgraph", "version": 1, )";
    text += extra;
    text += R"("parameters": [)";
    text += parameters;
    text += R"(], "layers": [)";
    text += layers;
    text += "]}";
    std::string expanded;
    for (const char c : text)
    {
        expanded += c == '@' ? std::string(kClipPrefix) : std::string(1, c);
    }
    return expanded;
}

Vec4 quatValue(Quat q)
{
    return Vec4(q.x, q.y, q.z, q.w);
}

AnimationChannel channel(std::string joint, AnimationPath path, std::vector<f32> times,
                         std::vector<Vec4> values)
{
    return AnimationChannel{std::move(joint), path, AnimationInterpolation::Linear, std::move(times),
                            std::move(values)};
}

AnimationClip clip(std::string name, f32 duration, std::vector<AnimationChannel> channels)
{
    return AnimationClip{std::move(name), duration, std::move(channels)};
}

// The arm's translation held at @p value for one second.
AnimationClip armAt(std::string name, Vec3 value)
{
    return clip(std::move(name), 1.0f,
                {channel("arm", AnimationPath::Translation, {0.0f}, {Vec4(value, 0.0f)})});
}

// root -> spine -> arm, each one unit from its parent.
Skeleton makeSkeleton()
{
    Skeleton skeleton;
    skeleton.joints.resize(3);
    skeleton.joints.at(0).name = "root";
    skeleton.joints.at(1).name = "spine";
    skeleton.joints.at(1).parent = 0;
    skeleton.joints.at(1).translation = Vec3(0.0f, 1.0f, 0.0f);
    skeleton.joints.at(2).name = "arm";
    skeleton.joints.at(2).parent = 1;
    skeleton.joints.at(2).translation = Vec3(1.0f, 0.0f, 0.0f);
    return skeleton;
}

std::map<std::string, AnimationClip, std::less<>> makeClips()
{
    std::map<std::string, AnimationClip, std::less<>> clips;
    const auto add = [&](AnimationClip c) {
        std::string name = c.name;
        clips.emplace(std::move(name), std::move(c));
    };
    add(armAt("v10", Vec3(10.0f, 0.0f, 0.0f)));
    add(armAt("v20", Vec3(20.0f, 0.0f, 0.0f)));
    add(armAt("v30", Vec3(30.0f, 0.0f, 0.0f)));
    add(armAt("p00", Vec3(0.0f, 0.0f, 0.0f)));
    add(armAt("p10", Vec3(1.0f, 0.0f, 0.0f)));
    add(armAt("p01", Vec3(0.0f, 1.0f, 0.0f)));
    add(armAt("p11", Vec3(1.0f, 1.0f, 0.0f)));
    add(clip("ramp", 1.0f,
             {channel("arm", AnimationPath::Translation, {0.0f, 1.0f},
                      {Vec4(0.0f), Vec4(1.0f, 0.0f, 0.0f, 0.0f)})}));
    add(clip("ramp2", 2.0f,
             {channel("arm", AnimationPath::Translation, {0.0f, 2.0f},
                      {Vec4(0.0f), Vec4(0.0f, 2.0f, 0.0f, 0.0f)})}));
    const auto everyJoint = [](std::string name, f32 x) {
        return clip(std::move(name), 1.0f,
                    {channel("root", AnimationPath::Translation, {0.0f}, {Vec4(x, 0.0f, 0.0f, 0.0f)}),
                     channel("spine", AnimationPath::Translation, {0.0f}, {Vec4(x, 1.0f, 0.0f, 0.0f)}),
                     channel("arm", AnimationPath::Translation, {0.0f},
                             {Vec4(x == 0.0f ? 1.0f : x, 0.0f, 0.0f, 0.0f)})});
    };
    add(everyJoint("lo", 0.0f));
    add(everyJoint("hi", 5.0f));
    add(clip(
        "bump", 1.0f,
        {channel("arm", AnimationPath::Translation, {0.0f, 1.0f}, {Vec4(0.0f), Vec4(0.0f, 1.0f, 0.0f, 0.0f)}),
         channel("arm", AnimationPath::Rotation, {0.0f, 1.0f},
                 {quatValue(Quat(1.0f, 0.0f, 0.0f, 0.0f)),
                  quatValue(glm::angleAxis(kPi / 2.0f, Vec3(0.0f, 0.0f, 1.0f)))})}));
    add(clip("turn30", 1.0f,
             {channel("arm", AnimationPath::Translation, {0.0f}, {Vec4(1.0f, 0.0f, 0.0f, 0.0f)}),
              channel("arm", AnimationPath::Rotation, {0.0f},
                      {quatValue(glm::angleAxis(kPi / 6.0f, Vec3(0.0f, 1.0f, 0.0f)))})}));
    add(clip("slide", 1.0f,
             {channel("root", AnimationPath::Translation, {0.0f, 1.0f},
                      {Vec4(0.0f), Vec4(0.0f, 0.5f, 2.0f, 0.0f)}),
              channel("root", AnimationPath::Rotation, {0.0f, 1.0f},
                      {quatValue(Quat(1.0f, 0.0f, 0.0f, 0.0f)),
                       quatValue(glm::angleAxis(kPi / 2.0f, Vec3(0.0f, 1.0f, 0.0f)))})}));
    return clips;
}

// A parsed and bound graph with its state; not movable, since the binding points into it.
struct Rig
{
    Skeleton skeleton = makeSkeleton();
    std::map<std::string, AnimationClip, std::less<>> clips = makeClips();
    AnimationGraph graph;
    std::optional<GraphBinding> binding;
    AnimationGraphState state;
    LinearAllocator scratch{usize{1} << 16};
    std::vector<JointPose> pose;
    RootMotionDelta root;
    std::array<GraphEvent, 8> events{};
    u32 fired = 0;

    explicit Rig(const std::string& json)
    {
        Expected<AnimationGraph, std::string> parsed = parseAnimationGraph(json);
        INFO((parsed ? std::string() : parsed.error()));
        REQUIRE(parsed.hasValue());
        graph = std::move(*parsed);
        Expected<GraphBinding, std::string> bound =
            bindGraph(graph, skeleton, [this](const AssetRef& ref) { return find(ref); });
        INFO((bound ? std::string() : bound.error()));
        REQUIRE(bound.hasValue());
        binding.emplace(std::move(*bound));
        state = makeGraphState(*binding);
        pose.resize(skeleton.joints.size());
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    const AnimationClip* find(const AssetRef& ref) const
    {
        const std::string_view path = ref.path();
        if (!path.starts_with(kClipPrefix))
        {
            return nullptr;
        }
        const auto found = clips.find(path.substr(kClipPrefix.size()));
        return found != clips.end() ? &found->second : nullptr;
    }

    void step(f32 seconds)
    {
        fired = stepGraph(*binding, state, seconds, events);
        evaluateGraph(*binding, state, scratch, pose, &root);
        CHECK(scratch.used() == 0);
    }

    void set(std::string_view name, f32 value) { state.setValue(graph.findParameter(name), value); }
    void trigger(std::string_view name) { state.setTrigger(graph.findParameter(name)); }

    [[nodiscard]] Vec3 arm() const { return pose.at(2).translation; }
    [[nodiscard]] const GraphLayerState& layer(usize index = 0) const { return state.layers.at(index); }
};

std::string singleLayer(std::string_view states, std::string_view rest = "")
{
    return std::string(R"({"name": "base", "states": [)") + std::string(states) + "]" + std::string(rest) +
           "}";
}

void checkVec(Vec3 actual, Vec3 expected)
{
    CHECK(actual.x == doctest::Approx(expected.x).epsilon(1e-5));
    CHECK(actual.y == doctest::Approx(expected.y).epsilon(1e-5));
    CHECK(actual.z == doctest::Approx(expected.z).epsilon(1e-5));
}

void checkRotation(Quat actual, Quat expected)
{
    CHECK(std::abs(glm::dot(actual, expected)) == doctest::Approx(1.0f).epsilon(1e-5));
}

} // namespace

TEST_CASE("blendPoses blends by weight and mask along the shortest arc")
{
    std::vector<JointPose> from(2);
    std::vector<JointPose> to(2);
    to[0].translation = Vec3(2.0f, 0.0f, 0.0f);
    to[0].scale = Vec3(3.0f);
    to[0].rotation = -glm::angleAxis(kPi / 2.0f, Vec3(0.0f, 0.0f, 1.0f));
    to[1].translation = Vec3(4.0f, 0.0f, 0.0f);
    const std::vector<f32> mask = {1.0f, 0.0f};
    blendPoses(from, to, 0.5f, mask, from);
    checkVec(from[0].translation, Vec3(1.0f, 0.0f, 0.0f));
    checkVec(from[0].scale, Vec3(2.0f));
    checkRotation(from[0].rotation, glm::angleAxis(kPi / 4.0f, Vec3(0.0f, 0.0f, 1.0f)));
    checkVec(from[1].translation, Vec3(0.0f));

    std::vector<JointPose> out(2);
    blendPoses(from, to, 1.0f, {}, out);
    CHECK(out[1].translation == to[1].translation);
    CHECK(out[0].rotation == to[0].rotation);
}

TEST_CASE("addPose adds the difference from the reference pose")
{
    std::vector<JointPose> base(1);
    std::vector<JointPose> additive(1);
    std::vector<JointPose> reference(1);
    base[0].translation = Vec3(1.0f, 0.0f, 0.0f);
    additive[0].translation = Vec3(0.0f, 2.0f, 0.0f);
    reference[0].translation = Vec3(0.0f, 1.0f, 0.0f);
    additive[0].scale = Vec3(2.0f);
    reference[0].rotation = glm::angleAxis(kPi / 2.0f, Vec3(0.0f, 0.0f, 1.0f));
    additive[0].rotation = glm::angleAxis(kPi, Vec3(0.0f, 0.0f, 1.0f));
    addPose(base, additive, reference, 0.5f, {});
    checkVec(base[0].translation, Vec3(1.0f, 0.5f, 0.0f));
    checkVec(base[0].scale, Vec3(1.5f));
    checkRotation(base[0].rotation, glm::angleAxis(kPi / 4.0f, Vec3(0.0f, 0.0f, 1.0f)));

    const std::vector<f32> masked = {0.0f};
    addPose(base, additive, reference, 1.0f, masked);
    checkVec(base[0].translation, Vec3(1.0f, 0.5f, 0.0f));
}

TEST_CASE("bindGraph resolves clips, masks and the root motion joint")
{
    const Skeleton skeleton = makeSkeleton();
    const auto clips = makeClips();
    const auto findClip = [&](const AssetRef& ref) -> const AnimationClip* {
        const std::string_view path = ref.path();
        const auto found = clips.find(path.substr(std::min(path.size(), kClipPrefix.size())));
        return found != clips.end() ? &found->second : nullptr;
    };
    const std::string parameters = R"({"name": "s", "type": "Float"})";
    const std::string base = singleLayer(
        R"({"name": "a", "clip": "@v10"}, {"name": "b", "blend1d": {"parameter": "s", "points": [
              {"value": 0, "clip": "@v10"}, {"value": 1, "clip": "@v20"}, {"value": 2, "clip": "@CLIP"}]}})");
    const auto bind = [&](const std::string& json) {
        Expected<AnimationGraph, std::string> graph = parseAnimationGraph(json);
        REQUIRE(graph.hasValue());
        auto graphCopy = std::make_unique<AnimationGraph>(std::move(*graph));
        Expected<GraphBinding, std::string> binding = bindGraph(*graphCopy, skeleton, findClip);
        return std::make_pair(std::move(graphCopy), std::move(binding));
    };
    const auto replace = [](std::string text, std::string_view from, std::string_view to) {
        text.replace(text.find(from), from.size(), to);
        return text;
    };

    {
        const auto [graph, binding] = bind(graphJson(
            parameters,
            replace(base, "CLIP", "v30") +
                R"(, {"name": "upper", "mask": [{"joint": "spine", "weight": 0.5}, {"joint": "arm"}],
                                     "states": [{"name": "c", "clip": "@v30"}]})",
            R"("rootMotion": "root", )"));
        REQUIRE(binding.hasValue());
        CHECK(binding->clips.size() == 5);
        CHECK(binding->clipBindings.size() == 5);
        CHECK(binding->clips.at(3) == &clips.at("v30"));
        CHECK(binding->firstClip == std::vector<u32>{0, 1, 4});
        CHECK(binding->firstState == std::vector<u32>{0, 2});
        REQUIRE(binding->masks.size() == 2);
        CHECK(binding->masks[0].empty());
        CHECK(binding->masks[1] == std::vector<f32>{0.0f, 0.5f, 1.0f});
        CHECK(binding->rootJoint == 0);
    }
    {
        const auto [graph, binding] = bind(graphJson(parameters, replace(base, "CLIP", "nothing")));
        REQUIRE_FALSE(binding.hasValue());
        CHECK(binding.error() ==
              "layers[0].states[1].blend1d.points[2].clip: clip 'game:/rig.glb#animation/nothing' not found");
    }
    {
        const auto [graph, binding] = bind(graphJson(
            parameters, replace(base, "CLIP", "v30") + R"(, {"name": "upper", "mask": [{"joint": "tail"}],
                                                            "states": [{"name": "c", "clip": "@v30"}]})"));
        REQUIRE_FALSE(binding.hasValue());
        CHECK(binding.error() == "layers[1].mask[0].joint: the skeleton has no joint 'tail'");
    }
    {
        const auto [graph, binding] =
            bind(graphJson(parameters, replace(base, "CLIP", "v30"), R"("rootMotion": "hips", )"));
        REQUIRE_FALSE(binding.hasValue());
        CHECK(binding.error() == "rootMotion: the skeleton has no joint 'hips'");
    }
}

TEST_CASE("makeGraphState starts in the entry states with default parameters")
{
    Rig rig(graphJson(
        R"({"name": "f", "type": "Float", "default": 2.5}, {"name": "go", "type": "Trigger"},
                         {"name": "n", "type": "Int", "default": 3})",
        singleLayer(R"({"name": "a", "clip": "@v10"}, {"name": "b", "clip": "@v20"})", R"(, "entry": "b")")));
    CHECK(rig.layer().state == 1);
    CHECK(rig.layer().previous == -1);
    CHECK(rig.state.parameters == std::vector<f32>{2.5f, 0.0f, 3.0f});
    CHECK(rig.state.triggers ==
          std::vector<u8>{AnimationGraphState::kNotTrigger, 0, AnimationGraphState::kNotTrigger});

    rig.state.setValue(1, 1.0f);
    rig.state.setValue(3, 1.0f);
    rig.state.setValue(-1, 1.0f);
    rig.state.setValue(0, std::nanf(""));
    rig.state.setValue(0, HUGE_VALF);
    rig.state.setTrigger(0);
    rig.state.setTrigger(7);
    CHECK(rig.state.parameters == std::vector<f32>{2.5f, 0.0f, 3.0f});
    CHECK(rig.state.triggers ==
          std::vector<u8>{AnimationGraphState::kNotTrigger, 0, AnimationGraphState::kNotTrigger});
    rig.state.setValue(2, -1.0f);
    rig.state.setTrigger(1);
    CHECK(rig.state.parameters.at(2) == -1.0f);
    CHECK(rig.state.triggers.at(1) == 1);
}

TEST_CASE("A clip state advances by its speed, wraps when looping and holds otherwise")
{
    SUBCASE("looping")
    {
        Rig rig(graphJson("", singleLayer(R"({"name": "a", "clip": "@ramp"})")));
        rig.step(0.25f);
        CHECK(rig.layer().time == doctest::Approx(0.25f));
        checkVec(rig.arm(), Vec3(0.25f, 0.0f, 0.0f));
        rig.step(1.0f);
        CHECK(rig.layer().time == doctest::Approx(0.25f));
        CHECK(rig.layer().lastTime == doctest::Approx(0.25f));
        checkVec(rig.arm(), Vec3(0.25f, 0.0f, 0.0f));
    }
    SUBCASE("held")
    {
        Rig rig(graphJson("", singleLayer(R"({"name": "a", "clip": "@ramp", "loop": false})")));
        rig.step(1.5f);
        CHECK(rig.layer().time == doctest::Approx(1.5f));
        checkVec(rig.arm(), Vec3(1.0f, 0.0f, 0.0f));
    }
    SUBCASE("speed times a parameter, never backwards")
    {
        Rig rig(graphJson(
            R"({"name": "rate", "type": "Float", "default": 0.25})",
            singleLayer(R"({"name": "a", "clip": "@ramp", "speed": 2, "speedParameter": "rate"})")));
        rig.step(0.5f);
        CHECK(rig.layer().time == doctest::Approx(0.25f));
        rig.set("rate", -1.0f);
        rig.step(0.5f);
        CHECK(rig.layer().time == doctest::Approx(0.25f));
    }
}

TEST_CASE("A transition fires when its condition holds, for every kind of compare")
{
    struct Case
    {
        std::string_view parameter;
        std::string_view condition;
        f32 value;
        bool fires;
    };
    const std::vector<Case> cases = {
        {R"({"name": "p", "type": "Float"})", R"("compare": "Greater", "value": 1)", 1.5f, true},
        {R"({"name": "p", "type": "Float"})", R"("compare": "Greater", "value": 1)", 1.0f, false},
        {R"({"name": "p", "type": "Float"})", R"("compare": "Less", "value": 1)", 0.5f, true},
        {R"({"name": "p", "type": "Float"})", R"("compare": "Less", "value": 1)", 1.0f, false},
        {R"({"name": "p", "type": "Int"})", R"("compare": "Equal", "value": 2)", 2.0f, true},
        {R"({"name": "p", "type": "Int"})", R"("compare": "Equal", "value": 2)", 3.0f, false},
        {R"({"name": "p", "type": "Int"})", R"("compare": "NotEqual", "value": 2)", 3.0f, true},
        {R"({"name": "p", "type": "Int"})", R"("compare": "NotEqual", "value": 2)", 2.0f, false},
        {R"({"name": "p", "type": "Int"})", R"("compare": "Greater", "value": 2)", 3.0f, true},
        {R"({"name": "p", "type": "Int"})", R"("compare": "Less", "value": 2)", 2.0f, false},
        {R"({"name": "p", "type": "Bool"})", R"("compare": "Equal", "value": true)", 1.0f, true},
        {R"({"name": "p", "type": "Bool"})", R"("compare": "Equal", "value": true)", 0.0f, false},
        {R"({"name": "p", "type": "Bool"})", R"("compare": "NotEqual", "value": true)", 0.0f, true},
        {R"({"name": "p", "type": "Bool"})", R"("compare": "NotEqual", "value": true)", 1.0f, false},
        {R"({"name": "p", "type": "Trigger"})", "", 1.0f, true},
        {R"({"name": "p", "type": "Trigger"})", "", 0.0f, false},
    };
    for (const Case& test : cases)
    {
        CAPTURE(test.parameter);
        CAPTURE(test.condition);
        CAPTURE(test.value);
        const std::string condition = std::string(R"({"parameter": "p")") +
                                      (test.condition.empty() ? "" : ", " + std::string(test.condition)) +
                                      "}";
        Rig rig(graphJson(test.parameter,
                          singleLayer(R"({"name": "a", "clip": "@v10"}, {"name": "b", "clip": "@v20"})",
                                      R"(, "transitions": [{"from": "a", "to": "b", "conditions": [)" +
                                          condition + "]}]")));
        if (test.condition.empty())
        {
            if (test.value != 0.0f)
            {
                rig.trigger("p");
            }
        }
        else
        {
            rig.set("p", test.value);
        }
        rig.step(0.1f);
        CHECK(rig.layer().state == (test.fires ? 1u : 0u));
        CHECK(rig.arm().x == (test.fires ? 20.0f : 10.0f));
    }
}

TEST_CASE("A transition with an exit time waits for its source's normalized time")
{
    SUBCASE("inside one loop")
    {
        Rig rig(
            graphJson("", singleLayer(R"({"name": "a", "clip": "@ramp"}, {"name": "b", "clip": "@v20"})",
                                      R"(, "transitions": [{"from": "a", "to": "b", "exitTime": 0.5}])")));
        rig.step(0.25f);
        CHECK(rig.layer().state == 0);
        rig.step(0.3f);
        CHECK(rig.layer().state == 1);
        CHECK(rig.layer().time == 0.0f);
    }
    SUBCASE("at the end of a looping state")
    {
        Rig rig(graphJson("", singleLayer(R"({"name": "a", "clip": "@ramp"}, {"name": "b", "clip": "@v20"})",
                                          R"(, "transitions": [{"from": "a", "to": "b", "exitTime": 1}])")));
        rig.step(0.9f);
        CHECK(rig.layer().state == 0);
        rig.step(0.2f);
        CHECK(rig.layer().state == 1);
    }
    SUBCASE("at the end of a held state")
    {
        Rig rig(graphJson(
            "", singleLayer(R"({"name": "a", "clip": "@ramp", "loop": false}, {"name": "b", "clip": "@v20"})",
                            R"(, "transitions": [{"from": "a", "to": "b", "exitTime": 1}])")));
        rig.step(0.9f);
        CHECK(rig.layer().state == 0);
        rig.step(0.2f);
        CHECK(rig.layer().state == 1);
    }
}

TEST_CASE("An any-state transition never targets the current state")
{
    Rig rig(graphJson(
        R"({"name": "go", "type": "Trigger"})",
        singleLayer(R"({"name": "a", "clip": "@ramp"}, {"name": "b", "clip": "@ramp"})",
                    R"(, "transitions": [{"from": "*", "to": "b", "conditions": [{"parameter": "go"}]}])")));
    rig.trigger("go");
    rig.step(0.1f);
    CHECK(rig.layer().state == 1);
    CHECK(rig.layer().time == 0.0f);
    rig.trigger("go");
    rig.step(0.1f);
    CHECK(rig.layer().state == 1);
    CHECK(rig.layer().time == doctest::Approx(0.1f));
    CHECK(rig.state.triggers.at(0) == 0);
}

TEST_CASE("Transitions fire in priority order")
{
    const std::string states =
        R"({"name": "a", "clip": "@v10"}, {"name": "b", "clip": "@v20"}, {"name": "c", "clip": "@v30"})";
    {
        Rig rig(graphJson(
            "",
            singleLayer(states, R"(, "transitions": [{"from": "a", "to": "b"}, {"from": "a", "to": "c"}])")));
        rig.step(0.1f);
        CHECK(rig.layer().state == 1);
    }
    {
        Rig rig(graphJson(
            "",
            singleLayer(states, R"(, "transitions": [{"from": "*", "to": "c"}, {"from": "a", "to": "b"}])")));
        rig.step(0.1f);
        CHECK(rig.layer().state == 2);
        CHECK(rig.arm().x == 30.0f);
    }
}

TEST_CASE("A trigger is consumed by the transition it fires and cleared at the end of the step")
{
    SUBCASE("consumed before a later layer sees it")
    {
        const std::string layers =
            singleLayer(
                R"({"name": "a", "clip": "@v10"}, {"name": "b", "clip": "@v20"})",
                R"(, "transitions": [{"from": "a", "to": "b", "conditions": [{"parameter": "go"}]}])") +
            R"(, {"name": "upper", "states": [{"name": "x", "clip": "@v10"}, {"name": "y", "clip": "@v20"}],
                  "transitions": [{"from": "x", "to": "y", "conditions": [{"parameter": "go"}]}]})";
        Rig rig(graphJson(R"({"name": "go", "type": "Trigger"})", layers));
        rig.trigger("go");
        rig.step(0.1f);
        CHECK(rig.layer(0).state == 1);
        CHECK(rig.layer(1).state == 0);
        CHECK(rig.state.triggers.at(0) == 0);
    }
    SUBCASE("cleared when nothing fires")
    {
        Rig rig(graphJson(
            R"({"name": "go", "type": "Trigger"}, {"name": "ready", "type": "Bool"})",
            singleLayer(R"({"name": "a", "clip": "@v10"}, {"name": "b", "clip": "@v20"})",
                        R"(, "transitions": [{"from": "a", "to": "b", "conditions": [{"parameter": "go"},
                                            {"parameter": "ready", "compare": "Equal", "value": true}]}])")));
        rig.trigger("go");
        rig.step(0.1f);
        CHECK(rig.layer().state == 0);
        CHECK(rig.state.triggers.at(0) == 0);
        rig.set("ready", 1.0f);
        rig.step(0.1f);
        CHECK(rig.layer().state == 0);
        rig.trigger("go");
        rig.step(0.1f);
        CHECK(rig.layer().state == 1);
    }
}

TEST_CASE("A cross-fade blends the two states over its duration and is not interrupted")
{
    Rig rig(graphJson(
        R"({"name": "go", "type": "Trigger"}, {"name": "back", "type": "Trigger"})",
        singleLayer(
            R"({"name": "a", "clip": "@v10"}, {"name": "b", "clip": "@v20"})",
            R"(, "transitions": [{"from": "a", "to": "b", "duration": 1, "conditions": [{"parameter": "go"}]},
                                       {"from": "b", "to": "a", "duration": 1, "conditions": [{"parameter": "back"}]}])")));
    rig.trigger("go");
    rig.step(0.0f);
    CHECK(rig.layer().state == 1);
    CHECK(rig.layer().previous == 0);
    CHECK(rig.layer().fadeDuration == 1.0f);
    CHECK(rig.arm().x == doctest::Approx(10.0f));

    rig.step(0.5f);
    CHECK(rig.arm().x == doctest::Approx(15.0f));

    rig.trigger("back");
    rig.step(0.25f);
    CHECK(rig.layer().state == 1);
    CHECK(rig.layer().previous == 0);
    CHECK(rig.arm().x == doctest::Approx(17.5f));

    rig.step(0.25f);
    CHECK(rig.layer().state == 1);
    CHECK(rig.layer().previous == -1);
    CHECK(rig.layer().fadeDuration == 0.0f);
    CHECK(rig.arm().x == doctest::Approx(20.0f));
}

TEST_CASE("A 1D blend space blends the two neighbours of the parameter value")
{
    Rig rig(graphJson(
        R"({"name": "s", "type": "Float"})",
        singleLayer(R"({"name": "move", "blend1d": {"parameter": "s", "points": [{"value": 0, "clip": "@v10"},
                                     {"value": 2, "clip": "@v20"}, {"value": 6, "clip": "@v30"}]}})")));
    const std::vector<std::pair<f32, f32>> cases = {{-1.0f, 10.0f}, {0.0f, 10.0f}, {1.0f, 15.0f},
                                                    {2.0f, 20.0f},  {4.0f, 25.0f}, {6.0f, 30.0f},
                                                    {9.0f, 30.0f}};
    for (const auto& [value, expected] : cases)
    {
        CAPTURE(value);
        rig.set("s", value);
        rig.step(0.0f);
        CHECK(rig.arm().x == doctest::Approx(expected));
    }
}

TEST_CASE("A 2D blend space is barycentric inside its triangles and uses the nearest hull edge outside")
{
    Rig rig(graphJson(R"({"name": "x", "type": "Float"}, {"name": "y", "type": "Float"})",
                      singleLayer(R"({"name": "move", "blend2d": {"x": "x", "y": "y", "points": [
                          {"x": 0, "y": 0, "clip": "@p00"}, {"x": 1, "y": 0, "clip": "@p10"},
                          {"x": 0, "y": 1, "clip": "@p01"}, {"x": 1, "y": 1, "clip": "@p11"}]}})")));
    // Each clip puts the arm at its point, so inside the hull the arm follows the parameters exactly.
    struct Case
    {
        Vec2 at;
        Vec2 expected;
    };
    const std::vector<Case> cases = {
        {{0.25f, 0.5f}, {0.25f, 0.5f}}, {{0.9f, 0.3f}, {0.9f, 0.3f}}, {{1.0f, 1.0f}, {1.0f, 1.0f}},
        {{0.5f, 0.0f}, {0.5f, 0.0f}},   {{2.0f, 0.5f}, {1.0f, 0.5f}}, {{-1.0f, -1.0f}, {0.0f, 0.0f}},
        {{0.5f, 3.0f}, {0.5f, 1.0f}},
    };
    for (const Case& test : cases)
    {
        CAPTURE(test.at.x);
        CAPTURE(test.at.y);
        rig.set("x", test.at.x);
        rig.set("y", test.at.y);
        rig.step(0.0f);
        checkVec(rig.arm(), Vec3(test.expected, 0.0f));
    }
}

TEST_CASE("Clips of a blend space play in sync over their weighted duration")
{
    Rig rig(graphJson(
        R"({"name": "s", "type": "Float", "default": 0.5})",
        singleLayer(
            R"({"name": "move", "blend1d": {"parameter": "s", "points": [{"value": 0, "clip": "@ramp"},
                                     {"value": 1, "clip": "@ramp2"}]}})")));
    rig.step(0.75f);
    CHECK(rig.layer().time == doctest::Approx(0.5f));
    checkVec(rig.arm(), Vec3(0.25f, 0.5f, 0.0f));
}

TEST_CASE("Beyond kMaxActiveClips the lightest clips are dropped and their layer renormalized")
{
    const auto graphWith = [](u32 additiveLayers) {
        std::string layers =
            R"({"name": "base", "states": [{"name": "m", "blend1d": {"parameter": "base", "points": [
                                   {"value": 0, "clip": "@v10"}, {"value": 10, "clip": "@v20"}]}}]})";
        for (u32 l = 1; l <= additiveLayers; ++l)
        {
            const char* parameter = l == 1 ? "l1" : "half";
            layers += R"(, {"name": "add)" + std::to_string(l) +
                      R"(", "blend": "Additive", "states": [{"name": "m",
                         "blend1d": {"parameter": ")" +
                      std::string(parameter) + R"(", "points": [
                         {"value": 0, "clip": "@v10"}, {"value": )" +
                      (l == 1 ? std::string("10") : std::string("2")) + R"(, "clip": "@v20"}]}}]})";
        }
        return graphJson(
            R"({"name": "base", "type": "Float", "default": 7}, {"name": "l1", "type": "Float", "default": 4},
                            {"name": "half", "type": "Float", "default": 1})",
            layers);
    };
    // Additive layers of constant clips add nothing, so the base layer alone decides the pose.
    {
        Rig rig(graphWith(7));
        rig.step(0.1f);
        CHECK(rig.arm().x == doctest::Approx(17.0f));
    }
    {
        Rig rig(graphWith(8));
        rig.step(0.1f);
        CHECK(rig.arm().x == doctest::Approx(20.0f));
    }
}

TEST_CASE("An Override layer replaces the pose by its weight and mask")
{
    const auto layers = [](std::string_view mask) {
        return singleLayer(R"({"name": "a", "clip": "@lo"})") +
               R"(, {"name": "upper", "weight": 0.5, "mask": [)" + std::string(mask) +
               R"(], "states": [{"name": "b", "clip": "@hi"}]})";
    };
    {
        Rig rig(graphJson("", layers(R"({"joint": "spine", "weight": 0.5}, {"joint": "arm"})")));
        rig.step(0.1f);
        checkVec(rig.pose.at(0).translation, Vec3(0.0f));
        checkVec(rig.pose.at(1).translation, Vec3(1.25f, 1.0f, 0.0f));
        checkVec(rig.arm(), Vec3(3.0f, 0.0f, 0.0f));
    }
    {
        Rig rig(graphJson("", layers(R"({"joint": "arm"}, {"joint": "spine", "weight": 0.5})")));
        rig.step(0.1f);
        checkVec(rig.arm(), Vec3(2.0f, 0.0f, 0.0f));
    }
}

TEST_CASE("An Additive layer adds each clip's difference from its first frame")
{
    const auto layers = [](std::string_view weight) {
        return singleLayer(R"({"name": "a", "clip": "@turn30"})") +
               R"(, {"name": "upper", "blend": "Additive", "weight": )" + std::string(weight) +
               R"(, "states": [{"name": "b", "clip": "@bump"}]})";
    };
    const Quat turn = glm::angleAxis(kPi / 6.0f, Vec3(0.0f, 1.0f, 0.0f));
    {
        Rig rig(graphJson("", layers("1")));
        rig.step(0.5f);
        checkVec(rig.arm(), Vec3(1.0f, 0.5f, 0.0f));
        checkRotation(rig.pose.at(2).rotation, turn * glm::angleAxis(kPi / 4.0f, Vec3(0.0f, 0.0f, 1.0f)));
    }
    {
        Rig rig(graphJson("", layers("0.5")));
        rig.step(0.5f);
        checkVec(rig.arm(), Vec3(1.0f, 0.25f, 0.0f));
        checkRotation(rig.pose.at(2).rotation, turn * glm::angleAxis(kPi / 8.0f, Vec3(0.0f, 0.0f, 1.0f)));
    }
}

TEST_CASE("Events fire when a step crosses them")
{
    SUBCASE("across the wrap of a looping state")
    {
        Rig rig(graphJson("", singleLayer(R"({"name": "a", "clip": "@ramp",
                                               "events": [{"name": "late", "time": 0.9}, {"name": "early", "time": 0.1}]})")));
        rig.step(0.8f);
        REQUIRE(rig.fired == 1);
        CHECK(rig.events[0].event == 0);
        rig.step(0.4f);
        REQUIRE(rig.fired == 2);
        CHECK(rig.events[0].event == 1);
        CHECK(rig.events[1].event == 0);
        CHECK(rig.events[1].layer == 0);
        CHECK(rig.events[1].state == 0);
        CHECK(rig.graph.layers[0].states[0].events[rig.events[1].event].name == "early");
    }
    SUBCASE("once for a state that does not loop")
    {
        Rig rig(graphJson("", singleLayer(R"({"name": "a", "clip": "@ramp", "loop": false,
                                               "events": [{"name": "hit", "time": 0.5}]})")));
        rig.step(0.4f);
        CHECK(rig.fired == 0);
        rig.step(0.2f);
        CHECK(rig.fired == 1);
        rig.step(1.0f);
        CHECK(rig.fired == 0);
        rig.step(5.0f);
        CHECK(rig.fired == 0);
    }
    SUBCASE("for both states of a cross-fade while they have weight")
    {
        Rig rig(
            graphJson(R"({"name": "go", "type": "Trigger"})",
                      singleLayer(R"({"name": "a", "clip": "@ramp", "events": [{"name": "a", "time": 0.5}]},
                                         {"name": "b", "clip": "@ramp", "events": [{"name": "b", "time": 0.25}]})",
                                  R"(, "transitions": [{"from": "a", "to": "b", "duration": 1,
                                           "conditions": [{"parameter": "go"}]}])")));
        rig.step(0.4f);
        CHECK(rig.fired == 0);
        rig.trigger("go");
        rig.step(0.0f);
        CHECK(rig.fired == 0);
        rig.step(0.2f);
        REQUIRE(rig.fired == 1);
        CHECK(rig.events[0].state == 0);
        rig.step(0.1f);
        REQUIRE(rig.fired == 1);
        CHECK(rig.events[0].state == 1);
        // Ends the fade: the faded-out state has no weight left, so its crossing at 1.5 does not count.
        rig.step(0.9f);
        CHECK(rig.fired == 0);
        CHECK(rig.layer().previous == -1);
    }
    SUBCASE("counting those that do not fit")
    {
        Rig rig(
            graphJson("", singleLayer(R"({"name": "a", "clip": "@ramp", "events": [{"name": "a", "time": 0.1},
                                               {"name": "b", "time": 0.2}, {"name": "c", "time": 0.3}]})")));
        std::array<GraphEvent, 1> one{};
        CHECK(stepGraph(*rig.binding, rig.state, 0.5f, one) == 3);
        CHECK(one[0].event == 0);
        CHECK(stepGraph(*rig.binding, rig.state, 0.5f, {}) == 0);
    }
}

TEST_CASE("Root motion is taken from the clips across a loop wrap and removed from the pose")
{
    Rig rig(graphJson("", singleLayer(R"({"name": "a", "clip": "@slide"})"), R"("rootMotion": "root", )"));
    rig.step(0.75f);
    checkVec(rig.root.translation, Vec3(0.0f, 0.0f, 1.5f));
    CHECK(rig.root.yaw == doctest::Approx(0.75f * kPi / 2.0f));

    rig.step(0.5f);
    checkVec(rig.root.translation, Vec3(0.0f, 0.0f, 1.0f));
    CHECK(rig.root.yaw == doctest::Approx(kPi / 4.0f));
    checkVec(rig.pose.at(0).translation, Vec3(0.0f, 0.125f, 0.0f));
    checkRotation(rig.pose.at(0).rotation, Quat(1.0f, 0.0f, 0.0f, 0.0f));
    checkVec(rig.pose.at(1).translation, Vec3(0.0f, 1.0f, 0.0f));

    evaluateGraph(*rig.binding, rig.state, rig.scratch, rig.pose, nullptr);
    checkVec(rig.pose.at(0).translation, Vec3(0.0f, 0.125f, 0.5f));
    checkRotation(rig.pose.at(0).rotation, glm::angleAxis(kPi / 8.0f, Vec3(0.0f, 1.0f, 0.0f)));
}

TEST_CASE("Root motion is weighted by the clips' blend weights")
{
    Rig rig(graphJson(
        R"({"name": "s", "type": "Float", "default": 0.5})",
        singleLayer(
            R"({"name": "move", "blend1d": {"parameter": "s", "points": [{"value": 0, "clip": "@slide"},
                                     {"value": 1, "clip": "@v10"}]}})"),
        R"("rootMotion": "root", )"));
    rig.step(0.5f);
    checkVec(rig.root.translation, Vec3(0.0f, 0.0f, 0.5f));
    CHECK(rig.root.yaw == doctest::Approx(kPi / 8.0f));
    checkVec(rig.pose.at(0).translation, Vec3(0.0f, 0.125f, 0.0f));
}

namespace
{

const std::string kBusyGraph = graphJson(
    R"({"name": "x", "type": "Float"}, {"name": "y", "type": "Float"}, {"name": "aim", "type": "Float"},
       {"name": "jump", "type": "Trigger"}, {"name": "armed", "type": "Bool"})",
    R"({"name": "base", "states": [
          {"name": "move", "blend2d": {"x": "x", "y": "y", "points": [{"x": 0, "y": 0, "clip": "@slide"},
              {"x": 1, "y": 0, "clip": "@p10"}, {"x": 0, "y": 1, "clip": "@ramp"}, {"x": 1, "y": 1, "clip": "@ramp2"}]},
           "events": [{"name": "step", "time": 0.3}, {"name": "step", "time": 0.8}]},
          {"name": "jump", "clip": "@bump", "loop": false, "events": [{"name": "land", "time": 0.9}]}],
        "transitions": [{"from": "*", "to": "jump", "duration": 0.2, "conditions": [{"parameter": "jump"}]},
                        {"from": "jump", "to": "move", "duration": 0.3, "exitTime": 0.8}]},
       {"name": "upper", "weight": 0.7, "mask": [{"joint": "spine"}],
        "states": [{"name": "aim", "blend1d": {"parameter": "aim", "points": [{"value": -1, "clip": "@turn30"},
                       {"value": 1, "clip": "@hi"}]}},
                   {"name": "rest", "clip": "@lo"}],
        "transitions": [{"from": "aim", "to": "rest", "duration": 0.25, "conditions": [{"parameter": "armed", "compare": "Equal", "value": false}]},
                        {"from": "rest", "to": "aim", "duration": 0.25, "conditions": [{"parameter": "armed", "compare": "Equal", "value": true}]}]},
       {"name": "breathe", "blend": "Additive", "weight": 0.5, "states": [{"name": "b", "clip": "@bump"}]})",
    R"("rootMotion": "root", )");

// Every float a run of @p steps produces: poses, root motion and events.
std::vector<f32> randomRun(u32 seed, u32 steps)
{
    Rig rig(kBusyGraph);
    std::mt19937 random(seed);
    std::uniform_real_distribution<f32> unit(0.0f, 1.0f);
    std::vector<f32> trace;
    for (u32 i = 0; i < steps; ++i)
    {
        rig.set("x", unit(random) * 1.4f - 0.2f);
        rig.set("y", unit(random) * 1.4f - 0.2f);
        rig.set("aim", unit(random) * 2.0f - 1.0f);
        rig.set("armed", unit(random) < 0.5f ? 1.0f : 0.0f);
        if (unit(random) < 0.05f)
        {
            rig.trigger("jump");
        }
        rig.step(unit(random) * 0.05f);
        for (const JointPose& joint : rig.pose)
        {
            trace.insert(trace.end(),
                         {joint.translation.x, joint.translation.y, joint.translation.z, joint.rotation.x,
                          joint.rotation.y, joint.rotation.z, joint.rotation.w, joint.scale.x});
        }
        trace.insert(trace.end(), {rig.root.translation.x, rig.root.translation.z, rig.root.yaw,
                                   static_cast<f32>(rig.fired)});
        for (u32 e = 0; e < std::min<u32>(rig.fired, static_cast<u32>(rig.events.size())); ++e)
        {
            trace.push_back(static_cast<f32>(u32{rig.events[e].layer} * 100u +
                                             u32{rig.events[e].state} * 10u + rig.events[e].event));
        }
    }
    return trace;
}

} // namespace

TEST_CASE("A long random parameter sequence gives the same poses, root motion and events every run")
{
    const std::vector<f32> first = randomRun(1234, 2000);
    const std::vector<f32> second = randomRun(1234, 2000);
    REQUIRE(first.size() == second.size());
    CHECK(std::memcmp(first.data(), second.data(), first.size() * sizeof(f32)) == 0);
    CHECK(std::all_of(first.begin(), first.end(), [](f32 value) { return std::isfinite(value); }));
}

TEST_CASE("stepGraph and evaluateGraph do not allocate")
{
    Rig rig(kBusyGraph);
    std::array<GraphEvent, 4> events{};
    RootMotionDelta root;
    const i32 jump = rig.graph.findParameter("jump");
    const i32 armed = rig.graph.findParameter("armed");
    const i32 x = rig.graph.findParameter("x");
    rig.step(0.016f);
    usize allocations = 0;
    {
        const test::ScopedAllocationCounter counter;
        for (u32 i = 0; i < 300; ++i)
        {
            rig.state.setValue(x, static_cast<f32>(i % 10) * 0.1f);
            rig.state.setValue(armed, static_cast<f32>((i / 40) % 2));
            if (i % 50 == 0)
            {
                rig.state.setTrigger(jump);
            }
            (void)stepGraph(*rig.binding, rig.state, 0.016f, events);
            evaluateGraph(*rig.binding, rig.state, rig.scratch, rig.pose, &root);
        }
        allocations = counter.count();
    }
    CHECK(allocations == 0);
}
