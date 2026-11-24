#include <illusion/animation/AnimationGraph.hpp>
#include <illusion/core/AssetId.hpp>
#include <illusion/core/Json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <initializer_list>
#include <optional>
#include <utility>

namespace illusion
{
namespace
{

constexpr std::string_view kFormatName = "illusion.animgraph";

constexpr std::array<std::string_view, 4> kParameterTypes = {"Float", "Int", "Bool", "Trigger"};
constexpr std::array<std::string_view, 4> kCompareNames = {"Greater", "Less", "Equal", "NotEqual"};
constexpr std::array<std::string_view, 2> kBlendNames = {"Override", "Additive"};
constexpr std::array<std::string_view, 2> kSpaceNames = {"Local", "Model"};

std::string member(const std::string& path, std::string_view key)
{
    return path.empty() ? std::string(key) : std::format("{}.{}", path, key);
}

std::string element(const std::string& path, usize index)
{
    return std::format("{}[{}]", path, index);
}

struct Point2
{
    f64 x = 0.0;
    f64 y = 0.0;
};

f64 cross(Point2 o, Point2 a, Point2 b) noexcept
{
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

// True if @p d lies strictly inside the circumcircle of the counter-clockwise triangle (a, b, c).
bool inCircumcircle(Point2 a, Point2 b, Point2 c, Point2 d) noexcept
{
    const f64 ax = a.x - d.x;
    const f64 ay = a.y - d.y;
    const f64 bx = b.x - d.x;
    const f64 by = b.y - d.y;
    const f64 cx = c.x - d.x;
    const f64 cy = c.y - d.y;
    const f64 det = (ax * ax + ay * ay) * (bx * cy - cx * by) - (bx * bx + by * by) * (ax * cy - cx * ay) +
                    (cx * cx + cy * cy) * (ax * by - bx * ay);
    return det > 0.0;
}

// Area of the convex hull of @p points (Andrew's monotone chain).
f64 hullArea(std::vector<Point2> points)
{
    std::sort(points.begin(), points.end(),
              [](Point2 a, Point2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    std::vector<Point2> hull(points.size() * 2);
    usize k = 0;
    for (usize i = 0; i < points.size(); ++i)
    {
        while (k >= 2 && cross(hull.at(k - 2), hull.at(k - 1), points.at(i)) <= 0.0)
        {
            --k;
        }
        hull.at(k++) = points.at(i);
    }
    for (usize i = points.size() - 1, lower = k + 1; i > 0; --i)
    {
        while (k >= lower && cross(hull.at(k - 2), hull.at(k - 1), points.at(i - 1)) <= 0.0)
        {
            --k;
        }
        hull.at(k++) = points.at(i - 1);
    }
    f64 area = 0.0;
    for (usize i = 0; i + 1 < k; ++i)
    {
        area += hull.at(i).x * hull.at(i + 1).y - hull.at(i + 1).x * hull.at(i).y;
    }
    return 0.5 * area;
}

// Bowyer-Watson triangulation of @p points (not all collinear, distinct), points inserted in order;
// counter-clockwise triangles sorted by their indices. Empty when it does not cover the convex hull, which
// only happens for nearly collinear hull points.
std::vector<u32> delaunay(const std::vector<Point2>& input)
{
    Point2 low = input.front();
    Point2 high = input.front();
    for (const Point2 p : input)
    {
        low = {std::min(low.x, p.x), std::min(low.y, p.y)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y)};
    }
    const Point2 center{(low.x + high.x) * 0.5, (low.y + high.y) * 0.5};
    const f64 extent = std::max(high.x - low.x, high.y - low.y);
    std::vector<Point2> points;
    points.reserve(input.size() + 3);
    for (const Point2 p : input)
    {
        points.push_back({p.x - center.x, p.y - center.y});
    }
    const f64 far = extent * 1.0e4;
    const auto n = static_cast<u32>(input.size());
    points.push_back({-2.0 * far, -far});
    points.push_back({2.0 * far, -far});
    points.push_back({0.0, 2.0 * far});

    std::vector<std::array<u32, 3>> triangles;
    triangles.push_back({n, n + 1, n + 2});
    std::vector<std::array<u32, 2>> edges;
    for (u32 i = 0; i < n; ++i)
    {
        const Point2 p = points.at(i);
        edges.clear();
        std::vector<std::array<u32, 3>> kept;
        for (const std::array<u32, 3>& t : triangles)
        {
            if (!inCircumcircle(points.at(t[0]), points.at(t[1]), points.at(t[2]), p))
            {
                kept.push_back(t);
                continue;
            }
            for (u32 e = 0; e < 3; ++e)
            {
                const std::array<u32, 2> edge{t.at(e), t.at((e + 1) % 3)};
                // An edge shared by two removed triangles is inside the cavity; it appears reversed.
                const auto shared =
                    std::find(edges.begin(), edges.end(), std::array<u32, 2>{edge[1], edge[0]});
                if (shared != edges.end())
                {
                    edges.erase(shared);
                }
                else
                {
                    edges.push_back(edge);
                }
            }
        }
        for (const std::array<u32, 2>& edge : edges)
        {
            kept.push_back({edge[0], edge[1], i});
        }
        triangles = std::move(kept);
    }

    std::vector<std::array<u32, 3>> result;
    f64 area = 0.0;
    for (std::array<u32, 3> t : triangles)
    {
        if (t[0] >= n || t[1] >= n || t[2] >= n)
        {
            continue;
        }
        const f64 doubled = cross(points.at(t[0]), points.at(t[1]), points.at(t[2]));
        if (std::abs(doubled) <= 1.0e-12 * extent * extent)
        {
            continue;
        }
        if (doubled < 0.0)
        {
            std::swap(t[1], t[2]);
        }
        // Lowest index first, keeping the winding.
        while (t[0] > t[1] || t[0] > t[2])
        {
            t = {t[1], t[2], t[0]};
        }
        area += 0.5 * std::abs(doubled);
        result.push_back(t);
    }
    const f64 hull = hullArea(input);
    if (result.empty() || std::abs(area - hull) > 1.0e-9 * hull)
    {
        return {};
    }
    std::sort(result.begin(), result.end());
    std::vector<u32> indices;
    indices.reserve(result.size() * 3);
    for (const std::array<u32, 3>& t : result)
    {
        indices.insert(indices.end(), t.begin(), t.end());
    }
    return indices;
}

class Parser
{
public:
    std::string error;
    u64 version = kAnimationGraphVersion;

    bool fail(const std::string& path, std::string_view message)
    {
        if (error.empty())
        {
            error = path.empty() ? std::string(message) : std::format("{}: {}", path, message);
        }
        return false;
    }

    // @p value must be an object whose keys are all in @p allowed.
    bool object(JsonValue value, const std::string& path, std::initializer_list<std::string_view> allowed)
    {
        if (!value.isObject())
        {
            return fail(path, std::format("expected an object, found {}", jsonTypeName(value.type())));
        }
        for (const JsonMember& entry : value.members())
        {
            if (std::find(allowed.begin(), allowed.end(), entry.key) == allowed.end())
            {
                return fail(path, std::format("unknown key '{}'", entry.key));
            }
        }
        return true;
    }

    bool string(JsonValue parent, const std::string& path, std::string_view key, std::string& out,
                bool required)
    {
        const JsonValue value = parent.find(key);
        if (value.isAbsent())
        {
            return !required || fail(member(path, key), "is required");
        }
        const std::optional<std::string_view> text = value.asString();
        if (!text)
        {
            return fail(member(path, key),
                        std::format("expected a string, found {}", jsonTypeName(value.type())));
        }
        if (text->empty())
        {
            return fail(member(path, key), "must not be empty");
        }
        out = *text;
        return true;
    }

    // An optional number in [@p low, @p high]; @p out keeps its value when the key is absent.
    bool number(JsonValue parent, const std::string& path, std::string_view key, f32& out, f32 low, f32 high)
    {
        const JsonValue value = parent.find(key);
        if (value.isAbsent())
        {
            return true;
        }
        return numberValue(value, member(path, key), out, low, high);
    }

    bool numberValue(JsonValue value, const std::string& path, f32& out, f32 low, f32 high)
    {
        const std::optional<f64> number = value.asF64();
        if (!number)
        {
            return fail(path, std::format("expected a number, found {}", jsonTypeName(value.type())));
        }
        const auto single = static_cast<f32>(*number);
        if (!std::isfinite(single))
        {
            return fail(path, "must be a finite number");
        }
        if (single < low || single > high)
        {
            if (high == HUGE_VALF)
            {
                return fail(path, std::format("must be at least {}", low));
            }
            if (low == -HUGE_VALF)
            {
                return fail(path, std::format("must be at most {}", high));
            }
            return fail(path, std::format("must be in [{}, {}]", low, high));
        }
        out = single;
        return true;
    }

    bool boolean(JsonValue parent, const std::string& path, std::string_view key, bool& out)
    {
        const JsonValue value = parent.find(key);
        if (value.isAbsent())
        {
            return true;
        }
        const std::optional<bool> flag = value.asBool();
        if (!flag)
        {
            return fail(member(path, key),
                        std::format("expected true or false, found {}", jsonTypeName(value.type())));
        }
        out = *flag;
        return true;
    }

    // An array (absent: empty unless @p required) of at least @p minimum elements.
    bool array(JsonValue parent, const std::string& path, std::string_view key, JsonValue& out, bool required,
               usize minimum)
    {
        out = parent.find(key);
        if (out.isAbsent())
        {
            return (!required && minimum == 0) || fail(member(path, key), "is required");
        }
        if (!out.isArray())
        {
            return fail(member(path, key),
                        std::format("expected an array, found {}", jsonTypeName(out.type())));
        }
        if (out.size() < minimum)
        {
            return fail(member(path, key),
                        std::format("needs at least {} entr{}", minimum, minimum == 1 ? "y" : "ies"));
        }
        return true;
    }

    // Index of the entry of @p names equal to the string at @p key, which must name one of them.
    template <class T, class Name>
    bool reference(JsonValue parent, const std::string& path, std::string_view key,
                   const std::vector<T>& items, Name name, std::string_view what, u32& out)
    {
        std::string text;
        if (!string(parent, path, key, text, true))
        {
            return false;
        }
        for (usize i = 0; i < items.size(); ++i)
        {
            if (name(items[i]) == text)
            {
                out = static_cast<u32>(i);
                return true;
            }
        }
        return fail(member(path, key), std::format("unknown {} '{}'", what, text));
    }

    bool floatParameter(JsonValue parent, const std::string& path, std::string_view key,
                        const AnimationGraph& graph, u32& out)
    {
        if (!reference(
                parent, path, key, graph.parameters,
                [](const AnimationParameter& p) -> const std::string& { return p.name; }, "parameter", out))
        {
            return false;
        }
        if (graph.parameters.at(out).type != AnimationParameterType::Float)
        {
            return fail(member(path, key),
                        std::format("parameter '{}' is not a Float", graph.parameters.at(out).name));
        }
        return true;
    }

    bool clip(JsonValue parent, const std::string& path, AssetRef& out, std::string_view key = "clip")
    {
        std::string text;
        if (!string(parent, path, key, text, true))
        {
            return false;
        }
        const std::optional<std::string> normalized = normalizeAssetPath(text);
        const std::optional<AssetPathParts> parts =
            normalized ? splitAssetPath(*normalized) : std::optional<AssetPathParts>{};
        if (!parts || parts->fragment != AssetFragment::Animation)
        {
            return fail(
                member(path, key),
                std::format("'{}' is not an animation clip path (\"game:/model.glb#animation/<name>\")",
                            text));
        }
        out = AssetRef(*normalized);
        if (out.empty())
        {
            return fail(member(path, key), std::format("'{}' could not be recorded", text));
        }
        return true;
    }

    bool parameters(JsonValue root, AnimationGraph& graph)
    {
        JsonValue list;
        if (!array(root, "", "parameters", list, false, 0))
        {
            return false;
        }
        usize i = 0;
        for (const JsonValue entry : list.elements())
        {
            const std::string path = element("parameters", i++);
            AnimationParameter parameter;
            std::string type;
            if (!object(entry, path, {"name", "type", "default"}) ||
                !string(entry, path, "name", parameter.name, true) ||
                !string(entry, path, "type", type, true))
            {
                return false;
            }
            for (const AnimationParameter& other : graph.parameters)
            {
                if (other.name == parameter.name)
                {
                    return fail(member(path, "name"),
                                std::format("duplicate parameter '{}'", parameter.name));
                }
            }
            const auto kind = std::find(kParameterTypes.begin(), kParameterTypes.end(), type);
            if (kind == kParameterTypes.end())
            {
                return fail(member(path, "type"),
                            std::format("unknown type '{}' (Float, Int, Bool or Trigger)", type));
            }
            parameter.type = static_cast<AnimationParameterType>(kind - kParameterTypes.begin());
            const JsonValue value = entry.find("default");
            if (!value.isAbsent() && !defaultValue(value, member(path, "default"), parameter))
            {
                return false;
            }
            graph.parameters.push_back(std::move(parameter));
        }
        return true;
    }

    bool defaultValue(JsonValue value, const std::string& path, AnimationParameter& parameter)
    {
        switch (parameter.type)
        {
        case AnimationParameterType::Float:
            return numberValue(value, path, parameter.defaultValue, -HUGE_VALF, HUGE_VALF);
        case AnimationParameterType::Int:
            return integerValue(value, path, parameter.defaultValue);
        case AnimationParameterType::Bool:
            if (!value.isBool())
            {
                return fail(path, "a Bool takes true or false");
            }
            parameter.defaultValue = *value.asBool() ? 1.0f : 0.0f;
            return true;
        case AnimationParameterType::Trigger:
            return fail(path, "a Trigger has no default");
        }
        return true;
    }

    // Int parameters are stored as f32, so whole numbers beyond 2^24 would not round-trip.
    bool integerValue(JsonValue value, const std::string& path, f32& out)
    {
        constexpr i64 kLimit = i64{1} << 24;
        const std::optional<i64> number = value.asI64();
        if (!number)
        {
            return fail(path, std::format("expected a whole number, found {}", jsonTypeName(value.type())));
        }
        if (*number < -kLimit || *number > kLimit)
        {
            return fail(path, std::format("must be in [{}, {}]", -kLimit, kLimit));
        }
        out = static_cast<f32>(*number);
        return true;
    }

    bool state(JsonValue entry, const std::string& path, const AnimationGraph& graph, AnimationState& state)
    {
        if (!object(entry, path,
                    {"name", "clip", "blend1d", "blend2d", "speed", "speedParameter", "loop", "events"}) ||
            !string(entry, path, "name", state.name, true))
        {
            return false;
        }
        if (state.name == "*")
        {
            return fail(member(path, "name"), "'*' is reserved for transitions from any state");
        }
        const int motions = (entry.find("clip").isAbsent() ? 0 : 1) +
                            (entry.find("blend1d").isAbsent() ? 0 : 1) +
                            (entry.find("blend2d").isAbsent() ? 0 : 1);
        if (motions != 1)
        {
            return fail(path, "needs exactly one of clip, blend1d or blend2d");
        }
        if (!entry.find("clip").isAbsent())
        {
            state.motion.type = AnimationMotionType::Clip;
            if (!clip(entry, path, state.motion.clip))
            {
                return false;
            }
        }
        else if (!entry.find("blend1d").isAbsent())
        {
            if (!blend1d(entry.find("blend1d"), member(path, "blend1d"), graph, state.motion))
            {
                return false;
            }
        }
        else if (!blend2d(entry.find("blend2d"), member(path, "blend2d"), graph, state.motion))
        {
            return false;
        }
        if (!number(entry, path, "speed", state.speed, 0.0f, HUGE_VALF) ||
            !boolean(entry, path, "loop", state.loop))
        {
            return false;
        }
        if (!entry.find("speedParameter").isAbsent())
        {
            u32 index = 0;
            if (!floatParameter(entry, path, "speedParameter", graph, index))
            {
                return false;
            }
            state.speedParameter = static_cast<i32>(index);
        }
        JsonValue events;
        if (!array(entry, path, "events", events, false, 0))
        {
            return false;
        }
        usize i = 0;
        for (const JsonValue value : events.elements())
        {
            const std::string eventPath = element(member(path, "events"), i++);
            AnimationStateEvent event;
            if (!object(value, eventPath, {"name", "time"}) ||
                !string(value, eventPath, "name", event.name, true))
            {
                return false;
            }
            if (value.find("time").isAbsent())
            {
                return fail(member(eventPath, "time"), "is required");
            }
            if (!number(value, eventPath, "time", event.time, 0.0f, 1.0f))
            {
                return false;
            }
            // Insertion keeps events of equal time in file order.
            auto at = state.events.end();
            while (at != state.events.begin() && (at - 1)->time > event.time)
            {
                --at;
            }
            state.events.insert(at, std::move(event));
        }
        return true;
    }

    bool blend1d(JsonValue value, const std::string& path, const AnimationGraph& graph,
                 AnimationMotion& motion)
    {
        motion.type = AnimationMotionType::Blend1D;
        JsonValue points;
        if (!object(value, path, {"parameter", "points"}) ||
            !floatParameter(value, path, "parameter", graph, motion.parameterX) ||
            !array(value, path, "points", points, true, 2))
        {
            return false;
        }
        usize i = 0;
        for (const JsonValue entry : points.elements())
        {
            const std::string pointPath = element(member(path, "points"), i++);
            AnimationBlendPoint point;
            if (!object(entry, pointPath, {"value", "clip"}))
            {
                return false;
            }
            if (entry.find("value").isAbsent())
            {
                return fail(member(pointPath, "value"), "is required");
            }
            if (!number(entry, pointPath, "value", point.x, -HUGE_VALF, HUGE_VALF) ||
                !clip(entry, pointPath, point.clip))
            {
                return false;
            }
            if (!motion.points.empty() && !(point.x > motion.points.back().x))
            {
                return fail(member(pointPath, "value"), "points must be sorted by value, without repeats");
            }
            motion.points.push_back(std::move(point));
        }
        return true;
    }

    bool blend2d(JsonValue value, const std::string& path, const AnimationGraph& graph,
                 AnimationMotion& motion)
    {
        motion.type = AnimationMotionType::Blend2D;
        JsonValue points;
        if (!object(value, path, {"x", "y", "points"}) ||
            !floatParameter(value, path, "x", graph, motion.parameterX) ||
            !floatParameter(value, path, "y", graph, motion.parameterY))
        {
            return false;
        }
        if (motion.parameterX == motion.parameterY)
        {
            return fail(member(path, "y"), "x and y must be different parameters");
        }
        if (!array(value, path, "points", points, true, 3))
        {
            return false;
        }
        usize i = 0;
        std::vector<Point2> coordinates;
        for (const JsonValue entry : points.elements())
        {
            const std::string pointPath = element(member(path, "points"), i++);
            AnimationBlendPoint point;
            if (!object(entry, pointPath, {"x", "y", "clip"}))
            {
                return false;
            }
            for (const std::string_view key : {"x", "y"})
            {
                if (entry.find(key).isAbsent())
                {
                    return fail(member(pointPath, key), "is required");
                }
            }
            if (!number(entry, pointPath, "x", point.x, -HUGE_VALF, HUGE_VALF) ||
                !number(entry, pointPath, "y", point.y, -HUGE_VALF, HUGE_VALF) ||
                !clip(entry, pointPath, point.clip))
            {
                return false;
            }
            for (const AnimationBlendPoint& other : motion.points)
            {
                if (other.x == point.x && other.y == point.y)
                {
                    return fail(pointPath, std::format("repeats the point ({}, {})", point.x, point.y));
                }
            }
            coordinates.push_back({static_cast<f64>(point.x), static_cast<f64>(point.y)});
            motion.points.push_back(std::move(point));
        }
        f64 area = 0.0;
        f64 extent = 0.0;
        for (const Point2 p : coordinates)
        {
            extent = std::max(
                {extent, std::abs(p.x - coordinates.front().x), std::abs(p.y - coordinates.front().y)});
            for (const Point2 q : coordinates)
            {
                area = std::max(area, std::abs(cross(coordinates.front(), p, q)));
            }
        }
        if (!(area > 1.0e-9 * extent * extent))
        {
            return fail(member(path, "points"), "points must not all lie on one line");
        }
        motion.triangles = delaunay(coordinates);
        if (motion.triangles.empty())
        {
            return fail(member(path, "points"),
                        "points could not be triangulated (nearly collinear points on the outline)");
        }
        return true;
    }

    bool layer(JsonValue entry, const std::string& path, const AnimationGraph& graph, AnimationLayer& layer)
    {
        const bool keys =
            version >= 2
                ? object(entry, path,
                         {"name", "weight", "blend", "reference", "referenceTime", "space", "mask", "entry",
                          "states", "transitions"})
                : object(entry, path, {"name", "weight", "blend", "mask", "entry", "states", "transitions"});
        if (!keys || !string(entry, path, "name", layer.name, true) ||
            !number(entry, path, "weight", layer.weight, 0.0f, 1.0f))
        {
            return false;
        }
        for (const AnimationLayer& other : graph.layers)
        {
            if (other.name == layer.name)
            {
                return fail(member(path, "name"), std::format("duplicate layer '{}'", layer.name));
            }
        }
        std::string blend;
        if (!string(entry, path, "blend", blend, false))
        {
            return false;
        }
        if (!blend.empty())
        {
            const auto found = std::find(kBlendNames.begin(), kBlendNames.end(), blend);
            if (found == kBlendNames.end())
            {
                return fail(member(path, "blend"),
                            std::format("unknown blend '{}' (Override or Additive)", blend));
            }
            layer.blend = static_cast<AnimationLayerBlend>(found - kBlendNames.begin());
        }
        if (!additiveOptions(entry, path, layer))
        {
            return false;
        }
        JsonValue list;
        if (!array(entry, path, "mask", list, false, 0))
        {
            return false;
        }
        usize i = 0;
        for (const JsonValue value : list.elements())
        {
            const std::string maskPath = element(member(path, "mask"), i++);
            AnimationMaskJoint joint;
            if (!object(value, maskPath, {"joint", "weight"}) ||
                !string(value, maskPath, "joint", joint.joint, true) ||
                !number(value, maskPath, "weight", joint.weight, 0.0f, 1.0f))
            {
                return false;
            }
            for (const AnimationMaskJoint& other : layer.mask)
            {
                if (other.joint == joint.joint)
                {
                    return fail(member(maskPath, "joint"), std::format("duplicate joint '{}'", joint.joint));
                }
            }
            layer.mask.push_back(std::move(joint));
        }
        if (!array(entry, path, "states", list, true, 1))
        {
            return false;
        }
        i = 0;
        for (const JsonValue value : list.elements())
        {
            const std::string statePath = element(member(path, "states"), i++);
            AnimationState parsed;
            if (!state(value, statePath, graph, parsed))
            {
                return false;
            }
            for (const AnimationState& other : layer.states)
            {
                if (other.name == parsed.name)
                {
                    return fail(member(statePath, "name"), std::format("duplicate state '{}'", parsed.name));
                }
            }
            layer.states.push_back(std::move(parsed));
        }
        const auto stateName = [](const AnimationState& s) -> const std::string& {
            return s.name;
        };
        if (!entry.find("entry").isAbsent() &&
            !reference(entry, path, "entry", layer.states, stateName, "state", layer.entry))
        {
            return false;
        }
        if (!array(entry, path, "transitions", list, false, 0))
        {
            return false;
        }
        i = 0;
        for (const JsonValue value : list.elements())
        {
            const std::string transitionPath = element(member(path, "transitions"), i++);
            AnimationTransition parsed;
            if (!transition(value, transitionPath, graph, layer, parsed))
            {
                return false;
            }
            layer.transitions.push_back(std::move(parsed));
        }
        return true;
    }

    // The version 2 keys of a layer: the reference pose and the space of an Additive layer.
    bool additiveOptions(JsonValue entry, const std::string& path, AnimationLayer& layer)
    {
        const bool additive = layer.blend == AnimationLayerBlend::Additive;
        for (const std::string_view key : {"reference", "referenceTime", "space"})
        {
            if (!additive && !entry.find(key).isAbsent())
            {
                return fail(member(path, key), "only an Additive layer takes it");
            }
        }
        if (!entry.find("reference").isAbsent() && !clip(entry, path, layer.reference, "reference"))
        {
            return false;
        }
        if (!entry.find("referenceTime").isAbsent() && layer.reference.empty())
        {
            return fail(member(path, "referenceTime"), "needs a reference");
        }
        if (!number(entry, path, "referenceTime", layer.referenceTime, 0.0f, 1.0f))
        {
            return false;
        }
        std::string space;
        if (!string(entry, path, "space", space, false))
        {
            return false;
        }
        if (!space.empty())
        {
            const auto found = std::find(kSpaceNames.begin(), kSpaceNames.end(), space);
            if (found == kSpaceNames.end())
            {
                return fail(member(path, "space"), std::format("unknown space '{}' (Local or Model)", space));
            }
            layer.space = static_cast<AnimationAdditiveSpace>(found - kSpaceNames.begin());
        }
        return true;
    }

    bool transition(JsonValue entry, const std::string& path, const AnimationGraph& graph,
                    const AnimationLayer& layer, AnimationTransition& transition)
    {
        const auto stateName = [](const AnimationState& s) -> const std::string& {
            return s.name;
        };
        if (!object(entry, path, {"from", "to", "duration", "exitTime", "conditions"}))
        {
            return false;
        }
        std::string from;
        if (!string(entry, path, "from", from, true))
        {
            return false;
        }
        if (from != "*")
        {
            u32 index = 0;
            if (!reference(entry, path, "from", layer.states, stateName, "state", index))
            {
                return false;
            }
            transition.from = static_cast<i32>(index);
        }
        if (!reference(entry, path, "to", layer.states, stateName, "state", transition.to))
        {
            return false;
        }
        if (transition.from == static_cast<i32>(transition.to))
        {
            return fail(member(path, "to"), "a transition must lead to another state");
        }
        if (!number(entry, path, "duration", transition.duration, 0.0f, HUGE_VALF) ||
            !number(entry, path, "exitTime", transition.exitTime, 0.0f, 1.0f))
        {
            return false;
        }
        JsonValue list;
        if (!array(entry, path, "conditions", list, false, 0))
        {
            return false;
        }
        usize i = 0;
        for (const JsonValue value : list.elements())
        {
            const std::string conditionPath = element(member(path, "conditions"), i++);
            AnimationCondition parsed;
            if (!condition(value, conditionPath, graph, parsed))
            {
                return false;
            }
            transition.conditions.push_back(parsed);
        }
        return true;
    }

    bool condition(JsonValue entry, const std::string& path, const AnimationGraph& graph,
                   AnimationCondition& condition)
    {
        const auto parameterName = [](const AnimationParameter& p) -> const std::string& {
            return p.name;
        };
        if (!object(entry, path, {"parameter", "compare", "value"}) ||
            !reference(entry, path, "parameter", graph.parameters, parameterName, "parameter",
                       condition.parameter))
        {
            return false;
        }
        const AnimationParameter& parameter = graph.parameters.at(condition.parameter);
        const JsonValue value = entry.find("value");
        if (parameter.type == AnimationParameterType::Trigger)
        {
            if (!entry.find("compare").isAbsent() || !value.isAbsent())
            {
                return fail(path,
                            std::format("'{}' is a Trigger: a condition on it takes no compare or value",
                                        parameter.name));
            }
            condition.compare = AnimationCompare::TriggerSet;
            return true;
        }
        std::string compare;
        if (!string(entry, path, "compare", compare, true))
        {
            return false;
        }
        const auto found = std::find(kCompareNames.begin(), kCompareNames.end(), compare);
        if (found == kCompareNames.end())
        {
            return fail(member(path, "compare"),
                        std::format("unknown compare '{}' (Greater, Less, Equal or NotEqual)", compare));
        }
        condition.compare = static_cast<AnimationCompare>(found - kCompareNames.begin());
        const bool ordered =
            condition.compare == AnimationCompare::Greater || condition.compare == AnimationCompare::Less;
        if (parameter.type == AnimationParameterType::Float && !ordered)
        {
            return fail(member(path, "compare"),
                        std::format("'{}' is a Float: compare with Greater or Less", parameter.name));
        }
        if (parameter.type == AnimationParameterType::Bool && ordered)
        {
            return fail(member(path, "compare"),
                        std::format("'{}' is a Bool: compare with Equal or NotEqual", parameter.name));
        }
        if (value.isAbsent())
        {
            return fail(member(path, "value"), "is required");
        }
        switch (parameter.type)
        {
        case AnimationParameterType::Float:
            return numberValue(value, member(path, "value"), condition.value, -HUGE_VALF, HUGE_VALF);
        case AnimationParameterType::Int:
            return integerValue(value, member(path, "value"), condition.value);
        case AnimationParameterType::Bool:
            if (!value.isBool())
            {
                return fail(member(path, "value"), "a Bool compares with true or false");
            }
            condition.value = *value.asBool() ? 1.0f : 0.0f;
            return true;
        case AnimationParameterType::Trigger:
            break;
        }
        return true;
    }
};

const char* parameterTypeName(AnimationParameterType type) noexcept
{
    return kParameterTypes.at(static_cast<usize>(type)).data();
}

} // namespace

i32 AnimationGraph::findParameter(std::string_view name) const noexcept
{
    for (usize i = 0; i < parameters.size(); ++i)
    {
        if (parameters[i].name == name)
        {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

i32 AnimationGraph::findLayer(std::string_view name) const noexcept
{
    for (usize i = 0; i < layers.size(); ++i)
    {
        if (layers[i].name == name)
        {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

Expected<AnimationGraph, std::string> parseAnimationGraph(std::string_view json)
{
    Expected<JsonDocument, JsonError> doc = JsonDocument::parse(json);
    if (!doc)
    {
        return unexpected(std::format("invalid JSON at line {}, column {}: {}", doc.error().line,
                                      doc.error().column, doc.error().message));
    }
    const JsonValue root = doc->root();
    if (!root.isObject() || root.find("format").asString() != kFormatName)
    {
        return unexpected(
            std::string("not an animation graph file (missing \"format\": \"illusion.animgraph\")"));
    }
    const std::optional<u64> version = root.find("version").asU64();
    if (!version || *version == 0 || *version > kAnimationGraphVersion)
    {
        return unexpected(
            std::format("version: unsupported animation graph version (this build reads 1 to {})",
                        kAnimationGraphVersion));
    }
    Parser parser;
    parser.version = *version;
    AnimationGraph graph;
    JsonValue layers;
    if (!parser.object(root, "", {"format", "version", "parameters", "rootMotion", "layers"}) ||
        !parser.parameters(root, graph) || !parser.string(root, "", "rootMotion", graph.rootMotion, false) ||
        !parser.array(root, "", "layers", layers, true, 1))
    {
        return unexpected(std::move(parser.error));
    }
    usize i = 0;
    for (const JsonValue entry : layers.elements())
    {
        AnimationLayer layer;
        if (!parser.layer(entry, element("layers", i++), graph, layer))
        {
            return unexpected(std::move(parser.error));
        }
        graph.layers.push_back(std::move(layer));
    }
    return graph;
}

std::string writeAnimationGraph(const AnimationGraph& graph)
{
    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    root.add("format", doc.makeString(kFormatName));
    // A graph without version 2 features is written as version 1, which older builds still read.
    const bool second = std::ranges::any_of(graph.layers, [](const AnimationLayer& layer) {
        return !layer.reference.empty() || layer.referenceTime != 0.0f ||
               layer.space != AnimationAdditiveSpace::Local;
    });
    root.add("version", doc.makeU64(second ? kAnimationGraphVersion : 1));
    if (!graph.parameters.empty())
    {
        JsonMutValue parameters = root.addArray("parameters");
        for (const AnimationParameter& parameter : graph.parameters)
        {
            JsonMutValue entry = doc.makeObject();
            entry.add("name", doc.makeString(parameter.name));
            entry.add("type", doc.makeString(parameterTypeName(parameter.type)));
            if (parameter.defaultValue != 0.0f || std::signbit(parameter.defaultValue))
            {
                switch (parameter.type)
                {
                case AnimationParameterType::Float:
                    entry.add("default", doc.makeF32(parameter.defaultValue));
                    break;
                case AnimationParameterType::Int:
                    entry.add("default", doc.makeI64(static_cast<i64>(parameter.defaultValue)));
                    break;
                case AnimationParameterType::Bool:
                    entry.add("default", doc.makeBool(parameter.defaultValue != 0.0f));
                    break;
                case AnimationParameterType::Trigger:
                    break;
                }
            }
            parameters.push(entry);
        }
    }
    if (!graph.rootMotion.empty())
    {
        root.add("rootMotion", doc.makeString(graph.rootMotion));
    }
    const auto clipValue = [&](const AssetRef& clip) {
        return doc.makeString(clip.path());
    };
    JsonMutValue layers = root.addArray("layers");
    for (const AnimationLayer& layer : graph.layers)
    {
        JsonMutValue entry = doc.makeObject();
        entry.add("name", doc.makeString(layer.name));
        if (layer.weight != 1.0f)
        {
            entry.add("weight", doc.makeF32(layer.weight));
        }
        if (layer.blend != AnimationLayerBlend::Override)
        {
            entry.add("blend", doc.makeString(kBlendNames.at(static_cast<usize>(layer.blend))));
        }
        if (!layer.reference.empty())
        {
            entry.add("reference", clipValue(layer.reference));
        }
        if (layer.referenceTime != 0.0f)
        {
            entry.add("referenceTime", doc.makeF32(layer.referenceTime));
        }
        if (layer.space != AnimationAdditiveSpace::Local)
        {
            entry.add("space", doc.makeString(kSpaceNames.at(static_cast<usize>(layer.space))));
        }
        if (!layer.mask.empty())
        {
            JsonMutValue mask = entry.addArray("mask");
            for (const AnimationMaskJoint& joint : layer.mask)
            {
                JsonMutValue value = doc.makeObject();
                value.add("joint", doc.makeString(joint.joint));
                if (joint.weight != 1.0f)
                {
                    value.add("weight", doc.makeF32(joint.weight));
                }
                mask.push(value);
            }
        }
        if (layer.entry < layer.states.size())
        {
            entry.add("entry", doc.makeString(layer.states[layer.entry].name));
        }
        JsonMutValue states = entry.addArray("states");
        for (const AnimationState& state : layer.states)
        {
            JsonMutValue value = doc.makeObject();
            value.add("name", doc.makeString(state.name));
            const AnimationMotion& motion = state.motion;
            const auto parameterName = [&](u32 index) {
                return doc.makeString(index < graph.parameters.size()
                                          ? std::string_view(graph.parameters[index].name)
                                          : std::string_view());
            };
            switch (motion.type)
            {
            case AnimationMotionType::Clip:
                value.add("clip", clipValue(motion.clip));
                break;
            case AnimationMotionType::Blend1D:
            {
                JsonMutValue blend = value.addObject("blend1d");
                blend.add("parameter", parameterName(motion.parameterX));
                JsonMutValue points = blend.addArray("points");
                for (const AnimationBlendPoint& point : motion.points)
                {
                    JsonMutValue p = doc.makeObject();
                    p.add("value", doc.makeF32(point.x));
                    p.add("clip", clipValue(point.clip));
                    points.push(p);
                }
                break;
            }
            case AnimationMotionType::Blend2D:
            {
                JsonMutValue blend = value.addObject("blend2d");
                blend.add("x", parameterName(motion.parameterX));
                blend.add("y", parameterName(motion.parameterY));
                JsonMutValue points = blend.addArray("points");
                for (const AnimationBlendPoint& point : motion.points)
                {
                    JsonMutValue p = doc.makeObject();
                    p.add("x", doc.makeF32(point.x));
                    p.add("y", doc.makeF32(point.y));
                    p.add("clip", clipValue(point.clip));
                    points.push(p);
                }
                break;
            }
            }
            if (state.speed != 1.0f)
            {
                value.add("speed", doc.makeF32(state.speed));
            }
            if (state.speedParameter >= 0)
            {
                value.add("speedParameter", parameterName(static_cast<u32>(state.speedParameter)));
            }
            if (!state.loop)
            {
                value.add("loop", doc.makeBool(false));
            }
            if (!state.events.empty())
            {
                JsonMutValue events = value.addArray("events");
                for (const AnimationStateEvent& event : state.events)
                {
                    JsonMutValue e = doc.makeObject();
                    e.add("name", doc.makeString(event.name));
                    e.add("time", doc.makeF32(event.time));
                    events.push(e);
                }
            }
            states.push(value);
        }
        if (!layer.transitions.empty())
        {
            JsonMutValue transitions = entry.addArray("transitions");
            const auto stateName = [&](usize index) {
                return doc.makeString(index < layer.states.size() ? std::string_view(layer.states[index].name)
                                                                  : std::string_view());
            };
            for (const AnimationTransition& transition : layer.transitions)
            {
                JsonMutValue value = doc.makeObject();
                value.add("from", transition.from < 0 ? doc.makeString("*")
                                                      : stateName(static_cast<usize>(transition.from)));
                value.add("to", stateName(transition.to));
                if (transition.duration != 0.0f)
                {
                    value.add("duration", doc.makeF32(transition.duration));
                }
                if (transition.exitTime >= 0.0f)
                {
                    value.add("exitTime", doc.makeF32(transition.exitTime));
                }
                if (!transition.conditions.empty())
                {
                    JsonMutValue conditions = value.addArray("conditions");
                    for (const AnimationCondition& condition : transition.conditions)
                    {
                        JsonMutValue c = doc.makeObject();
                        const bool known = condition.parameter < graph.parameters.size();
                        c.add("parameter",
                              doc.makeString(
                                  known ? std::string_view(graph.parameters[condition.parameter].name)
                                        : std::string_view()));
                        if (condition.compare != AnimationCompare::TriggerSet)
                        {
                            c.add("compare",
                                  doc.makeString(kCompareNames.at(static_cast<usize>(condition.compare))));
                            const AnimationParameterType type =
                                known ? graph.parameters[condition.parameter].type
                                      : AnimationParameterType::Float;
                            if (type == AnimationParameterType::Bool)
                            {
                                c.add("value", doc.makeBool(condition.value != 0.0f));
                            }
                            else if (type == AnimationParameterType::Int)
                            {
                                c.add("value", doc.makeI64(static_cast<i64>(condition.value)));
                            }
                            else
                            {
                                c.add("value", doc.makeF32(condition.value));
                            }
                        }
                        conditions.push(c);
                    }
                }
                transitions.push(value);
            }
        }
        layers.push(entry);
    }
    doc.setRoot(root);
    std::string text;
    if (Expected<void, JsonError> written = doc.write(text); !written)
    {
        return {};
    }
    return text;
}

} // namespace illusion
