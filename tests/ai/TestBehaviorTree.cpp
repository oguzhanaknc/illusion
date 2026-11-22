#include <illusion/ai/BehaviorTree.hpp>

#include <doctest/doctest.h>

#include <array>
#include <string>
#include <string_view>

using namespace illusion;

namespace
{

constexpr std::string_view kKeys = R"([{"name": "flag", "type": "Bool"}, {"name": "count", "type": "Int"},
    {"name": "speed", "type": "Float"}, {"name": "home", "type": "Vector3"}, {"name": "target", "type": "Entity"}])";

std::string document(std::string_view blackboard, std::string_view root)
{
    std::string text = R"({"format": "illusion.behavior", "version": 1, "blackboard": )";
    text += blackboard;
    text += R"(, "root": )";
    text += root;
    text += "}";
    return text;
}

std::string errorOf(std::string_view json)
{
    const Expected<BehaviorTree, std::string> tree = parseBehaviorTree(json);
    return tree ? std::string("(parsed)") : tree.error();
}

struct ErrorCase
{
    std::string_view input;
    std::string_view error;
};

} // namespace

TEST_CASE("a behaviour tree file is flattened in pre-order with contiguous children")
{
    const Expected<BehaviorTree, std::string> tree = parseBehaviorTree(document(kKeys, R"(
        {"selector": [
          {"guard": {"key": "target", "compare": "IsSet", "abort": "LowerPriority",
            "child": {"name": "chase", "sequence": [{"moveTo": {"key": "target", "acceptance": 1.5}},
                                                     {"action": "bite"}]}}},
          {"sequence": [{"wander": {"key": "home", "radius": 8}}, {"wait": {"seconds": 2, "deviation": 1}}]}
        ]})"));
    REQUIRE_MESSAGE(tree.hasValue(), (tree ? std::string() : tree.error()));
    REQUIRE(tree->nodes.size() == 8);
    CHECK(tree->blackboard.size() == 5);
    CHECK(tree->findKey("home") == 3);
    CHECK(tree->findKey("nothing") == -1);

    const std::array<BehaviorNodeType, 8> order = {BehaviorNodeType::Selector, BehaviorNodeType::Guard,
                                                   BehaviorNodeType::Sequence, BehaviorNodeType::MoveTo,
                                                   BehaviorNodeType::Action,   BehaviorNodeType::Sequence,
                                                   BehaviorNodeType::Wander,   BehaviorNodeType::Wait};
    for (usize i = 0; i < order.size(); ++i)
    {
        CHECK(tree->nodes[i].type == order.at(i));
    }
    const BehaviorNode& root = tree->nodes[0];
    REQUIRE(root.childCount == 2);
    CHECK(tree->children[root.firstChild] == 1);
    CHECK(tree->children[root.firstChild + 1] == 5);
    CHECK(tree->children[tree->nodes[2].firstChild + 1] == 4);
    CHECK(tree->nodes[0].name == "selector");
    CHECK(tree->nodes[2].name == "chase");
    CHECK(tree->nodes[1].abort == BehaviorAbort::LowerPriority);
    CHECK(tree->nodes[1].key == 4);
    CHECK(tree->nodes[3].radius == 1.5f);
    CHECK(tree->nodes[4].action == "bite");
    CHECK(tree->nodes[6].radius == 8.0f);
    CHECK(tree->nodes[7].seconds == 2.0f);
    CHECK(tree->nodes[7].deviation == 1.0f);
}

TEST_CASE("blackboard keys get their defaults and types")
{
    const Expected<BehaviorTree, std::string> tree = parseBehaviorTree(document(
        R"([{"name": "a", "type": "Bool", "default": true}, {"name": "b", "type": "Int", "default": -7},
            {"name": "c", "type": "Float", "default": 2.5}, {"name": "d", "type": "Vector3", "default": [1, 2, 3]},
            {"name": "e", "type": "Vector3"}, {"name": "f", "type": "Entity"}, {"name": "g", "type": "Int"}])",
        R"({"stop": {}})"));
    REQUIRE_MESSAGE(tree.hasValue(), (tree ? std::string() : tree.error()));
    const std::vector<BlackboardKey>& keys = tree->blackboard;
    REQUIRE(keys.size() == 7);
    CHECK(keys[0].defaultValue.flag);
    CHECK(keys[0].defaultValue.set);
    CHECK(keys[1].defaultValue.integer == -7);
    CHECK(keys[2].defaultValue.number == 2.5f);
    CHECK(keys[3].defaultValue.set);
    CHECK(keys[3].defaultValue.vector == Vec3(1.0f, 2.0f, 3.0f));
    CHECK_FALSE(keys[4].defaultValue.set);
    CHECK_FALSE(keys[5].defaultValue.set);
    CHECK(keys[5].defaultValue.type == BlackboardType::Entity);
    CHECK(keys[6].defaultValue.set);
    CHECK(keys[6].defaultValue.integer == 0);
}

TEST_CASE("a behaviour tree is written back unchanged")
{
    const std::string source = document(
        R"([{"name": "flag", "type": "Bool", "default": true}, {"name": "count", "type": "Int", "default": 3},
            {"name": "speed", "type": "Float", "default": -0.5}, {"name": "home", "type": "Vector3",
            "default": [1.5, 0, -2]}, {"name": "target", "type": "Entity"}, {"name": "spot", "type": "Vector3"}])",
        R"({"name": "brain", "parallel": {"successes": 1, "children": [
          {"selector": [
            {"guard": {"key": "target", "compare": "IsSet", "abort": "LowerPriority",
              "child": {"timeLimit": {"seconds": 4, "child": {"moveTo": {"key": "target", "acceptance": 2}}}}}},
            {"guard": {"key": "count", "compare": "GreaterEqual", "value": 2, "abort": "Self",
              "child": {"cooldown": {"seconds": 1.5, "child": {"action": "howl"}}}}},
            {"retry": {"count": 2, "child": {"wander": {"radius": 6}}}},
            {"repeat": {"count": 3, "child": {"wander": {"key": "home", "radius": 4}}}}
          ]},
          {"sequence": [
            {"condition": {"key": "speed", "compare": "Less", "value": 2.25}},
            {"condition": {"key": "flag", "compare": "NotEqual", "value": false}},
            {"inverter": {"child": {"condition": {"key": "spot", "compare": "IsNotSet"}}}},
            {"succeeder": {"child": {"failer": {"child": {"stop": {}}}}}},
            {"setValue": {"key": "spot", "value": [4, 5, 6]}},
            {"setValue": {"key": "target", "value": null}},
            {"setValue": {"key": "count", "value": -4}},
            {"name": "pause", "wait": {"seconds": 0.75, "deviation": 0.25}},
            {"repeat": {"child": {"wait": {"seconds": 0}}}}
          ]}
        ]}})");
    const Expected<BehaviorTree, std::string> first = parseBehaviorTree(source);
    REQUIRE_MESSAGE(first.hasValue(), (first ? std::string() : first.error()));
    const std::string written = writeBehaviorTree(*first);
    const Expected<BehaviorTree, std::string> second = parseBehaviorTree(written);
    REQUIRE_MESSAGE(second.hasValue(), (second ? std::string() : second.error()));
    CHECK(writeBehaviorTree(*second) == written);
    REQUIRE(second->nodes.size() == first->nodes.size());
    CHECK(second->children == first->children);
    for (usize i = 0; i < first->nodes.size(); ++i)
    {
        const BehaviorNode& a = first->nodes[i];
        const BehaviorNode& b = second->nodes[i];
        CHECK(a.type == b.type);
        CHECK(a.name == b.name);
        CHECK(a.key == b.key);
        CHECK(a.compare == b.compare);
        CHECK(a.abort == b.abort);
        CHECK(a.seconds == b.seconds);
        CHECK(a.deviation == b.deviation);
        CHECK(a.radius == b.radius);
        CHECK(a.count == b.count);
        CHECK(a.action == b.action);
        CHECK(a.value.set == b.value.set);
        CHECK(a.value.flag == b.value.flag);
        CHECK(a.value.integer == b.value.integer);
        CHECK(a.value.number == b.value.number);
        CHECK(a.value.vector == b.value.vector);
    }
    for (usize i = 0; i < first->blackboard.size(); ++i)
    {
        CHECK(first->blackboard[i].name == second->blackboard[i].name);
        CHECK(first->blackboard[i].defaultValue.vector == second->blackboard[i].defaultValue.vector);
        CHECK(first->blackboard[i].defaultValue.number == second->blackboard[i].defaultValue.number);
    }
    CHECK(first->nodes[0].name == "brain");
    CHECK(first->nodes[0].count == 1);
}

TEST_CASE("a behaviour tree file that is not one says so")
{
    CHECK(errorOf("{").starts_with("invalid JSON at line 1"));
    CHECK(errorOf(R"({"version": 1, "root": {"stop": {}}})") ==
          "not a behaviour tree file (missing \"format\": \"illusion.behavior\")");
    CHECK(errorOf(R"({"format": "illusion.behavior", "version": 2, "root": {"stop": {}}})") ==
          "version: unsupported behaviour tree version (this build reads 1 to 1)");
    CHECK(errorOf(R"({"format": "illusion.behavior", "root": {"stop": {}}})") ==
          "version: unsupported behaviour tree version (this build reads 1 to 1)");
    CHECK(errorOf(R"({"format": "illusion.behavior", "version": 1, "roots": {}})") == "unknown key 'roots'");
    CHECK(errorOf(R"({"format": "illusion.behavior", "version": 1})") == "root: is required");
}

TEST_CASE("every error of a blackboard names its JSON path")
{
    std::string many = "[";
    for (int i = 0; i < 33; ++i)
    {
        many += (i == 0 ? "" : ", ") + std::string(R"({"name": "k)") + std::to_string(i) +
                R"(", "type": "Bool"})";
    }
    many += "]";
    CHECK(errorOf(document(many, R"({"stop": {}})")) == "blackboard: has more than 32 keys");

    const std::array<ErrorCase, 13> cases = {{
        {R"({})", "blackboard: expected an array, found object"},
        {R"([3])", "blackboard[0]: expected an object, found integer"},
        {R"([{"name": "a", "type": "Bool", "kind": 1}])", "blackboard[0]: unknown key 'kind'"},
        {R"([{"type": "Bool"}])", "blackboard[0].name: is required"},
        {R"([{"name": "", "type": "Bool"}])", "blackboard[0].name: must not be empty"},
        {R"([{"name": "a", "type": "Bool"}, {"name": "a", "type": "Int"}])",
         "blackboard[1].name: duplicate key 'a'"},
        {R"([{"name": "a"}])", "blackboard[0].type: is required"},
        {R"([{"name": "a", "type": "Text"}])",
         "blackboard[0].type: unknown type 'Text' (Bool, Int, Float, Vector3 or Entity)"},
        {R"([{"name": "a", "type": "Bool", "default": 1}])",
         "blackboard[0].default: expected true or false, found integer"},
        {R"([{"name": "a", "type": "Int", "default": 1.5}])",
         "blackboard[0].default: expected a whole number from -2147483648 to 2147483647"},
        {R"([{"name": "a", "type": "Float", "default": 1e39}])",
         "blackboard[0].default: must be a finite number"},
        {R"([{"name": "a", "type": "Vector3", "default": [1, "x", 3]}])",
         "blackboard[0].default[1]: must be a finite number"},
        {R"([{"name": "a", "type": "Entity", "default": null}])",
         "blackboard[0].default: an Entity key takes no value in a file"},
    }};
    for (const ErrorCase& c : cases)
    {
        CAPTURE(c.input);
        CHECK(errorOf(document(c.input, R"({"stop": {}})")) == c.error);
    }
    CHECK(errorOf(document(R"([{"name": "a", "type": "Int", "default": 3000000000}])", R"({"stop": {}})")) ==
          "blackboard[0].default: expected a whole number from -2147483648 to 2147483647");
    CHECK(errorOf(document(R"([{"name": "a", "type": "Vector3", "default": [1, 2]}])", R"({"stop": {}})")) ==
          "blackboard[0].default: expected an array of 3 numbers");
}

TEST_CASE("every error of a node names its JSON path")
{
    const std::array<ErrorCase, 47> cases = {{
        {R"([])", "root: expected a node object, found array"},
        {R"({})", "root: needs a node type such as 'sequence', 'moveTo' or 'action'"},
        {R"({"sequnce": []})", "root: unknown key 'sequnce'"},
        {R"({"stop": {}, "action": "x"})", "root: has two node types ('stop' and 'action')"},
        {R"({"name": "", "stop": {}})", "root.name: must not be empty"},
        {R"({"name": 3, "stop": {}})", "root.name: expected a string, found integer"},
        {R"({"sequence": []})", "root.sequence: expected an array of at least one node"},
        {R"({"selector": {"stop": {}}})", "root.selector: expected an array of at least one node"},
        {R"({"selector": [{"stop": {}}, {"wait": {}}]})", "root.selector[1].wait.seconds: is required"},
        {R"({"sequence": [{"stop": {}}, 4]})", "root.sequence[1]: expected a node object, found integer"},
        {R"({"parallel": {"successes": 1}})",
         "root.parallel.children: expected an array of at least one node"},
        {R"({"parallel": {"successes": 3, "children": [{"stop": {}}, {"stop": {}}]}})",
         "root.parallel.successes: needs 3 successes of 2 children"},
        {R"({"parallel": {"needed": 1, "children": [{"stop": {}}]}})", "root.parallel: unknown key 'needed'"},
        {R"({"parallel": {"children": [{"stop": 1}]}})",
         "root.parallel.children[0].stop: expected an object, found integer"},
        {R"({"inverter": {}})", "root.inverter.child: is required"},
        {R"({"succeeder": {"count": 1, "child": {"stop": {}}}})", "root.succeeder: unknown key 'count'"},
        {R"({"failer": {"child": {"stp": {}}}})", "root.failer.child: unknown key 'stp'"},
        {R"({"repeat": {"count": -1, "child": {"stop": {}}}})",
         "root.repeat.count: expected a whole number from 0 to 4294967295"},
        {R"({"retry": {"count": 1.5, "child": {"stop": {}}}})",
         "root.retry.count: expected a whole number from 0 to 4294967295"},
        {R"({"cooldown": {"seconds": 0, "child": {"stop": {}}}})",
         "root.cooldown.seconds: must be greater than 0"},
        {R"({"timeLimit": {"child": {"stop": {}}}})", "root.timeLimit.seconds: is required"},
        {R"({"selector": [{"guard": {"key": "targt", "compare": "IsSet", "child": {"stop": {}}}}]})",
         "root.selector[0].guard.key: unknown key 'targt'"},
        {R"({"guard": {"compare": "IsSet", "child": {"stop": {}}}})", "root.guard.key: is required"},
        {R"({"guard": {"key": "target", "compare": "Same", "child": {"stop": {}}}})",
         "root.guard.compare: unknown comparison 'Same' (IsSet, IsNotSet, Equal, NotEqual, Greater, Less, "
         "GreaterEqual or LessEqual)"},
        {R"({"guard": {"key": "target", "child": {"stop": {}}}})", "root.guard.compare: is required"},
        {R"({"guard": {"key": "count", "compare": "IsSet", "child": {"stop": {}}}})",
         "root.guard.compare: IsSet does not apply to the Int key 'count'"},
        {R"({"condition": {"key": "flag", "compare": "Greater", "value": true}})",
         "root.condition.compare: Greater does not apply to the Bool key 'flag'"},
        {R"({"condition": {"key": "speed", "compare": "GreaterEqual", "value": 1}})",
         "root.condition.compare: GreaterEqual does not apply to the Float key 'speed'"},
        {R"({"condition": {"key": "home", "compare": "Equal", "value": [0, 0, 0]}})",
         "root.condition.compare: Equal does not apply to the Vector3 key 'home'"},
        {R"({"condition": {"key": "count", "compare": "Equal"}})", "root.condition.value: is required"},
        {R"({"condition": {"key": "home", "compare": "IsSet", "value": 1}})",
         "root.condition.value: is not used by IsSet"},
        {R"({"condition": {"key": "flag", "compare": "Equal", "value": "yes"}})",
         "root.condition.value: expected true or false, found string"},
        {R"({"condition": {"key": "count", "compare": "Less", "value": 2, "abort": "Self"}})",
         "root.condition: unknown key 'abort'"},
        {R"({"selector": [{"guard": {"key": "home", "compare": "IsSet", "abort": "Always", "child": {"stop": {}}}}]})",
         "root.selector[0].guard.abort: unknown abort mode 'Always' (None, Self or LowerPriority)"},
        {R"({"sequence": [{"guard": {"key": "home", "compare": "IsSet", "abort": "LowerPriority",
             "child": {"stop": {}}}}]})",
         "root.sequence[0].guard.abort: LowerPriority needs a guard whose parent is a selector"},
        {R"({"wait": {"seconds": -1}})", "root.wait.seconds: must be at least 0"},
        {R"({"wait": {"seconds": "2"}})", "root.wait.seconds: expected a number, found string"},
        {R"({"wait": {"seconds": 1, "deviation": 2}})", "root.wait.deviation: must be at most seconds"},
        {R"({"moveTo": {}})", "root.moveTo.key: is required"},
        {R"({"moveTo": {"key": "flag"}})",
         "root.moveTo.key: key 'flag' is of type Bool; moveTo needs a Vector3 or Entity key"},
        {R"({"moveTo": {"key": "home", "acceptance": 0}})", "root.moveTo.acceptance: must be greater than 0"},
        {R"({"wander": {"key": "target", "radius": 3}})",
         "root.wander.key: key 'target' is of type Entity; wander needs a Vector3 key"},
        {R"({"wander": {"key": "home"}})", "root.wander.radius: is required"},
        {R"({"stop": {"now": true}})", "root.stop: unknown key 'now'"},
        {R"({"setValue": {"key": "flag"}})", "root.setValue.value: is required"},
        {R"({"action": ""})", "root.action: must not be empty"},
        {R"({"action": 7})", "root.action: expected a string, found integer"},
    }};
    for (const ErrorCase& c : cases)
    {
        CAPTURE(c.input);
        CHECK(errorOf(document(kKeys, c.input)) == c.error);
    }
}

TEST_CASE("setValue checks its value against the key's type")
{
    const std::array<ErrorCase, 5> cases = {{
        {R"({"setValue": {"key": "target", "value": 3}})",
         "root.setValue.value: an Entity key can only be cleared (null)"},
        {R"({"setValue": {"key": "flag", "value": null}})",
         "root.setValue.value: expected true or false, found null"},
        {R"({"setValue": {"key": "home", "value": [1, 2]}})",
         "root.setValue.value: expected an array of 3 numbers or null"},
        {R"({"setValue": {"key": "count", "value": 2.5}})",
         "root.setValue.value: expected a whole number from -2147483648 to 2147483647"},
        {R"({"setValue": {"key": "nothing", "value": 1}})", "root.setValue.key: unknown key 'nothing'"},
    }};
    for (const ErrorCase& c : cases)
    {
        CAPTURE(c.input);
        CHECK(errorOf(document(kKeys, c.input)) == c.error);
    }
    const Expected<BehaviorTree, std::string> cleared =
        parseBehaviorTree(document(kKeys, R"({"setValue": {"key": "home", "value": null}})"));
    REQUIRE(cleared.hasValue());
    CHECK_FALSE(cleared->nodes[0].value.set);
    CHECK(cleared->nodes[0].value.type == BlackboardType::Vector3);
}

TEST_CASE("a tree of more than 1024 nodes is rejected")
{
    std::string children;
    for (u32 i = 0; i < kMaxBehaviorNodes - 1; ++i)
    {
        children += i == 0 ? R"({"stop": {}})" : R"(, {"stop": {}})";
    }
    CHECK(parseBehaviorTree(document("[]", "{\"sequence\": [" + children + "]}")).hasValue());
    CHECK(errorOf(document("[]", "{\"sequence\": [" + children + R"(, {"stop": {}}]})")) ==
          "root.sequence[1023]: the tree has more than 1024 nodes");
}

TEST_CASE("node type names are the keys of the file")
{
    CHECK(behaviorNodeTypeName(BehaviorNodeType::TimeLimit) == "timeLimit");
    CHECK(behaviorNodeTypeName(BehaviorNodeType::SetValue) == "setValue");
    CHECK(behaviorNodeTypeName(BehaviorNodeType::Action) == "action");
}
