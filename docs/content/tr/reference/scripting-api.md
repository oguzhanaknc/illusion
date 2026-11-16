# Scripting API

Oyun kodu, bir [oyun modülünde](../manual/modules.md) C++ olarak yazılır. Shared bir modül (editörün hot
reload yapılabilen Build'i) `illusion::core`'un kendi kopyasını taşır, ama başka hiçbir engine
kütüphanesini taşımaz: fiziğe, sese, parçacıklara, girdiye, sahnelere, mağazaya, oyun UI'ına ve kayıtlı oyunlara sekiz
**host servis arayüzü** üzerinden ulaşır.
Her biri veri üyesi olmayan soyut bir sınıftır; host onu kendi engine nesnesi üzerinde bir adaptörle
uygular ve modüle verir, modül de onu `context.service<T>()` ile bulur. Tasarım ADR 0041'dedir;
[API referansının](api.md) "Scripting API" Doxygen grubu her fonksiyonu belgeler.

| Arayüz | `USES` | Header | Host adaptörü |
| --- | --- | --- | --- |
| `PhysicsService` | `physics` | `illusion/physics/PhysicsService.hpp` | `PhysicsWorld` üzerinde `PhysicsWorldService` |
| `AudioService` | `audio` | `illusion/audio/AudioService.hpp` | `AudioWorld` üzerinde `AudioWorldService` |
| `ParticleService` | `particles` | `illusion/particles/ParticleService.hpp` | `ParticleWorld` üzerinde `ParticleWorldService` |
| `InputService` | `input` | `illusion/input/InputService.hpp` | `InputActions` üzerinde `InputActionsService` |
| `SceneService` | `runtime` | `illusion/runtime/SceneService.hpp` | `SceneLoaderService` (`illusion::runtime`) |
| `StoreService` | `store` | `illusion/store/StoreService.hpp` | `NullStoreService` veya `SteamStoreService` (ADR 0056) |
| `UiService` | `ui` | `illusion/ui/UiService.hpp` | `UiWorld` üzerinde `UiWorldService` (`illusion::ui`; ADR 0057) |
| `SaveService` | `save` | `illusion/save/SaveService.hpp` | `SaveSystem` üzerinde `SaveSystemService` (`illusion::save`; ADR 0058) |

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
  `kModuleApiVersion`'ı değiştirir (`SaveService` ile birlikte 6) ve modüller yeniden Build edilmelidir.

## Kapsam

"Scripting API" Doxygen grubu `ModuleContext`'i ve sekiz arayüzü içerir.
[Showcase modülü](../manual/editor/showcase.md#showcase-modulu) grubun her public fonksiyonunu çağırır;
biri orada çağrılmıyorsa ya da modül testlerinin bir sayaçlı proxy'sinin (`tests/modules/CountingServices.hpp`)
sayaç sayısı yanlışsa `python tools/check_api_coverage.py` CI'da başarısız olur. Bu yüzden yeni bir
fonksiyon `samples/showcase/modules/showcase/showcase.cpp`'de gerçek bir kullanım ister.

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

## ParticleService

| Fonksiyon | Amaç |
| --- | --- |
| `play`, `stop`, `clear` | Bir emitter'ı baştan yeniden başlatır, yaymayı durdurur, canlı parçacıklarını siler |
| `emit` | Oynasın ya da oynamasın, emitter'ın durduğu yerde hemen bir parçacık patlaması; kaçının sığdığını döner |
| `aliveCount`, `isPlaying`, `totalAlive` | Bir emitter'ın canlı parçacıkları, yayıp yaymadığı, hepsinin canlı parçacıkları |

## SceneService

| Fonksiyon | Amaç |
| --- | --- |
| `exists` | Sanal bir yolda dosya varsa true |
| `instantiate` | Bir sahne dosyasını yeni EntityGuid'lerle World'e yükler; Entity sayısını veya nullopt döner |
| `lastError` | Son `instantiate`'in neden başarısız olduğu |
| `requestExit` | Host'tan oyunu bu update'ten sonra bitirmesini ister: player kapanır, editörün Play modu durur |

## StoreService

Oyunun içinde çalıştığı mağaza ya da mağazasızlık; bkz. [Mağaza servisleri](../manual/runtime.md#magaza-servisleri).
Hatalar `Expected<T, StoreError>`'dır; adları `isValidStoreName()` denetler.

| Fonksiyon | Amaç |
| --- | --- |
| `available`, `storeName`, `userName`, `userId` | Oturum açmış bir kullanıcıyla bir mağazanın çalışıp çalışmadığı, hangi mağaza, kim |
| `unlockAchievement`, `clearAchievement`, `isAchievementUnlocked` | API adına göre başarımlar |
| `setAchievementProgress`, `achievementProgress` | current/max olarak ilerleme; max'a ulaşmak kilidi açar |
| `setStatInt`, `statInt`, `setStatFloat`, `statFloat`, `storeStats` | İstatistikler; `storeStats` mağazaya gönderir |
| `writeCloudFile`, `readCloudFile`, `removeCloudFile` | Ada göre bulut dosyaları; okuma çağıranın span'ini doldurur ve boyutu döner |
| `listCloudFiles`, `cloudFileExists`, `cloudQuota` | Çağıranın span'ine adlar, varlık, boyut ve boş alan |
| `overlayActive`, `openOverlay` | Her karede sorgulanan overlay durumu ve onu açma isteği |
| `glyphPath` | Bir gamepad kontrolünün mağaza tarzındaki görselinin yolu ya da hiçbir şey |

## UiService

World'ün oyun UI'ı; bkz. [UI'ı script'lemek](../manual/ui.md#scripting-the-ui). Bir Entity alan fonksiyonlar, ölü
bir Entity için ya da ihtiyaç duydukları Component'e sahip olmayan bir Entity için false (ya da hiçbir şey)
döndürür. Değişiklikler `World::markChanged()` olmadan yerleşime ulaşır.

| Fonksiyon | Amaç |
| --- | --- |
| `find` | Bir kökün altında ya da çizim sırasıyla herhangi bir canvas'ta verilen `Name`'e sahip ilk Entity |
| `setText`, `setTextKey` | Düz bir metin ya da `UiTextArgument::number/string` ile biçimlendirilmiş bir string table mesajı |
| `setVisible`, `isVisible`, `setInteractable` | Bir elemanı ve torunlarını göstermek ya da gizlemek, gösterilip gösterilmediği, `UiInteractable::enabled` |
| `setToggle`, `toggle`, `setSliderValue`, `sliderValue` | Widget değerleri; birini ayarlamak olay bildirmez |
| `setFocus`, `focused` | Odağı taşır (null temizler), odaktaki eleman |
| `rect` | Son yerleşimin dikdörtgeni, canvas birimi cinsinden |
| `invalidateLayout` | UI'a UI Entity'lerinin Component'lerinin doğrudan yazıldığını bildirir |
| `playTransition` | Bir elemanın `UiTransition`'ını, tetikleyicisi ne olursa olsun, değişmiş durumuna ya da dinlenme durumuna doğru götürür |
| `transitionProgress` | Bir elemanın `UiTransition` ilerlemesi: dinlenmede 0, değişmiş durumda 1 |
| `pollEvents` | Önceki karenin UI update'indeki olayları bir span'e kopyalar; sığandan fazla olabilen toplamı döndürür |
| `setLanguage`, `language` | Oyunun dili; projede locale dosyası olan bir BCP 47 etiketi |

## SaveService

`Saved` etiketli Entity'lerin ve torunlarının kayıtlı oyunları (ADR 0058). İstekler çağrı sırasıyla kuyruğa
girer ve host'un kayıt thread'inde çalışır; `save`, `load` ve `remove` hemen yalnızca `InvalidName`,
`InvalidValue` ya da `Unavailable` ile başarısız olur, sonrasındaki her şey bir `SaveEvent`'tir. Bir kayıt,
World'ü aynı update'in Modules adımından sonra yakalar; bir yükleme kayıtlı Entity'leri daha sonraki bir
update'te değiştirir, bu yüzden `Entity` Handle'ları değişir: onları `Loaded` ya da `LoadedBackup`'tan sonra
yeniden bulun.

| Fonksiyon | Amaç |
| --- | --- |
| `available` | Host'un bir kayıt klasörü olup olmadığı |
| `save`, `load`, `remove` | Bir slotu yazma, uygulama ya da silme isteğini kuyruğa ekler (1 ile 32 arası harf, rakam, `_`, `-`) |
| `busy` | Bir isteğin olayını henüz vermemiş olup olmadığı |
| `slots`, `slot` | Slot listesi (ad, `SaveMetadata`, `savedAt`, `SaveSlotState`, `screenshot`) çağıranın span'ine, ya da tek bir slot; hiçbir dosya okunmaz |
| `pollEvents` | Son update'in olaylarını (`Saved`, `Loaded`, `LoadedBackup`, `Removed`, bir `SaveError` ile `Failed`) bir span'e kopyalar; toplamı döndürür |

`SaveSlotInfo::screenshot`, slotun 320 x 180 PNG'sinin sanal yoludur (player'da `user:/saves/<slot>.png`) ya da
slotun görüntüsü yoksa boştur. Bulutu uzakta olan bir mağazayla slot dosyaları buluta da yansıtılır ve oturum
başladığında daha yeni bir bulut dosyası yerel slotun yerini alır; bu hiçbir olay üretmez
([Buluttaki kayıtlı oyunlar](../manual/runtime.md#buluttaki-kaytl-oyunlar)).
