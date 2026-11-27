#include <illusion/animation/AnimationGraph.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace illusion;

namespace
{

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
        expanded += c == '@' ? std::string("game:/rig.glb#animation/") : std::string(1, c);
    }
    return expanded;
}

constexpr std::string_view kOneState = R"({"name": "base", "states": [{"name": "idle", "clip": "@idle"}]})";

AnimationGraph parse(const std::string& json)
{
    Expected<AnimationGraph, std::string> graph = parseAnimationGraph(json);
    INFO((graph ? std::string() : graph.error()));
    REQUIRE(graph.hasValue());
    return std::move(*graph);
}

f64 triangleArea(const AnimationMotion& motion, usize t)
{
    const auto& p = motion.points;
    const AnimationBlendPoint& a = p.at(motion.triangles.at(t));
    const AnimationBlendPoint& b = p.at(motion.triangles.at(t + 1));
    const AnimationBlendPoint& c = p.at(motion.triangles.at(t + 2));
    const auto d = [](f32 v) {
        return static_cast<f64>(v);
    };
    return 0.5 * ((d(b.x) - d(a.x)) * (d(c.y) - d(a.y)) - (d(b.y) - d(a.y)) * (d(c.x) - d(a.x)));
}

// The Delaunay triangles of a blend space over @p points ("x, y" pairs).
AnimationMotion triangulate(const std::vector<std::pair<f32, f32>>& points)
{
    std::string list;
    for (const auto& [x, y] : points)
    {
        list += (list.empty() ? "" : ", ") + std::string(R"({"x": )") + std::to_string(x) + R"(, "y": )" +
                std::to_string(y) + R"(, "clip": "@c"})";
    }
    const AnimationGraph graph = parse(graphJson(
        R"({"name": "x", "type": "Float"}, {"name": "y", "type": "Float"})",
        R"({"name": "base", "states": [{"name": "move", "blend2d": {"x": "x", "y": "y", "points": [)" + list +
            "]}}]}"));
    return graph.layers.at(0).states.at(0).motion;
}

} // namespace

TEST_CASE("parseAnimationGraph reads every part of a graph file")
{
    const AnimationGraph graph = parse(graphJson(
        R"({"name": "speed", "type": "Float", "default": 1.5}, {"name": "weapon", "type": "Int", "default": 2},
           {"name": "crouch", "type": "Bool", "default": true}, {"name": "jump", "type": "Trigger"})",
        R"({"name": "base", "entry": "move", "states": [
              {"name": "jump", "clip": "@jump", "loop": false, "speed": 2},
              {"name": "move", "blend1d": {"parameter": "speed", "points": [{"value": 0, "clip": "@idle"},
                  {"value": 2, "clip": "@walk"}]}, "speedParameter": "speed",
               "events": [{"name": "step", "time": 0.6}, {"name": "step", "time": 0.1}]}],
            "transitions": [
              {"from": "*", "to": "jump", "duration": 0.1, "conditions": [{"parameter": "jump"}]},
              {"from": "jump", "to": "move", "duration": 0.2, "exitTime": 0.9,
               "conditions": [{"parameter": "weapon", "compare": "NotEqual", "value": 3},
                              {"parameter": "crouch", "compare": "Equal", "value": false},
                              {"parameter": "speed", "compare": "Less", "value": 4.5}]}]},
           {"name": "upper", "weight": 0.5, "blend": "Additive", "mask": [{"joint": "spine", "weight": 0.25}],
            "states": [{"name": "aim", "clip": "@aim"}]})",
        R"("rootMotion": "hips", )"));

    REQUIRE(graph.parameters.size() == 4);
    CHECK(graph.parameters[0].type == AnimationParameterType::Float);
    CHECK(graph.parameters[0].defaultValue == 1.5f);
    CHECK(graph.parameters[1].type == AnimationParameterType::Int);
    CHECK(graph.parameters[1].defaultValue == 2.0f);
    CHECK(graph.parameters[2].defaultValue == 1.0f);
    CHECK(graph.parameters[3].type == AnimationParameterType::Trigger);
    CHECK(graph.rootMotion == "hips");
    CHECK(graph.findParameter("jump") == 3);
    CHECK(graph.findParameter("missing") == -1);
    CHECK(graph.findLayer("upper") == 1);
    CHECK(graph.findLayer("lower") == -1);

    REQUIRE(graph.layers.size() == 2);
    const AnimationLayer& base = graph.layers[0];
    CHECK(base.entry == 1);
    CHECK(base.weight == 1.0f);
    CHECK(base.blend == AnimationLayerBlend::Override);
    REQUIRE(base.states.size() == 2);
    CHECK(base.states[0].motion.type == AnimationMotionType::Clip);
    CHECK(base.states[0].motion.clip.path() == "game:/rig.glb#animation/jump");
    CHECK_FALSE(base.states[0].loop);
    CHECK(base.states[0].speed == 2.0f);
    const AnimationState& move = base.states[1];
    CHECK(move.motion.type == AnimationMotionType::Blend1D);
    CHECK(move.motion.parameterX == 0);
    REQUIRE(move.motion.points.size() == 2);
    CHECK(move.motion.points[1].x == 2.0f);
    CHECK(move.speedParameter == 0);
    REQUIRE(move.events.size() == 2);
    CHECK(move.events[0].time == 0.1f);
    CHECK(move.events[1].time == 0.6f);

    REQUIRE(base.transitions.size() == 2);
    CHECK(base.transitions[0].from == -1);
    CHECK(base.transitions[0].to == 0);
    CHECK(base.transitions[0].exitTime == -1.0f);
    CHECK(base.transitions[0].conditions[0].compare == AnimationCompare::TriggerSet);
    const AnimationTransition& back = base.transitions[1];
    CHECK(back.from == 0);
    CHECK(back.to == 1);
    CHECK(back.duration == 0.2f);
    CHECK(back.exitTime == 0.9f);
    REQUIRE(back.conditions.size() == 3);
    CHECK(back.conditions[0].compare == AnimationCompare::NotEqual);
    CHECK(back.conditions[0].value == 3.0f);
    CHECK(back.conditions[1].compare == AnimationCompare::Equal);
    CHECK(back.conditions[1].value == 0.0f);
    CHECK(back.conditions[2].compare == AnimationCompare::Less);
    CHECK(back.conditions[2].value == 4.5f);

    const AnimationLayer& upper = graph.layers[1];
    CHECK(upper.weight == 0.5f);
    CHECK(upper.blend == AnimationLayerBlend::Additive);
    REQUIRE(upper.mask.size() == 1);
    CHECK(upper.mask[0].joint == "spine");
    CHECK(upper.mask[0].weight == 0.25f);
    CHECK(upper.entry == 0);
}

TEST_CASE("writeAnimationGraph round-trips a graph exactly")
{
    const std::string source = graphJson(
        R"({"name": "x", "type": "Float", "default": -0.1}, {"name": "y", "type": "Float"},
           {"name": "n", "type": "Int", "default": -4}, {"name": "b", "type": "Bool"}, {"name": "go", "type": "Trigger"})",
        R"({"name": "base", "entry": "grid", "states": [
              {"name": "idle", "clip": "@idle", "speed": 0.75, "loop": false, "events": [{"name": "e", "time": 1}]},
              {"name": "line", "blend1d": {"parameter": "x", "points": [{"value": -1.5, "clip": "@a"}, {"value": 0.1, "clip": "@b"}]}},
              {"name": "grid", "blend2d": {"x": "x", "y": "y", "points": [{"x": 0, "y": 0, "clip": "@a"},
                  {"x": 1, "y": 0, "clip": "@b"}, {"x": 0.5, "y": 1e-3, "clip": "@c"}, {"x": 0.3, "y": 0.7, "clip": "@d"}]},
               "speedParameter": "y"}],
            "transitions": [{"from": "*", "to": "idle", "conditions": [{"parameter": "go"}]},
              {"from": "idle", "to": "line", "duration": 0.25, "exitTime": 0,
               "conditions": [{"parameter": "n", "compare": "Greater", "value": -2},
                              {"parameter": "b", "compare": "NotEqual", "value": true},
                              {"parameter": "x", "compare": "Greater", "value": 0.3}]}]},
           {"name": "upper", "weight": 0.3, "blend": "Additive", "mask": [{"joint": "spine"}, {"joint": "arm", "weight": 0}],
            "states": [{"name": "s", "clip": "@s"}]})",
        R"("rootMotion": "root", )");
    const AnimationGraph graph = parse(source);
    const std::string written = writeAnimationGraph(graph);
    const AnimationGraph again = parse(written);
    CHECK(writeAnimationGraph(again) == written);

    CHECK(again.parameters.size() == graph.parameters.size());
    CHECK(std::signbit(again.parameters.at(0).defaultValue));
    CHECK(again.parameters.at(0).defaultValue == -0.1f);
    CHECK(again.parameters.at(2).defaultValue == -4.0f);
    CHECK(again.rootMotion == "root");
    const AnimationLayer& base = again.layers.at(0);
    CHECK(base.entry == 2);
    CHECK(base.states.at(0).speed == 0.75f);
    CHECK_FALSE(base.states.at(0).loop);
    CHECK(base.states.at(0).events.at(0).time == 1.0f);
    CHECK(base.states.at(1).motion.points.at(0).x == -1.5f);
    CHECK(base.states.at(2).motion.points.at(2).y == 1e-3f);
    CHECK(base.states.at(2).motion.triangles == graph.layers.at(0).states.at(2).motion.triangles);
    CHECK(base.states.at(2).speedParameter == 1);
    CHECK(base.transitions.at(1).exitTime == 0.0f);
    CHECK(base.transitions.at(1).duration == 0.25f);
    CHECK(base.transitions.at(1).conditions.at(0).value == -2.0f);
    CHECK(base.transitions.at(1).conditions.at(1).value == 1.0f);
    const AnimationLayer& upper = again.layers.at(1);
    CHECK(upper.weight == 0.3f);
    CHECK(upper.blend == AnimationLayerBlend::Additive);
    CHECK(upper.mask.at(1).weight == 0.0f);
}

TEST_CASE("parseAnimationGraph names the JSON path of every error")
{
    struct Case
    {
        std::string json;
        std::string_view message;
    };
    const auto layerWith = [](std::string_view states, std::string_view rest = "") {
        return std::string(R"({"name": "base", "states": [)") + std::string(states) + "]" +
               std::string(rest) + "}";
    };
    const std::string idle = R"({"name": "idle", "clip": "@idle"})";
    const std::string run = R"({"name": "run", "clip": "@run"})";
    const std::string speed = R"({"name": "speed", "type": "Float"})";
    const auto withTransition = [&](std::string_view parameters, std::string_view transition) {
        return graphJson(parameters, layerWith(idle + ", " + run, std::string(R"(, "transitions": [)") +
                                                                      std::string(transition) + "]"));
    };
    const auto withCondition = [&](std::string_view parameters, std::string_view condition) {
        return withTransition(parameters, std::string(R"({"from": "idle", "to": "run", "conditions": [)") +
                                              std::string(condition) + "]}");
    };
    const auto withState = [&](std::string_view parameters, std::string_view state) {
        return graphJson(parameters, layerWith(state));
    };
    const std::vector<Case> cases = {
        {"{", "invalid JSON at line 1"},
        {R"({"version": 1, "layers": []})", "not an animation graph file"},
        {R"({"format": "illusion.animgraph", "version": 3, "layers": []})",
         "version: unsupported animation graph version"},
        {graphJson("", kOneState, R"("extra": 1, )"), "unknown key 'extra'"},
        {graphJson("", ""), "layers: needs at least 1 entry"},
        {R"({"format": "illusion.animgraph", "version": 1})", "layers: is required"},
        {graphJson(speed + ", " + speed, kOneState), "parameters[1].name: duplicate parameter 'speed'"},
        {graphJson(R"({"name": "a", "type": "Double"})", kOneState),
         "parameters[0].type: unknown type 'Double' (Float, Int, Bool or Trigger)"},
        {graphJson(R"({"name": "a", "type": "Trigger", "default": 1})", kOneState),
         "parameters[0].default: a Trigger has no default"},
        {graphJson(R"({"name": "a", "type": "Int", "default": 1.5})", kOneState),
         "parameters[0].default: expected a whole number, found real"},
        {graphJson(R"({"name": "a", "type": "Bool", "default": 1})", kOneState),
         "parameters[0].default: a Bool takes true or false"},
        {graphJson(R"({"name": "a", "type": "Float", "default": 1e300})", kOneState),
         "parameters[0].default: must be a finite number"},
        {graphJson(R"({"name": "", "type": "Float"})", kOneState), "parameters[0].name: must not be empty"},
        {graphJson("", std::string(kOneState) + ", " + std::string(kOneState)),
         "layers[1].name: duplicate layer 'base'"},
        {graphJson("", layerWith(idle, R"(, "weight": 1.5)")), "layers[0].weight: must be in [0, 1]"},
        {graphJson("", layerWith(idle, R"(, "blend": "Multiply")")),
         "layers[0].blend: unknown blend 'Multiply' (Override or Additive)"},
        {graphJson("", layerWith(idle, R"(, "mask": [{"joint": "spine"}, {"joint": "spine"}])")),
         "layers[0].mask[1].joint: duplicate joint 'spine'"},
        {graphJson("", layerWith(idle, R"(, "mask": [{"joint": "spine", "weight": -0.5}])")),
         "layers[0].mask[0].weight: must be in [0, 1]"},
        {graphJson("", layerWith(idle + ", " + idle)), "layers[0].states[1].name: duplicate state 'idle'"},
        {graphJson("", layerWith("")), "layers[0].states: needs at least 1 entry"},
        {withState("", R"({"name": "*", "clip": "@idle"})"),
         "layers[0].states[0].name: '*' is reserved for transitions from any state"},
        {withState("", R"({"name": "idle"})"),
         "layers[0].states[0]: needs exactly one of clip, blend1d or blend2d"},
        {withState(speed,
                   R"({"name": "idle", "clip": "@idle", "blend1d": {"parameter": "speed", "points": []}})"),
         "layers[0].states[0]: needs exactly one of clip, blend1d or blend2d"},
        {withState("", R"({"name": "idle", "clip": "game:/rig.glb#mesh/0"})"),
         "layers[0].states[0].clip: 'game:/rig.glb#mesh/0' is not an animation clip path"},
        {withState("", R"({"name": "idle", "clip": "@idle", "speed": -1})"),
         "layers[0].states[0].speed: must be at least 0"},
        {withState("", R"({"name": "idle", "clip": "@idle", "lop": false})"),
         "layers[0].states[0]: unknown key 'lop'"},
        {withState("", R"({"name": "idle", "clip": "@idle", "loop": 1})"),
         "layers[0].states[0].loop: expected true or false, found integer"},
        {withState(R"({"name": "n", "type": "Int"})",
                   R"({"name": "idle", "clip": "@idle", "speedParameter": "n"})"),
         "layers[0].states[0].speedParameter: parameter 'n' is not a Float"},
        {withState("", R"({"name": "idle", "clip": "@idle", "events": [{"name": "e", "time": 1.25}]})"),
         "layers[0].states[0].events[0].time: must be in [0, 1]"},
        {withState("", R"({"name": "idle", "clip": "@idle", "events": [{"name": "e"}]})"),
         "layers[0].states[0].events[0].time: is required"},
        {withState(
             speed,
             R"({"name": "line", "blend1d": {"parameter": "speed", "points": [{"value": 1, "clip": "@a"}]}})"),
         "layers[0].states[0].blend1d.points: needs at least 2 entries"},
        {withState(
             speed,
             R"({"name": "line", "blend1d": {"parameter": "speed", "points": [{"value": 1, "clip": "@a"},
             {"value": 1, "clip": "@b"}]}})"),
         "layers[0].states[0].blend1d.points[1].value: points must be sorted by value, without repeats"},
        {withState(
             speed,
             R"({"name": "line", "blend1d": {"parameter": "speed", "points": [{"value": 2, "clip": "@a"},
             {"value": 1, "clip": "@b"}]}})"),
         "layers[0].states[0].blend1d.points[1].value: points must be sorted by value, without repeats"},
        {withState(speed, R"({"name": "line", "blend1d": {"parameter": "sped", "points": []}})"),
         "layers[0].states[0].blend1d.parameter: unknown parameter 'sped'"},
        {withState(
             speed + R"(, {"name": "y", "type": "Float"})",
             R"({"name": "grid", "blend2d": {"x": "speed", "y": "y", "points": [{"x": 0, "y": 0, "clip": "@a"},
                      {"x": 1, "y": 1, "clip": "@b"}, {"x": 2, "y": 2, "clip": "@c"}]}})"),
         "layers[0].states[0].blend2d.points: points must not all lie on one line"},
        {withState(
             speed + R"(, {"name": "y", "type": "Float"})",
             R"({"name": "grid", "blend2d": {"x": "speed", "y": "y", "points": [{"x": 0, "y": 0, "clip": "@a"},
                      {"x": 1, "y": 0, "clip": "@b"}, {"x": 1, "y": 0, "clip": "@c"}]}})"),
         "layers[0].states[0].blend2d.points[2]: repeats the point (1, 0)"},
        {withState(
             speed + R"(, {"name": "y", "type": "Float"})",
             R"({"name": "grid", "blend2d": {"x": "speed", "y": "y", "points": [{"x": 0, "y": 0, "clip": "@a"},
                      {"x": 1, "y": 0, "clip": "@b"}]}})"),
         "layers[0].states[0].blend2d.points: needs at least 3 entries"},
        {withState(speed, R"({"name": "grid", "blend2d": {"x": "speed", "y": "speed", "points": []}})"),
         "layers[0].states[0].blend2d.y: x and y must be different parameters"},
        {withState(
             speed + R"(, {"name": "y", "type": "Float"})",
             R"({"name": "grid", "blend2d": {"x": "speed", "y": "y", "points": [{"x": 0, "clip": "@a"}]}})"),
         "layers[0].states[0].blend2d.points: needs at least 3 entries"},
        {graphJson("", layerWith(idle, R"(, "entry": "walk")")), "layers[0].entry: unknown state 'walk'"},
        {withTransition("", R"({"from": "walk", "to": "run"})"),
         "layers[0].transitions[0].from: unknown state 'walk'"},
        {withTransition("", R"({"from": "idle", "to": "walk"})"),
         "layers[0].transitions[0].to: unknown state 'walk'"},
        {withTransition("", R"({"from": "idle", "to": "idle"})"),
         "layers[0].transitions[0].to: a transition must lead to another state"},
        {withTransition("", R"({"from": "idle", "to": "run", "duration": -0.5})"),
         "layers[0].transitions[0].duration: must be at least 0"},
        {withTransition("", R"({"from": "idle", "to": "run", "exitTime": 1.5})"),
         "layers[0].transitions[0].exitTime: must be in [0, 1]"},
        {withTransition("", R"({"to": "run"})"), "layers[0].transitions[0].from: is required"},
        {withTransition(speed, R"({"from": "idle", "to": "run"}, {"from": "run", "to": "idle",
             "conditions": [{"parameter": "sped", "compare": "Greater", "value": 1}]})"),
         "layers[0].transitions[1].conditions[0].parameter: unknown parameter 'sped'"},
        {withCondition(speed, R"({"parameter": "speed", "compare": "Equal", "value": 1})"),
         "layers[0].transitions[0].conditions[0].compare: 'speed' is a Float: compare with Greater or Less"},
        {withCondition(R"({"name": "b", "type": "Bool"})",
                       R"({"parameter": "b", "compare": "Greater", "value": true})"),
         "layers[0].transitions[0].conditions[0].compare: 'b' is a Bool: compare with Equal or NotEqual"},
        {withCondition(R"({"name": "b", "type": "Bool"})",
                       R"({"parameter": "b", "compare": "Equal", "value": 1})"),
         "layers[0].transitions[0].conditions[0].value: a Bool compares with true or false"},
        {withCondition(R"({"name": "go", "type": "Trigger"})",
                       R"({"parameter": "go", "compare": "Equal", "value": 1})"),
         "layers[0].transitions[0].conditions[0]: 'go' is a Trigger: a condition on it takes no compare or "
         "value"},
        {withCondition(speed, R"({"parameter": "speed", "compare": "Greater"})"),
         "layers[0].transitions[0].conditions[0].value: is required"},
        {withCondition(speed, R"({"parameter": "speed", "value": 1})"),
         "layers[0].transitions[0].conditions[0].compare: is required"},
        {withCondition(speed, R"({"parameter": "speed", "compare": "Above", "value": 1})"),
         "layers[0].transitions[0].conditions[0].compare: unknown compare 'Above' (Greater, Less, Equal or "
         "NotEqual)"},
        {withCondition(R"({"name": "n", "type": "Int"})",
                       R"({"parameter": "n", "compare": "Equal", "value": 0.5})"),
         "layers[0].transitions[0].conditions[0].value: expected a whole number, found real"},
        {withCondition(R"({"name": "n", "type": "Int"})",
                       R"({"parameter": "n", "compare": "Equal", "value": 100000000})"),
         "layers[0].transitions[0].conditions[0].value: must be in [-16777216, 16777216]"},
        {withCondition(speed, R"({"parameter": "speed", "compare": "Less", "value": "1"})"),
         "layers[0].transitions[0].conditions[0].value: expected a number, found string"},
        {graphJson("", kOneState, R"("rootMotion": 3, )"), "rootMotion: expected a string, found integer"},
    };
    for (const Case& test : cases)
    {
        CAPTURE(test.json);
        const Expected<AnimationGraph, std::string> graph = parseAnimationGraph(test.json);
        REQUIRE_FALSE(graph.hasValue());
        CAPTURE(graph.error());
        CHECK(graph.error().starts_with(test.message));
    }
}

TEST_CASE("Blend2D points are triangulated into Delaunay triangles that cover their hull")
{
    SUBCASE("a triangle")
    {
        const AnimationMotion motion = triangulate({{0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f}});
        CHECK(motion.triangles == std::vector<u32>{0, 2, 1});
    }
    SUBCASE("a square splits into two triangles")
    {
        const AnimationMotion motion = triangulate({{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}});
        REQUIRE(motion.triangles.size() == 6);
        CHECK(triangleArea(motion, 0) + triangleArea(motion, 3) == doctest::Approx(1.0));
    }
    SUBCASE("a 3 x 3 grid gives eight triangles")
    {
        std::vector<std::pair<f32, f32>> grid;
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                grid.emplace_back(static_cast<f32>(x), static_cast<f32>(y));
            }
        }
        const AnimationMotion motion = triangulate(grid);
        REQUIRE(motion.triangles.size() == 24);
        f64 area = 0.0;
        for (usize t = 0; t < motion.triangles.size(); t += 3)
        {
            CHECK(triangleArea(motion, t) == doctest::Approx(0.5));
            area += triangleArea(motion, t);
        }
        CHECK(area == doctest::Approx(4.0));
    }
    SUBCASE("the empty circumcircle property holds")
    {
        const AnimationMotion motion = triangulate({{0.0f, 0.0f},
                                                    {4.0f, 0.0f},
                                                    {4.0f, 3.0f},
                                                    {0.0f, 3.0f},
                                                    {1.0f, 1.0f},
                                                    {3.0f, 2.0f},
                                                    {2.0f, 0.5f},
                                                    {1.5f, 2.5f}});
        f64 area = 0.0;
        for (usize t = 0; t < motion.triangles.size(); t += 3)
        {
            const f64 doubled = 2.0 * triangleArea(motion, t);
            CHECK(doubled > 0.0);
            area += doubled * 0.5;
            const auto& a = motion.points.at(motion.triangles.at(t));
            const auto& b = motion.points.at(motion.triangles.at(t + 1));
            const auto& c = motion.points.at(motion.triangles.at(t + 2));
            for (const AnimationBlendPoint& d : motion.points)
            {
                const auto f = [](f32 v) {
                    return static_cast<f64>(v);
                };
                const f64 ax = f(a.x) - f(d.x), ay = f(a.y) - f(d.y), bx = f(b.x) - f(d.x),
                          by = f(b.y) - f(d.y), cx = f(c.x) - f(d.x), cy = f(c.y) - f(d.y);
                const f64 det = (ax * ax + ay * ay) * (bx * cy - cx * by) -
                                (bx * bx + by * by) * (ax * cy - cx * ay) +
                                (cx * cx + cy * cy) * (ax * by - bx * ay);
                CHECK(det <= 1e-9);
            }
        }
        CHECK(area == doctest::Approx(12.0));
    }
}
