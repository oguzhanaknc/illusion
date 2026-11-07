# Elle Tetris

Bu eğitim, [Ajanla Tetris](tetris-with-an-agent.md) eğitimindeki oyunu editör penceresinde kendiniz kurar: bir
proje, Material'lar, bir sahne, input eylemleri ve oyun çalışırken derleyip hot reload yaptığınız bir C++ oyun
modülü. Kurallar `tests/acceptance/tetris/SPEC.md` dosyasındakilerdir; her kuralın eksiksiz uygulaması bu
eğitimin izlediği referans modül `tests/acceptance/tetris/module/Tetris.cpp` dosyasındadır.

illusion'ın bir Release Build'ine ve editörün derlendiği C++ derleyicisine ihtiyacınız var.

## 1. Projeyi oluşturun

Editörü bir proje olmadan başlatın:

```bash
build/bin/Release/illusion_editor --workspace ~/games
```

Welcome penceresi açılır. **Empty** şablonundan, `tetris` klasöründe **Tetris** adlı bir proje oluşturun.
Editör projeyi başlangıç sahnesi `assets:/scenes/main.scene` olarak açar.

## 2. Material'lar

**Assets → New Material**, Material'ın yolunu ve değerini sorar. Yedi parça rengini ve çerçeveyi oluşturun:

| Yol | Değer |
| --- | --- |
| `assets:/materials/i.material` | `{"material": {"name": "I", "baseColorFactor": [0, 1, 1, 1]}}` |
| `assets:/materials/o.material` | `{"material": {"name": "O", "baseColorFactor": [1, 1, 0, 1]}}` |
| `assets:/materials/t.material` | `{"material": {"name": "T", "baseColorFactor": [0.5, 0, 0.5, 1]}}` |
| `assets:/materials/s.material` | `{"material": {"name": "S", "baseColorFactor": [0, 1, 0, 1]}}` |
| `assets:/materials/z.material` | `{"material": {"name": "Z", "baseColorFactor": [1, 0, 0, 1]}}` |
| `assets:/materials/j.material` | `{"material": {"name": "J", "baseColorFactor": [0, 0, 1, 1]}}` |
| `assets:/materials/l.material` | `{"material": {"name": "L", "baseColorFactor": [1, 0.5, 0, 1]}}` |
| `assets:/materials/frame.material` | `{"material": {"name": "Frame", "baseColorFactor": [0.3, 0.3, 0.3, 1], "roughnessFactor": 0.8}}` |

[Asset Browser](../manual/editor/assets.md) bunları `materials` klasöründe gösterir.

## 3. Tahta sahnesi

Bu Entity'leri **Entity → Create Empty** ile oluşturun, **F2** ile yeniden adlandırın ve Component'lerini
[Inspector](../manual/editor/scene-editing.md) içindeki **Add Component** ile ekleyin:

| Entity | Component'ler |
| --- | --- |
| Camera | (4.5, 9.5, 30) konumunda Transform, 45° görüş açılı Camera |
| Sun | X ekseni etrafında −45° döndürülmüş Transform, yoğunluğu 3 olan DirectionalLight |
| Sky | Transform, `builtin:/sky/procedural` ile SkyLight, ProceduralSky |
| Board | Transform |
| Left wall, Right wall, Floor | Board'un çocukları (Outliner'da üzerine sürükleyin): (−1, 9.5, 0) konumunda (1, 22, 1) ölçekli, (10, 9.5, 0) konumunda (1, 22, 1) ölçekli ve (4.5, −1, 0) konumunda (10, 1, 1) ölçekli Transform'lar; `builtin:/mesh/cube` ve çerçeve Material'ı ile MeshRenderer'lar |

`frame.material` dosyasını Asset Browser'dan her MeshRenderer'ın Material alanına sürükleyin. Game görünümü
tahtayı kameradan gösterir. **Ctrl+S** ile kaydedin.

## 4. Input eylemleri

Oyun beş Button eylemini `assets:/input/tetris.input` dosyasından okur. Komut paletini açın
(**Ctrl+Shift+P**), **Define input action** komutunu çalıştırın ve ilk eylemi girin:

```json
{"asset": "assets:/input/tetris.input", "name": "tetris.move_left", "type": "Button", "bindings": ["Key.Left", "Gamepad.DPadLeft"]}
```

Sonra artık `tetris.input` dosyasını gösteren **Window → Input Actions** penceresini açın ve diğer dördünü alttaki
satırla ve **Add binding** ile ekleyin:

| Eylem | Bağlamalar |
| --- | --- |
| `tetris.move_right` | `Key.Right`, `Gamepad.DPadRight` |
| `tetris.rotate_cw` | `Key.Up`, `Gamepad.South` |
| `tetris.soft_drop` | `Key.Down`, `Gamepad.DPadDown` |
| `tetris.hard_drop` | `Key.Space`, `Gamepad.North` |

Son olarak projeyi bu Asset'e yönlendirin: **Window → Project Settings** içinde Input ayarlarının eylem
Asset'ini `assets:/input/tetris.input` yapın.

## 5. Oyun modülü

Komut paletinden `tetris` adıyla **New module** komutunu çalıştırın. Komut `modules/CMakeLists.txt` dosyasını
ve içinde bir `CMakeLists.txt` ile `tetris.cpp` bulunan `modules/tetris/` klasörünü yazar. Oyunun yalnızca
input, render ve runtime kütüphanelerine ihtiyacı var:

```cmake
illusion_add_module(tetris SHARED SOURCES tetris.cpp USES input render runtime)
```

`modules/tetris/tetris.cpp` dosyasını kod editörünüzde açın. Önce Component'ler gelir. Bunlar düz veridir ve
modül onları kararlı adlarla reflect eder; böylece Inspector, sahne dosyaları ve ajanlar onları kullanabilir:

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

`load` onları reflect eder; `update`, Play modunun her karesinde çalışır ve eylemleri `InputService`
üzerinden okur:

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

Kurallar `updateTetris` içine yazılır. Referans modül bunları yazmanın bir yolunu gösterir: tahtayı her
güncellemede `tetris.Cell` Entity'lerinden türetir, düşen parçayı dört çocuk bloğu olan bir
`tetris.ActivePiece` içinde tutar ve Entity'leri yalnızca Query'lerinin dışında oluşturup yok eder. Gerisini
[Oyun Modülleri kılavuzu](../manual/modules.md) ve [Scripting API](../reference/scripting-api.md) açıklar.

**Ctrl+B** (**Modules → Build Modules**) tuşlarına basın. İlk Build, CMake'i editörün modül SDK'sıyla
yapılandırır; hatalar dosyası ve satırıyla Console'da görünür. Build başarılı olduğunda editör modülü yükler ve
Tetris Component'leri **Add Component** içinde Tetris kategorisinde görünür.

## 6. Oyunu sahneye koyun

Board Entity'sine **Tetris board** ekleyin, sonra iki Entity oluşturun: **Piece queue** içeren **Queue**
(bir seed ve boş `forced`) ve **Tetris score** içeren **Score**. Sahneyi kaydedin.

## 7. Oynayın

**Play**'e basın ve ok tuşları ile Space'le oynayın. **Pause** ve **Step** oyunu kare kare ilerletir; **Stop**
sahneyi Play'den önceki hâline tam olarak geri getirir.

Oyun çalışırken kodu değiştirin, örneğin bir satır temizlemenin puanını, ve yeniden **Ctrl+B**'ye basın: modül
hot reload edilir, Component'leri değerlerini korur ve oyun çalışmaya devam eder (bkz.
[Play Modu](../manual/editor/play-mode.md#oyun-modulleri)).

## Sonraki adımlar

- Oyununuzu kabul sözleşmesinin kontrolleriyle karşılaştırın (SPEC.md'deki C3 ile C5 arası).
- Buradan sonrasını bir ajana bırakın: [Ajanla Tetris](tetris-with-an-agent.md).
