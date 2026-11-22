// The code of the tutorials: tests/CMakeLists.txt builds the C++ blocks of "Tetris by Hand"
// (docs/content/en/tutorials/tetris-by-hand.md) as the module illusion_tutorial_tetris_module and those of
// "A HUD and a pause menu" (hud-and-pause-menu.md) as illusion_tutorial_hud_module and those of
// "Animate a character" (animated-character.md) as illusion_tutorial_animation_module and those of
// "A creature that wanders and chases" (creature-ai.md) as illusion_tutorial_creature_module.

#include "TestSupport.hpp"

#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/modules/ModuleHost.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <string_view>

using namespace illusion;

TEST_CASE("the module of the Tetris by Hand tutorial loads, reflects its Components and updates")
{
    test::TempDirectory directory;
    JobSystem jobs{JobSystemDesc{2, 256}};
    ModuleHostDesc desc;
    desc.shadowDirectory = directory.path / "shadow";
    ModuleHost modules(jobs, desc);
    const std::optional<std::string> loadError = modules.load(utf8Path(TUTORIAL_TETRIS_MODULE));
    REQUIRE_MESSAGE(!loadError, loadError.value_or(""));

    World world;
    modules.attachWorld(world, WorldRole::Simulation);
    for (const std::string_view name : {"tetris.Board", "tetris.Cell", "tetris.PieceQueue", "tetris.Score"})
    {
        INFO(name);
        CHECK(world.registry().findByStableName(name) != kInvalidComponentId);
    }
    // Without an input service the tutorial's update returns at once; it must not fail either way.
    for (int frame = 0; frame < 3; ++frame)
    {
        modules.update(world, 1.0 / 60.0);
    }
    CHECK(modules.detachWorld(world));
}

TEST_CASE("the module of the HUD and pause menu tutorial loads and updates a World without a UI")
{
    test::TempDirectory directory;
    JobSystem jobs{JobSystemDesc{2, 256}};
    ModuleHostDesc desc;
    desc.shadowDirectory = directory.path / "shadow";
    ModuleHost modules(jobs, desc);
    const std::optional<std::string> loadError = modules.load(utf8Path(TUTORIAL_HUD_MODULE));
    REQUIRE_MESSAGE(!loadError, loadError.value_or(""));

    World world;
    modules.attachWorld(world, WorldRole::Simulation);
    // Without a UiService the module creates nothing and its update returns at once.
    for (int frame = 0; frame < 3; ++frame)
    {
        modules.update(world, 1.0 / 60.0);
    }
    CHECK(modules.detachWorld(world));
}

TEST_CASE("the module of the Animate a character tutorial loads and updates a World without services")
{
    test::TempDirectory directory;
    JobSystem jobs{JobSystemDesc{2, 256}};
    ModuleHostDesc desc;
    desc.shadowDirectory = directory.path / "shadow";
    ModuleHost modules(jobs, desc);
    const std::optional<std::string> loadError = modules.load(utf8Path(TUTORIAL_ANIMATION_MODULE));
    REQUIRE_MESSAGE(!loadError, loadError.value_or(""));

    World world;
    modules.attachWorld(world, WorldRole::Simulation);
    for (int frame = 0; frame < 3; ++frame)
    {
        modules.update(world, 1.0 / 60.0);
    }
    CHECK(modules.detachWorld(world));
}

TEST_CASE(
    "the module of the creature tutorial loads, reflects its state and updates a World without services")
{
    test::TempDirectory directory;
    JobSystem jobs{JobSystemDesc{2, 256}};
    ModuleHostDesc desc;
    desc.shadowDirectory = directory.path / "shadow";
    ModuleHost modules(jobs, desc);
    const std::optional<std::string> loadError = modules.load(utf8Path(TUTORIAL_CREATURE_MODULE));
    REQUIRE_MESSAGE(!loadError, loadError.value_or(""));

    World world;
    modules.attachWorld(world, WorldRole::Simulation);
    CHECK(world.registry().findByStableName("creature.State") != kInvalidComponentId);
    for (int frame = 0; frame < 3; ++frame)
    {
        modules.update(world, 1.0 / 60.0);
    }
    CHECK(modules.detachWorld(world));
}
