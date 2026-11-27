// The example graphs of the docs: character.animgraph in the manual page "Animation"
// (docs/content/{en,tr}/manual/animation.md) and hero.animgraph in the tutorial "Animate a character"
// (docs/content/{en,tr}/tutorials/animated-character.md).

#include <illusion/animation/AnimationGraph.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

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
    const std::string text = buffer.str();
    std::string normalized;
    for (const char c : text)
    {
        if (c != '\r')
        {
            normalized += c;
        }
    }
    return normalized;
}

std::string exampleText(const char* name = "character")
{
    return readText(std::string(ILLUSION_TEST_DATA_DIR) + "/animation/" + name + ".animgraph");
}

} // namespace

TEST_CASE("the example graph of the manual parses and has the layers it describes")
{
    const std::string text = exampleText();
    Expected<AnimationGraph, std::string> graph = parseAnimationGraph(text);
    INFO((graph ? std::string() : graph.error()));
    REQUIRE(graph.hasValue());
    CHECK(graph->rootMotion == "hips");
    REQUIRE(graph->layers.size() == 3);
    CHECK(graph->layers[0].states.size() == 3);
    CHECK(graph->layers[0].states[1].motion.type == AnimationMotionType::Blend2D);
    CHECK(!graph->layers[0].states[1].motion.triangles.empty());
    CHECK(graph->layers[1].mask.size() == 1);
    CHECK(graph->layers[2].blend == AnimationLayerBlend::Additive);
    CHECK(graph->findParameter("jump") >= 0);
}

TEST_CASE("the manual pages in English and Turkish show the example graph unchanged")
{
    const std::string text = exampleText();
    for (const char* language : {"en", "tr"})
    {
        INFO(language);
        const std::string page =
            readText(std::string(ILLUSION_DOCS_DIR) + "/" + language + "/manual/animation.md");
        CHECK(page.find("```json\n" + text + "```") != std::string::npos);
    }
}

TEST_CASE("the tutorial graph parses and the tutorial pages show it unchanged")
{
    const std::string text = exampleText("hero");
    Expected<AnimationGraph, std::string> graph = parseAnimationGraph(text);
    INFO((graph ? std::string() : graph.error()));
    REQUIRE(graph.hasValue());
    REQUIRE(graph->layers.size() == 2);
    CHECK(graph->findParameter("speed") == 0);
    CHECK(graph->findParameter("jump") == 1);
    CHECK(graph->findParameter("aimYaw") == 2);
    CHECK(graph->findParameter("aimPitch") == 3);
    const AnimationLayer& aim = graph->layers[1];
    CHECK(aim.blend == AnimationLayerBlend::Additive);
    CHECK(aim.space == AnimationAdditiveSpace::Model);
    CHECK(aim.reference.path() == "assets:/models/hero.glb#animation/aim_center");
    REQUIRE(aim.states.size() == 1);
    CHECK(aim.states[0].motion.type == AnimationMotionType::Blend2D);
    CHECK(aim.states[0].motion.points.size() == 9);
    for (const char* language : {"en", "tr"})
    {
        INFO(language);
        const std::string page =
            readText(std::string(ILLUSION_DOCS_DIR) + "/" + language + "/tutorials/animated-character.md");
        CHECK(page.find("```json\n" + text + "```") != std::string::npos);
    }
}
