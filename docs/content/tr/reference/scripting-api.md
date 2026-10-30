# Scripting API

Oyun kodu, bir [oyun modülünde](../manual/modules.md) C++ olarak yazılır. Shared bir modül (editörün hot
reload yapılabilen Build'i) `illusion::core`'un kendi kopyasını taşır, ama başka hiçbir engine
kütüphanesini taşımaz: fiziğe, sese, girdiye ve sahnelere dört **host servis arayüzü** üzerinden ulaşır.
Her biri veri üyesi olmayan soyut bir sınıftır; host onu kendi engine nesnesi üzerinde bir adaptörle
uygular ve modüle verir, modül de onu `context.service<T>()` ile bulur. Tasarım ADR 0041'dedir;
[API referansının](api.md) "Scripting API" Doxygen grubu her fonksiyonu belgeler.

| Arayüz | `USES` | Header | Host adaptörü |
| --- | --- | --- | --- |
| `PhysicsService` | `physics` | `illusion/physics/PhysicsService.hpp` | `PhysicsWorld` üzerinde `PhysicsWorldService` |
| `AudioService` | `audio` | `illusion/audio/AudioService.hpp` | `AudioWorld` üzerinde `AudioWorldService` |
| `InputService` | `input` | `illusion/input/InputService.hpp` | `InputActions` üzerinde `InputActionsService` |
| `SceneService` | `runtime` | `illusion/runtime/SceneService.hpp` | `SceneLoaderService` (`illusion::runtime`) |

```cmake
illusion_add_module(tetris SOURCES game/Tetris.cpp USES physics audio input runtime)
```

```cpp
void update(illusion::ModuleContext& context, illusion::f64)
{
    illusion::InputService* input = context.service<illusion::InputService>();
    illusion::PhysicsService* physics = context.service<illusion::PhysicsService>();
    if (input == nullptr || physics == nullptr)
    {
        return; // this World was attached without them
    }
    if (const std::optional<illusion::ActionId> jump = input->findAction("Jump"); jump && input->wasPressed(*jump))
    {
        physics->addImpulse(player, illusion::Vec3(0.0f, 5.0f, 0.0f));
    }
}
```

## Kurallar

- **Her pointer'ı kontrol edin.** Ne World ne de host `T`'yi sağlıyorsa (bir edit veya preview World'ü,
  bir test host'u) `context.service<T>()` null döner.
- **Nesneleri değil, Handle'ları tutun.** Geometri (`PhysicsMeshHandle`), klipler (`AudioClipHandle`),
  sesler ve Entity'ler düz değerlerdir; arkalarındaki nesneler host'a aittir ve hot reload'lardan sağ
  çıkar. Handle'ları, verilerini reload'lar boyunca koruyan Component'lerde saklayın.
- **Span'ler ve view'lar ödünçtür.** `contactEvents()`, `brokenJoints()`, `bindings()`, `actionName()` ve
  `lastError()` host belleğini gösterir ve bir sonraki güncellemeye veya değişikliğe kadar geçerlidir;
  sakladığınızı kopyalayın.
- **Çıktılar sizin tamponlarınıza yazılır.** `overlap()` ve `instantiate()` sığdığı kadar Entity'yi bir
  span'e yazar ve toplam sayıyı döner; böylece sınırı hiçbir container geçmez.
- **World başına veya host başına.** Host fiziği, sesi ve sahneleri her Simulation World'üne verir
  (`attachWorld(world, role, services)`), böylece her oynatma oturumunun kendisine ait olanı olur; girdi
  genellikle host genelindedir (`ModuleHost::provide()`).
- **Engine kodu host'ta kalır.** Shared bir modülden çağrılan inline olmayan bir engine fonksiyonu bir link
  hatasıdır; düz veri yardımcıları (Component'ler, `InputBinding::key()`, ...) inline'dır ve her yerde
  çalışır. Statik modüller (export edilen oyunlar) engine'i linkler ve kütüphaneleri doğrudan
  kullanabilir, ama servisler orada da çalışır.
- **Maliyet.** Bir çağrı tek bir dolaylı çağrıdır; `services/virtual-call` Benchmark'ı onu 10 ns'nin
  altında tutar.
- **Uyumluluk.** Arayüzler modül ABI'sinin parçasıdır: bir fonksiyon eklemek veya değiştirmek
  `kModuleApiVersion`'ı değiştirir ve modüller yeniden Build edilmelidir.

## PhysicsService

Aynı adlı `PhysicsWorld` fonksiyonları gibi davranır.

| Fonksiyon | Amaç |
| --- | --- |
| `setGravity`, `gravity` | m/s² cinsinden World yerçekimi |
| `setLayersCollide`, `layersCollide` | Katmanlar arası çarpışma filtresi |
| `rebuild` | RigidBody veya Collider'ı değişen bir gövdeyi yeniden oluşturur |
| `setLinearVelocity`, `linearVelocity`, `setAngularVelocity`, `angularVelocity` | Gövde hızları |
| `addForce`, `addTorque`, `addImpulse` | Bir sonraki adımın kuvvetleri, anlık itmeler |
| `mass`, `isAwake` | Gövde durumu |
| `createMesh`, `createConvexHull`, `destroyMesh`, `isMeshValid` | `Collider::geometry` için host'a ait geometri |
| `setCharacterVelocity`, `characterVelocity`, `isCharacterGrounded` | CharacterController hareketi |
| `jointPosition`, `brokenJoints` | Joint durumu ve kırılma |
| `raycast`, `shapeCast`, `overlap` | Sorgular; tüm katmanlar ve hiçbir şeyi yok saymamak için `0xFFFFFFFFu` ve `kNullEntity` verin |
| `contactEvents` | Son güncellemede başlayan veya biten temaslar |

## AudioService

Aynı adlı `AudioWorld` fonksiyonları gibi davranır; `stopVoice` ve `isVoicePlaying`, onun
`stop(AudioVoiceHandle)` ve `isPlaying(AudioVoiceHandle)` fonksiyonlarıdır.

| Fonksiyon | Amaç |
| --- | --- |
| `loadClip`, `requestClip`, `clipState`, `createClip`, `unloadClip`, `clipInfo` | Host'a ait klipler |
| `play`, `stop`, `isPlaying` | Bir Entity'nin AudioSource'u |
| `playOneShot`, `stopVoice`, `isVoicePlaying` | Entity'siz sesler |
| `setMasterVolume`, `masterVolume`, `setBusVolume`, `busVolume` | Miks seviyeleri |
| `setBusPaused`, `isBusPaused`, `setBusFilter`, `busFilter` | Bus duraklatma ve filtreleri |
| `setReverb`, `reverb` | Ortak reverb |

## InputService

Aynı adlı `InputActions` fonksiyonları gibi davranır. `addAction`, aynı ad ve türdeki mevcut action'ı
döner; ad başka bir türce alınmışsa nullopt döner.

| Fonksiyon | Amaç |
| --- | --- |
| `addAction`, `findAction`, `actionCount`, `actionName`, `actionType` | Action'lar |
| `bindings`, `setBindings` | Yeniden atama |
| `isDown`, `wasPressed`, `wasReleased`, `value`, `vector` | Geçerli karedeki durum |
| `lastDevice`, `lastGamepadType` | Hangi glyph'lerin gösterileceği |

## SceneService

| Fonksiyon | Amaç |
| --- | --- |
| `exists` | Sanal bir yolda dosya varsa true |
| `instantiate` | Bir sahne dosyasını yeni EntityGuid'lerle World'e yükler; Entity sayısını veya nullopt döner |
| `lastError` | Son `instantiate`'in neden başarısız olduğu |
