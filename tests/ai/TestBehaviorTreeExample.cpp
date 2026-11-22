// The example trees of the docs: watchman.behavior in the manual page "AI"
// (docs/content/{en,tr}/manual/ai.md) and creature.behavior in the tutorial "A creature that wanders and
// chases" (docs/content/{en,tr}/tutorials/creature-ai.md).

#include <illusion/ai/BehaviorTree.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

#include <array>
#include <fstream>
#include <sstream>
#include <string>

using namespace illusion;

namespace
{

std::string readText(const std::string& path)
{
    std::ifstream file(utf8Path(path), std::ios::binary);
    REQUIRE_MESSAGE(file.is_open(), path);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string normalized;
    for (const char c : buffer.str())
    {
        if (c != '\r')
        {
            normalized += c;
        }
    }
    return normalized;
}

std::string exampleText(const char* name)
{
    return readText(std::string(ILLUSION_TEST_DATA_DIR) + "/ai/" + name + ".behavior");
}

std::string pageText(const char* language, const char* page)
{
    return readText(std::string(ILLUSION_DOCS_DIR) + "/" + language + "/" + page);
}

} // namespace

TEST_CASE("the example tree of the manual parses and uses every node type and every key type")
{
    const std::string text = exampleText("watchman");
    Expected<BehaviorTree, std::string> tree = parseBehaviorTree(text);
    INFO((tree ? std::string() : tree.error()));
    REQUIRE(tree.hasValue());

    std::array<bool, static_cast<usize>(BehaviorNodeType::Action) + 1> used{};
    for (const BehaviorNode& node : tree->nodes)
    {
        used[static_cast<usize>(node.type)] = true;
    }
    for (usize type = 0; type < used.size(); ++type)
    {
        INFO(behaviorNodeTypeName(static_cast<BehaviorNodeType>(type)));
        CHECK(used[type]);
    }
    std::array<bool, 5> keyTypes{};
    for (const BlackboardKey& key : tree->blackboard)
    {
        keyTypes[static_cast<usize>(key.type)] = true;
    }
    for (const bool seen : keyTypes)
    {
        CHECK(seen);
    }
    CHECK(tree->findKey("ammo") >= 0);
    CHECK(tree->nodes[0].name == "watchman");
}

TEST_CASE("the manual pages in English and Turkish show the example tree unchanged")
{
    const std::string text = exampleText("watchman");
    for (const char* language : {"en", "tr"})
    {
        INFO(language);
        CHECK(pageText(language, "manual/ai.md").find("```json\n" + text + "```") != std::string::npos);
    }
}

TEST_CASE("the tutorial tree parses and the tutorial pages show it unchanged")
{
    const std::string text = exampleText("creature");
    Expected<BehaviorTree, std::string> tree = parseBehaviorTree(text);
    INFO((tree ? std::string() : tree.error()));
    REQUIRE(tree.hasValue());
    CHECK(tree->blackboard.size() == 2);
    CHECK(tree->findKey("target") == 0);
    CHECK(tree->findKey("lastSeen") == 1);
    CHECK(tree->nodes[0].type == BehaviorNodeType::Selector);
    for (const char* language : {"en", "tr"})
    {
        INFO(language);
        CHECK(pageText(language, "tutorials/creature-ai.md").find("```json\n" + text + "```") !=
              std::string::npos);
    }
}
