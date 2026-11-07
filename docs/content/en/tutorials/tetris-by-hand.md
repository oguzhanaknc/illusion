# Tetris by Hand

This tutorial builds the game of [Tetris with an Agent](tetris-with-an-agent.md) yourself, in the editor
window: a project, materials, a scene, input actions and a C++ game module that you compile and hot-reload
while the game runs. The rules are those of `tests/acceptance/tetris/SPEC.md`; the full implementation of
every rule is the reference module `tests/acceptance/tetris/module/Tetris.cpp`, which this tutorial follows.

You need a Release build of illusion and the C++ compiler the editor was built with.

## 1. Create the project

Start the editor without a project:

```bash
build/bin/Release/illusion_editor --workspace ~/games
```

The Welcome window opens. Create a project named **Tetris** in the folder `tetris` from the **Empty**
template. The editor opens it with `assets:/scenes/main.scene` as the startup scene.

## 2. Materials

**Assets → New Material** asks for the path and the value of the material. Create the seven piece colours and
the frame:

| Path | Value |
| --- | --- |
| `assets:/materials/i.material` | `{"material": {"name": "I", "baseColorFactor": [0, 1, 1, 1]}}` |
| `assets:/materials/o.material` | `{"material": {"name": "O", "baseColorFactor": [1, 1, 0, 1]}}` |
| `assets:/materials/t.material` | `{"material": {"name": "T", "baseColorFactor": [0.5, 0, 0.5, 1]}}` |
| `assets:/materials/s.material` | `{"material": {"name": "S", "baseColorFactor": [0, 1, 0, 1]}}` |
| `assets:/materials/z.material` | `{"material": {"name": "Z", "baseColorFactor": [1, 0, 0, 1]}}` |
| `assets:/materials/j.material` | `{"material": {"name": "J", "baseColorFactor": [0, 0, 1, 1]}}` |
| `assets:/materials/l.material` | `{"material": {"name": "L", "baseColorFactor": [1, 0.5, 0, 1]}}` |
| `assets:/materials/frame.material` | `{"material": {"name": "Frame", "baseColorFactor": [0.3, 0.3, 0.3, 1], "roughnessFactor": 0.8}}` |

The [Asset Browser](../manual/editor/assets.md) shows them in `materials`.

## 3. The board scene

Create these entities with **Entity → Create Empty**, rename them with **F2** and add their Components with
**Add Component** in the [Inspector](../manual/editor/scene-editing.md):

| Entity | Components |
| --- | --- |
| Camera | Transform at (4.5, 9.5, 30), Camera with a 45° field of view |
| Sun | Transform rotated −45° around X, DirectionalLight with intensity 3 |
| Sky | Transform, SkyLight with `builtin:/sky/procedural`, ProceduralSky |
| Board | Transform |
| Left wall, Right wall, Floor | Children of Board (drag them onto it in the Outliner): Transforms at (−1, 9.5, 0) scaled (1, 22, 1), at (10, 9.5, 0) scaled (1, 22, 1) and at (4.5, −1, 0) scaled (10, 1, 1); MeshRenderers with `builtin:/mesh/cube` and the frame material |

Drag `frame.material` from the Asset Browser onto the Material field of each MeshRenderer. The Game view
shows the board through the camera. Save with **Ctrl+S**.

## 4. Input actions

The game reads five Button actions from `assets:/input/tetris.input`. Open the command palette
(**Ctrl+Shift+P**), run **Define input action** and fill in the first one:

```json
{"asset": "assets:/input/tetris.input", "name": "tetris.move_left", "type": "Button", "bindings": ["Key.Left", "Gamepad.DPadLeft"]}
```

Then open **Window → Input Actions**, which now shows `tetris.input`, and add the other four with the row at
the bottom and **Add binding**:

| Action | Bindings |
| --- | --- |
| `tetris.move_right` | `Key.Right`, `Gamepad.DPadRight` |
| `tetris.rotate_cw` | `Key.Up`, `Gamepad.South` |
| `tetris.soft_drop` | `Key.Down`, `Gamepad.DPadDown` |
| `tetris.hard_drop` | `Key.Space`, `Gamepad.North` |

Finally point the project at the asset: in **Window → Project Settings**, set the actions asset of the Input
settings to `assets:/input/tetris.input`.

## 5. The game module

Run **New module** from the command palette with the name `tetris`. It writes `modules/CMakeLists.txt` and
`modules/tetris/` with a `CMakeLists.txt` and `tetris.cpp`. The game only needs input, render and runtime:

```cmake
illusion_add_module(tetris SHARED SOURCES tetris.cpp USES input render runtime)
```

Open `modules/tetris/tetris.cpp` in your code editor. The Components come first. They are plain data, and the
module reflects them under stable names so that the Inspector, scene files and agents can use them:

```cpp
#include <illusion/core/Module.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/InputService.hpp>

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace tetris
{

using namespace illusion;

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

struct Board
{
    u32 width = 10;
    u32 height = 20;
    f32 gravitySeconds = 0.8f;
    Vec3 origin{0.0f};
};

struct Cell
{
    i32 x = 0;
    i32 y = 0;
    Piece kind = Piece::I;
};

struct PieceQueue
{
    u32 seed = 0;
    std::string forced;
};

struct Score
{
    u32 lines = 0;
    u32 points = 0;
    u32 level = 1;
    bool gameOver = false;
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
```

`load` reflects them; `update` runs every frame of Play mode and reads the actions through the
`InputService`:

```cpp
namespace
{

using namespace illusion;
using namespace tetris;

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
    return true;
}

void updateTetris(ModuleContext& context, f64 deltaSeconds)
{
    World& world = context.world();
    bool gameOver = false;
    world.query<Score>().each([&](const Score& score) { gameOver = gameOver || score.gameOver; });
    const InputService* input = context.service<InputService>();
    if (gameOver || input == nullptr)
    {
        return;
    }
    const auto pressed = [&](std::string_view name) {
        const std::optional<ActionId> action = input->findAction(name);
        return action && input->wasPressed(*action);
    };
    const i32 move = (pressed("tetris.move_right") ? 1 : 0) - (pressed("tetris.move_left") ? 1 : 0);
    // Spawn the next piece, move it by `move`, rotate it with the SRS kicks, let it fall by deltaSeconds, lock
    // it and clear lines: tests/acceptance/tetris/module/Tetris.cpp shows each step.
    static_cast<void>(move);
    static_cast<void>(deltaSeconds);
}

constexpr ModuleApi kTetris =
    makeModuleApi("tetris", "1.0.0", ModuleCallbacks{.load = &loadTetris, .update = &updateTetris});

} // namespace

ILL_MODULE(tetris, kTetris)
```

The rules go into `updateTetris`. The reference module shows one way to write them: it derives the grid from
the `tetris.Cell` entities on every update, keeps the falling piece in a `tetris.ActivePiece` with four child
blocks, and creates and destroys entities only outside its queries. The [Game Modules manual](../manual/modules.md)
and the [Scripting API](../reference/scripting-api.md) explain the rest.

Press **Ctrl+B** (**Modules → Build Modules**). The first build configures CMake with the editor's module SDK;
errors appear in the Console with their file and line. When the build succeeds, the editor loads the module
and the Tetris Components appear in **Add Component** under the category Tetris.

## 6. Put the game in the scene

Add **Tetris board** to the Board entity, then create two entities: **Queue** with a **Piece queue** (a seed,
and `forced` empty) and **Score** with a **Tetris score**. Save the scene.

## 7. Play

Press **Play** and play with the arrow keys and Space. **Pause** and **Step** advance the game one frame at a
time; **Stop** puts the scene back exactly as it was before Play.

Change the code while the game runs, for example the score of a line clear, and press **Ctrl+B** again: the
module is hot-reloaded, its Components keep their values and the game keeps running (see
[Play Mode](../manual/editor/play-mode.md#game-modules)).

## Next steps

- Compare your game with the checks of the acceptance contract (C3 to C5 in SPEC.md).
- Let an agent continue from here: [Tetris with an Agent](tetris-with-an-agent.md).
