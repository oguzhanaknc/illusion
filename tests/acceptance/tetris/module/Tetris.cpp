// The Tetris reference module of the agent acceptance test (tests/acceptance/tetris/SPEC.md): the game an
// agent must build through the editor, written by hand so that CI can replay the play scenarios (C5)
// against it. Every rule of the contract is here: SRS rotation with the standard wall kicks, gravity that
// speeds up with the level, soft and hard drops, line clears and their score, a forced sequence then a
// seeded 7-bag, and game over when a new piece overlaps a Cell. The board is derived from the tetris.Cell
// entities on every update, so commands that add or remove Cells change the game at once.

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/core/Random.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/InputService.hpp>
#include <illusion/render/SceneComponents.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace tetris
{

using namespace illusion;

/// The seven tetrominoes, in the order of SPEC.md.
enum class Piece : u8
{
    I,
    O,
    T,
    S,
    Z,
    J,
    L,
};

/// The playfield: cell (x, y) sits at `origin + (x, y, 0)`, y up.
struct Board
{
    u32 width = 10;
    u32 height = 20;
    f32 gravitySeconds = 0.8f;
    Vec3 origin{0.0f};
};

/// A locked block, a child of the Board.
struct Cell
{
    i32 x = 0;
    i32 y = 0;
    Piece kind = Piece::I;
};

/// The falling piece: (x, y) is the top-left corner of its SRS bounding box; four ActiveBlock children.
struct ActivePiece
{
    Piece kind = Piece::I;
    u32 rotation = 0;
    i32 x = 0;
    i32 y = 0;
};

/// Tag of the four blocks of the falling piece.
struct ActiveBlock
{
};

/// What comes next: the characters of `forced` (I, O, T, S, Z, J, L), then 7-bags shuffled by `seed`.
struct PieceQueue
{
    u32 seed = 0;
    std::string forced;
};

/// The score and the end of the game.
struct Score
{
    u32 lines = 0;
    u32 points = 0;
    u32 level = 1;
    bool gameOver = false;
};

/// Runtime state on the PieceQueue entity (not saved): time towards the next row and pieces dealt.
struct State
{
    f64 fallSeconds = 0.0;
    u32 dealt = 0;
};

} // namespace tetris

template <>
struct illusion::EnumReflection<tetris::Piece>
{
    static constexpr std::array kValues{illusion::EnumValue{"I", 0}, illusion::EnumValue{"O", 1},
                                        illusion::EnumValue{"T", 2}, illusion::EnumValue{"S", 3},
                                        illusion::EnumValue{"Z", 4}, illusion::EnumValue{"J", 5},
                                        illusion::EnumValue{"L", 6}};
};

namespace
{

using namespace illusion;
using namespace tetris;

constexpr i32 kMaxSide = 64; // boards are clamped to 64 x 64 cells
constexpr std::string_view kPieceNames = "IOTSZJL";

struct Point
{
    i32 x;
    i32 y; // down, in the bounding box
};

using Shape = std::array<Point, 4>;

// The SRS shapes of each piece in rotations 0 (spawn), R, 2 and L, in bounding-box coordinates.
constexpr std::array<std::array<Shape, 4>, 7> kShapes{{
    // I (4 x 4)
    {{{{{0, 1}, {1, 1}, {2, 1}, {3, 1}}},
      {{{2, 0}, {2, 1}, {2, 2}, {2, 3}}},
      {{{0, 2}, {1, 2}, {2, 2}, {3, 2}}},
      {{{1, 0}, {1, 1}, {1, 2}, {1, 3}}}}},
    // O (4 x 2: columns 1 and 2, as the guideline spawns it)
    {{{{{1, 0}, {2, 0}, {1, 1}, {2, 1}}},
      {{{1, 0}, {2, 0}, {1, 1}, {2, 1}}},
      {{{1, 0}, {2, 0}, {1, 1}, {2, 1}}},
      {{{1, 0}, {2, 0}, {1, 1}, {2, 1}}}}},
    // T
    {{{{{1, 0}, {0, 1}, {1, 1}, {2, 1}}},
      {{{1, 0}, {1, 1}, {2, 1}, {1, 2}}},
      {{{0, 1}, {1, 1}, {2, 1}, {1, 2}}},
      {{{1, 0}, {0, 1}, {1, 1}, {1, 2}}}}},
    // S
    {{{{{1, 0}, {2, 0}, {0, 1}, {1, 1}}},
      {{{1, 0}, {1, 1}, {2, 1}, {2, 2}}},
      {{{1, 1}, {2, 1}, {0, 2}, {1, 2}}},
      {{{0, 0}, {0, 1}, {1, 1}, {1, 2}}}}},
    // Z
    {{{{{0, 0}, {1, 0}, {1, 1}, {2, 1}}},
      {{{2, 0}, {1, 1}, {2, 1}, {1, 2}}},
      {{{0, 1}, {1, 1}, {1, 2}, {2, 2}}},
      {{{1, 0}, {0, 1}, {1, 1}, {0, 2}}}}},
    // J
    {{{{{0, 0}, {0, 1}, {1, 1}, {2, 1}}},
      {{{1, 0}, {2, 0}, {1, 1}, {1, 2}}},
      {{{0, 1}, {1, 1}, {2, 1}, {2, 2}}},
      {{{1, 0}, {1, 1}, {0, 2}, {1, 2}}}}},
    // L
    {{{{{2, 0}, {0, 1}, {1, 1}, {2, 1}}},
      {{{1, 0}, {1, 1}, {1, 2}, {2, 2}}},
      {{{0, 1}, {1, 1}, {2, 1}, {0, 2}}},
      {{{0, 0}, {1, 0}, {1, 1}, {1, 2}}}}},
}};

// The SRS wall kicks of a clockwise rotation from rotation r (r -> r + 1), as (x, y) offsets with y up.
using Kicks = std::array<std::array<IVec2, 5>, 4>;
constexpr Kicks kKicks{{
    {{{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}}},
    {{{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}}},
    {{{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}}},
    {{{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}}},
}};
constexpr Kicks kKicksI{{
    {{{0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2}}},
    {{{0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1}}},
    {{{0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2}}},
    {{{0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1}}},
}};

// Score of clearing 1 to 4 lines at once, times the level.
constexpr std::array<u32, 5> kLinePoints{0, 100, 300, 500, 800};

constexpr std::array<std::string_view, 7> kMaterials{
    "assets:/materials/i.material", "assets:/materials/o.material", "assets:/materials/t.material",
    "assets:/materials/s.material", "assets:/materials/z.material", "assets:/materials/j.material",
    "assets:/materials/l.material"};

const Shape& shapeOf(Piece kind, u32 rotation) noexcept
{
    return kShapes[static_cast<usize>(kind)][rotation % 4];
}

// The occupied cells of the board, rebuilt from the Cell entities on every update.
struct Grid
{
    i32 width = 0;
    i32 height = 0;
    std::array<bool, static_cast<usize>(kMaxSide* kMaxSide)> filled{};

    [[nodiscard]] bool inside(i32 x, i32 y) const noexcept
    {
        return x >= 0 && x < width && y >= 0 && y < height;
    }

    [[nodiscard]] bool& at(i32 x, i32 y) noexcept { return filled[static_cast<usize>(y * kMaxSide + x)]; }

    [[nodiscard]] bool free(i32 x, i32 y) const noexcept
    {
        return inside(x, y) && !filled[static_cast<usize>(y * kMaxSide + x)];
    }

    [[nodiscard]] bool fits(Piece kind, u32 rotation, i32 x, i32 y) const noexcept
    {
        return std::all_of(shapeOf(kind, rotation).begin(), shapeOf(kind, rotation).end(),
                           [&](Point cell) { return free(x + cell.x, y - cell.y); });
    }
};

// The singletons of the game, found by their Components (the first of each kind).
struct Game
{
    Entity board;
    Entity queue;
    Entity score;
    Entity active;
};

Game findGame(World& world)
{
    Game game;
    world.query<Board>().each(
        [&](Entity entity, const Board&) { game.board = game.board.isNull() ? entity : game.board; });
    world.query<PieceQueue>().each(
        [&](Entity entity, const PieceQueue&) { game.queue = game.queue.isNull() ? entity : game.queue; });
    world.query<Score>().each(
        [&](Entity entity, const Score&) { game.score = game.score.isNull() ? entity : game.score; });
    world.query<ActivePiece>().each([&](Entity entity, const ActivePiece&) {
        game.active = game.active.isNull() ? entity : game.active;
    });
    return game;
}

// The piece dealt as number @p index: the forced characters first (unknown ones count as I), then 7-bags.
Piece pieceAt(const PieceQueue& queue, u32 index)
{
    if (index < queue.forced.size())
    {
        const usize found = kPieceNames.find(queue.forced[index]);
        return found == std::string_view::npos ? Piece::I : static_cast<Piece>(found);
    }
    const u32 bagIndex = (index - static_cast<u32>(queue.forced.size())) / 7;
    const u32 position = (index - static_cast<u32>(queue.forced.size())) % 7;
    Random random(queue.seed);
    std::array<Piece, 7> bag{};
    for (u32 b = 0; b <= bagIndex; ++b)
    {
        for (u32 i = 0; i < 7; ++i)
        {
            bag[i] = static_cast<Piece>(i);
        }
        for (u32 i = 6; i > 0; --i)
        {
            std::swap(bag[i], bag[static_cast<usize>(random.below(i + 1))]);
        }
    }
    return bag[position];
}

Transform boardTransform(World& world, Entity board)
{
    const Transform* transform = world.tryGet<Transform>(board);
    return transform != nullptr ? *transform : Transform{};
}

// The placement under the board of a block at the world position @p position, unrotated and unscaled.
LocalTransform underBoard(const Transform& board, Vec3 position)
{
    const auto safe = [](f32 value) {
        return std::abs(value) > 1e-6f ? value : 1.0f;
    };
    const Vec3 scale(safe(board.scale.x), safe(board.scale.y), safe(board.scale.z));
    const Quat inverse = glm::inverse(board.rotation);
    return LocalTransform{(inverse * (position - board.position)) / scale, inverse, Vec3(1.0f) / scale};
}

Vec3 cellPosition(const Board& board, i32 x, i32 y)
{
    return board.origin + Vec3(static_cast<f32>(x), static_cast<f32>(y), 0.0f);
}

MeshRenderer blockRenderer(Piece kind)
{
    MeshRenderer renderer;
    renderer.mesh = AssetRef("builtin:/mesh/cube");
    renderer.material = AssetRef(kMaterials[static_cast<usize>(kind)]);
    return renderer;
}

// Moves the four blocks of the falling piece to where @p piece is.
void placeBlocks(World& world, const Board& board, const ActivePiece& piece)
{
    const Shape& shape = shapeOf(piece.kind, piece.rotation);
    usize index = 0;
    world.query<LocalTransform>().with<ActiveBlock>().each([&](LocalTransform& local) {
        const Point cell = shape[std::min<usize>(index++, 3)];
        local.position = cellPosition(board, piece.x + cell.x, piece.y - cell.y);
    });
}

void spawnActive(World& world, const Board& board, const ActivePiece& piece)
{
    const Entity active = world.create(piece, Transform{});
    for (usize i = 0; i < 4; ++i)
    {
        const Entity block = world.create(Transform{}, blockRenderer(piece.kind));
        world.emplace<ActiveBlock>(block);
        (void)setParent(world, block, active, ParentMode::KeepLocal);
    }
    placeBlocks(world, board, piece);
}

// Locks the falling piece into Cells, clears the full rows and scores them.
void lock(World& world, const Game& game, const Board& board, const ActivePiece& piece, Grid& grid,
          Score& score)
{
    const Transform parent = boardTransform(world, game.board);
    for (const Point cell : shapeOf(piece.kind, piece.rotation))
    {
        const i32 x = piece.x + cell.x;
        const i32 y = piece.y - cell.y;
        const Entity locked =
            world.create(Cell{x, y, piece.kind}, underBoard(parent, cellPosition(board, x, y)), Transform{},
                         blockRenderer(piece.kind));
        (void)setParent(world, locked, game.board, ParentMode::KeepLocal);
        if (grid.inside(x, y))
        {
            grid.at(x, y) = true;
        }
    }
    (void)destroyRecursive(world, game.active);

    std::array<bool, static_cast<usize>(kMaxSide)> full{};
    u32 cleared = 0;
    for (i32 y = 0; y < grid.height; ++y)
    {
        bool row = true;
        for (i32 x = 0; x < grid.width && row; ++x)
        {
            row = !grid.free(x, y);
        }
        full[static_cast<usize>(y)] = row;
        cleared += row ? 1 : 0;
    }
    if (cleared == 0)
    {
        return;
    }
    std::vector<Entity> removed;
    world.query<Cell, LocalTransform>().each([&](Entity entity, Cell& cell, LocalTransform& local) {
        if (cell.y >= 0 && cell.y < grid.height && full[static_cast<usize>(cell.y)])
        {
            removed.push_back(entity);
            return;
        }
        i32 below = 0;
        for (i32 y = 0; y < std::min(cell.y, grid.height); ++y)
        {
            below += full[static_cast<usize>(y)] ? 1 : 0;
        }
        cell.y -= below;
        local = underBoard(parent, cellPosition(board, cell.x, cell.y));
    });
    for (const Entity entity : removed)
    {
        (void)destroyRecursive(world, entity);
    }
    score.points += kLinePoints[std::min<u32>(cleared, 4)] * score.level;
    score.lines += cleared;
    score.level = 1 + score.lines / 10;
}

bool loadTetris(ModuleContext&)
{
    reflect<Board>("tetris.Board")
        .meta({.displayName = "Tetris board", .category = "Tetris", .icon = "grid_on"})
        .field("width", &Board::width, {.min = 4.0, .max = 64.0})
        .field("height", &Board::height, {.min = 4.0, .max = 64.0})
        .field("gravitySeconds", &Board::gravitySeconds, {.unit = Unit::Seconds, .min = 0.01})
        .field("origin", &Board::origin);
    reflect<Cell>("tetris.Cell")
        .meta({.displayName = "Tetris cell", .category = "Tetris", .icon = "square"})
        .field("x", &Cell::x)
        .field("y", &Cell::y)
        .field("kind", &Cell::kind);
    reflect<ActivePiece>("tetris.ActivePiece")
        .meta({.displayName = "Active piece", .category = "Tetris", .icon = "extension"})
        .field("kind", &ActivePiece::kind)
        .field("rotation", &ActivePiece::rotation, {.min = 0.0, .max = 3.0})
        .field("x", &ActivePiece::x)
        .field("y", &ActivePiece::y);
    reflect<ActiveBlock>("tetris.ActiveBlock")
        .meta({.displayName = "Active block", .category = "Tetris", .icon = "square"});
    reflect<PieceQueue>("tetris.PieceQueue")
        .meta({.displayName = "Piece queue", .category = "Tetris", .icon = "queue"})
        .field("seed", &PieceQueue::seed)
        .field("forced", &PieceQueue::forced);
    reflect<Score>("tetris.Score")
        .meta({.displayName = "Tetris score", .category = "Tetris", .icon = "scoreboard"})
        .field("lines", &Score::lines)
        .field("points", &Score::points)
        .field("level", &Score::level)
        .field("gameOver", &Score::gameOver);
    reflectRuntime<State>("tetris.State");
    return true;
}

void updateTetris(ModuleContext& context, f64 deltaSeconds)
{
    World& world = context.world();
    Game game = findGame(world);
    if (game.board.isNull() || game.queue.isNull() || game.score.isNull() ||
        world.get<Score>(game.score).gameOver)
    {
        return;
    }
    Board board = world.get<Board>(game.board);
    board.width = std::clamp<u32>(board.width, 4, kMaxSide);
    board.height = std::clamp<u32>(board.height, 4, kMaxSide);
    Score score = world.get<Score>(game.score);
    score.level = std::max<u32>(score.level, 1);
    if (!world.has<State>(game.queue))
    {
        world.emplace<State>(game.queue);
    }
    State state = world.get<State>(game.queue);

    Grid grid;
    grid.width = static_cast<i32>(board.width);
    grid.height = static_cast<i32>(board.height);
    world.query<Cell>().each([&](const Cell& cell) {
        if (grid.inside(cell.x, cell.y))
        {
            grid.at(cell.x, cell.y) = true;
        }
    });

    ActivePiece piece;
    if (game.active.isNull())
    {
        piece.kind = pieceAt(world.get<PieceQueue>(game.queue), state.dealt++);
        piece.x = 3;
        piece.y = grid.height - 1;
        state.fallSeconds = 0.0;
        if (!grid.fits(piece.kind, 0, piece.x, piece.y))
        {
            score.gameOver = true;
            world.get<Score>(game.score) = score;
            world.get<State>(game.queue) = state;
            return;
        }
        spawnActive(world, board, piece);
        game.active = findGame(world).active;
    }
    else
    {
        piece = world.get<ActivePiece>(game.active);
        piece.rotation %= 4;
    }

    bool locked = false;
    const auto down = [&] {
        if (grid.fits(piece.kind, piece.rotation, piece.x, piece.y - 1))
        {
            --piece.y;
            return true;
        }
        locked = true;
        return false;
    };
    if (const InputService* input = context.service<InputService>())
    {
        const auto pressed = [&](std::string_view name) {
            const std::optional<ActionId> action = input->findAction(name);
            return action && input->wasPressed(*action);
        };
        for (const i32 step : {pressed("tetris.move_left") ? -1 : 0, pressed("tetris.move_right") ? 1 : 0})
        {
            if (step != 0 && grid.fits(piece.kind, piece.rotation, piece.x + step, piece.y))
            {
                piece.x += step;
            }
        }
        if (pressed("tetris.rotate_cw"))
        {
            const u32 next = (piece.rotation + 1) % 4;
            const Kicks& kicks = piece.kind == Piece::I ? kKicksI : kKicks;
            for (const IVec2 kick : kicks[piece.rotation])
            {
                if (grid.fits(piece.kind, next, piece.x + kick.x, piece.y + kick.y))
                {
                    piece.rotation = next;
                    piece.x += kick.x;
                    piece.y += kick.y;
                    break;
                }
            }
        }
        if (pressed("tetris.soft_drop"))
        {
            (void)down();
        }
        if (!locked && pressed("tetris.hard_drop"))
        {
            while (down())
            {
            }
        }
    }
    if (!locked)
    {
        const f64 interval = std::max(static_cast<f64>(board.gravitySeconds), 0.01) *
                             std::pow(0.85, static_cast<f64>(score.level - 1));
        state.fallSeconds += std::max(deltaSeconds, 0.0);
        while (!locked && state.fallSeconds >= interval)
        {
            state.fallSeconds -= interval;
            (void)down();
        }
    }
    if (locked)
    {
        state.fallSeconds = 0.0;
        lock(world, game, board, piece, grid, score);
    }
    else
    {
        world.get<ActivePiece>(game.active) = piece;
        placeBlocks(world, board, piece);
    }
    world.get<Score>(game.score) = score;
    world.get<State>(game.queue) = state;
}

constexpr ModuleApi kTetris =
    makeModuleApi("tetris", "1.0.0", ModuleCallbacks{.load = &loadTetris, .update = &updateTetris});

} // namespace

ILL_MODULE(tetris, kTetris)
