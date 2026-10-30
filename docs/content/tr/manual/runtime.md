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
`ModuleHost`'a dört servisle birlikte bir Simulation World olarak ekler. Modüller bu servislere
`context.service<T>()` ile ulaşır: `PhysicsService`, `AudioService`, `InputService` ve `SceneService`
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
| `seed` | `fixedStep` ile, çalışırken oluşturulan Entity'ler EntityGuid'lerini bu tohumdan alır |

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
`RenderSettings` oyunu gösteren host'a aittir.

Ayar dosyası formatı runtime'a bağlı değildir: `writeSettingsFile()` ve `readSettingsFile()`
(`illusion/ecs/SettingsFile.hpp`) reflection ile tanımlanmış her struct'ı aynı biçimde kaydeder.
