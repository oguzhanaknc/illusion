# Bir karakteri canlandırın

Bu eğitim sahneye rig'li bir karakter koyar ve onu yürütür, koşturur ve zıplatır. Klipli bir glTF modelini içe
aktarır, kliplerin nasıl harmanlanacağını bir animasyon grafında tarif eder, karaktere bir `CharacterController`
verir ve grafın `speed` parametresini input'tan ayarlayan, bir zıplamayı tetikleyen, bir animasyon olayında ayak
sesi çalan ve root motion'ın karakteri hareket ettirmesine izin veren küçük bir C++ oyun modülü yazarsınız.

Bir illusion Release build'ine, editörün derlendiği C++ derleyicisine, klipleri `idle`, `walk`, `run` ve `jump`
adlı rig'li bir glTF modeline (herhangi bir glTF kütüphanesinden bir karakter olur; klip adları aşağıdaki grafın
kullandıklarıdır) ve kısa bir `footstep.wav` dosyasına ihtiyacınız var. Burada kullanılan her parçayı
[Animasyon kılavuzu](../manual/animation.md), modülün çağrılarını da
[Scripting API](../reference/scripting-api.md#animationservice) açıklar.

## 1. Ne yapacaksınız

- Boşta pozunda duran, kolu yarıya kadar ittiğinizde yürüyen, sonuna kadar ittiğinizde (ya da bir hareket tuşuna
  bastığınızda) koşan ve zıplama düğmesinde kısa bir cross-fade ile zıplayan bir karakter.
- Tek katmanlı bir graf dosyası `assets/animations/hero.animgraph`: `speed` parametresi üzerinde idle, walk ve
  run'ı harmanlayan bir locomotion durumu ve bir trigger'ın başlattığı bir jump durumu.
- Root motion: ayaklar kaymaz, çünkü karakteri kod değil klipler hareket ettirir. `CharacterController`
  yatay hızı animasyondan alır ve dikey hızını kendisi tutar.
- Parametreleri input action'larından yöneten ve her `footstep` olayında bir ses çalan bir modül.

## 2. Projeyi oluşturun ve modeli içe aktarın

Editörü başlatın ve **3D Starter** şablonundan `hero` klasöründe **Hero** adlı bir proje oluşturun:

```bash
build/bin/Release/illusion_editor --workspace ~/games
```

`hero.glb` dosyasını `assets/models/` içine, `footstep.wav` dosyasını `assets/audio/` içine kopyalayın. Editör
modeli içe aktarır: Asset Browser Mesh'lerini, skin'ini (`hero.glb#skin/0`) ve kliplerini
(`hero.glb#animation/idle`, `walk`, `run`, `jump`) gösterir. Bir klip eklemleri **ada göre** hareket ettirir, bu
yüzden bu eklem adlarına sahip her iskelette oynar. Grafın root motion eklemi (aşağıdaki `hips`) iskeletin bir
eklemi olmalıdır; modelinizin pelvis ekleminin adını kullanın.

## 3. Animasyon grafı

Kod editörünüzde `assets/animations/hero.animgraph` dosyasını oluşturun:

```json
{
  "format": "illusion.animgraph",
  "version": 1,
  "parameters": [
    {"name": "speed", "type": "Float"},
    {"name": "jump", "type": "Trigger"}
  ],
  "rootMotion": "hips",
  "layers": [
    {
      "name": "base",
      "entry": "locomotion",
      "states": [
        {
          "name": "locomotion",
          "blend1d": {
            "parameter": "speed",
            "points": [
              {"value": 0, "clip": "assets:/models/hero.glb#animation/idle"},
              {"value": 1.5, "clip": "assets:/models/hero.glb#animation/walk"},
              {"value": 4, "clip": "assets:/models/hero.glb#animation/run"}
            ]
          },
          "events": [
            {"name": "footstep", "time": 0.25},
            {"name": "footstep", "time": 0.75}
          ]
        },
        {
          "name": "jump",
          "clip": "assets:/models/hero.glb#animation/jump",
          "loop": false
        }
      ],
      "transitions": [
        {"from": "locomotion", "to": "jump", "duration": 0.1, "conditions": [{"parameter": "jump"}]},
        {"from": "jump", "to": "locomotion", "duration": 0.2, "exitTime": 0.9}
      ]
    }
  ]
}
```

Baştan okuyun:

- `speed` modülün ayarlayacağı bir Float'tır; `jump` bir geçiş onu kullanana kadar kurulu kalan bir Trigger'dır.
- `rootMotion`, yatay hareketi ve yaw'ı pozdan çıkıp karakteri hareket ettiren eklemi adlandırır.
- Katman `locomotion` ile başlar; bu bir **1D blend space**'tir: `speed` 0'da yalnızca `idle`, 1,5'te yalnızca
  `walk`, 4'te yalnızca `run` oynar, arada komşu klipler harmanlanır. Üç klip tek bir normalize zamanda senkron
  oynar, bu yüzden ağırlıklar değişirken adımları birlikte kalır.
- Döngünün 0,25 ve 0,75'indeki `footstep` olayları, walk ve run kliplerinizin bir ayağı bastığı anlardır;
  kliplere bakın ve zamanları uyduracak şekilde ayarlayın.
- İlk geçiş, `jump` trigger'ı kurulduğunda `locomotion`'dan `jump`'ı 0,1 sn'lik bir cross-fade ile başlatır.
  Jump durumu döngüye girmez; ikinci geçiş, zıplama %90 oynandığında (`exitTime`) 0,2 sn içinde `locomotion`'a
  döner.

Bir ajan aynı dosyayı dosya araçlarıyla yazar; dosya yüklenirken kontrol edilir ve bir hata JSON yoluyla
bildirilir (`layers[0].transitions[1].to: ...`).

## 4. Karakter

Bir zemin ve karakteri ekleyin. Editörde bir `MeshRenderer` (`builtin:/mesh/cube`), (0, -0.5, 0) konumunda
(20, 1, 20) ölçekli bir `Transform`, hareket türü Static olan bir `RigidBody` ve bir `Collider` (Box, yarı
boyutlar 0,5) içeren **Floor** Entity'sini oluşturun. Sonra bir `MeshRenderer` (Mesh
`assets:/models/hero.glb#mesh/0`), bir `CharacterController` ve bir `Animator` içeren **Hero**'yu oluşturun:

| Animator alanı | Değer |
| --- | --- |
| `skeleton` | `assets:/models/hero.glb#skin/0` |
| `graph` | `assets:/animations/hero.animgraph` |

Bir ajan için komutlar olarak:

```json
{"call": "entity.create", "params": {"name": "Floor", "components": {"illusion.Transform": {"position": [0, -0.5, 0], "scale": [20, 1, 20]}, "illusion.MeshRenderer": {"mesh": "builtin:/mesh/cube", "material": "builtin:/material/default"}, "illusion.RigidBody": {"motionType": "Static"}, "illusion.Collider": {}}}}
{"call": "entity.create", "params": {"name": "Hero", "components": {"illusion.Transform": {"position": [0, 1, 0]}, "illusion.MeshRenderer": {"mesh": "assets:/models/hero.glb#mesh/0"}, "illusion.CharacterController": {"radius": 0.3, "height": 1.8}, "illusion.Animator": {"skeleton": "assets:/models/hero.glb#skin/0", "graph": "assets:/animations/hero.animgraph"}}}}
```

Play modu dışında Scene view Hero'yu giriş durumunda (Animator'ın `time` değerinde idle) pozlar. `clip`, `speed`,
`time`, `loop` ve `playing` alanları olduğu gibi kalır: bir grafla `clip` ve `loop` kullanılmaz, `speed` ve
`playing` ise grafın ne kadar hızlı çalıştığını ve çalışıp çalışmadığını denetlemeye devam eder.

## 5. Input action'ları

Modül iki action okur. `assets/input/hero.input` dosyasını oluşturun:

```json
{
  "format": "illusion.input",
  "version": 1,
  "actions": [
    {"name": "hero.move", "type": "Vector2", "bindings": ["Keys.W.S.A.D", "Stick.Left"]},
    {"name": "hero.jump", "type": "Button", "bindings": ["Key.Space", "Gamepad.South"]}
  ]
}
```

ve Input ayarlarının action Asset'ini (**Window → Project Settings**) [Elle Tetris](tetris-by-hand.md#4-input-eylemleri)
eğitiminin yaptığı gibi `assets:/input/hero.input` olarak ayarlayın.

## 6. Modül

Komut paletinden `hero` adıyla **New module** çalıştırın ve ona kullandığı servisleri verin:

```cmake
illusion_add_module(hero SHARED SOURCES hero.cpp USES animation audio input physics runtime)
```

Modülün durumu tek bir runtime Component'tir, böylece hot reload onu korur:

```cpp
#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/animation/AnimationService.hpp>
#include <illusion/audio/AudioService.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/InputService.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsService.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <span>

namespace hero
{

using namespace illusion;

struct State
{
    ActionId move = 0;
    ActionId jump = 0;
    AudioClipHandle footstep{};
};

} // namespace hero
```

`start` iki action'ı bulur ve sesi bir kez yükler. Servisler host'a aittir, bu yüzden klip Handle'ı hot
reload'lar boyunca geçerli kalır:

```cpp
namespace
{

using namespace illusion;
using hero::State;

constexpr f32 kRunSpeed = 4.0f;  // the `speed` of the run clip in the graph
constexpr f32 kJumpSpeed = 5.0f; // m/s upwards
constexpr f32 kMinStepSpeed = 0.1f;

bool loadHero(ModuleContext&)
{
    reflectRuntime<State>("hero.State", {.displayName = "Hero state"});
    return true;
}

void startHero(ModuleContext& context)
{
    InputService* input = context.service<InputService>();
    AudioService* audio = context.service<AudioService>();
    if (input == nullptr || audio == nullptr || !context.hasWorld() || context.worldRole() != WorldRole::Simulation)
    {
        return;
    }
    const std::optional<ActionId> move = input->findAction("hero.move");
    const std::optional<ActionId> jump = input->findAction("hero.jump");
    if (!move || !jump)
    {
        return;
    }
    const AudioClipHandle footstep = audio->loadClip("assets:/audio/footstep.wav", AudioClipMode::Decode);
    context.world().create(Name{"Hero State"}, State{*move, *jump, footstep});
}
```

Her update `speed`'i move vektörünün uzunluğundan ayarlar (klavye 1 verir, bu bir koşudur; yarıya itilmiş bir
kol yürür), karakter yerdeyken bir zıplama başlatır ve son animasyon update'inin olaylarını işler.
`setFloat()` ve `setTrigger()` çağrıları bir sonraki Animation adımında etki eder:

```cpp
void updateHero(ModuleContext& context, f64)
{
    InputService* input = context.service<InputService>();
    AnimationService* animation = context.service<AnimationService>();
    PhysicsService* physics = context.service<PhysicsService>();
    AudioService* audio = context.service<AudioService>();
    if (input == nullptr || animation == nullptr || physics == nullptr || audio == nullptr || !context.hasWorld())
    {
        return;
    }
    World& world = context.world();
    State state;
    bool found = false;
    world.query<const State>().each([&](Entity, const State& value) {
        state = value;
        found = true;
    });
    Entity hero{};
    world.query<const Animator, const CharacterController>().each(
        [&hero](Entity entity, const Animator&, const CharacterController&) { hero = entity; });
    if (!found || !world.isAlive(hero))
    {
        return;
    }

    const f32 speed = glm::length(input->vector(state.move)) * kRunSpeed;
    static_cast<void>(animation->setFloat(hero, "speed", speed));
    if (input->wasPressed(state.jump) && physics->isCharacterGrounded(hero))
    {
        // Root motion owns the horizontal velocity; the jump is physics, so it sets the vertical one.
        Vec3 velocity = physics->characterVelocity(hero);
        velocity.y = kJumpSpeed;
        physics->setCharacterVelocity(hero, velocity);
        static_cast<void>(animation->setTrigger(hero, "jump"));
    }

    std::array<AnimationEvent, 16> events{};
    const u32 count = std::min<u32>(animation->pollEvents(events), static_cast<u32>(events.size()));
    for (const AnimationEvent& event : std::span(events).first(count))
    {
        // The idle pose also crosses footstep events (the state is the same), so stand still silently.
        if (event.entity == hero && event.name == "footstep" && speed > kMinStepSpeed && state.footstep.valid())
        {
            AudioPlayParams params;
            params.volume = 0.6f;
            static_cast<void>(audio->playOneShot(state.footstep, params));
        }
    }
}

constexpr ModuleApi kHero =
    makeModuleApi("hero", "1.0.0", {.load = &loadHero, .start = &startHero, .update = &updateHero});

} // namespace

ILL_MODULE(hero, kHero)
```

**Ctrl+B**'ye (**Modules → Build Modules**) basın. Build başarılı olduğunda modül yüklenir.

## 7. Deneyin

**Play**'e basın:

- Hero idle pozunda durur. **W**'ye basılı tutun: `run` klibinin root motion'ının sürdüğü şekilde ileri koşar
  ve her döngünün iki işaretli anında bir ayak sesi duyulur. Yarıya itilmiş bir kol, daha yavaş klip ve daha
  yavaş adımlarla yürür; aralarında klipler harmanlanır.
- **Space**'e basın: `jump` 0,1 sn'lik bir cross-fade ile oynar, karakter yerden kalkar ve klip bitmek üzereyken
  graf locomotion'a geri geçer.
- Oyun çalışırken `hero.animgraph`'ı değiştirin (başka bir `exitTime`, yeni bir olay zamanı): editör dosyayı
  yeniden yükler ve Hero yeni grafla yeniden başlar.

Bir ajan oyunu `play.start` ile başlatır, grafın durumunu modülün kendi mantığı ya da `play.step` ile okur ve
pozu `viewport.screenshot` ile görür; grafın parametrelerini yalnızca oyun kodu ayarlar.

## 8. Sınırlar

- Root motion karakteri kliplerin yönünde ileri hareket ettirir. Kola doğru dönmek oyun kodudur: Entity'nin
  `Transform`'unu Animation adımından önce döndürün (root motion'lı bir graf Entity'yi yalnızca klibin yaw'ı
  kadar döndürür).
- Süren bir geçiş kesilmez, bu yüzden başka bir duruma fade sırasında basılan bir zıplama kaybolur: trigger
  update'in sonunda temizlenir. Cross-fade'leri kısa tutun ya da yeniden basın.
- Bir iskeletin animasyonları farklı eklem adları arasında paylaşılmaz; klipleri bir DCC aracında retarget edin.
- Grafın canlı durumu kaydedilmez: kayıtlı bir oyunu yüklemek Hero'yu `locomotion`'da yeniden başlatır.

## Sonraki adımlar

- İki boyutlu blend space'ler (yana adım atma), maskeli katmanlar (üst gövdeyle nişan alma) ve additive
  katmanlar (nefes alma) için [Animasyon kılavuzunu](../manual/animation.md) okuyun.
- Showcase projesinin `Column`'u ve `assets/animations/column.animgraph`'ı bir trigger ve bir olay içeren bir
  graf gösterir; modülü bütün `AnimationService`'i çağırır.
- [HUD ve duraklatma menüsü](hud-and-pause-menu.md) oyuna bir puan göstergesi ve bir menü ekler.
