# Bir karakteri canlandırın

Bu eğitim sahneye rig'li bir karakter koyar ve onu yürütür, koşturur ve zıplatır. Klipli bir glTF modelini içe
aktarır, kliplerin nasıl harmanlanacağını bir animasyon grafında tarif eder, karaktere bir `CharacterController`
verir ve grafın `speed` parametresini input'tan ayarlayan, bir zıplamayı tetikleyen, bir animasyon olayında ayak
sesi çalan ve root motion'ın karakteri hareket ettirmesine izin veren küçük bir C++ oyun modülü yazarsınız.
Sonra ele bir fener asar, başı bir ziyaretçiye çevirir, ayakları bir basamakta tutar ve karakteri ragdoll gibi
düşürürsünüz.

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
- Sağ elde bir fener (bir socket), bir ziyaretçiyi izleyen bir baş (look-at), bir basamakta duran ayaklar
  (foot placement) ve karakteri düşürüp ayağa kaldıran bir tuş (bir ragdoll).

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

Modül üç action okur; sonuncusu 10. adımda kullanılır. `assets/input/hero.input` dosyasını oluşturun:

```json
{
  "format": "illusion.input",
  "version": 1,
  "actions": [
    {"name": "hero.move", "type": "Vector2", "bindings": ["Keys.W.S.A.D", "Stick.Left"]},
    {"name": "hero.jump", "type": "Button", "bindings": ["Key.Space", "Gamepad.South"]},
    {"name": "hero.fall", "type": "Button", "bindings": ["Key.R", "Gamepad.East"]}
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
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/InputService.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsService.hpp>
#include <illusion/physics/Ragdoll.hpp>

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
    ActionId fall = 0;
    bool fallen = false;
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
    const std::optional<ActionId> fall = input->findAction("hero.fall");
    if (!move || !jump || !fall)
    {
        return;
    }
    const AudioClipHandle footstep = audio->loadClip("assets:/audio/footstep.wav", AudioClipMode::Decode);
    context.world().create(Name{"Hero State"}, State{*move, *jump, footstep, *fall, false});
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

} // namespace
```

Modül henüz bitmedi: 7'den 10'a kadar olan adımlar bir fener, bir bakış, foot placement ve bir ragdoll ekler;
11. adım modülü kaydeder ve derler.

## 7. Elde bir fener

Bir **socket** bir Entity'yi bir eklemde asılı tutur. Feneri Hero'nun çocuğu yapın ve ona bir `BoneAttachment`
verin; Animation adımı her update'ten sonra transform'unu eklemin pozundan ve ofsetten yazar:

```json
{"call": "entity.create", "params": {"name": "Lantern", "parent": "/Hero", "components": {"illusion.MeshRenderer": {"mesh": "builtin:/mesh/sphere", "material": "builtin:/material/default"}, "illusion.BoneAttachment": {"joint": "hand_r", "translation": [0, 0.05, 0.1], "scale": [0.15, 0.15, 0.15]}}}}
```

`joint` için modelinizin sağ el ekleminin adını kullanın. `translation` eklemin kendi z ekseni boyunca 10 cm ve
y boyunca 5 cm'dir, böylece fener avucta asılı durur; `scale` küreyi 15 cm genişliğinde yapar.
`BoneAttachment` fenerin `Transform`'una sahiptir, bu yüzden boyut Transform'da değil burada ayarlanır. Feneri
Scene görünümünde idle pozunda görmelisiniz; Play sırasında elle birlikte sallanır.

## 8. Takip eden bir baş

Bir `LookAt`, animasyondan sonra baş eklemini ve altındaki birkaç eklemi bir hedefe çevirir; en büyük bir
açıya kadar ve sınırlı bir hızla. Yukarıdaki komutlar (3, 1.6, 4) konumuna bir **Visitor** küresi ve Hero'ya
başı ve boynu en fazla 1,2 rad (yaklaşık 70°) ve 5 rad/s ile çeviren bir `LookAt` ekler. Inspector'da `target`
alanını Visitor'ın Entity'sine ayarlamak yeter. Bir modül ise başı `AnimationService::setLookAtTarget()` ile
herhangi bir noktaya çevirebilir; bu çağrı `target`'ı temizler. Bu fonksiyon Visitor'ın konumunu izler, yani
Visitor'ı hareket ettiren bir oyun bakışı da hareket ettirir:

```json
{"call": "entity.create", "params": {"name": "Visitor", "components": {"illusion.Transform": {"position": [3, 1.6, 4]}, "illusion.MeshRenderer": {"mesh": "builtin:/mesh/sphere", "material": "builtin:/material/default"}}}}
{"call": "component.add", "params": {"entity": "/Hero", "type": "illusion.LookAt", "value": {"joint": "head", "chainLength": 2, "maxAngle": 1.2, "speed": 5}}}
```

```cpp
namespace
{

void updateGaze(ModuleContext& context)
{
    AnimationService* animation = context.service<AnimationService>();
    if (animation == nullptr || !context.hasWorld())
    {
        return;
    }
    World& world = context.world();
    Entity hero{};
    world.query<const Animator, const LookAt>().each(
        [&hero](Entity entity, const Animator&, const LookAt&) { hero = entity; });
    std::optional<Vec3> visitor;
    world.query<const Name, const Transform>().each([&visitor](Entity, const Name& name, const Transform& transform) {
        if (name.value == "Visitor")
        {
            visitor = transform.position;
        }
    });
    if (visitor && world.isAlive(hero))
    {
        static_cast<void>(animation->setLookAtTarget(hero, *visitor));
    }
}

} // namespace
```

`updateGaze`, modülün update'inden 11. adımda çağrılır. Oyun çalışırken Visitor'ı Scene görünümünde hareket
ettirin: Hero'nun başı onu yalnızca `maxAngle`'a kadar izler ve Visitor karakterin arkasına geçince animasyonun
yönüne döner.

## 9. Engebeli zeminde ayaklar

`FootPlacement` her Animation adımından önce her ayağın altına bir ray atar, ayağı zemine kaldırır ya da
indirir ve pelvis'i en alçak ayağa göre aşağı çeker. Ayaklar Animator'ın `TwoBoneIk` uzuvlarıdır; ilki
Hero'da, diğerleri çocuklarında durabilir. İki bacağı, foot placement'ı ve üzerinden geçilecek alçak bir
basamağı ekleyin:

```json
{"call": "entity.create", "params": {"name": "Foot Target Left", "parent": "/Hero", "components": {"illusion.TwoBoneIk": {"joint": "foot_l", "polePoint": [0, 0.5, 1]}}}}
{"call": "entity.create", "params": {"name": "Foot Target Right", "parent": "/Hero", "components": {"illusion.TwoBoneIk": {"joint": "foot_r", "polePoint": [0, 0.5, 1]}}}}
{"call": "component.add", "params": {"entity": "/Hero", "type": "illusion.FootPlacement", "value": {"pelvis": "hips", "rayLength": 1.6, "footHeight": 0.08, "maxStep": 0.4, "speed": 10, "layerMask": 1}}}
{"call": "entity.create", "params": {"name": "Step", "components": {"illusion.Transform": {"position": [2, 0.15, 0], "scale": [2, 0.3, 2]}, "illusion.MeshRenderer": {"mesh": "builtin:/mesh/cube", "material": "builtin:/material/default"}, "illusion.RigidBody": {"motionType": "Static"}, "illusion.Collider": {"halfExtents": [0.5, 0.5, 0.5]}}}}
```

`polePoint`, dizin hangi yöne büküleceğidir, Hero'nun kendi uzayında: ileri ve biraz yukarı. Zemin ray'leri kalçanın
1,6 m altına uzanır ve en fazla 0,4 m düzeltir (basamak 0,3 m yüksektir). `layerMask` 1 zeminin physics
katmanıdır ve üzerindeki **Step** gövdesi statik bir `RigidBody`'dir. Hero'yu basamağın üzerinden koşturun:
bacaklar zemine gömülmek yerine ayaklar basamağın üstünde kalır ve kalça iner. Play modu dışındaki editörde
ayaklar animasyonlu pozu korur, çünkü ray'ler oyun oturumunda çalışır.

## 10. Düşün

Animator'ın Entity'sindeki bir `Ragdoll`, `active` olduğu sürece animasyonlu karakteri bir physics ragdoll'una
çevirir. Bir `profile` olmadan oturum iskeletten kapsüller üretir:

```json
{"call": "component.add", "params": {"entity": "/Hero", "type": "illusion.Ragdoll", "value": {"mass": 70, "blendSeconds": 0.3}}}
```

Modülün son fonksiyonunu ekleyin; `hero.fall` action'ında ragdoll'u açıp kapatır ve hangi yöne gittiğini
modülün `State`'inde hatırlar:

```cpp
namespace
{

void updateFall(ModuleContext& context)
{
    InputService* input = context.service<InputService>();
    AnimationService* animation = context.service<AnimationService>();
    if (input == nullptr || animation == nullptr || !context.hasWorld())
    {
        return;
    }
    World& world = context.world();
    Entity hero{};
    world.query<const Animator, const Ragdoll>().each(
        [&hero](Entity entity, const Animator&, const Ragdoll&) { hero = entity; });
    if (!world.isAlive(hero))
    {
        return;
    }
    world.query<State>().each([&](Entity, State& state) {
        if (input->wasPressed(state.fall))
        {
            const bool fall = !state.fallen;
            if (animation->setRagdoll(hero, fall).hasValue())
            {
                state.fallen = fall;
            }
        }
    });
}

} // namespace
```

`setRagdoll()` `Ragdoll::active`'i yazar. Karakter mevcut pozundan ve hızından, **blendSeconds** kadar bir
harmanlamayla düşer; ikinci bir basış pozu aynı sürede animasyona geri harmanlar. Fener bir socket olarak
elle birlikte düşer.

## 11. Derleyin ve deneyin

Modül yeni fonksiyonları update'inden çağırır. Modülü onunla kaydedin:

```cpp
namespace
{

void updateAll(ModuleContext& context, f64 deltaSeconds)
{
    updateHero(context, deltaSeconds);
    updateGaze(context);
    updateFall(context);
}

constexpr ModuleApi kHero =
    makeModuleApi("hero", "1.0.0", {.load = &loadHero, .start = &startHero, .update = &updateAll});

} // namespace

ILL_MODULE(hero, kHero)
```

**Ctrl+B**'ye (**Modules → Build Modules**) basın. Build başarılı olduğunda modül yüklenir.

**Play**'e basın:

- Hero elinde fenerle idle pozunda durur. **W**'ye basılı tutun: `run` klibinin root motion'ının sürdüğü şekilde
  ileri koşar ve her döngünün iki işaretli anında bir ayak sesi duyulur. Yarıya itilmiş bir kol, daha yavaş klip ve
  daha yavaş adımlarla yürür; aralarında klipler harmanlanır.
- **Space**'e basın: `jump` 0,1 sn'lik bir cross-fade ile oynar, karakter yerden kalkar ve klip bitmek üzereyken
  graf locomotion'a geri geçer.
- Baş, Hero'nun önünde olduğu sürece Visitor'a döner ve ayaklar basamağı izler.
- **R**'ye basın: Hero elindeki fenerle yığılır. **R**'ye yeniden basın: Entity'sinin durduğu yerde animasyona
  geri harmanlanır (ragdoll Entity'yi hareket ettirmez; karakterin yattığı yerde ayağa kalkmayı
  [kılavuz](../manual/animation.md#ragdoll-calsrken-askya-alnanlar) gösterir).
- Oyun çalışırken `hero.animgraph`'ı değiştirin (başka bir `exitTime`, yeni bir olay zamanı): editör dosyayı
  yeniden yükler ve Hero yeni grafla yeniden başlar.

Bir ajan oyunu `play.start` ile başlatır, grafın durumunu modülün kendi mantığı ya da `play.step` ile okur ve
pozu `viewport.screenshot` ile görür; grafın parametrelerini yalnızca oyun kodu ayarlar.

## 12. Sınırlar

- Root motion karakteri kliplerin yönünde ileri hareket ettirir. Kola doğru dönmek oyun kodudur: Entity'nin
  `Transform`'unu Animation adımından önce döndürün (root motion'lı bir graf Entity'yi yalnızca klibin yaw'ı
  kadar döndürür).
- Süren bir geçiş kesilmez, bu yüzden başka bir duruma fade sırasında basılan bir zıplama kaybolur: trigger
  update'in sonunda temizlenir. Cross-fade'leri kısa tutun ya da yeniden basın.
- Bir iskeletin animasyonları farklı eklem adları arasında paylaşılmaz; klipleri bir DCC aracında retarget edin.
- Grafın canlı durumu kaydedilmez: kayıtlı bir oyunu yüklemek Hero'yu `locomotion`'da yeniden başlatır.
- Bir ragdoll yalnızca düşer: motoru yoktur ve root motion'ı ile `CharacterController`'ı askıya alır.
  Foot placement ve look-at simüle edilen bir karakterde görünmez.

## Sonraki adımlar

- İki boyutlu blend space'ler (yana adım atma), maskeli katmanlar (üst gövdeyle nişan alma) ve additive
  katmanlar (nefes alma) için [Animasyon kılavuzunu](../manual/animation.md) okuyun.
- Showcase projesinin `Column`'u ve `assets/animations/column.animgraph`'ı bir trigger ve bir olay içeren bir
  graf gösterir; modülü bütün `AnimationService`'i çağırır.
- [HUD ve duraklatma menüsü](hud-and-pause-menu.md) oyuna bir puan göstergesi ve bir menü ekler.
