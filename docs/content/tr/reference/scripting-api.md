# Scripting API

Oyun kodu, bir [oyun modülünde](../manual/modules.md) C++ olarak yazılır. Shared bir modül (editörün hot
reload yapılabilen Build'i) `illusion::core`'un kendi kopyasını taşır, ama başka hiçbir engine
kütüphanesini taşımaz: fiziğe, sese, parçacıklara, animasyona, navigasyona, AI'a, girdiye, sahnelere, mağazaya, oyun UI'ına ve kayıtlı oyunlara on bir
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
| `AnimationService` | `animation` | `illusion/animation/AnimationService.hpp` | `AnimationWorld` üzerinde `AnimationWorldService` (`illusion::animation`; ADR 0060) |
| `NavService` | `nav` | `illusion/nav/NavService.hpp` | `NavWorld` üzerinde `NavWorldService` (`illusion::nav`; ADR 0062) |
| `AiService` | `ai` | `illusion/ai/AiService.hpp` | `AiWorld` üzerinde `AiWorldService` (`illusion::ai`; ADR 0062) |

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
  `kModuleApiVersion`'ı değiştirir (`AiService` ile birlikte 12) ve modüller yeniden Build edilmelidir.

## Kapsam

"Scripting API" Doxygen grubu `ModuleContext`'i ve on arayüzü içerir.
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
([Buluttaki kayıtlı oyunlar](../manual/runtime.md#buluttaki-kaytl-oyunlar)). [Kayıtlı oyunlar](../manual/saved-games.md)
kılavuz sayfası `Saved` tag'ini, metadata'yı, olayları ve dosyaları anlatır.

## AnimationService

Animator'ların animasyon grafları ve klipleri, eklemleri, look-at ve IK'leri (ADR 0059, ADR 0060, ADR 0061). Parametre yazımları bir sonraki
Animation adımında kullanılır; okumalar grafın kullandığı değerleri döndürür. `Animator`'ı olmayan bir
Entity üzerindeki çağrı `NoAnimator` döndürür; graf gerektiren bir çağrı, Animator'ın grafı yokken ya da
grafı henüz yüklenmemişken `NoGraph` döndürür; bilinmeyen adlar `UnknownName`, yanlış türler
`TypeMismatch` döndürür. Hiçbir şey assert etmez.

| Fonksiyon | Amaç |
| --- | --- |
| `setFloat`, `setInt`, `setBool` | Entity'nin grafındaki bir parametreyi ayarlar (sonlu olmayan bir değer ya da 2^24'ün ötesindeki bir Int için `InvalidValue`) |
| `setTrigger` | Bir trigger'ı, bir geçiş onu tüketene ya da sonraki adım bitene kadar kurar |
| `parameter` | Bir Float, Int ya da Bool parametresinin değeri (Int ve Bool sayı olarak) |
| `currentState`, `normalizedTime` | Bir katmanın geçerli durumu (süren bir geçişin hedefi) ve normalize zamanı |
| `play` | Grafı olmayan bir Animator'ı `fadeSeconds` boyunca `model.glb#animation/<name>` klibine cross-fade eder, 0 zamanından başlatır; `Animator::clip`'i ayarlar (graf varsa `HasGraph`) |
| `pollEvents` | Son update'in durum olaylarını (`AnimationEvent`: Entity, olay, katman, durum) update sırasıyla bir span'e kopyalar; toplamı döndürür |
| `jointTransform` | Entity'nin iskeletindeki bir eklemin son update'teki dünya uzayı matrisi: Entity'nin `Transform`'u ile eklemin model uzayındaki pozunun çarpımı, yani bir [socket](../manual/animation.md#socketler)'in bir çocuğu yerleştireceği yer (iskelette olmayan bir eklem için `UnknownName`, poz yüklenirken `NoGraph`) |
| `setLookAtTarget` | Entity'nin [`LookAt`](../manual/animation.md#look-at)'inin baktığı dünya uzayındaki noktayı ayarlar ve hedef Entity'sini temizler (`LookAt` yoksa `UnknownName`) |
| `setIkTarget` | Animator'ın uç eklemi `joint` olan [`TwoBoneIk`](../manual/animation.md#two-bone-ik)'inin (Entity'de ya da bir çocukta) dünya uzayındaki hedefini ayarlar ve hedef Entity'sini temizler (yoksa `UnknownName`) |
| `setIkWeight` | O `TwoBoneIk`'in ağırlığını [0, 1] aralığına sıkıştırarak ayarlar; 0 animasyondaki uzvu gösterir |

Her fonksiyon bir `Expected<..., AnimationError>` döndürür; `animationErrorName()` adı verir:

| Hata | Ne zaman |
| --- | --- |
| `NoAnimator` | Entity canlı değil ya da `Animator`'ı yok |
| `NoGraph` | Çağrı bir graf gerektirir ve Animator'ın grafı yok ya da henüz yüklenmedi; `jointTransform()` için poz henüz yüklenmedi |
| `HasGraph` | Grafı onu yöneten bir Animator'da `play()` |
| `UnknownName` | O adda parametre, katman, klip ya da eklem yok ya da onun için `LookAt` veya `TwoBoneIk` yok (`model.glb#animation/<name>` olmayan bir `play()` yolu da) |
| `TypeMismatch` | Parametre başka bir türde (bir Trigger'da `setFloat()`, bir Float'ta `setTrigger()`, ...) |
| `InvalidValue` | Bir değer sonlu değil, bir Int 2^24'ün ötesinde ya da bir fade negatif |

Bir update'in olayları bir sonraki Animation update'ine kadar yayımda kalır: arada yapılan her `pollEvents()`
çağrısı aynı olayları döndürür (toplamı, span'in boyutunu aşsa bile), bu yüzden span'i en yoğun update'e göre
boyutlandırın ya da update başına bir kez okuyun. Bir `AnimationEvent`'in ve `currentState()`'in döndürdüğü
adlar bir sonraki update'e kadar geçerlidir. Oyun duraklatılmışken yapılan parametre yazımları bir sonraki
Animation adımında kullanılır.

```cpp
if (illusion::AnimationService* animation = context.service<illusion::AnimationService>())
{
    static_cast<void>(animation->setFloat(hero, "speed", 3.0f));
    std::array<illusion::AnimationEvent, 16> events{};
    const illusion::u32 count = animation->pollEvents(events);
    for (illusion::u32 i = 0; i < count && i < events.size(); ++i)
    {
        if (events[i].name == "footstep")
        {
            playFootstep(events[i].entity);
        }
    }
}
```

[Animasyon](../manual/animation.md#animasyon-graflar) kılavuz sayfası grafları anlatır,
[eğitim](../tutorials/animated-character.md) onları kullanır; root motion'ı
[oyun oturumu](../manual/runtime.md) uygular.

## NavService

Oyunun navmesh'leri ve NavAgent'ları (ADR 0062). Sorgular (`findPath`, `nearestPoint`, `raycast`,
`randomPoint`) bir `NavQueryFilter` (agent type, area mask, search extents) alır ve köşe noktalarını sizin
span'inize yazar. Hedef değişiklikleri (`setDestination`, `stop`) `NavAgent::destination`'a kaydedilir ve bir
sonraki Navigation adımında etki eder; `warp` hemen taşır. Canlı olmayan ya da `NavAgent`'ı olmayan bir Entity
üzerindeki çağrı `NoAgent` döndürür; hiçbir şey assert etmez.

| Fonksiyon | Amaç |
| --- | --- |
| `findPath` | En kısa yolun köşe noktalarını bir span'e yazar ve `NavPathResult` döndürür: yazılan `count`, tüm yolun `total`'ı, m cinsinden `length` ve `partial` (yol, hedefe en yakın erişilebilir noktada biter) |
| `nearestPoint` | Bir konuma `searchExtents` içinde en yakın navmesh noktası |
| `raycast` | Navmesh üzerinde bir noktadan ötekine yürür ve `NavRaycastHit` döndürür: `hit`, `point`, duvarın yatay `normal`'i ve yürünen `fraction` |
| `randomPoint` | Merkezden erişilebilen, bir yarıçap içindeki bir navmesh noktası; aynı seed aynı noktayı verir |
| `setDestination`, `stop` | Entity'nin agent'ını dünya uzayındaki bir noktaya gönderir ya da hedefini temizler |
| `warp` | Agent'ı bir konuma en yakın navmesh noktasına hemen taşır ve yolunu bırakır |
| `status`, `remainingDistance` | Agent'ın son update'teki `NavAgentStatus`'ı ve yolunda kalan mesafe (m); henüz update edilmemiş bir agent `Idle`'dır (hedefi varsa `Pending`), kalan 0 m |
| `pollEvents` | Son update'in olaylarını (`NavEvent`: agent, tür, link, link türü, konum) Entity sırasıyla bir span'e kopyalar; toplamı döndürür |

`pollEvents` dışındaki her fonksiyon bir `Expected<..., NavError>` döndürür; `navErrorName()` adı verir:

| Hata | Ne zaman |
| --- | --- |
| `NoAgent` | Entity canlı değil ya da `NavAgent`'ı yok |
| `NoNavMesh` | O agent type'ın henüz yüklenmiş bir navmesh'i yok |
| `OffMesh` | Bir noktanın `searchExtents` içinde navmesh poligonu yok |
| `NoPath` | Noktalar navmesh üzerinde ama erişilebilen hiçbir şey hedefe başlangıçtan daha yakın değil |
| `InvalidValue` | Bir değer sonlu değil ya da aralık dışı: 8 veya daha büyük bir agent type, negatif bir yarıçap ya da extent, boş bir span |

Bir update'in olayları bir sonraki Navigation update'ine kadar yayımda kalır: arada yapılan her `pollEvents()`
çağrısı aynı olayları döndürür (toplamı, span'in boyutunu aşsa bile), bu yüzden span'i en yoğun update'e göre
boyutlandırın ya da update başına bir kez okuyun.

```cpp
if (illusion::NavService* nav = context.service<illusion::NavService>())
{
    const illusion::NavQueryFilter filter{
        .agentType = 0, .areaMask = 0xFFFFFFFFu, .searchExtents = illusion::Vec3(2.0f, 4.0f, 2.0f)};
    if (auto point = nav->randomPoint(home, 10.0f, seed, filter))
    {
        static_cast<void>(nav->setDestination(creature, *point));
    }
    if (nav->status(creature).valueOr(illusion::NavAgentStatus::Idle) == illusion::NavAgentStatus::Moving)
    {
        const illusion::f32 left = nav->remainingDistance(creature).valueOr(0.0f);
        showDistance(left);
    }
    std::array<illusion::NavEvent, 16> events{};
    const illusion::u32 count = nav->pollEvents(events);
    for (illusion::u32 i = 0; i < count && i < events.size(); ++i)
    {
        if (events[i].type == illusion::NavEventType::LinkEntered)
        {
            playJump(events[i].agent);
        }
    }
}
```

[Navigasyon](../manual/navigation.md#bir-modulden) kılavuz sayfası surface'leri, agent'ları ve crowd'u
anlatır.

## AiService

Oyunun behaviour tree'leri (ADR 0062). Bir tree'nin **blackboard**'u, node'larının okuduğu ve yazdığı tipli
key'leri tutar; setter'lar (`setBool`, `setInt`, `setFloat`, `setVector`, `setEntity`) Entity'nin tree'sindeki
bir key'i yazar ve bir sonraki AI adımı değeri görür. Bir tree'nin oyun action'ları (`{"action": "bite"}`)
kodunuza verilir: `pollActions` son update'in başlattığı ya da iptal ettiği action'ları listeler,
`finishAction` çalışan birini bitirir. Canlı olmayan ya da `BehaviorAgent`'ı veya `AiPerception`'ı olmayan bir
Entity üzerindeki çağrı `NoAgent` döndürür; hiçbir şey assert etmez.

| Fonksiyon | Amaç |
| --- | --- |
| `setBool`, `setInt`, `setFloat`, `setVector`, `setEntity` | Adındaki tipte bir key yazar; null bir Entity, Entity key'ini temizler |
| `clearValue` | Bir Vector3 ya da Entity key'ini temizler |
| `value` | Bir key'i `BlackboardValue` olarak okur (`type`'ı, `set` olup olmadığı ve o tipin alanı), son yazma ya da AI adımı itibarıyla |
| `pollActions` | Son update'in `AiActionRequest`'lerini (agent, action id'si, node'un `name`'i ve `Started` ya da `Aborted` fazı) agent sırasıyla bir span'e yazar; toplamı döndürür |
| `finishAction` | Çalışan bir action'ı id'siyle bitirir: node'u bir sonraki AI adımında başarılı ya da başarısız olur; `Aborted` bir action bitirilmez |
| `reportNoise` | Algılayanların bir sonraki AI adımında duyacağı bir `AiNoise` (konum, yarıçap, instigator, team) bildirir |
| `perceived` | Bir `AiPerception`'ın algıladığı ya da hatırladığı şeyleri (`AiPerceived`: Entity, duyu, bilinen son konum, yaş, güncel olup olmadığı) en yakın önce bir span'e yazar; sayıyı döndürür |
| `runningNode` | Tree'nin çalışan en derin node'unun adı, hiçbiri çalışmıyorsa boş ad |
| `restart` | Tree'yi kökünden, başlangıç blackboard'uyla yeniden başlatır; çalışan action'lar iptal olur |

`pollActions` dışındaki her fonksiyon bir `Expected<..., AiError>` döndürür; `aiErrorName()` adı verir:

| Hata | Ne zaman |
| --- | --- |
| `NoAgent` | Entity canlı değil ya da ne `BehaviorAgent`'ı ne de `AiPerception`'ı var |
| `NoTree` | Agent'ın tree'si henüz yüklenmedi ya da yüklenemedi |
| `UnknownKey` | Tree'de o adda bir key yok |
| `TypeMismatch` | Key başka bir tipte (Bool, Int ya da Float key'inde `clearValue` de) |
| `UnknownAction` | Entity'de o id'de çalışan bir action yok |
| `InvalidValue` | Bir değer sonlu değil ya da aralık dışı: negatif bir noise yarıçapı, 32 veya daha büyük bir team |
| `Full` | Bu update'in noise'ları dolu (`AiWorldDesc::maxNoisesPerUpdate`) |

Bir update'in action'ları bir sonraki AI update'ine kadar yayımda kalır: arada yapılan her `pollActions()`
çağrısı aynı action'ları döndürür (toplamı, span'in boyutunu aşsa bile), bu yüzden span'i en yoğun update'e göre
boyutlandırın ya da update başına bir kez okuyun. Bir action'ın `name`'i ve `runningNode`'un döndürdüğü string
bir sonraki update'e kadar geçerlidir.

```cpp
if (illusion::AiService* ai = context.service<illusion::AiService>())
{
    std::array<illusion::AiActionRequest, 16> actions{};
    const illusion::u32 count = ai->pollActions(actions);
    for (illusion::u32 i = 0; i < count && i < actions.size(); ++i)
    {
        if (actions[i].phase == illusion::AiActionPhase::Started && actions[i].name == "bite")
        {
            static_cast<void>(ai->finishAction(actions[i].entity, actions[i].action, bite(actions[i].entity)));
        }
    }
    static_cast<void>(ai->setEntity(creature, "target", player));
    if (jumped)
    {
        static_cast<void>(ai->reportNoise({.position = playerPosition, .radius = 8.0f, .instigator = player}));
    }
    std::array<illusion::AiPerceived, 8> seen{};
    const illusion::u32 seenCount = ai->perceived(creature, seen).valueOr(0u);
    showAlert(seenCount > 0);
}
```

[AI](../manual/ai.md#bir-modulden) kılavuz sayfası tree'leri, action'ları ve algılamayı anlatır.
