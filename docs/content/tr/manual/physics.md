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

`offset` ve `rotation`, şekli Entity'ye göre konumlandırır. `Transform` ölçeği şekli ölçekler; küreler
ve kapsüller tek biçimli (uniform) ölçek kullanır. Geçersiz bir collider (örneğin negatif yarıçap) bir
uyarı yazar ve cismi olmayan bir `PhysicsBody` alır; düzeltip `rebuild()` çağırın.

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

## Çarpışma katmanları

Her cismin [0, 16) aralığında bir katmanı vardır. Varsayılan olarak tüm katmanlar çarpışır:

```cpp
illusion::RigidBody ghost;
ghost.layer = 1;
physics.setLayersCollide(1, 0, false); // layer 1 passes through layer 0
```

## Sahneler ve thread'ler

- `reflectPhysicsComponents()`, sahne dosyaları için `RigidBody` (`illusion.RigidBody`) ve `Collider`
  (`illusion.Collider`) Component'lerini yansıtır; `PhysicsBody` çalışma zamanı durumudur ve kaydedilmez.
- `PhysicsWorld` thread-safe değildir: onu tek bir thread'den, World Query'lerinin dışında çağırın. World
  ondan uzun yaşamalıdır; onu yok etmek `PhysicsBody` Component'lerini kaldırır.
- Aynı girdiler için sonuçlar deterministiktir ve Job System Worker sayısından bağımsızdır.

## Örnek

`illusion_physics_playground` kutulardan bir kule dizer; Space (veya gamepad'in güney düğmesi) kuleye ağır
bir top fırlatır, R kuleyi yeniden kurar.
