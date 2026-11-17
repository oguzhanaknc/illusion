# Oyun Runtime'ı

Bir `GameSession` çalışan tek bir oyundur: oyunun oynandığı World ve onu çalıştıran fizik, ses, input
action'ları, Asset çözücüleri ve [oyun modülleri](modules.md). Editörün Play modu ve dışa aktarılan oyuncu
aynı oturumu oluşturur; böylece bir oyun nerede çalışırsa çalışsın aynı System'lerden aynı sırayla geçer
(ADR 0042).

Header: `#include <illusion/runtime/GameSession.hpp>` (kütüphane `illusion::runtime`)

## Bir oyunu çalıştırma

```cpp
illusion::reflectRuntimeStructs();
illusion::ProjectSettings settings;
if (auto loaded = illusion::loadProjectSettings(files, settings); !loaded)
{
    return fail(loaded.error());
}

illusion::GameSessionDesc desc;
desc.jobs = &jobs;
desc.files = &files;
desc.settings = settings;
desc.modules = &modules;           // null runs no game code
desc.streamer = &streamer;         // background loads of clips and physics meshes
desc.renderAssets = &renderAssets; // null when nothing renders (servers, tests)
desc.gamepads = gamepads.get();
desc.uiAssets = &uiAssets;         // fonts and images of the UI; null draws solid colours only
auto session = illusion::GameSession::create(desc);
if (!session)
{
    return fail(session.error()); // for example, the input actions asset cannot be read
}

illusion::loadScene((*session)->world(), sceneJson);
(*session)->start(); // modules start() on the World
while (running)
{
    streamer.update();
    gamepads->update(window.isFocused());
    (*session)->keyboardMouse() = window.input();
    (*session)->setInputFocus(window.isFocused());
    (*session)->setUiFrame({window.framebufferSize()});
    (*session)->update(frameSeconds);
    renderer.render((*session)->world());
}
(*session)->stop(); // the destructor stops it too
```

`create()` proje ayarlarından boş bir World ve System'lerini kurar ve `InputSettings::actions` ile
belirtilen input action'larını ve locale dosyalarını yükler. Sahneyi `world()` içine yükleyin; ardından `start()` World'ü
`ModuleHost`'a altı servisle birlikte bir Simulation World olarak ekler. Modüller bu servislere
`context.service<T>()` ile ulaşır: `PhysicsService`, `AudioService`, `ParticleService`, `InputService`,
`SceneService` ve `UiService`
(bkz. [scripting API](../reference/scripting-api.md)); `GameSessionDesc::store` ile `StoreService`
([Mağaza servisleri](#magaza-servisleri)), bir `SaveSystem` olan `GameSessionDesc::saves` ile de `SaveService`
eklenir (kayıtlı oyunlar, ADR 0058; player onları `user:/saves`'te, Play modu `cache:/saves`'te tutar).
`SceneService::requestExit()` çağıran bir
modül oyunu bitirir: session'ın `exitRequested()` değeri true olur ve host o update'ten sonra durur.

Host, diğer görünümlerle paylaştıklarını kendisi tutar: `AssetStreamer`'ı ve `GamepadSystem`'i her karede
bir kez günceller ve World'ü render eder.

## Adım sırası

Her `update(deltaSeconds)` şunları çalıştırır:

| Adım | Ne olur |
| --- | --- |
| 1. Input | `InputActions`, `keyboardMouse()`'u ve gamepad'leri okur; son update modal bir canvas gösterdiyse yalnızca `ui.*` action'ları input alır |
| 2. Modüller | World üzerinde `ModuleHost::update()`; ardından `EntityGuid`'siz oluşturulan Entity'ler bir tane alır |
| 3. Kayıtlar | `GameSessionDesc::saves` ile: kayıt thread'inin bitirdiği yüklemeler kayıtlı Entity'lerin yerini alır ve biten istekler bir sonraki update'in `SaveService` olayları olur; ardından 2. adımdaki `save`, `load` ve `remove` çağrıları çağrı sırasıyla gönderilir, her `save` World'ü o anda yakalar. `fixedStep` ile önce kayıt thread'ini bekler, böylece sonuçlar her zaman bir sonraki update'te gelir |
| 4. Animasyon | `GameSessionDesc::streamer` ile: `AnimationWorld::update()` Animator'lara poz verir (bkz. [İskelet animasyonu](rendering.md#iskelet-animasyonu)); oturum başlatılmışken `time`'ları ilerler. `animation()` pozları renderer'a ve host araçlarına verir |
| 5. Fizik | `PhysicsWorld::update()`: gövdeler World ile eşitlenir, adım atar ve Transform'larını yazar |
| 6. Hiyerarşi | `updateTransformHierarchy()`: çocuklar ebeveynlerini izler, `Disabled` `Inactive` olur |
| 7. UI | `UiWorld`, `setUiFrame()`'in karesi için UI Entity'lerini yerleştirir, işaretçiyi ve `ui.navigate`, `ui.submit` ile `ui.cancel` action'larını işler (Play modu) ve çizim listesini kaydeder (`ui()`), bkz. [Oyun UI'ı](ui.md) |
| 8. Parçacıklar ve ses | `ParticleWorld::update()` ve `AudioWorld::update()`: kaynaklar Entity'lerini izler, dinleyici hareket eder |
| 9. Çözücüler | Asset referansları runtime Component'lerine dönüşür (`Collider` Mesh'leri, `MeshRenderer` → `RenderMesh`) |

Oturumun `localization()` nesnesi, `create()`'in okuduğu projenin `assets:/locales/*.json` dosyalarındaki
string tablolarını tutar: her dosyanın anahtarları ve `en.json`'un İngilizce metni. `UiText::key` orada aranır
([Oyun UI'ı](ui.md#literal-and-localized-text)); `ui()`, hostun sahnenin üzerine çizdiği çizim listesini verir.

Ardından `keyboardMouse()`'un basılma ve bırakılma kenarları temizlenir; böylece host bir sonraki karenin
input'unu basılı tuşların üzerine yazar. `updateCount()` ve `time()` güncellemeleri ve oyun zamanını sayar.

## Modlar ve determinizm

| Ayar | Etkisi |
| --- | --- |
| `GameSessionMode::Play` | Oyunun tamamı |
| `GameSessionMode::Simulate` | Input ve modül yok: fizik gövdeleri hareket ettirir, oyun kodu çalışmaz |
| `fixedStep = true` | Her `update()`, geçen zamandan bağımsız olarak tam bir fizik adımı ilerler |
| `seed` | `fixedStep` ile, çalışırken oluşturulan Entity'ler EntityGuid'lerini bu tohumdan alır; oyun kodunun `SceneService` ile oluşturduğu sahneler ve prefab'lar da |

`fixedStep` ile aynı sahne, input ve tohum aynı World'ü verir: testler 600 adımdan sonra kaydedilen
sahnenin hash'ini alıp karşılaştırır. Bu, aynı Build ve makinede geçerlidir; platformlar arasında sonuçlar
farklı olabilir.

## Input ve odak

Oyun, host'un `keyboardMouse()` içine yazdığı klavye ve fareyi görür: oyuncuda pencerenin input'u,
editörün Game görünümünde viewport'a göre bir kopya, bir testte ise enjekte edilen tuşlar.
`setInputFocus(false)` hem klavyeyi hem gamepad'leri action'lardan gizler; böylece basılı action'lar
bırakılır. `InputSettings::gamepadsInBackground` ile gamepad'ler odak olmadan da oyuna ulaşır.

UI aynı input'u okur ([Input ve odak](ui.md#input-and-focus)). Projenin input Asset'inde yoksa `create()`,
`ui.navigate`, `ui.submit` ve `ui.cancel` action'larını varsayılan atamalarla ekler. Son update modal bir
canvas'ta ([Modal canvas'lar](ui.md#modal-canvases)) bir eleman gösterdiği sürece session
`InputActions::setExclusivePrefix("ui.")` çağırır; böylece diğer tüm action'lar bırakılmış okunur, canvas
gidince de bunu temizler.

Son update odakta bir `UiTextField` bıraktığı sürece ([Metin alanları](ui.md#text-fields-scrolling-and-glyphs))
klavye onu düzenler: `ui.navigate` ve `ui.submit`'in klavye binding'leri yok sayılır, böylece onlara bağlı
harfler yazılır; yalnızca gamepad binding'leri sayılır, `ui.cancel` tüm binding'lerini korur ve diğer tüm
action'lar bırakılmış okunur.

İşaretçi, `keyboardMouse()` imlecinin `setUiFrame(frame, pointerScale)` içindeki `pointerScale` ile çarpımıdır
ve karenin piksellerindedir. Player, karenin boyutunun pencerenin boyutuna oranını verir; editörün Game görünümü
imleci kendi görüntüsünün pikselleriyle yazar ve 1 verir. Input odağı yoksa UI işaretçi görmez.

## Proje ayarları

Ayarlar [reflection](reflection.md) ile tanımlanmış struct'lardır; her biri `settings:/` altında kendi
ayar dosyasındadır. Var olmayan bir dosya varsayılanları korur, bu yüzden yeni bir projenin hiçbirine
ihtiyacı yoktur; `loadProjectSettings()`, okunamayan ya da geçersiz değerler içeren bir dosyada o dosyanın
adını vererek başarısız olur.

| Dosya | Struct | Alanlar |
| --- | --- | --- |
| `settings:/physics.json` | `PhysicsSettings` | `gravity`, `stepSeconds`, `maxStepsPerUpdate`, `layerNames`, `collisionMatrix` |
| `settings:/audio.json` | `AudioSettings` | `masterVolume`, `busVolumes` (Sfx, Music, Ambience, Dialogue, Ui), `maxVoices`, `speedOfSound` |
| `settings:/input.json` | `InputSettings` | `actions` (bir [input action Asset'i](actions.md#input-action-assetleri)), `gamepadsInBackground` |
| `settings:/render.json` | `RenderSettings` | `vsync`, `maxFramesPerSecond`, `renderScale` |
| `settings:/window.json` | `WindowSettings` | `width`, `height` (piksel), `mode` (`Windowed`, `Borderless` veya `Fullscreen`), `resizable` |
| `settings:/save.json` | `SaveSettings` | `cloud` (slot dosyalarını mağazanın bulutuna yansıtır, varsayılan olarak açık; bkz. [Buluttaki kayıtlı oyunlar](#buluttaki-kaytl-oyunlar)) |

```json
{
  "format": "illusion.settings",
  "type": "illusion.PhysicsSettings",
  "version": 1,
  "value": {"gravity": [0, -20, 0], "stepSeconds": 0.01}
}
```

`collisionMatrix` her katman için bir bit maskesi tutar: *a* ve *b* katmanları yalnızca *a* girdisinin *b*
biti ve *b* girdisinin *a* biti birlikte ayarlıysa çarpışır; herhangi bir tarafı temizlemek onları ayırır.
Oturum ayarları oluşturulduğunda uygular; bir değişiklik bir sonraki oturumda etkili olur.
`RenderSettings` oyunu gösteren host'a aittir. `WindowSettings` [player](#oyuncu-player)'a aittir: `width` ve
`height` bir `Windowed` ya da `Fullscreen` pencerenin boyutunu belirler, `Borderless` birincil monitörü kaplar ve
`resizable` oyuncunun bir `Windowed` pencereyi yeniden boyutlandırmasına izin verir. Editörün Game görünümü
bunları yok sayar.

Ayar dosyası formatı runtime'a bağlı değildir: `writeSettingsFile()` ve `readSettingsFile()`
(`illusion/ecs/SettingsFile.hpp`) reflection ile tanımlanmış her struct'ı aynı biçimde kaydeder.

## Oyuncu (player)

`runPlayer()` (`illusion/player/Player.hpp`, `illusion::player` kütüphanesi) dışa aktarılmış bir oyunun
çalıştırılabilir tarafıdır ([ADR 0055](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0055-exported-games.md)). `main()` içinden bir kez, bir
`PlayerDesc` ile çağrılır: oyunun `name`'i (pencere başlığı ve kullanıcı veri dizini), `organization`,
`version` (log'a yazılır), `startupScene` ve statik `modules`. Sonra şunları yapar:

1. `assets`, `settings` ve `cache`'i veri dizinindeki `assets.illpak`, `settings.illpak` ve `cache.illpak`
   arşivlerinden mount eder; veri dizini, çalıştırılabilir dosyaya göre `PlayerDesc::dataDirectory`'dir
   (varsayılan `data`; bir macOS bundle'ında `Contents/Resources`). `user`'ı oyunun kullanıcıya özel dizinine,
   `userDataDirectory(organization, name)`'e okuma-yazma olarak mount eder;
2. log'u `user:/player.log` dosyasına yazar;
3. [proje ayarlarını](#proje-ayarlar) yükler, `WindowSettings`'in penceresini ve bir Vulkan cihazını açar,
   statik modülleri ekler ve bir `GameSession` oluşturur;
4. başlangıç sahnesini yükler ve pencere kapanana ya da bir modül `SceneService::requestExit()` çağırana
   (oyunun kendi menüsündeki bir Çıkış düğmesi) kadar oyunu çalıştırır; çıkış kodu bu durumda `Ok` olur.

Process'in çıkış kodu bir `PlayerExit` değeridir: `Ok` (0), `StartFailed` (1: pencere ya da Vulkan cihazı
yok, ya da bir modül veya başlangıç sahnesi yüklenemedi), `DataMissing` (2: veri dizinindeki bir arşiv eksik
veya bozuk) ya da `BadArguments` (3: komut satırı okunamadı). Oyun başlayamadığında player nedeni İngilizce
olarak log'a yazar ve `--no-dialogs` verilmedikçe string table'ındaki bir metinle bir mesaj kutusu gösterir.

Komut satırı:

| Seçenek | Etki |
| --- | --- |
| `--windowed`, `--borderless`, `--fullscreen` | `WindowSettings::mode`'u geçersiz kılar. |
| `--size <width>x<height>` | Pencerenin boyutunu geçersiz kılar. |
| `--data <directory>` | `PlayerDesc::dataDirectory`'yi geçersiz kılar (aynı şekilde çalıştırılabilir dosyaya göre). |
| `--user-data <directory>` | `user`'ı kullanıcı veri dizini yerine oraya mount eder; testler ve taşınabilir kurulumlar içindir. |
| `--fixed-step` | Oturumu deterministik çalıştırır. |
| `--frames <count>` | Oyunu o kadar frame sonra bitirir. |
| `--screenshot <file.png>` | Oyun bittiğinde son frame'i PNG olarak yazar. |
| `--no-dialogs` | Hataları log'da ve stderr'de bırakır. |

```bash
showcase_game --windowed --size 1280x720
showcase_game --fixed-step --frames 300 --screenshot shot.png --no-dialogs
```

`illusion_add_game()` (`cmake/IllusionGame.cmake`) bir oyunun çalıştırılabilir dosyasını üretir: `PlayerDesc`'i
dolduran `main()`'i oluşturur, `illusion::player` ile oyunun statik modüllerini bağlar ve `PROJECT` verilirse
projenin verisini `illusion_pack` ile çalıştırılabilir dosyanın yanına paketler. Showcase projesi böyle
derlenir:

```cmake
illusion_add_module(showcase_game_module STATIC ID showcase
    SOURCES showcase/modules/showcase/showcase.cpp
    USES physics audio particles input render runtime store ui save)
illusion_add_game(showcase_game
    MODULES showcase_game_module
    NAME "Showcase"
    ORGANIZATION "illusion"
    VERSION "0.1.0"
    STARTUP_SCENE "assets:/scenes/everything.scene"
    PROJECT showcase)
```

`illusion_add_game()` ve `illusion::pack`'i sağlayan CMake paketi ve kurulumu [Player SDK](modules.md#player-sdk) bölümünde anlatılır.

Editor'ün bu oyunu sizin yerinize nasıl export ettiği [Oyunu Export Etmek](editor/export.md) sayfasında anlatılır.

## Mağaza servisleri

Oyun, satıldığı mağazaya `StoreService` (`illusion/store/StoreService.hpp`, `USES store`;
[ADR 0056](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0056-store-service-and-steamworks.md)) üzerinden ulaşır: başarımlar ve
ilerlemeleri, tamsayı ve float istatistikler, bulut dosyaları, overlay ve gamepad glyph'leri. Aynı çağrılar her
mağazayla ve mağaza olmadan çalışır; böylece oyun kodu Play modu, testler ve Steam arasında değişmez.

```cpp
void update(illusion::ModuleContext& context, illusion::f64)
{
    illusion::StoreService* store = context.service<illusion::StoreService>();
    if (store == nullptr || store->overlayActive())
    {
        return; // no store service, or the overlay covers the game
    }
    if (won(context.world()))
    {
        static_cast<void>(store->unlockAchievement("FIRST_WIN"));
        static_cast<void>(store->setStatInt("wins", wins(context.world())));
        static_cast<void>(store->storeStats());
        const std::array<std::byte, 4> save = saveGame(context.world());
        if (const auto written = store->writeCloudFile("slot1.save", save); !written)
        {
            ILL_LOG_WARN("Game", "not saved: {}", illusion::storeErrorName(written.error()));
        }
    }
}
```

- **Callback yok.** Her sonuç döndürülür ya da sorgulanır. `overlayActive()` oyunun her karede okuduğu bir
  değerdir (oyunlar overlay açıkken genellikle durur); hiçbir şey bir modülü geri çağırmaz, bu yüzden hot reload
  güvenli kalır.
- **Hatalar** `Expected<T, StoreError>`'dır: `Unavailable`, `InvalidName`, `InvalidValue`, `NotFound`,
  `TypeMismatch`, `TooLarge` veya `QuotaExceeded`, `IoError`. Başarım, istatistik ve bulut dosyası adları 1 ile
  64 arası ASCII harf, rakam, `_`, `-` ve `.`'dır; `.` ile başlamaz ve bitmez. Bir bulut dosyası en çok 100 MiB
  tutar.
- **Container değil, span.** `readCloudFile()` oyunun bir tamponunu doldurur ve dosyanın boyutunu döner (boş
  bir tampon yalnızca boyutu sorar); `listCloudFiles()` bir ad span'ini doldurur ve kaç tane olduğunu döner.

Bir oyunun hangi mağazayı aldığı:

| Nerede | Mağaza | Bulut dosyaları |
| --- | --- | --- |
| Editor'deki Play modu | Her oturum için yeni bir `NullStoreService` | Projenin `cache:/store/cloud`'u |
| Mağazasız export edilmiş oyun | `NullStoreService` | `user:/cloud` |
| `store.kind` değeri `steam` olan export edilmiş oyun | Steam çalışıyorsa `SteamStoreService`, yoksa `NullStoreService` | Steam Cloud ya da `user:/cloud` |

`NullStoreService` `available()` için false, `storeName()` için "none" bildirir. Başarımları, ilerlemeyi ve
istatistikleri oturum boyunca bellekte tutar, böylece oyun mantığı test edilebilir; bulut dosyalarını dosya
olarak saklar (toplam 100 MiB), böylece kayıtlar mağaza olmadan da çalıştırmalar arasında kalır.
`openOverlay()` false, `glyphPath()` hiçbir şey döner.

### Buluttaki kayıtlı oyunlar

Mağaza bulut dosyalarını uzakta tuttuğunda (`StoreBackend::cloudIsRemote()`: Steam evet, `NullStoreService`
hayır), `available()` olduğunda ve `SaveSettings::cloud` açıkken `SaveService` slot dosyalarını yansıtır: yazılan
her slot `writeCloudFile("<slot>.save")` ile kopyalanır, silinen her slot `removeCloudFile()` ile silinir. Oturum
başladığında `savedAt` değeri yerel slotunkinden yeni olan bir `<slot>.save` bulut dosyası onun yerini alır ve
yerel dosya slotun yedeği olur; daha eski ya da aynı yaştaki bir bulut dosyası hiçbir şeyi değiştirmez. Host,
`GameSessionDesc::storeCloudIsRemote` değerini mağazasından ayarlar. Bulut hataları (yer kalmaması, bozuk bir bulut
dosyası) yalnızca loglanır: kaydın kendisi yine başarılı olur ve oyun bunlar için bir olay görmez. Null mağazanın
bulut dosyaları zaten yerel dosyalardır, bu yüzden oraya hiçbir şey yansıtılmaz. Yansıtma varken oturumu
durdurmak kayıt thread'ini bekler ve bitirdiği yazma ve silmeleri yansıtır; böylece oyun kapanmadan hemen önce
yapılan bir kayıt da buluta ulaşır.

### Slot görüntüleri

Kaydedilen bir slot bir görüntü alır: son gösterilen kare, 16:9'a kırpılıp 320 x 180'e ölçeklenir ve slot
dosyasının yanına `<slot>.png` olarak yazılır (`SaveSlotInfo::screenshot` onun sanal yolunu tutar ya da boştur).
Player kendi görünümünü, Play modu Game görünümünü (GPU'lu pencereli ya da headless editör) geri okur; kareyi
yalnızca bir kayıt geri okur. Buluttan alınan bir slotun, yeniden kaydedilene kadar görüntüsü yoktur. Görüntü
atomik yazmanın parçası değildir: bir çökmeden sonra bir slot önceki kaydının görüntüsünü gösterebilir.

`SteamStoreService` (`illusion::steam` kütüphanesi) yalnızca `ILLUSION_STEAMWORKS_SDK` CMake değişkeni bir
Steamworks SDK'sını gösterdiğinde Build edilir; SDK'yı Valve'dan siz indirirsiniz, illusion onu hiçbir zaman
dağıtmaz ya da indirmez. Başarımları, istatistikleri, Remote Storage'ı ve overlay'i eşler; henüz glyph
döndürmez. Player onu pencere açılmadan önce başlatır, böylece overlay grafik cihazına bağlanabilir, ve her
karede bir kez `update()`'ini çağırır. Bir Steam oyununu export etmek [Oyunu Export Etmek](editor/export.md#magaza)
sayfasında anlatılır.

!!! warning "Burada test edilmedi"
    Steam mağazası SDK olmadan Build edilemez; bu yüzden ne CI ne de engine'in testleri onu Build eder ya da
    çalıştırır. Yayınlamadan önce bir Steam export'unu SDK'lı ve Steam'li bir makinede test edin.
