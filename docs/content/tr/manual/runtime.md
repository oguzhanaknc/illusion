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
    (*session)->update(frameSeconds);
    renderer.render((*session)->world());
}
(*session)->stop(); // the destructor stops it too
```

`create()` proje ayarlarından boş bir World ve System'lerini kurar ve `InputSettings::actions` ile
belirtilen input action'larını yükler. Sahneyi `world()` içine yükleyin; ardından `start()` World'ü
`ModuleHost`'a beş servisle birlikte bir Simulation World olarak ekler. Modüller bu servislere
`context.service<T>()` ile ulaşır: `PhysicsService`, `AudioService`, `ParticleService`, `InputService` ve
`SceneService`
(bkz. [scripting API](../reference/scripting-api.md)).

Host, diğer görünümlerle paylaştıklarını kendisi tutar: `AssetStreamer`'ı ve `GamepadSystem`'i her karede
bir kez günceller ve World'ü render eder.

## Adım sırası

Her `update(deltaSeconds)` şunları çalıştırır:

| Adım | Ne olur |
| --- | --- |
| 1. Input | `InputActions`, `keyboardMouse()`'u ve gamepad'leri okur |
| 2. Modüller | World üzerinde `ModuleHost::update()`; ardından `EntityGuid`'siz oluşturulan Entity'ler bir tane alır |
| 3. Fizik | `PhysicsWorld::update()`: gövdeler World ile eşitlenir, adım atar ve Transform'larını yazar |
| 4. Hiyerarşi | `updateTransformHierarchy()`: çocuklar ebeveynlerini izler, `Disabled` `Inactive` olur |
| 5. Ses | `AudioWorld::update()`: kaynaklar Entity'lerini izler, dinleyici hareket eder |
| 6. Çözücüler | Asset referansları runtime Component'lerine dönüşür (`Collider` Mesh'leri, `MeshRenderer` → `RenderMesh`) |

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
4. başlangıç sahnesini yükler ve pencere kapanana kadar oyunu çalıştırır.

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
    USES physics audio particles input render runtime)
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
