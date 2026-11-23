# Üçüncü şahıs kamerası

Bu eğitim bir oyuncuya üçüncü şahıs kamerası verir: omzun arkasında, çubuğun ya da klavyenin döndürdüğü, duvarların
kısalttığı ve oyuncuyu ekranın merkezinden biraz uzakta tutan bir yaylı kol. Bir düğme omzu değiştirir, bir
başkası beynin geçiş yaptığı ikinci, daha yakın bir sanal kamerayla nişan aldırır ve bir vuruş ekranı sarsar.
Kameraları sahnede tarif eder, input action'larını bağlar ve `CameraService` ile omuz değiştiren, nişan alan
ve sarsan küçük bir C++ oyun modülü yazarsınız.

illusion'ın bir Release build'ine ve editörün derlendiği C++ derleyicisine ihtiyacınız var.
[Kameralar kılavuzu](../manual/cameras.md) aşamaları, beyni ve geçişleri,
[Scripting API](../reference/scripting-api.md#cameraservice) modülün çağrılarını anlatır.

## 1. Ne yapıyorsunuz

- Bir zemin, **W A S D** (ya da sol çubuk) ile hareket ettirdiğiniz bir **Player** ve arkasına saklanacak bir duvar.
- Bir **Explore** sanal kamerası: sağ omuz üzerinde, **I K J L** (ya da sağ çubuk) ile döndürülen bir yaylı kol ve
  oyuncuyu merkezin soluna yerleştiren bir composer.
- Aynı oyuncu üzerinde daha kısa kollu ve daha dar lensli bir **Aim** sanal kamerası. **E** tuşuna basılı tutmak
  onu Explore'un üstüne çıkarır; beyin ona çeyrek saniyede geçer ve bıraktığınızda geri döner.
- **Q** iki kolun omzunu da değiştirir. **F** bir vuruştur: bir `CameraImpulse` yakınındaki her kamerayı sarsar.

## 2. Projeyi oluşturun

Editörü başlatın ve **3D Starter** şablonundan `shoulder` klasöründe **Shoulder** adlı bir proje oluşturun:

```bash
build/bin/Release/illusion_editor --workspace ~/games
```

## 3. Sahne

Önce zemin, bir duvar, oyuncu ve gerçek `Camera` üzerindeki beyin gelir. `CameraBrain` varsayılan olarak 0,5 sn
boyunca geçiş yapar:

```json
{"call": "entity.create", "params": {"name": "Floor", "components": {"illusion.Transform": {"position": [0, -0.5, 0], "scale": [40, 1, 40]}, "illusion.MeshRenderer": {"mesh": "builtin:/mesh/cube", "material": "builtin:/material/default"}, "illusion.RigidBody": {"motionType": "Static"}, "illusion.Collider": {}}}}
{"call": "entity.create", "params": {"name": "Wall", "components": {"illusion.Transform": {"position": [4, 1.5, 0], "scale": [0.5, 3, 8]}, "illusion.MeshRenderer": {"mesh": "builtin:/mesh/cube", "material": "builtin:/material/default"}, "illusion.RigidBody": {"motionType": "Static"}, "illusion.Collider": {}}}}
{"call": "entity.create", "params": {"name": "Player", "components": {"illusion.Transform": {"position": [0, 1, 0]}, "illusion.MeshRenderer": {"mesh": "builtin:/mesh/cube", "material": "builtin:/material/default"}, "illusion.CharacterController": {"radius": 0.3, "height": 1.8}}}}
{"call": "entity.create", "params": {"name": "Camera", "components": {"illusion.Transform": {"position": [0, 3, 6]}, "illusion.Camera": {}, "illusion.CameraBrain": {"defaultBlend": {"style": "EaseInOut", "duration": 0.5}}}}}
```

İki sanal kamera oyuncuyu izler ve ona bakar; bu yüzden yukarıdaki `entity.create` sonucundan oyuncunun `id`
değeri gerekir (`1f3a9c52e0b7d468` burada onun yerine durur):

```json
{"call": "entity.create", "params": {"name": "Explore", "components": {"illusion.VirtualCamera": {"priority": 10, "follow": "1f3a9c52e0b7d468", "lookAt": "1f3a9c52e0b7d468", "fovY": 1.0472}, "illusion.CameraSpringArm": {"shoulderOffset": [0.6, 1.6, 0], "armLength": 3.5, "collisionRadius": 0.25, "pullInSpeed": 20, "recoverySpeed": 4, "alignToTarget": false, "lookAction": "camera.look", "lookSpeed": [2.5, 1.5]}, "illusion.CameraComposer": {"targetOffset": [0, 1.4, 0], "screenPosition": [-0.2, 0]}}}}
{"call": "entity.create", "params": {"name": "Aim", "components": {"illusion.VirtualCamera": {"priority": 5, "follow": "1f3a9c52e0b7d468", "lookAt": "1f3a9c52e0b7d468", "fovY": 0.7, "customBlend": true, "blendIn": {"style": "EaseInOut", "duration": 0.25}}, "illusion.CameraSpringArm": {"shoulderOffset": [0.5, 1.6, 0], "armLength": 1.8, "collisionRadius": 0.2, "pullInSpeed": 20, "recoverySpeed": 4, "alignToTarget": false, "lookAction": "camera.look", "lookSpeed": [1.5, 1]}, "illusion.CameraComposer": {"targetOffset": [0, 1.4, 0], "screenPosition": [-0.3, 0]}}}}
{"call": "entity.create", "params": {"name": "Hit", "components": {"illusion.Transform": {"position": [0, 1, 0]}, "illusion.CameraImpulse": {"strength": 0.8, "radius": 20, "duration": 0.5, "playOnStart": false}}}}
```

- Explore'un `priority` değeri yüksektir; beyin onu gösterir. Aim, modül onu yükseltene kadar 5'te bekler.
- Bir yaylı kol, omuzdan fizik katmanlarına karşı `collisionRadius` yarıçaplı bir küre atar ve önünde duvar varsa
  kısalır (`pullInSpeed` saniyede metre, `20` neredeyse anında) ve geri uzar (`recoverySpeed`). Küre, izlenen
  hedefin kendi gövdelerinden geçer.
- `alignToTarget` kapalıyken kolun yaw'ı dünyanındır; böylece oyuncu ekranda yürürken kamera savrulmaz.
- `customBlend` ve `blendIn`, Aim'e geçişi beynin varsayılanından hızlı yapar. Explore'a dönüş beynin 0,5 sn'sini
  kullanır.
- `Hit` Entity'si başlangıçta tetiklenmez (`playOnStart`); modül onu oynatır.

## 4. Input action'ları

Modül dört action okur, kameralar da biri olan `camera.look`'u adıyla okur. `assets/input/shoulder.input` dosyasını
oluşturun:

```json
{
  "format": "illusion.input",
  "version": 1,
  "actions": [
    {"name": "shoulder.move", "type": "Vector2", "bindings": ["Keys.W.S.A.D", "Stick.Left"]},
    {"name": "camera.look", "type": "Vector2", "bindings": ["Keys.I.K.J.L", "Stick.Right"]},
    {"name": "camera.swap", "type": "Button", "bindings": ["Key.Q", "Gamepad.West"]},
    {"name": "camera.aim", "type": "Button", "bindings": ["Key.E", "Gamepad.North"]},
    {"name": "camera.hit", "type": "Button", "bindings": ["Key.F", "Gamepad.East"]}
  ]
}
```

ve Input ayarlarının actions asset'ini (**Window → Project Settings**) [Elle Tetris](tetris-by-hand.md#4-input-eylemleri)
eğitiminde olduğu gibi `assets:/input/shoulder.input` yapın. Yaylı kol tam sapmada saniyede `lookSpeed` radyan
döner; bu yüzden çubuk doğru hissettirir, tuşlar ise onu bu tam hızda döndürür. Fareyle döndürmek için modülde bir
`Delta` action okuyun ve `CameraService::setAngles()` çağırın.

## 5. Modül

Komut paletinden `shoulder` adıyla **New module** çalıştırın ve kullandığı servisleri verin:

```cmake
illusion_add_module(shoulder SHARED SOURCES shoulder.cpp USES camera input physics runtime)
```

Modülün durumu tek bir runtime Component'idir; böylece hot reload onu korur:

```cpp
#include <illusion/camera/CameraComponents.hpp>
#include <illusion/camera/CameraService.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/InputService.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsService.hpp>

#include <optional>
#include <string_view>

namespace shoulder
{

using namespace illusion;

struct State
{
    ActionId move = 0;
    ActionId swap = 0;
    ActionId aim = 0;
    ActionId hit = 0;
    Entity player;
    Entity explore;
    Entity aimCamera;
    Entity impulse;
    bool aiming = false;
};

} // namespace shoulder
```

`start` action'ları ve dört Entity'yi adlarıyla bulur ve durumu oluşturur:

```cpp
namespace
{

using namespace illusion;
using shoulder::State;

constexpr f32 kPlayerSpeed = 5.0f; // m/s

bool loadShoulder(ModuleContext&)
{
    reflectRuntime<State>("shoulder.State", {.displayName = "Shoulder state"});
    return true;
}

Entity findNamed(World& world, std::string_view wanted)
{
    Entity found{};
    world.query<const Name>().each([&](Entity entity, const Name& name) {
        if (name.value == wanted)
        {
            found = entity;
        }
    });
    return found;
}

void startShoulder(ModuleContext& context)
{
    InputService* input = context.service<InputService>();
    if (input == nullptr || !context.hasWorld() || context.worldRole() != WorldRole::Simulation)
    {
        return;
    }
    const std::optional<ActionId> move = input->findAction("shoulder.move");
    const std::optional<ActionId> swap = input->findAction("camera.swap");
    const std::optional<ActionId> aim = input->findAction("camera.aim");
    const std::optional<ActionId> hit = input->findAction("camera.hit");
    if (!move || !swap || !aim || !hit)
    {
        return;
    }
    World& world = context.world();
    State state;
    state.move = *move;
    state.swap = *swap;
    state.aim = *aim;
    state.hit = *hit;
    state.player = findNamed(world, "Player");
    state.explore = findNamed(world, "Explore");
    state.aimCamera = findNamed(world, "Aim");
    state.impulse = findNamed(world, "Hit");
    world.create(Name{"Shoulder State"}, state);
}
```

Her update oyuncuyu yürütür, omzu değiştirir, nişan kamerasını yükseltir ya da indirir ve impulse'u oynatır. İki
kolun omzu birlikte değişir; böylece Aim, Explore'un bulunduğu tarafta kalır. `activate()` Aim'i, `deactivate()`
önceliğini geri verene kadar diğer her kameranın üstüne çıkarır ve beyin her seferinde geçiş yapar; çağrılar yalnızca
düğmenin kenarında yapılır, yani `aiming` değiştiğinde:

```cpp
void updateShoulder(ModuleContext& context, f64)
{
    CameraService* cameras = context.service<CameraService>();
    InputService* input = context.service<InputService>();
    PhysicsService* physics = context.service<PhysicsService>();
    if (cameras == nullptr || input == nullptr || physics == nullptr || !context.hasWorld())
    {
        return;
    }
    World& world = context.world();
    State* state = nullptr;
    world.query<State>().each([&state](Entity, State& value) { state = &value; });
    if (state == nullptr || !world.isAlive(state->player))
    {
        return;
    }

    const Vec2 move = input->vector(state->move);
    Vec3 velocity = physics->characterVelocity(state->player);
    velocity.x = move.x * kPlayerSpeed;
    velocity.z = -move.y * kPlayerSpeed;
    physics->setCharacterVelocity(state->player, velocity);

    if (input->wasPressed(state->swap))
    {
        static_cast<void>(cameras->swapShoulder(state->explore));
        static_cast<void>(cameras->swapShoulder(state->aimCamera));
    }

    const bool aiming = input->isDown(state->aim);
    if (aiming != state->aiming)
    {
        state->aiming = aiming;
        static_cast<void>(aiming ? cameras->activate(state->aimCamera)
                                 : cameras->deactivate(state->aimCamera));
    }

    if (input->wasPressed(state->hit))
    {
        static_cast<void>(cameras->playImpulse(state->impulse));
    }
}

constexpr ModuleApi kShoulder = makeModuleApi(
    "shoulder", "1.0.0", {.load = &loadShoulder, .start = &startShoulder, .update = &updateShoulder});

} // namespace

ILL_MODULE(shoulder, kShoulder)
```

**Ctrl+B**'ye basın (**Modules → Build Modules**). Build başarılı olduğunda modül yüklenir.

## 6. Deneyin

**Play**'e basın:

- Kamera oyuncunun sağ omzunun arkasında durur ve oyuncu merkezin solundadır. Onu **I K J L** ile döndürün;
  **W A S D** ile yürüyün.
- Duvarın arkasına yürüyün ve kamerayı ona doğru çevirin: kol içinden geçmek yerine kısalır ve uzaklaştığınızda
  geri uzar.
- **Q**'ya basın: kol sol omzun üzerine geçer. Geri dönmek için yeniden basın.
- **E** tuşunu basılı tutun: görüntü daha yakın ve dar Aim kamerasına yumuşakça geçer; bırakın, geri döner.
- **F**'ye basın: 20 m içindeki her kamera yarım saniye sarsılır.

Bir agent aynı şeyleri izleyip sürebilir. `camera.status`, oyun adım adım ilerlerken beynin ne gösterdiğini, devam
eden bir geçiş dahil, gösterir:

```json
{"call": "camera.list", "params": {}}
{"call": "component.set_field", "params": {"entity": "/Aim", "type": "illusion.VirtualCamera", "path": "priority", "value": 20}}
{"call": "play.step", "params": {"steps": 15}}
{"call": "camera.status", "params": {}}
```

`camera.status`, `live` değerinin `/Aim`, `from` değerinin `/Explore` ve `progress` değerinin 0 ile 1 arasında
olduğunu söyler. **Scene** görünümünde canlı kameranın frustum'u sarı, geçişin başladığı kameranınki turuncudur.

## 7. Sınırlar

- Buradaki oyuncu kameraya göre değil dünya eksenlerinde hareket eder. Kameraya göre yürüyüş için bakış açılarını
  modülde tutun, hareketi yaw ile döndürün ve açıları `setAngles()` ile kollara verin.
- `setFieldOfView()` atlar. Aim, bir geçiş iki lens arasında yumuşak geçtiği için ikinci bir kamerayla zoom yapar.
- Bir omuz değişimi kolu `sideDamping` saniyede karşıya geçirir; daha hızlı değişim için onu küçültün.
- Önceki update'te değerlendirilmeyen kameralar sönümlemesine baştan başlar: Aim ilk gösterildiğinde hedefine
  atlar, sonra yumuşakça izler.

## Sonraki adımlar

- Orbit ve dolly kameraları, beynin çift geçişleri ve elde çekim gürültüsü için [Kameralar kılavuzunu](../manual/cameras.md)
  okuyun.
- Showcase projesinin modülü `CameraService`'in tamamını çağırır; rig'inde dört gövde aşaması da vardır.
- [Dolaşan ve kovalayan bir yaratık](creature-ai.md) kameranın döneceği bir şey ekler.
