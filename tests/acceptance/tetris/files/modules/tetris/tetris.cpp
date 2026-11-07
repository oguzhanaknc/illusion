// The game module tetris: the rules of SPEC.md on Components.
//
// The board is derived from the tetris.Cell entities on every update, so the editor can add, move or remove
// Cells while the game runs. Cells and the falling piece are children of the Board, placed in its space at
// origin + (x, y, 0).

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
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tetris
{

using namespace illusion;

/// The seven tetrominoes.
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

/// The playfield: width x height cells, y up, cell (x, y) at origin + (x, y, 0) in the Board's space.
struct Board
{
    u32 width = 10;
    u32 height = 20;
    f32 gravitySeconds = 0.8f;
    Vec3 origin{0.0f};
};

/// A locked block (a child of the Board).
struct Cell
{
    i32 x = 0;
    i32 y = 0;
    Piece kind = Piece::I;
};

/// The falling piece; (x, y) is the top-left cell of its SRS bounding box. Its four blocks are children.
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

/// What comes next: the letters of `forced`, then 7-bags shuffled by illusion::Random seeded with `seed`.
struct PieceQueue
{
    u32 seed = 0;
    std::string forced;
};

/// Lines, points, level and the end of the game.
struct Score
{
    u32 lines = 0;
    u32 points = 0;
    u32 level = 1;
    bool gameOver = false;
};

/// Runtime state on the PieceQueue entity (not saved): pieces dealt and time towards the next row.
struct Progress
{
    u32 dealt = 0;
    f64 fallSeconds = 0.0;
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

constexpr i32 kLargestBoard = 64;
constexpr std::string_view kLetters = "IOTSZJL";

struct Offset
{
    i32 x = 0;
    i32 y = 0; // down from the top of the bounding box
};

using Blocks = std::array<Offset, 4>;

// The spawn orientation of each piece in its bounding box (y down) and the size of the box.
constexpr std::array<Blocks, 7> kSpawn{{
    {{{0, 1}, {1, 1}, {2, 1}, {3, 1}}}, // I
    {{{1, 0}, {2, 0}, {1, 1}, {2, 1}}}, // O
    {{{1, 0}, {0, 1}, {1, 1}, {2, 1}}}, // T
    {{{1, 0}, {2, 0}, {0, 1}, {1, 1}}}, // S
    {{{0, 0}, {1, 0}, {1, 1}, {2, 1}}}, // Z
    {{{0, 0}, {0, 1}, {1, 1}, {2, 1}}}, // J
    {{{2, 0}, {0, 1}, {1, 1}, {2, 1}}}, // L
}};
constexpr std::array<i32, 7> kBoxSize{4, 0, 3, 3, 3, 3, 3}; // 0: the O piece does not rotate

// The blocks of @p kind in @p rotation (0 to 3, clockwise): SRS rotates the blocks inside the box.
Blocks blocksOf(Piece kind, u32 rotation)
{
    Blocks blocks = kSpawn[static_cast<usize>(kind)];
    const i32 size = kBoxSize[static_cast<usize>(kind)];
    for (u32 turn = 0; size != 0 && turn < rotation % 4; ++turn)
    {
        for (Offset& block : blocks)
        {
            block = Offset{size - 1 - block.y, block.x};
        }
    }
    return blocks;
}

// The SRS wall kicks (x right, y up) tried in order when rotating clockwise from rotation r.
using KickTable = std::array<std::array<IVec2, 5>, 4>;
constexpr KickTable kKicks{{
    {{{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}}}, // 0 -> R
    {{{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}}},     // R -> 2
    {{{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}}},    // 2 -> L
    {{{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}}},  // L -> 0
}};
constexpr KickTable kKicksForI{{
    {{{0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2}}},
    {{{0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1}}},
    {{{0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2}}},
    {{{0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1}}},
}};

constexpr std::array<u32, 5> kClearPoints{0, 100, 300, 500, 800};

constexpr std::array<std::string_view, 7> kMaterialPaths{
    "assets:/materials/i.material", "assets:/materials/o.material", "assets:/materials/t.material",
    "assets:/materials/s.material", "assets:/materials/z.material", "assets:/materials/j.material",
    "assets:/materials/l.material"};

// Which cells are taken, rebuilt from the Cells every update.
class Playfield
{
public:
    Playfield(i32 width, i32 height) noexcept
        : m_width(width)
        , m_height(height)
    {
    }

    [[nodiscard]] i32 width() const noexcept { return m_width; }
    [[nodiscard]] i32 height() const noexcept { return m_height; }

    [[nodiscard]] bool contains(i32 x, i32 y) const noexcept
    {
        return x >= 0 && x < m_width && y >= 0 && y < m_height;
    }

    void take(i32 x, i32 y) noexcept
    {
        if (contains(x, y))
        {
            m_taken[index(x, y)] = true;
        }
    }

    [[nodiscard]] bool isFree(i32 x, i32 y) const noexcept { return contains(x, y) && !m_taken[index(x, y)]; }

    [[nodiscard]] bool fits(Piece kind, u32 rotation, i32 x, i32 y) const
    {
        const Blocks blocks = blocksOf(kind, rotation);
        return std::all_of(blocks.begin(), blocks.end(),
                           [&](const Offset& block) { return isFree(x + block.x, y - block.y); });
    }

    [[nodiscard]] bool rowFull(i32 y) const noexcept
    {
        for (i32 x = 0; x < m_width; ++x)
        {
            if (isFree(x, y))
            {
                return false;
            }
        }
        return true;
    }

private:
    [[nodiscard]] static usize index(i32 x, i32 y) noexcept
    {
        return static_cast<usize>(y * kLargestBoard + x);
    }

    i32 m_width;
    i32 m_height;
    std::array<bool, static_cast<usize>(kLargestBoard* kLargestBoard)> m_taken{};
};

// The piece dealt as number @p number.
Piece dealt(const PieceQueue& queue, u32 number)
{
    const auto forced = static_cast<u32>(queue.forced.size());
    if (number < forced)
    {
        const usize letter = kLetters.find(queue.forced[number]);
        return letter == std::string_view::npos ? Piece::I : static_cast<Piece>(letter);
    }
    // Replay the shuffles of every bag up to the one that holds the piece, so the sequence depends on the
    // seed and the number alone (and survives hot reloads).
    Random random(queue.seed);
    std::array<Piece, 7> bag{};
    for (u32 b = 0; b <= (number - forced) / 7; ++b)
    {
        for (usize i = 0; i < bag.size(); ++i)
        {
            bag[i] = static_cast<Piece>(i);
        }
        for (u32 i = 6; i > 0; --i)
        {
            std::swap(bag[i], bag[static_cast<usize>(random.below(i + 1))]);
        }
    }
    return bag[(number - forced) % 7];
}

LocalTransform placeAt(const Board& board, i32 x, i32 y)
{
    return LocalTransform{board.origin + Vec3(static_cast<f32>(x), static_cast<f32>(y), 0.0f),
                          Quat(1.0f, 0.0f, 0.0f, 0.0f), Vec3(1.0f)};
}

MeshRenderer looksOf(Piece kind)
{
    MeshRenderer renderer;
    renderer.mesh = AssetRef("builtin:/mesh/cube");
    renderer.material = AssetRef(kMaterialPaths[static_cast<usize>(kind)]);
    return renderer;
}

template <typename T>
Entity firstWith(World& world)
{
    Entity found;
    world.query<T>().each([&](Entity entity, const T&) {
        if (found.isNull())
        {
            found = entity;
        }
    });
    return found;
}

// Creates the falling piece and its four blocks under the Board.
Entity spawn(World& world, Entity boardEntity, const Board& board, const ActivePiece& piece)
{
    const Entity active = world.create(piece, LocalTransform{}, Transform{});
    (void)setParent(world, active, boardEntity, ParentMode::KeepLocal);
    for (const Offset& block : blocksOf(piece.kind, piece.rotation))
    {
        const Entity entity = world.create(placeAt(board, piece.x + block.x, piece.y - block.y), Transform{},
                                           looksOf(piece.kind));
        world.emplace<ActiveBlock>(entity);
        (void)setParent(world, entity, active, ParentMode::KeepLocal);
    }
    return active;
}

// Moves the blocks of the falling piece to its cells.
void moveBlocks(World& world, const Board& board, const ActivePiece& piece)
{
    const Blocks blocks = blocksOf(piece.kind, piece.rotation);
    usize next = 0;
    world.query<LocalTransform>().with<ActiveBlock>().each([&](LocalTransform& local) {
        const Offset& block = blocks[std::min<usize>(next++, blocks.size() - 1)];
        local = placeAt(board, piece.x + block.x, piece.y - block.y);
    });
}

// Turns the falling piece into Cells, clears full rows and scores them.
void lockPiece(World& world, Entity boardEntity, Entity active, const Board& board, const ActivePiece& piece,
               Playfield& field, Score& score)
{
    for (const Offset& block : blocksOf(piece.kind, piece.rotation))
    {
        const i32 x = piece.x + block.x;
        const i32 y = piece.y - block.y;
        const Entity cell =
            world.create(Cell{x, y, piece.kind}, placeAt(board, x, y), Transform{}, looksOf(piece.kind));
        (void)setParent(world, cell, boardEntity, ParentMode::KeepLocal);
        field.take(x, y);
    }
    (void)destroyRecursive(world, active);

    std::array<bool, static_cast<usize>(kLargestBoard)> full{};
    u32 cleared = 0;
    for (i32 y = 0; y < field.height(); ++y)
    {
        full[static_cast<usize>(y)] = field.rowFull(y);
        cleared += full[static_cast<usize>(y)] ? 1u : 0u;
    }
    if (cleared == 0)
    {
        return;
    }
    std::vector<Entity> gone;
    world.query<Cell, LocalTransform>().each([&](Entity entity, Cell& cell, LocalTransform& local) {
        if (cell.y >= 0 && cell.y < field.height() && full[static_cast<usize>(cell.y)])
        {
            gone.push_back(entity);
            return;
        }
        i32 drop = 0;
        for (i32 y = 0; y < std::min(cell.y, field.height()); ++y)
        {
            drop += full[static_cast<usize>(y)] ? 1 : 0;
        }
        cell.y -= drop;
        local = placeAt(board, cell.x, cell.y);
    });
    for (const Entity entity : gone)
    {
        (void)destroyRecursive(world, entity);
    }
    score.points += kClearPoints[std::min<u32>(cleared, 4)] * score.level;
    score.lines += cleared;
    score.level = 1 + score.lines / 10;
}

bool load(ModuleContext&)
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
    reflectRuntime<Progress>("tetris.Progress");
    return true;
}

void start(ModuleContext&)
{
}

void update(ModuleContext& context, f64 deltaSeconds)
{
    World& world = context.world();
    const Entity boardEntity = firstWith<Board>(world);
    const Entity queueEntity = firstWith<PieceQueue>(world);
    const Entity scoreEntity = firstWith<Score>(world);
    if (boardEntity.isNull() || queueEntity.isNull() || scoreEntity.isNull() ||
        world.get<Score>(scoreEntity).gameOver)
    {
        return;
    }
    Board board = world.get<Board>(boardEntity);
    board.width = std::clamp<u32>(board.width, 4, kLargestBoard);
    board.height = std::clamp<u32>(board.height, 4, kLargestBoard);
    Score score = world.get<Score>(scoreEntity);
    score.level = std::max<u32>(score.level, 1);
    if (!world.has<Progress>(queueEntity))
    {
        world.emplace<Progress>(queueEntity);
    }
    Progress progress = world.get<Progress>(queueEntity);

    Playfield field(static_cast<i32>(board.width), static_cast<i32>(board.height));
    world.query<Cell>().each([&](const Cell& cell) { field.take(cell.x, cell.y); });

    Entity active = firstWith<ActivePiece>(world);
    ActivePiece piece;
    if (active.isNull())
    {
        piece.kind = dealt(world.get<PieceQueue>(queueEntity), progress.dealt++);
        piece.x = 3;
        piece.y = field.height() - 1;
        progress.fallSeconds = 0.0;
        if (!field.fits(piece.kind, 0, piece.x, piece.y))
        {
            score.gameOver = true;
            world.get<Score>(scoreEntity) = score;
            world.get<Progress>(queueEntity) = progress;
            return;
        }
        active = spawn(world, boardEntity, board, piece);
    }
    else
    {
        piece = world.get<ActivePiece>(active);
        piece.rotation %= 4;
    }

    bool landed = false;
    const auto stepDown = [&] {
        if (field.fits(piece.kind, piece.rotation, piece.x, piece.y - 1))
        {
            --piece.y;
            return true;
        }
        landed = true;
        return false;
    };

    if (const InputService* input = context.service<InputService>())
    {
        const auto pressed = [&](std::string_view name) {
            const std::optional<ActionId> action = input->findAction(name);
            return action.has_value() && input->wasPressed(*action);
        };
        const i32 shift = (pressed("tetris.move_right") ? 1 : 0) - (pressed("tetris.move_left") ? 1 : 0);
        if (shift != 0 && field.fits(piece.kind, piece.rotation, piece.x + shift, piece.y))
        {
            piece.x += shift;
        }
        if (pressed("tetris.rotate_cw"))
        {
            const u32 target = (piece.rotation + 1) % 4;
            const KickTable& kicks = piece.kind == Piece::I ? kKicksForI : kKicks;
            for (const IVec2& kick : kicks[piece.rotation])
            {
                if (field.fits(piece.kind, target, piece.x + kick.x, piece.y + kick.y))
                {
                    piece.rotation = target;
                    piece.x += kick.x;
                    piece.y += kick.y;
                    break;
                }
            }
        }
        if (pressed("tetris.soft_drop"))
        {
            (void)stepDown();
        }
        if (!landed && pressed("tetris.hard_drop"))
        {
            while (stepDown())
            {
            }
        }
    }

    if (!landed)
    {
        const f64 interval = std::max(static_cast<f64>(board.gravitySeconds), 0.01) *
                             std::pow(0.85, static_cast<f64>(score.level - 1));
        progress.fallSeconds += std::max(deltaSeconds, 0.0);
        while (!landed && progress.fallSeconds >= interval)
        {
            progress.fallSeconds -= interval;
            (void)stepDown();
        }
    }

    if (landed)
    {
        progress.fallSeconds = 0.0;
        lockPiece(world, boardEntity, active, board, piece, field, score);
    }
    else
    {
        world.get<ActivePiece>(active) = piece;
        moveBlocks(world, board, piece);
    }
    world.get<Score>(scoreEntity) = score;
    world.get<Progress>(queueEntity) = progress;
}

constexpr ModuleApi kModule =
    makeModuleApi("tetris", "1.0.0", ModuleCallbacks{.load = &load, .start = &start, .update = &update});

} // namespace

ILL_MODULE(tetris, kModule)
