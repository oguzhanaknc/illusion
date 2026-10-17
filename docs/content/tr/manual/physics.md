# Fizik

illusion katı cisimleri (rigid body) [Jolt Physics](https://github.com/jrouwe/JoltPhysics) ile simüle
eder. Cisimler sıradan Entity'lerdir: bir `Transform`, bir `RigidBody` ve bir `Collider` yeterlidir.

Header'lar: `<illusion/physics/PhysicsWorld.hpp>`, `<illusion/physics/PhysicsComponents.hpp>`

Kütüphane: `illusion::physics`

## Başlarken

```cpp
illusion::JobSystem jobs;
illusion::World world;
illusion::PhysicsWorld physics(world, jobs);

// Ground: a static box whose top is at y = 0.
illusion::Transform ground;
ground.position = {0.0f, -0.5f, 0.0f};
illusion::RigidBody groundBody;
groundBody.motionType = illusion::MotionType::Static;
illusion::Collider groundShape;
groundShape.halfExtents = {50.0f, 0.5f, 50.0f};
world.create(ground, groundBody, groundShape);

// A falling crate (dynamic is the default; the default Collider is a 1 m box).
illusion::Transform crate;
crate.position = {0.0f, 5.0f, 0.0f};
const illusion::Entity box = world.create(crate, illusion::RigidBody{}, illusion::Collider{});

while (running)
{
    physics.update(frameSeconds); // fixed steps; writes the crate's Transform
}
```

`PhysicsWorld::update()` her şeyi tek bir çağrıda yapar:

1. `Transform`, `RigidBody` ve `Collider` sahibi Entity'ler bir cisim ve bir `PhysicsBody` Component'i
   alır (çalışma zamanı bağlantısı; kendiniz eklemeyin). Yok edilen Entity'lerin ya da bu Component'lerden
   birini kaybeden Entity'lerin cisimleri kaldırılır.
2. Simülasyon sabit adımlarla ilerler (varsayılan 60 Hz, update başına en fazla 4 adım); böylece sonuçlar
   kare hızına bağlı değildir. Simülasyon işi Job System üzerinde çalışır.
3. Dinamik cisimler, akıcı hareket için son iki adım arasında interpolasyon yapılarak `Transform`'larını
   yazar. Bu yüzden görüntü simülasyonu en fazla bir adım geriden gösterir.

## Cisimler

| `MotionType` | Neyle hareket eder | Kullanım |
| --- | --- | --- |
| `Static` | `Transform`'u ile (nadiren) | Seviye geometrisi |
| `Kinematic` | Her update'te `Transform`'u ile; dinamik cisimleri iter | Hareketli platformlar, kapılar |
| `Dynamic` | Simülasyonla; `Transform`'unu yazar | Kasalar, ragdoll parçaları, mermiler |

- Dinamik bir cismin `Transform`'unu ayarlamak onu ışınlar; statik ve kinematik cisimler
  `Transform`'larını otomatik izler.
- `RigidBody::mass` 0 ise kütle collider hacminden hesaplanır (1000 kg/m³). Sürtünme, esneklik
  (restitution), sönümleme, yerçekimi çarpanı, uyku ve (hızlı cisimler için) sürekli çarpışma algılama
  da `RigidBody` alanlarıdır.
- `RigidBody::sensor` bir tetikleyici (trigger) yapar: temasları bildirir ama çarpışmaz.
- `RigidBody`, `Collider` veya `Transform` ölçeğindeki değişiklikler cisim oluşturulurken uygulanır;
  daha sonra uygulamak için `physics.rebuild(entity)` çağırın.

## Collider'lar

| `ColliderShape` | Boyut alanları |
| --- | --- |
| `Box` | `halfExtents` |
| `Sphere` | `radius` |
| `Capsule` | `halfHeight` (yerel Y boyunca düz kısım), `radius` |
| `Cylinder` | `halfHeight` (yerel Y boyunca), `radius` |
| `ConvexHull` | `createConvexHull()`'dan gelen `mesh` |
| `Mesh` | `createMesh()`'ten gelen `mesh`; yalnızca statik ve kinematik cisimler |

`offset` ve `rotation`, şekli Entity'ye göre konumlandırır. `Transform` ölçeği şekli ölçekler; küreler
ve kapsüller tek biçimli (uniform) ölçek kullanır. Geçersiz bir collider (örneğin negatif yarıçap) bir
uyarı yazar ve cismi olmayan bir `PhysicsBody` alır; düzeltip `rebuild()` çağırın.

### Mesh ve convex hull collider'ları

Üçgen Mesh'ler seviye geometrisi oluşturur; convex hull'lar her biçimdeki nesneye uyar ve dinamik
olabilir. İkisi de bir kez hazırlanır ve onları kullanan tüm cisimler tarafından paylaşılır:

```cpp
// Level geometry: vertices and three indices per triangle (counter-clockwise seen from the front).
const illusion::PhysicsMeshHandle level = physics.createMesh(positions, indices);
illusion::Collider levelShape;
levelShape.shape = illusion::ColliderShape::Mesh;
levelShape.mesh = level;
world.create(illusion::Transform{}, groundBody, levelShape); // a static RigidBody

// A dynamic rock: the convex hull of its vertices.
illusion::Collider rockShape;
rockShape.shape = illusion::ColliderShape::ConvexHull;
rockShape.mesh = physics.createConvexHull(rockPositions);
world.create(rockTransform, illusion::RigidBody{}, rockShape);
```

- Dinamik cisimlerdeki Mesh collider'ları reddedilir (Jolt kütlelerini hesaplayamaz); convex hull
  kullanın.
- `destroyMesh()` bir handle'ı serbest bırakır; geometriyi zaten kullanan cisimler onu korur.
- Handle çalışma zamanı verisidir ve sahnelere kaydedilmez; bir sahne yükledikten sonra yeniden atayın.

## Hızlar, kuvvetler, sorgular ve temaslar

```cpp
physics.update(0.0); // creates bodies for new Entities right away
physics.addImpulse(box, {0.0f, 500.0f, 0.0f}); // N·s; changes velocity by impulse / mass
physics.setLinearVelocity(box, {2.0f, 0.0f, 0.0f});
physics.addForce(box, {0.0f, 100.0f, 0.0f}); // N, applied during the next step

if (const auto hit = physics.raycast(cameraPosition, cameraForward, 100.0f))
{
    ILL_LOG_INFO("Game", "looking at entity {} at {} m", hit->entity.index, hit->distance);
}

for (const illusion::ContactEvent& contact : physics.contactEvents())
{
    if (contact.type == illusion::ContactEventType::Begin && contact.sensor)
    {
        // contact.a and contact.b entered a trigger zone
    }
}
```

- Cisim fonksiyonları henüz cismi olmayan Entity'ler için hiçbir şey yapmaz; Entity oluşturduktan sonra
  cisimlerini hemen oluşturmak için `update(0.0)` çağırın.
- `raycast()`, sensör olmayan en yakın isabeti Entity'si, noktası, normali ve uzaklığıyla döndürür.
- `contactEvents()`, son `update()` sırasında başlayan veya biten temasları, sıra thread zamanlamasına
  bağlı olmayacak şekilde sıralanmış olarak listeler. Her olayda `a` daha küçük Entity kimliğine sahiptir
  ve normal `a`'dan `b`'ye doğrudur.

### Şekil taramaları (shape cast) ve örtüşmeler (overlap)

```cpp
illusion::Collider ball;
ball.shape = illusion::ColliderShape::Sphere;
ball.radius = 0.3f;
const illusion::Quat identity(1.0f, 0.0f, 0.0f, 0.0f);

// Sweep a sphere forward: where would a thrown grenade first touch something?
if (const auto hit = physics.shapeCast(ball, start, identity, forward, 20.0f, allLayers, player))
{
    // hit->entity, hit->point, hit->normal, hit->distance
}

// Every body within 3 m of an explosion, sorted, each once.
std::vector<illusion::Entity> inRange; // reuse it between calls
ball.radius = 3.0f;
physics.overlap(ball, explosionCenter, identity, inRange);
```

- Sorgular `Mesh` dışındaki her `Collider`'ı alır; `offset` ve `rotation` uygulanır.
- Sensörler atlanır; `ignore` (`raycast()`, `shapeCast()` ve `overlap()`'in son argümanı) bir Entity'nin
  cismini atlar, örneğin oyuncunun kendi karakterini.
- `shapeCast()` ilk teması bildirir; bir cismin içinde başlayan şekil ona 0 mesafede çarpar.

## Karakterler

`CharacterController` bir Entity'yi oyuncu ya da NPC karakterine dönüştürür: kuvvetlerle değil oyun
koduyla hareket ettirilen dik bir kapsül. Basamakları ve eğimleri çıkar, duvarlar boyunca kayar, hareketli
platformlarda taşınır ve dinamik cisimleri iter. `Transform` konumu karakterin ayaklarındadır; dönüşü oyun
koduna bırakılır.

```cpp
illusion::CharacterController controller; // 1.8 m tall, 0.3 m radius, 0.35 m steps, 50° slopes
const illusion::Entity player = world.create(spawnTransform, controller);

// Every frame, before physics.update():
illusion::Vec3 velocity = physics.characterVelocity(player);
velocity.x = input.x * 5.0f; // walk
velocity.z = input.y * 5.0f;
if (jumpPressed && physics.isCharacterGrounded(player))
{
    velocity.y = 5.0f; // jump
}
physics.setCharacterVelocity(player, velocity);
```

- Yerçekimi (`gravityFactor` ile çarpılarak) karakteri havadayken hızlandırır; yere inmek aşağı doğru
  hızı, tavan ise yukarı doğru hızı durdurur. Yatay hız siz değiştirene kadar kalır.
- `maxSlope`'tan dik eğimler yürünebilir değildir; karakter onlardan aşağı kayar.
- `CharacterBody` (`PhysicsWorld` tarafından eklenir) son update'ten sonraki hızı, karakterin
  `grounded` olup olmadığını, zemin normalini ve `ground` Entity'sini tutar.
- Karakterler birbirleriyle çarpışır ve bir iç cisimleri vardır; böylece raycast'ler, sensörler ve temas
  olayları onları görür. `Transform`'u ayarlamak karakteri ışınlar.

## Joint'ler

`Joint`, Entity'sinin cismini `connected` cismine (ya da `connected` null ise dünyaya) bağlar. İki
Entity'nin de bir cismi olmalı ve en az biri dinamik olmalıdır. Joint, cisimlerin o anki pozlarında
oluşturulur.

| `JointType` | İzin verir | Kullanır |
| --- | --- | --- |
| `Fixed` | Hiçbir şey: cisimler tek parça hareket eder | `anchor` |
| `Point` | Anchor etrafında dönüş (bilyeli mafsal) | `anchor` |
| `Hinge` | `axis` etrafında dönüş (kapılar, tekerlekler) | `anchor`, `axis`, limitler, motor |
| `Slider` | `axis` boyunca hareket (pistonlar, çekmeceler) | `anchor`, `axis`, limitler, motor |
| `Distance` | Anchor'lar arası `minLimit` ile `maxLimit` arasında (halatlar, çubuklar) | `anchor`, `connectedAnchor`, limitler |

```cpp
// A door hinged at its left edge, opening with a motor.
illusion::Joint hinge;
hinge.type = illusion::JointType::Hinge;
hinge.anchor = {-0.5f, 0.0f, 0.0f};   // in the door's local space
hinge.axis = {0.0f, 1.0f, 0.0f};
hinge.limited = true;
hinge.minLimit = 0.0f;                // radians
hinge.maxLimit = 1.6f;
hinge.motorSpeed = 1.0f;              // rad/s; change it any frame
hinge.motorMaxForce = 500.0f;         // N·m; 0 turns the motor off
world.create(doorTransform, illusion::RigidBody{}, doorCollider, hinge);

// A lamp on a 2 m chain, which breaks when pulled with more than 2000 N.
illusion::Joint chain;
chain.type = illusion::JointType::Distance;
chain.connectedAnchor = ceilingHook; // world space: no connected body
chain.maxLimit = 2.0f;
chain.breakForce = 2000.0f;
world.create(lampTransform, illusion::RigidBody{}, lampCollider, chain);
```

- `jointPosition(entity)` hinge açısını, slider konumunu (ikisi de başlangıca göre) ya da anchor'lar
  arasındaki mesafeyi döndürür. Pozitif değerler ve motor hızları, bu Entity'nin +`axis` boyunca ya da
  etrafında hareket ettiği anlamına gelir.
- Kuvveti `breakForce`'u aşan bir joint devre dışı kalır: `PhysicsJoint::broken` ayarlanır ve Entity o
  update için `brokenJoints()` içinde listelenir.
- Joint'ler cisimlerinden biriyle birlikte kaldırılır ve bir cisim yeniden kurulduğunda yeniden
  oluşturulur. Oluşturulamayan bir joint (cisim yok, iki statik cisim) bir uyarı yazar; düzeltip
  `rebuild()` çağırın.

## Çarpışma katmanları

Her cismin [0, 16) aralığında bir katmanı vardır. Varsayılan olarak tüm katmanlar çarpışır:

```cpp
illusion::RigidBody ghost;
ghost.layer = 1;
physics.setLayersCollide(1, 0, false); // layer 1 passes through layer 0
```

## Sahneler ve thread'ler

- `reflectPhysicsComponents()`, sahne dosyaları için `RigidBody` (`illusion.RigidBody`), `Collider`
  (`illusion.Collider`), `CharacterController` (`illusion.CharacterController`) ve `Joint`
  (`illusion.Joint`; `connected` yüklemede yeniden eşlenir) Component'lerini yansıtır; `PhysicsBody`,
  `CharacterBody` ve `PhysicsJoint` çalışma zamanı durumudur ve kaydedilmez.
- `PhysicsWorld` thread-safe değildir: onu tek bir thread'den, World Query'lerinin dışında çağırın. World
  ondan uzun yaşamalıdır; onu yok etmek çalışma zamanı Component'lerini kaldırır.
- Aynı girdiler için sonuçlar deterministiktir ve Job System Worker sayısından bağımsızdır.

## Örnek

`illusion_physics_playground` kutulardan bir kule dizer; Space (veya gamepad'in güney düğmesi) kuleye ağır
bir top fırlatır, R kuleyi yeniden kurar.
