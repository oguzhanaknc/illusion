#include <illusion/ai/BehaviorTree.hpp>
#include <illusion/core/Json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <initializer_list>
#include <limits>
#include <optional>
#include <utility>

namespace illusion
{
namespace
{

constexpr std::string_view kFormatName = "illusion.behavior";

constexpr std::array<std::string_view, 5> kTypeNames = {"Bool", "Int", "Float", "Vector3", "Entity"};
constexpr std::array<std::string_view, 8> kCompareNames = {
    "IsSet", "IsNotSet", "Equal", "NotEqual", "Greater", "Less", "GreaterEqual", "LessEqual"};
constexpr std::array<std::string_view, 3> kAbortNames = {"None", "Self", "LowerPriority"};

constexpr std::array<BehaviorNodeType, 18> kNodeTypes = {
    BehaviorNodeType::Sequence,  BehaviorNodeType::Selector,  BehaviorNodeType::Parallel,
    BehaviorNodeType::Inverter,  BehaviorNodeType::Succeeder, BehaviorNodeType::Failer,
    BehaviorNodeType::Repeat,    BehaviorNodeType::Retry,     BehaviorNodeType::Cooldown,
    BehaviorNodeType::TimeLimit, BehaviorNodeType::Guard,     BehaviorNodeType::Wait,
    BehaviorNodeType::MoveTo,    BehaviorNodeType::Wander,    BehaviorNodeType::Stop,
    BehaviorNodeType::SetValue,  BehaviorNodeType::Condition, BehaviorNodeType::Action};

std::string member(const std::string& path, std::string_view key)
{
    return path.empty() ? std::string(key) : std::format("{}.{}", path, key);
}

std::string element(const std::string& path, usize index)
{
    return std::format("{}[{}]", path, index);
}

std::optional<BehaviorNodeType> nodeType(std::string_view name) noexcept
{
    for (const BehaviorNodeType type : kNodeTypes)
    {
        if (behaviorNodeTypeName(type) == name)
        {
            return type;
        }
    }
    return std::nullopt;
}

bool compareApplies(BlackboardCompare compare, BlackboardType type) noexcept
{
    switch (compare)
    {
    case BlackboardCompare::IsSet:
    case BlackboardCompare::IsNotSet:
        return type == BlackboardType::Vector3 || type == BlackboardType::Entity;
    case BlackboardCompare::Equal:
    case BlackboardCompare::NotEqual:
        return type == BlackboardType::Bool || type == BlackboardType::Int || type == BlackboardType::Float;
    case BlackboardCompare::Greater:
    case BlackboardCompare::Less:
        return type == BlackboardType::Int || type == BlackboardType::Float;
    case BlackboardCompare::GreaterEqual:
    case BlackboardCompare::LessEqual:
        return type == BlackboardType::Int;
    }
    return false;
}

bool takesOperand(BlackboardCompare compare) noexcept
{
    return compare != BlackboardCompare::IsSet && compare != BlackboardCompare::IsNotSet;
}

class Parser
{
public:
    std::string error;
    BehaviorTree tree;

    bool fail(const std::string& path, std::string_view message)
    {
        if (error.empty())
        {
            error = path.empty() ? std::string(message) : std::format("{}: {}", path, message);
        }
        return false;
    }

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

    bool stringValue(JsonValue value, const std::string& path, std::string& out)
    {
        const std::optional<std::string_view> text = value.asString();
        if (!text)
        {
            return fail(path, std::format("expected a string, found {}", jsonTypeName(value.type())));
        }
        if (text->empty())
        {
            return fail(path, "must not be empty");
        }
        out = *text;
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
        return stringValue(value, member(path, key), out);
    }

    // A number of at least @p low (more than @p low when @p exclusive); @p out keeps its value when the key
    // is absent and not @p required.
    bool number(JsonValue parent, const std::string& path, std::string_view key, f32& out, bool required,
                f32 low, bool exclusive)
    {
        const JsonValue value = parent.find(key);
        const std::string at = member(path, key);
        if (value.isAbsent())
        {
            return !required || fail(at, "is required");
        }
        const std::optional<f64> number = value.asF64();
        if (!number)
        {
            return fail(at, std::format("expected a number, found {}", jsonTypeName(value.type())));
        }
        const auto single = static_cast<f32>(*number);
        if (!std::isfinite(single))
        {
            return fail(at, "must be a finite number");
        }
        if (exclusive ? !(single > low) : single < low)
        {
            return fail(at, std::format("must be {} {}", exclusive ? "greater than" : "at least", low));
        }
        out = single;
        return true;
    }

    bool count(JsonValue parent, const std::string& path, std::string_view key, u32& out)
    {
        const JsonValue value = parent.find(key);
        if (value.isAbsent())
        {
            return true;
        }
        const std::optional<u64> number = value.isInteger() ? value.asU64() : std::nullopt;
        if (!number || *number > std::numeric_limits<u32>::max())
        {
            return fail(member(path, key), "expected a whole number from 0 to 4294967295");
        }
        out = static_cast<u32>(*number);
        return true;
    }

    template <usize N>
    bool choice(JsonValue parent, const std::string& path, std::string_view key,
                const std::array<std::string_view, N>& names, bool required, std::string_view what, u8& out)
    {
        std::string text;
        if (!string(parent, path, key, text, required))
        {
            return false;
        }
        if (text.empty())
        {
            return true;
        }
        const auto found = std::find(names.begin(), names.end(), text);
        if (found == names.end())
        {
            std::string list;
            for (usize i = 0; i < N; ++i)
            {
                list += i == 0 ? "" : (i + 1 == N ? " or " : ", ");
                list += names.at(i);
            }
            return fail(member(path, key), std::format("unknown {} '{}' ({})", what, text, list));
        }
        out = static_cast<u8>(found - names.begin());
        return true;
    }

    // A value of a key of @p type; null (unset) only when @p nullable.
    bool value(JsonValue json, const std::string& path, BlackboardType type, bool nullable,
               BlackboardValue& out)
    {
        out = BlackboardValue{.type = type,
                              .set = true,
                              .flag = false,
                              .integer = 0,
                              .number = 0.0f,
                              .vector = Vec3(0.0f),
                              .entity = Entity{}};
        if (json.isNull() && nullable && (type == BlackboardType::Vector3 || type == BlackboardType::Entity))
        {
            out.set = false;
            return true;
        }
        switch (type)
        {
        case BlackboardType::Bool:
            if (const std::optional<bool> flag = json.asBool())
            {
                out.flag = *flag;
                return true;
            }
            return fail(path, std::format("expected true or false, found {}", jsonTypeName(json.type())));
        case BlackboardType::Int:
        {
            const std::optional<i64> integer = json.isInteger() ? json.asI64() : std::nullopt;
            if (!integer || *integer < std::numeric_limits<i32>::min() ||
                *integer > std::numeric_limits<i32>::max())
            {
                return fail(path, "expected a whole number from -2147483648 to 2147483647");
            }
            out.integer = static_cast<i32>(*integer);
            return true;
        }
        case BlackboardType::Float:
        {
            const std::optional<f64> number = json.asF64();
            if (!number)
            {
                return fail(path, std::format("expected a number, found {}", jsonTypeName(json.type())));
            }
            out.number = static_cast<f32>(*number);
            if (!std::isfinite(out.number))
            {
                return fail(path, "must be a finite number");
            }
            return true;
        }
        case BlackboardType::Vector3:
        {
            if (!json.isArray() || json.size() != 3)
            {
                return fail(path, nullable ? "expected an array of 3 numbers or null"
                                           : "expected an array of 3 numbers");
            }
            for (usize i = 0; i < 3; ++i)
            {
                const std::optional<f64> number = json[i].asF64();
                const auto single = number ? static_cast<f32>(*number) : 0.0f;
                if (!number || !std::isfinite(single))
                {
                    return fail(element(path, i), "must be a finite number");
                }
                out.vector[static_cast<glm::length_t>(i)] = single;
            }
            return true;
        }
        case BlackboardType::Entity:
            return fail(path, nullable ? "an Entity key can only be cleared (null)"
                                       : "an Entity key takes no value in a file");
        }
        return false;
    }

    bool blackboard(JsonValue root)
    {
        const JsonValue list = root.find("blackboard");
        if (list.isAbsent())
        {
            return true;
        }
        if (!list.isArray())
        {
            return fail("blackboard", std::format("expected an array, found {}", jsonTypeName(list.type())));
        }
        if (list.size() > kMaxBlackboardKeys)
        {
            return fail("blackboard", std::format("has more than {} keys", kMaxBlackboardKeys));
        }
        usize i = 0;
        for (const JsonValue entry : list.elements())
        {
            const std::string path = element("blackboard", i++);
            BlackboardKey key;
            u8 type = 0;
            if (!object(entry, path, {"name", "type", "default"}) ||
                !string(entry, path, "name", key.name, true) ||
                !choice(entry, path, "type", kTypeNames, true, "type", type))
            {
                return false;
            }
            if (tree.findKey(key.name) >= 0)
            {
                return fail(member(path, "name"), std::format("duplicate key '{}'", key.name));
            }
            key.type = static_cast<BlackboardType>(type);
            key.defaultValue = BlackboardValue{.type = key.type,
                                               .set = key.type != BlackboardType::Vector3 &&
                                                      key.type != BlackboardType::Entity,
                                               .flag = false,
                                               .integer = 0,
                                               .number = 0.0f,
                                               .vector = Vec3(0.0f),
                                               .entity = Entity{}};
            const JsonValue initial = entry.find("default");
            if (!initial.isAbsent() &&
                !value(initial, member(path, "default"), key.type, false, key.defaultValue))
            {
                return false;
            }
            tree.blackboard.push_back(std::move(key));
        }
        return true;
    }

    // The key named at @p key of @p parent, which must be one of @p types.
    bool keyReference(JsonValue parent, const std::string& path, std::string_view key, bool required,
                      std::initializer_list<BlackboardType> types, std::string_view what, i32& out)
    {
        std::string name;
        if (!string(parent, path, key, name, required))
        {
            return false;
        }
        if (name.empty())
        {
            return true;
        }
        out = tree.findKey(name);
        if (out < 0)
        {
            return fail(member(path, key), std::format("unknown key '{}'", name));
        }
        const BlackboardType type = tree.blackboard[static_cast<usize>(out)].type;
        if (types.size() != 0 && std::find(types.begin(), types.end(), type) == types.end())
        {
            return fail(member(path, key),
                        std::format("key '{}' is of type {}; {}", name, blackboardTypeName(type), what));
        }
        return true;
    }

    bool comparison(JsonValue body, const std::string& path, BehaviorNode& node)
    {
        u8 compare = 0;
        if (!keyReference(body, path, "key", true, {}, "", node.key) ||
            !choice(body, path, "compare", kCompareNames, true, "comparison", compare))
        {
            return false;
        }
        node.compare = static_cast<BlackboardCompare>(compare);
        const BlackboardKey& key = tree.blackboard[static_cast<usize>(node.key)];
        if (!compareApplies(node.compare, key.type))
        {
            return fail(member(path, "compare"),
                        std::format("{} does not apply to the {} key '{}'", kCompareNames.at(compare),
                                    blackboardTypeName(key.type), key.name));
        }
        node.value = key.defaultValue;
        const JsonValue operand = body.find("value");
        if (!takesOperand(node.compare))
        {
            return operand.isAbsent() ||
                   fail(member(path, "value"), std::format("is not used by {}", kCompareNames.at(compare)));
        }
        if (operand.isAbsent())
        {
            return fail(member(path, "value"), "is required");
        }
        return value(operand, member(path, "value"), key.type, false, node.value);
    }

    bool parseNode(JsonValue json, const std::string& path, std::optional<BehaviorNodeType> parent, u32& out)
    {
        if (!json.isObject())
        {
            return fail(path, std::format("expected a node object, found {}", jsonTypeName(json.type())));
        }
        if (tree.nodes.size() >= kMaxBehaviorNodes)
        {
            return fail(path, std::format("the tree has more than {} nodes", kMaxBehaviorNodes));
        }
        std::optional<BehaviorNodeType> type;
        std::string_view typeKey;
        for (const JsonMember& entry : json.members())
        {
            if (entry.key == "name")
            {
                continue;
            }
            const std::optional<BehaviorNodeType> found = nodeType(entry.key);
            if (!found)
            {
                return fail(path, std::format("unknown key '{}'", entry.key));
            }
            if (type)
            {
                return fail(path, std::format("has two node types ('{}' and '{}')", typeKey, entry.key));
            }
            type = found;
            typeKey = behaviorNodeTypeName(*found);
        }
        if (!type)
        {
            return fail(path, "needs a node type such as 'sequence', 'moveTo' or 'action'");
        }
        BehaviorNode node;
        node.type = *type;
        node.name = typeKey;
        if (!json.find("name").isAbsent() && !string(json, path, "name", node.name, true))
        {
            return false;
        }
        const std::string at = member(path, typeKey);
        const JsonValue body = json.find(typeKey);
        std::vector<std::pair<JsonValue, std::string>> children;
        switch (node.type)
        {
        case BehaviorNodeType::Sequence:
        case BehaviorNodeType::Selector:
            if (!body.isArray() || body.size() == 0)
            {
                return fail(at, "expected an array of at least one node");
            }
            for (usize i = 0; i < body.size(); ++i)
            {
                children.emplace_back(body[i], element(at, i));
            }
            break;
        case BehaviorNodeType::Parallel:
        {
            const JsonValue list = body.find("children");
            if (!object(body, at, {"successes", "children"}) || !count(body, at, "successes", node.count))
            {
                return false;
            }
            if (!list.isArray() || list.size() == 0)
            {
                return fail(member(at, "children"), "expected an array of at least one node");
            }
            if (node.count > list.size())
            {
                return fail(member(at, "successes"),
                            std::format("needs {} successes of {} children", node.count, list.size()));
            }
            for (usize i = 0; i < list.size(); ++i)
            {
                children.emplace_back(list[i], element(member(at, "children"), i));
            }
            break;
        }
        case BehaviorNodeType::Inverter:
        case BehaviorNodeType::Succeeder:
        case BehaviorNodeType::Failer:
            if (!object(body, at, {"child"}))
            {
                return false;
            }
            break;
        case BehaviorNodeType::Repeat:
        case BehaviorNodeType::Retry:
            if (!object(body, at, {"count", "child"}) || !count(body, at, "count", node.count))
            {
                return false;
            }
            break;
        case BehaviorNodeType::Cooldown:
        case BehaviorNodeType::TimeLimit:
            if (!object(body, at, {"seconds", "child"}) ||
                !number(body, at, "seconds", node.seconds, true, 0.0f, true))
            {
                return false;
            }
            break;
        case BehaviorNodeType::Guard:
        {
            u8 abort = 0;
            if (!object(body, at, {"key", "compare", "value", "abort", "child"}) ||
                !comparison(body, at, node) ||
                !choice(body, at, "abort", kAbortNames, false, "abort mode", abort))
            {
                return false;
            }
            node.abort = static_cast<BehaviorAbort>(abort);
            if (node.abort == BehaviorAbort::LowerPriority && parent != BehaviorNodeType::Selector)
            {
                return fail(member(at, "abort"), "LowerPriority needs a guard whose parent is a selector");
            }
            break;
        }
        case BehaviorNodeType::Wait:
            if (!object(body, at, {"seconds", "deviation"}) ||
                !number(body, at, "seconds", node.seconds, true, 0.0f, false) ||
                !number(body, at, "deviation", node.deviation, false, 0.0f, false))
            {
                return false;
            }
            if (node.deviation > node.seconds)
            {
                return fail(member(at, "deviation"), "must be at most seconds");
            }
            break;
        case BehaviorNodeType::MoveTo:
            if (!object(body, at, {"key", "acceptance"}) ||
                !keyReference(body, at, "key", true, {BlackboardType::Vector3, BlackboardType::Entity},
                              "moveTo needs a Vector3 or Entity key", node.key) ||
                !number(body, at, "acceptance", node.radius, false, 0.0f, true))
            {
                return false;
            }
            break;
        case BehaviorNodeType::Wander:
            if (!object(body, at, {"key", "radius"}) ||
                !keyReference(body, at, "key", false, {BlackboardType::Vector3}, "wander needs a Vector3 key",
                              node.key) ||
                !number(body, at, "radius", node.radius, true, 0.0f, true))
            {
                return false;
            }
            break;
        case BehaviorNodeType::Stop:
            if (!object(body, at, {}))
            {
                return false;
            }
            break;
        case BehaviorNodeType::SetValue:
        {
            if (!object(body, at, {"key", "value"}) || !keyReference(body, at, "key", true, {}, "", node.key))
            {
                return false;
            }
            const JsonValue operand = body.find("value");
            if (operand.isAbsent())
            {
                return fail(member(at, "value"), "is required");
            }
            if (!value(operand, member(at, "value"), tree.blackboard[static_cast<usize>(node.key)].type, true,
                       node.value))
            {
                return false;
            }
            break;
        }
        case BehaviorNodeType::Condition:
            if (!object(body, at, {"key", "compare", "value"}) || !comparison(body, at, node))
            {
                return false;
            }
            break;
        case BehaviorNodeType::Action:
            if (!stringValue(body, at, node.action))
            {
                return false;
            }
            break;
        }
        const bool decorator =
            node.type == BehaviorNodeType::Inverter || node.type == BehaviorNodeType::Succeeder ||
            node.type == BehaviorNodeType::Failer || node.type == BehaviorNodeType::Repeat ||
            node.type == BehaviorNodeType::Retry || node.type == BehaviorNodeType::Cooldown ||
            node.type == BehaviorNodeType::TimeLimit || node.type == BehaviorNodeType::Guard;
        if (decorator)
        {
            const JsonValue child = body.find("child");
            if (child.isAbsent())
            {
                return fail(member(at, "child"), "is required");
            }
            children.emplace_back(child, member(at, "child"));
        }

        const auto index = static_cast<u32>(tree.nodes.size());
        node.firstChild = static_cast<u32>(tree.children.size());
        node.childCount = static_cast<u32>(children.size());
        const BehaviorNodeType ownType = node.type;
        tree.nodes.push_back(std::move(node));
        tree.children.resize(tree.children.size() + children.size());
        const u32 firstChild = tree.nodes[index].firstChild;
        for (usize i = 0; i < children.size(); ++i)
        {
            u32 child = 0;
            if (!parseNode(children[i].first, children[i].second, ownType, child))
            {
                return false;
            }
            tree.children[firstChild + i] = child;
        }
        out = index;
        return true;
    }
};

JsonMutValue writeValue(JsonMutDocument& doc, const BlackboardValue& value)
{
    switch (value.type)
    {
    case BlackboardType::Bool:
        return doc.makeBool(value.flag);
    case BlackboardType::Int:
        return doc.makeI64(value.integer);
    case BlackboardType::Float:
        return doc.makeF32(value.number);
    case BlackboardType::Vector3:
    {
        if (!value.set)
        {
            return doc.makeNull();
        }
        JsonMutValue vector = doc.makeArray();
        for (glm::length_t i = 0; i < 3; ++i)
        {
            vector.push(doc.makeF32(value.vector[i]));
        }
        return vector;
    }
    case BlackboardType::Entity:
        return doc.makeNull();
    }
    return doc.makeNull();
}

class Writer
{
public:
    Writer(const BehaviorTree& tree, JsonMutDocument& doc)
        : m_tree(tree)
        , m_doc(doc)
    {
    }

    JsonMutValue writeNode(u32 index)
    {
        const BehaviorNode& node = m_tree.nodes[index];
        const std::string_view typeName = behaviorNodeTypeName(node.type);
        JsonMutValue out = m_doc.makeObject();
        if (node.name != typeName)
        {
            out.add("name", m_doc.makeString(node.name));
        }
        const auto child = [&](usize i) {
            return writeNode(m_tree.children[node.firstChild + i]);
        };
        const auto key = [&]() {
            return m_doc.makeString(m_tree.blackboard[static_cast<usize>(node.key)].name);
        };
        JsonMutValue body = m_doc.makeObject();
        switch (node.type)
        {
        case BehaviorNodeType::Sequence:
        case BehaviorNodeType::Selector:
            body = m_doc.makeArray();
            for (usize i = 0; i < node.childCount; ++i)
            {
                body.push(child(i));
            }
            break;
        case BehaviorNodeType::Parallel:
        {
            if (node.count != 0)
            {
                body.add("successes", m_doc.makeU64(node.count));
            }
            JsonMutValue list = body.addArray("children");
            for (usize i = 0; i < node.childCount; ++i)
            {
                list.push(child(i));
            }
            break;
        }
        case BehaviorNodeType::Inverter:
        case BehaviorNodeType::Succeeder:
        case BehaviorNodeType::Failer:
            break;
        case BehaviorNodeType::Repeat:
        case BehaviorNodeType::Retry:
            if (node.count != 0)
            {
                body.add("count", m_doc.makeU64(node.count));
            }
            break;
        case BehaviorNodeType::Cooldown:
        case BehaviorNodeType::TimeLimit:
            body.add("seconds", m_doc.makeF32(node.seconds));
            break;
        case BehaviorNodeType::Guard:
            comparison(body, node);
            if (node.abort != BehaviorAbort::None)
            {
                body.add("abort", m_doc.makeString(kAbortNames.at(static_cast<usize>(node.abort))));
            }
            break;
        case BehaviorNodeType::Wait:
            body.add("seconds", m_doc.makeF32(node.seconds));
            if (node.deviation != 0.0f)
            {
                body.add("deviation", m_doc.makeF32(node.deviation));
            }
            break;
        case BehaviorNodeType::MoveTo:
            body.add("key", key());
            if (node.radius != 1.0f)
            {
                body.add("acceptance", m_doc.makeF32(node.radius));
            }
            break;
        case BehaviorNodeType::Wander:
            if (node.key >= 0)
            {
                body.add("key", key());
            }
            body.add("radius", m_doc.makeF32(node.radius));
            break;
        case BehaviorNodeType::Stop:
            break;
        case BehaviorNodeType::SetValue:
            body.add("key", key());
            body.add("value", writeValue(m_doc, node.value));
            break;
        case BehaviorNodeType::Condition:
            comparison(body, node);
            break;
        case BehaviorNodeType::Action:
            body = m_doc.makeString(node.action);
            break;
        }
        if (node.childCount == 1 && node.type != BehaviorNodeType::Sequence &&
            node.type != BehaviorNodeType::Selector && node.type != BehaviorNodeType::Parallel)
        {
            body.add("child", child(0));
        }
        out.add(typeName, body);
        return out;
    }

private:
    void comparison(JsonMutValue& body, const BehaviorNode& node)
    {
        body.add("key", m_doc.makeString(m_tree.blackboard[static_cast<usize>(node.key)].name));
        body.add("compare", m_doc.makeString(kCompareNames.at(static_cast<usize>(node.compare))));
        if (takesOperand(node.compare))
        {
            body.add("value", writeValue(m_doc, node.value));
        }
    }

    const BehaviorTree& m_tree;
    JsonMutDocument& m_doc;
};

} // namespace

i32 BehaviorTree::findKey(std::string_view name) const noexcept
{
    for (usize i = 0; i < blackboard.size(); ++i)
    {
        if (blackboard[i].name == name)
        {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

Expected<BehaviorTree, std::string> parseBehaviorTree(std::string_view json)
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
            std::string("not a behaviour tree file (missing \"format\": \"illusion.behavior\")"));
    }
    const std::optional<u64> version = root.find("version").asU64();
    if (!version || *version == 0 || *version > kBehaviorTreeVersion)
    {
        return unexpected(std::format(
            "version: unsupported behaviour tree version (this build reads 1 to {})", kBehaviorTreeVersion));
    }
    Parser parser;
    if (!parser.object(root, "", {"format", "version", "blackboard", "root"}) || !parser.blackboard(root))
    {
        return unexpected(std::move(parser.error));
    }
    const JsonValue top = root.find("root");
    if (top.isAbsent())
    {
        return unexpected(std::string("root: is required"));
    }
    u32 index = 0;
    if (!parser.parseNode(top, "root", std::nullopt, index))
    {
        return unexpected(std::move(parser.error));
    }
    return std::move(parser.tree);
}

std::string writeBehaviorTree(const BehaviorTree& tree)
{
    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    root.add("format", doc.makeString(kFormatName));
    root.add("version", doc.makeU64(kBehaviorTreeVersion));
    if (!tree.blackboard.empty())
    {
        JsonMutValue keys = root.addArray("blackboard");
        for (const BlackboardKey& key : tree.blackboard)
        {
            JsonMutValue entry = doc.makeObject();
            entry.add("name", doc.makeString(key.name));
            entry.add("type", doc.makeString(blackboardTypeName(key.type)));
            const BlackboardValue& initial = key.defaultValue;
            const bool written = (key.type == BlackboardType::Bool && initial.flag) ||
                                 (key.type == BlackboardType::Int && initial.integer != 0) ||
                                 (key.type == BlackboardType::Float &&
                                  (initial.number != 0.0f || std::signbit(initial.number))) ||
                                 (key.type == BlackboardType::Vector3 && initial.set);
            if (written)
            {
                entry.add("default", writeValue(doc, initial));
            }
            keys.push(entry);
        }
    }
    if (!tree.nodes.empty())
    {
        Writer writer(tree, doc);
        root.add("root", writer.writeNode(0));
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
