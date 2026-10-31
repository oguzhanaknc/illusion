// The Tetris reference module (tests/acceptance/tetris/module/Tetris.cpp) played through the editor's
// commands, as an agent plays it: input actions from an .input asset, input.inject and play.step.

#include "core/EditorCoreTestSupport.hpp"

#include <illusion/core/JobSystem.hpp>
#include <illusion/editor/play/PlaySession.hpp>

#include <set>

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::test;

namespace
{

// A project with the Tetris actions and module, a Board, a PieceQueue and a Score.
struct TetrisFixture
{
    explicit TetrisFixture(std::string_view forced = "", u32 seed = 1)
    {
        play = std::make_unique<PlaySession>(
            *base.editor, PlaySessionDesc{.jobs = &jobs, .audioOutput = AudioOutput::Offline});
        constexpr std::array<std::array<std::string_view, 3>, 5> kActions{{
            {"tetris.move_left", "Key.Left", "Gamepad.DPadLeft"},
            {"tetris.move_right", "Key.Right", "Gamepad.DPadRight"},
            {"tetris.rotate_cw", "Key.Up", "Gamepad.South"},
            {"tetris.soft_drop", "Key.Down", "Gamepad.DPadDown"},
            {"tetris.hard_drop", "Key.Space", "Gamepad.North"},
        }};
        for (const auto& [name, key, button] : kActions)
        {
            ok(editor(), "input.action_define",
               std::format(R"({{"asset":"assets:/input/tetris.input","name":"{}","type":"Button",)"
                           R"("bindings":["{}","{}"]}})",
                           name, key, button));
        }
        ok(editor(), "project.settings_set",
           R"({"type":"illusion.InputSettings","value":{"actions":"assets:/input/tetris.input"}})");
        const JsonDocument loaded =
            ok(editor(), "module.load", std::format(R"({{"path":"{}"}})", ILLUSION_TETRIS_MODULE));
        CHECK(loaded.root().pointer("/modules/0/name").asString() == "tetris");
        ok(editor(), "entity.create",
           R"({"name":"Board","components":{"illusion.Transform":{},"tetris.Board":{}}})");
        ok(editor(), "entity.create",
           std::format(
               R"({{"name":"Queue","components":{{"tetris.PieceQueue":{{"seed":{},"forced":"{}"}}}}}})", seed,
               forced));
        ok(editor(), "entity.create", R"({"name":"Score","components":{"tetris.Score":{}}})");
        ok(editor(), "play.start", R"({"fixedStep":true})");
    }

    ~TetrisFixture()
    {
        play->stop();
        (void)play->unloadModule("tetris");
    }

    TetrisFixture(const TetrisFixture&) = delete;
    TetrisFixture& operator=(const TetrisFixture&) = delete;

    EditorContext& editor() { return *base.editor; }

    void step(u32 steps = 1) { ok(editor(), "play.step", std::format(R"({{"steps":{}}})", steps)); }

    // Presses and releases @p key, one update each.
    void press(std::string_view key)
    {
        ok(editor(), "input.inject", std::format(R"({{"keys":[{{"key":"{}","down":true}}]}})", key));
        step();
        ok(editor(), "input.inject", std::format(R"({{"keys":[{{"key":"{}","down":false}}]}})", key));
        step();
    }

    // The entities with @p type in the play World.
    std::vector<std::string> ids(std::string_view type)
    {
        const JsonDocument found =
            ok(editor(), "scene.query", std::format(R"({{"has":["{}"],"limit":1000}})", type));
        std::vector<std::string> out;
        for (usize i = 0; i < found.root().find("entities").size(); ++i)
        {
            out.emplace_back(found.root().find("entities")[i].find("id").asString().value_or(""));
        }
        return out;
    }

    // The value of @p type on @p entity (an id or a path).
    JsonDocument value(std::string_view entity, std::string_view type)
    {
        return ok(editor(), "component.get", std::format(R"({{"entity":"{}","type":"{}"}})", entity, type));
    }

    // The falling piece: kind, rotation, x, y.
    struct Piece
    {
        std::string kind;
        i64 rotation = -1;
        i64 x = -100;
        i64 y = -100;
    };

    Piece active()
    {
        const std::vector<std::string> found = ids("tetris.ActivePiece");
        REQUIRE(found.size() == 1);
        const JsonDocument piece = value(found[0], "tetris.ActivePiece");
        const JsonValue v = piece.root().find("value");
        return Piece{std::string(v.find("kind").asString().value_or("")),
                     v.find("rotation").asI64().value_or(-1), v.find("x").asI64().value_or(-100),
                     v.find("y").asI64().value_or(-100)};
    }

    i64 score(std::string_view field)
    {
        const JsonDocument score = value("/Score", "tetris.Score");
        const JsonValue member = score.root().find("value").find(field);
        return member.asBool() ? (*member.asBool() ? 1 : 0) : member.asI64().value_or(-1);
    }

    // A locked Cell at (x, y) in the play World.
    void addCell(i32 x, i32 y)
    {
        ok(editor(), "entity.create",
           std::format(R"({{"parent":"/Board","components":{{"tetris.Cell":{{"x":{},"y":{},"kind":"O"}}}}}})",
                       x, y));
    }

    EditorFixture base;
    JobSystem jobs{JobSystemDesc{2, 1024}};
    std::unique_ptr<PlaySession> play;
};

} // namespace

TEST_CASE("a Tetris piece spawns at x 3 with its box's top row at the top, and walls stop it")
{
    TetrisFixture f("TI");
    f.step();
    TetrisFixture::Piece piece = f.active();
    CHECK(piece.kind == "T");
    CHECK(piece.x == 3);
    CHECK(piece.y == 19);
    CHECK(f.ids("tetris.ActiveBlock").size() == 4);
    for (int i = 0; i < 6; ++i)
    {
        f.press("Left");
    }
    CHECK(f.active().x == 0);
    for (int i = 0; i < 10; ++i)
    {
        f.press("Right");
    }
    CHECK(f.active().x == 7); // the T's box is three wide
}

TEST_CASE("Tetris rotation follows SRS with wall kicks")
{
    TetrisFixture f("I");
    f.step();
    f.press("Up");
    TetrisFixture::Piece piece = f.active();
    CHECK(piece.rotation == 1);
    CHECK(piece.x == 3);
    // Vertical against the left wall: the box column 2 is the I, so x reaches -2.
    for (int i = 0; i < 6; ++i)
    {
        f.press("Left");
    }
    CHECK(f.active().x == -2);
    // Back to rotation 2 would leave the board; the kick (+2, 0)... of 1 -> 2 moves it in.
    f.press("Up");
    piece = f.active();
    CHECK(piece.rotation == 2);
    CHECK(piece.x >= 0);
}

TEST_CASE("Tetris soft drop moves one row, hard drop locks four Cells and the next piece spawns")
{
    TetrisFixture f("OT");
    f.step();
    const i64 top = f.active().y;
    f.press("Down");
    CHECK(f.active().y == top - 1);
    f.press("Space");
    CHECK(f.ids("tetris.Cell").size() == 4);
    const JsonDocument cell = f.value(f.ids("tetris.Cell")[0], "tetris.Cell");
    CHECK(cell.root().pointer("/value/kind").asString() == "O");
    CHECK(cell.root().pointer("/value/y").asI64().value_or(-1) <= 1);
    CHECK(f.active().kind == "T");
}

TEST_CASE("Tetris clears full rows, moves the rows above down and scores 100 times the level")
{
    TetrisFixture f("I");
    for (const i32 x : {0, 1, 2, 7, 8, 9})
    {
        f.addCell(x, 0);
    }
    f.addCell(0, 1);
    f.step();
    f.press("Space");
    CHECK(f.score("lines") == 1);
    CHECK(f.score("points") == 100);
    CHECK(f.score("level") == 1);
    // The Cell of row 1 is now on row 0.
    const std::vector<std::string> cells = f.ids("tetris.Cell");
    REQUIRE(cells.size() == 1);
    CHECK(f.value(cells[0], "tetris.Cell").root().pointer("/value/y").asI64() == 0);
}

TEST_CASE("Tetris gravity moves the piece one row per gravitySeconds")
{
    TetrisFixture f("O");
    f.step();
    const i64 top = f.active().y;
    f.step(40); // 41 steps of 1/60 s: under 0.8 s
    CHECK(f.active().y == top);
    f.step(20);
    CHECK(f.active().y == top - 1);
}

TEST_CASE("Tetris ends when a new piece overlaps a Cell")
{
    TetrisFixture f("O");
    f.addCell(4, 19);
    f.step();
    CHECK(f.score("gameOver") == 1);
    CHECK(f.ids("tetris.ActivePiece").empty());
    f.step(5);
    CHECK(f.ids("tetris.ActivePiece").empty());
}

TEST_CASE("Tetris deals the forced pieces, then 7-bags that repeat for the same seed")
{
    const auto deal = [](u32 seed) {
        TetrisFixture f("", seed);
        std::vector<std::string> kinds;
        for (int i = 0; i < 7; ++i)
        {
            f.step();
            kinds.push_back(f.active().kind);
            for (int j = 0; j < 3 * (i % 3); ++j)
            {
                f.press(i % 2 == 0 ? "Left" : "Right");
            }
            f.press("Space");
        }
        return kinds;
    };
    const std::vector<std::string> first = deal(7);
    CHECK(std::set<std::string>(first.begin(), first.end()).size() == 7);
    CHECK(deal(7) == first);
}
