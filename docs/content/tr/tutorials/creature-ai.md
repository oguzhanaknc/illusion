# Dolaşan ve kovalayan bir yaratık

Bu eğitim sahneye kendi başına dolaşan, oyuncuyu gördüğünde ya da duyduğunda fark eden, kovalayıp ısıran ve
tekrar dolaşmaya başlamadan önce oyuncuyu en son gördüğü yeri arayan bir yaratık koyar. Bir navmesh bake eder,
bir behaviour tree yazar, yaratığa bir `NavAgent`, bir `AiPerception` ve bir `BehaviorAgent` verir ve oyuncuyu
hareket ettiren, bağırdığınızda bir gürültü çıkaran ve yaratığın `bite` action'ına cevap veren küçük bir C++ oyun
modülü yazarsınız.

Bir illusion Release build'ine ve editörün derlendiği C++ derleyicisine ihtiyacınız var. Ağacı, blackboard'u ve
perception'ı [AI kılavuzu](../manual/ai.md), navmesh'i [Navigasyon kılavuzu](../manual/navigation.md), modülün
çağrılarını da [Scripting API](../reference/scripting-api.md#aiservice) açıklar.

## 1. Ne yapacaksınız

- Navmesh'li bir zemin, durduğu yerin 10 m çevresinde dolaşan ve yürüyüşler arasında biraz bekleyen bir
  **Creature** ve klavyeyle hareket ettirdiğiniz bir **Player**.
- Yaratık, önündeki 14 m içindeki oyuncuyu görür (120 derecelik bir koni; duvarlar engeller) ve bağırışınızın
  gürültüsünü duyar. Sonra oyuncuya koşar ve ısırır.
- Oyuncuyu kaybettiğinde onu en son gördüğü yere yürür, orada bekler ve tekrar dolaşmaya döner.
- Öncelik sırasıyla üç davranışlı bir ağaç dosyası `assets/ai/creature.behavior`: chase, search, wander.

## 2. Projeyi oluşturun

Editörü başlatın ve **3D Starter** şablonundan `creature` klasöründe **Creature** adlı bir proje oluşturun:

```bash
build/bin/Release/illusion_editor --workspace ~/games
```

## 3. Behaviour tree

Kod editörünüzde `assets/ai/creature.behavior` dosyasını oluşturun:

```json
{
  "format": "illusion.behavior",
  "version": 1,
  "blackboard": [
    {"name": "target", "type": "Entity"},
    {"name": "lastSeen", "type": "Vector3"}
  ],
  "root": {"name": "creature", "selector": [
    {"name": "chase", "guard": {"key": "target", "compare": "IsSet", "abort": "LowerPriority",
      "child": {"sequence": [
        {"moveTo": {"key": "target", "acceptance": 1.2}},
        {"timeLimit": {"seconds": 2, "child": {"action": "bite"}}},
        {"wait": {"seconds": 1}}
      ]}}},
    {"name": "search", "guard": {"key": "lastSeen", "compare": "IsSet", "abort": "Self",
      "child": {"sequence": [
        {"moveTo": {"key": "lastSeen", "acceptance": 1}},
        {"wait": {"seconds": 3}},
        {"setValue": {"key": "lastSeen", "value": null}}
      ]}}},
    {"name": "wander", "sequence": [
      {"wander": {"radius": 10}},
      {"wait": {"seconds": 2, "deviation": 1}}
    ]}
  ]}
}
```

Baştan okuyun:

- Blackboard'un iki key'i var. `target` bir Entity key'i, `lastSeen` bir Vector3 key'idir; perception onları
  ayarlayana kadar ikisinin de değeri yoktur.
- Kök bir `selector`'dür: çocuklarını sırayla, biri başarılı olana ya da çalışmaya devam edene kadar dener; yani
  sıra önceliktir.
- **chase**, `target` üzerinde bir `guard`'dır. `target` ayarlıyken oyuncuya doğru `moveTo` çalıştırır (yaratık 1,2
  m içine girene kadar), sonra `bite` adlı `action`'ı en fazla 2 saniye (`timeLimit`) çalıştırır, sonra bir
  saniye bekler. `abort: LowerPriority`, `target` ayarlandığı anda guard'ın çalışan alt öncelikli çocuğu
  (dolaşmayı) kesmesini sağlar.
- **search**, `lastSeen` üzerinde bir `guard`'dır. O noktaya yürür, 3 saniye bekler, sonra `setValue` ve `null` ile
  key'i temizler. `abort: Self`, bu arada `lastSeen` temizlenirse aramayı hemen bitirir.
- **wander** yedek davranıştır: yaratığın ilk konumunun 10 m çevresinde navmesh üzerinde rastgele bir nokta, 2
  saniye artı eksi 1 saniyelik bir bekleme ve sonsuza dek tekrar.

Bir ajan aynı dosyayı dosya araçlarıyla yazar ve `ai.validate` Editor commands'ıyla denetler; bu komut ilk
hatanın JSON yolunu söyler (`root.selector[0].guard.key: ...`):

```json
{"call": "ai.validate", "params": {"path": "assets:/ai/creature.behavior"}}
```

## 4. Sahne

Bir zemin, oyuncu ve yaratık ekleyin. Önce zemin ve bir `NavMeshSurface`'li **Navigation** Entity'si gelir; surface
zeminin `Collider`'ını bir navmesh'e toplar:

```json
{"call": "entity.create", "params": {"name": "Floor", "components": {"illusion.Transform": {"position": [0, -0.5, 0], "scale": [24, 1, 24]}, "illusion.MeshRenderer": {"mesh": "builtin:/mesh/cube", "material": "builtin:/material/default"}, "illusion.RigidBody": {"motionType": "Static"}, "illusion.Collider": {}}}}
{"call": "entity.create", "params": {"name": "Navigation", "components": {"illusion.Transform": {"position": [0, 1, 0]}, "illusion.NavMeshSurface": {"size": [24, 4, 24]}}}}
{"call": "nav.bake", "params": {}}
```

`nav.bake`, `assets/navigation/navigation.navmesh` dosyasını yazar ve surface'in `data` alanını ona ayarlar. Sonra
iki karakter:

```json
{"call": "entity.create", "params": {"name": "Creature", "components": {"illusion.Transform": {"position": [8, 0.5, 8], "scale": [0.8, 1, 0.8]}, "illusion.MeshRenderer": {"mesh": "builtin:/mesh/cube", "material": "builtin:/material/default"}, "illusion.NavAgent": {"maxSpeed": 3.5, "radius": 0.4}, "illusion.AiPerception": {"sightRadius": 14, "eyeHeight": 0.8, "memorySeconds": 4, "targetKey": "target", "positionKey": "lastSeen"}, "illusion.BehaviorAgent": {"tree": "assets:/ai/creature.behavior"}}}}
{"call": "entity.create", "params": {"name": "Player", "components": {"illusion.Transform": {"position": [-8, 1, -8]}, "illusion.MeshRenderer": {"mesh": "builtin:/mesh/cube", "material": "builtin:/material/default"}, "illusion.CharacterController": {"radius": 0.3, "height": 1.8}, "illusion.AiStimulus": {"offset": [0, 0.9, 0]}}}}
```

- `NavAgent` navmesh üzerinde yürür ve yaratığın kendi konumunu yönlendirir; `BehaviorAgent` ağacı çalıştırır ve
  ona hedefler verir.
- `AiPerception` yaratığın +z ekseni boyunca görür. `targetKey` ve `positionKey`, perception'ın yazdığı
  blackboard key'lerini adlandırır: seçilen hedef ve onun son bilinen konumu. Artık algılamadığını
  `memorySeconds` sonra unutur; hedef key'i o zaman temizlenir, `lastSeen` konumu tutar.
- `AiStimulus`, Player'ı algılayıcıların fark ettiği bir şey yapar; `offset` baktıkları noktadır.

## 5. Input action'ları

Modül iki action okur. `assets/input/creature.input` dosyasını oluşturun:

```json
{
  "format": "illusion.input",
  "version": 1,
  "actions": [
    {"name": "creature.move", "type": "Vector2", "bindings": ["Keys.W.S.A.D", "Stick.Left"]},
    {"name": "creature.shout", "type": "Button", "bindings": ["Key.Space", "Gamepad.South"]}
  ]
}
```

ve Input ayarlarının action asset'ini (**Window → Project Settings**), [Elle Tetris](tetris-by-hand.md#4-input-eylemleri)
eğitiminde yaptığınız gibi `assets:/input/creature.input` olarak ayarlayın.

## 6. Modül

Komut paletinden `creature` adıyla **New module** çalıştırın ve ona kullandığı servisleri verin:

```cmake
illusion_add_module(creature SHARED SOURCES creature.cpp USES ai input physics runtime)
```

Modülün durumu tek bir runtime Component'tir; böylece hot reload onu korur:

```cpp
#include <illusion/ai/AiService.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/InputService.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsService.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <span>

namespace creature
{

using namespace illusion;

struct State
{
    ActionId move = 0;
    ActionId shout = 0;
    u32 bites = 0;
};

} // namespace creature
```

`start` iki action'ı bulur ve durumu oluşturur:

```cpp
namespace
{

using namespace illusion;
using creature::State;

constexpr f32 kPlayerSpeed = 5.0f; // m/s
constexpr f32 kShoutRadius = 15.0f; // m at which a listener with hearing 1 hears the shout

bool loadCreature(ModuleContext&)
{
    reflectRuntime<State>("creature.State", {.displayName = "Creature state"});
    return true;
}

void startCreature(ModuleContext& context)
{
    InputService* input = context.service<InputService>();
    if (input == nullptr || !context.hasWorld() || context.worldRole() != WorldRole::Simulation)
    {
        return;
    }
    const std::optional<ActionId> move = input->findAction("creature.move");
    const std::optional<ActionId> shout = input->findAction("creature.shout");
    if (!move || !shout)
    {
        return;
    }
    context.world().create(Name{"Creature State"}, State{*move, *shout, 0});
}
```

Her update oyuncuyu hareket ettirir, bağırdığınızda bir gürültü bildirir ve ağacın başlattığı action'lara cevap
verir. Ağacın bir `action` node'u oyun onu `finishAction()` ile bitirene kadar çalışır; burada ısırık hemen
başarılı olur ve sayılır:

```cpp
void updateCreature(ModuleContext& context, f64)
{
    AiService* ai = context.service<AiService>();
    InputService* input = context.service<InputService>();
    PhysicsService* physics = context.service<PhysicsService>();
    if (ai == nullptr || input == nullptr || physics == nullptr || !context.hasWorld())
    {
        return;
    }
    World& world = context.world();
    State* state = nullptr;
    world.query<State>().each([&state](Entity, State& value) { state = &value; });
    Entity player{};
    world.query<const Name, const CharacterController>().each(
        [&player](Entity entity, const Name& name, const CharacterController&) {
            if (name.value == "Player")
            {
                player = entity;
            }
        });
    if (state == nullptr || !world.isAlive(player))
    {
        return;
    }

    const Vec2 move = input->vector(state->move);
    Vec3 velocity = physics->characterVelocity(player);
    velocity.x = move.x * kPlayerSpeed;
    velocity.z = -move.y * kPlayerSpeed;
    physics->setCharacterVelocity(player, velocity);

    if (input->wasPressed(state->shout))
    {
        AiNoise noise;
        noise.position = world.get<Transform>(player).position;
        noise.radius = kShoutRadius;
        noise.instigator = player;
        static_cast<void>(ai->reportNoise(noise));
    }

    std::array<AiActionRequest, 16> actions{};
    const u32 count = std::min<u32>(ai->pollActions(actions), static_cast<u32>(actions.size()));
    for (const AiActionRequest& request : std::span(actions).first(count))
    {
        if (request.phase == AiActionPhase::Started && request.name == "bite")
        {
            ++state->bites;
            static_cast<void>(ai->finishAction(request.entity, request.action, true));
        }
    }
}

constexpr ModuleApi kCreature = makeModuleApi(
    "creature", "1.0.0", {.load = &loadCreature, .start = &startCreature, .update = &updateCreature});

} // namespace

ILL_MODULE(creature, kCreature)
```

**Ctrl+B**'ye (**Modules → Build Modules**) basın. Build başarılı olunca modül yüklenir.

## 7. Deneyin

**Play**'e basın:

- Yaratık dolaşır: başladığı yerin yakınında rastgele bir noktaya yürür, durur ve yürümeye devam eder.
- Oyuncuyu **W A S D** ile 14 m içinde onun önüne yürütün: dolaşmayı bırakır, koşar ve ısırır. Bir duvarın
  arkasına saklanın ya da sırtınızı dönün; dört saniye sonra sizi kaybeder, sizi en son gördüğü yere gider ve tekrar
  dolaşmadan önce orada bekler.
- Görüş alanının dışında durun ve **Space**'e basın: bağırış 15 m içinde, yaratığın arkasında ya da bir duvarın
  ardında bile duyulur ve yaratık gürültünün olduğu yere gelir.

**Scene** view'da **Visualisers** menüsünü açın ve yaratığın görüş konisini, gördüğü şeye bir çizgiyi, bir şeyi
hatırladığı yerde bir işareti ve her gürültü için bir daireyi görmek için **Perception**'ı açın. Bir ajan bunun
yerine editöre sorar:

```json
{"call": "ai.status", "params": {"entity": "/Creature"}}
{"call": "ai.setValue", "params": {"entity": "/Creature", "key": "lastSeen", "value": [0, 0, 0]}}
{"call": "ai.noise", "params": {"position": [-8, 0, -8], "radius": 30}}
```

`ai.status` çalışan node'u (`chase`, `search` ya da `wander`), iki key'i ve yaratığın algıladıklarını listeler;
`ai.setValue` bir key yazar (yaratık orijine yürür ve orada arar); `ai.noise` herhangi bir yerde bir gürültü
çıkarır. Üçü de çalışan bir play oturumu (`play.start`) ister.

Oyun çalışırken `creature.behavior` dosyasını değiştirin, örneğin `wander`'ın `radius` değerini: editör dosyayı
yeniden yükler ve yaratık yeni ağaçla yeniden başlar.

## 8. Sınırlar

- Ağaç karar verir; modül yalnızca action'lara cevap verir. Buradaki bir ısırık yalnızca sayılır: hasar, bir
  animasyon ya da bir ses modüle aittir; modül bunlar bittiğinde action'ı bitirir.
- Yaratık yalnızca bake edilmiş navmesh üzerinde yürür. Zemini taşıdıktan sonra yeniden bake edin (`nav.bake` yalnızca
  gereken tile'ları değiştirir) ya da oyun çalışırken surface'in `rebuildAtRuntime` alanının yapmasına izin verin.
- Görüş, yaratığın gözünden (`eyeHeight`) oyuncunun `offset`'ine kadar açık bir çizgi ister; yalnızca perception'ın
  `occluderMask` alanındaki fizik katmanları onu engeller.
- Bir ağacın canlı durumu kaydedilmez: kaydedilmiş bir oyunu yüklemek yaratığın ağacını blackboard'un
  varsayılanlarıyla yeniden başlatır.

## Sonraki adımlar

- Her node, abort modları ve perception'ın takım filtresi için [AI kılavuzunu](../manual/ai.md) okuyun.
- Showcase projesinin modülü `AiService`'in tamamını çağırır; guard ağacı bunun gibi `LowerPriority` kullanır.
- [Bir karakteri canlandırın](animated-character.md) yaratığa bacak verir: bir `NavAgent`'in `speedParameter`'ı bir
  animasyon grafının hızını besler.
