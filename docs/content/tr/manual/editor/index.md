# Editör Çekirdeği

Başlık: `#include <illusion/editor/core/EditorContext.hpp>` (hedef `illusion::editor_core`, yalnızca editör)

Editör çekirdeği, penceresi olmayan editördür: açık proje, düzenlenen sahne, seçim ve bunları değiştiren
her komut. Paneller, kısayollar ve komut paleti onun komutlarını çağırır; AI ajanları da aynı komutları
[RPC](automation.md) üzerinden çağırır (MCP M7k'den itibaren). Her şey penceresiz çalışır; testler de onu
böyle sürer.

## Component panosu ve görünüm durumu

`component.copy` bir Component'i ayrı bir bellek içi panoya kopyalar; Entity referanslarını kalıcı id'lerle
korur. `component.paste` değeri bütün hedeflere tek undo adımında uygular; eksik Component'ler ve
gerektirdikleri Component'ler birlikte eklenir. Bir hata yapıştırmanın tamamını geri alır. Entity panosu
bağımsızdır. Kopyalama ve görünüm durumunu değiştirme undo adımı oluşturmaz.

`component.reset`, yalnızca bir alanı sıfırlamak için isteğe bağlı `path` alır. Component komut yollarında
alanlar ve dizi indisleri `/` ile ayrılır; vektör eksenleri `x`, `y`, `z` ve `w` de kabul eder.

```json
{"method":"component.copy","params":{"entity":"/Light","type":"illusion.Transform"}}
{"method":"component.paste","params":{"entities":["$selection"]}}
{"method":"component.reset","params":{"entity":"/Light","type":"illusion.Transform","path":"position/x"}}
{"method":"view.set_entities","params":{"entities":["/Game"],"hidden":true,"locked":true}}
```

`view.get` gizli ve kilitli Entity'leri listeler. `view.set_entities`, `recursive` false olmadıkça alt
Entity'leri de kapsayarak iki bayraktan birini değiştirir. Bu durum mevcut editör oturumuna aittir,
sahne dosyalarına yazılmaz ve sahne değiştirildiğinde temizlenir. Dry run iki panoyu ve görünüm durumunu
değiştirmez.

## Editör bağlamı

`EditorContext`; [proje mount'larıyla](projects.md) bir `VirtualFileSystem`'i, açık sahneyi tutan tek bir
World'ü, seçimi, undo geçmişini, journal'ları ve komut kaydını sahiplenir:

```cpp
using namespace illusion::editor;

EditorContext editor(EditorContextOptions{.seed = 42}); // seeded ids for reproducible sessions
if (auto opened = editor.openProject("C:/Games/Tetris"); !opened)
{
    ILL_LOG_ERROR("Editor", "{}", opened.error().message);
}

EntityCreateParams params;
params.name = "Board";
auto created = editor.execute<EntityResult>("entity.create", params);
// created->entity.id is the new EntityGuid as 16 hex digits, created->entity.path "/Board"

editor.endFrame(1.0 / 60.0); // events, crash journal, autosave
```

Komutlar bağlamın sahibi olan thread'de, karenin komut noktasında çalışır; hiçbir zaman bir Query dolaşırken
çalışmaz. Bir komut başka bir komutu çalıştıramaz.

## Komutlar

Her komutun bir tanımlayıcısı vardır: `domain.verb` biçiminde bir isim, bir sürüm, yerelleştirilmiş başlık
ve açıklama, ajanlar için örnek bir çağrı içeren İngilizce bir özet, bir kategori, bir ikon, bayraklar, bir
izin sınıfı ve isteğe bağlı bir varsayılan kısayol. Parametreleri ve sonucu `reflectStruct()` ile reflect
edilen struct'lardır; JSON Schema'ları (2020-12) bunlardan üretilir.
[Komut referansı](../../reference/editor-commands.md) hepsini listeler.

Bir komutu çalıştırmanın iki yolu vardır ve ikisi de aynı yoldan geçer:

| Çağrı | Kullanan |
| --- | --- |
| `execute<Result>(name, params, options)` | C++ kodu: paneller, gizmo'lar, araçlar |
| `executeJson(name, json, options)` | RPC, MCP, script'ler ve testler; sonuç `_editor` üyesi olan bir JSON'dur |

```json
{"name": "Board", "parent": "/Game", "components": {"illusion.Transform": {"position": [4.5, 9.5, 0]}}}
```

JSON parametreler hiçbir şey çalışmadan önce kontrol edilir: `std::optional` olmayan her alan zorunludur,
bilinmeyen üyeler hatadır (en yakın alan isimleri öneri olarak verilir) ve her hata, değere işaret eden bir
JSON Pointer taşır; örneğin `/components/illusion.Transform/position`. Salt okunur olmayan her komut şu
seçenekleri de alır:

| Seçenek | Anlamı |
| --- | --- |
| `dryRun` | Hiçbir şeyi değiştirmeden doğrular ve kaç işlem yapacağını bildirir. |
| `mergeKey` | Aynı anahtar ve kaynakla yapılan sürekli düzenlemeler (bir sürükleme) tek undo adımı olur. |
| `expectRevision` | Sahne bu revizyonda değilse `conflict` ile başarısız olur. |
| `clientToken` | Bir idempotency anahtarı: [RPC](automation.md) üzerinden aynı token tekrarlanırsa komut yeniden çalışmaz, ilk yanıt döner. |

## Entity referansları

Komutlar Entity'leri string olarak alır ve `{id, name, path}` olarak döndürür; ham `Entity` Handle'ları asla
API'yi geçmez.

| Biçim | Örnek |
| --- | --- |
| 16 hex basamak: bir id | `9f2c41aa0b7d3e15` |
| İsimlerden oluşan hiyerarşi yolu | `/Game/Board/Cell 3` |
| `name:` ve yalnızca bir Entity'nin taşıdığı bir isim | `name:Board` |
| Tam olarak bir Entity seçiliyken seçim | `$selection` |

İsmi olmayan bir Entity'nin yol parçası `#` ve id'sidir. Entity listeleri seçili her Entity için
`$selection` alır. Birden fazla Entity'ye uyan isimler `ambiguous` verir; yanlış yazılanlar yakın eşleşmeleri
listeler.

## Değerler

Component değerleri scene v2 kodlamasını kullanır: vektörler dizi, quaternion'lar `[x, y, z, w]`, enum'lar
isim, Entity alanları id, Asset alanları sanal yol, u64 değerleri ondalık string'dir. Birimi olan alanlar
`{"deg": 90}` gibi etiketli bir sayıyı, renk alanları da `"#rrggbb"` değerini kabul eder. `component.set`
birleştirir: belirtmediği alanlar değerlerini korur.

## Transaction'lar ve undo

Geri alınabilir komutlar bir transaction içinde çalışır. Her temel düzenleme önce doğrular, sonra geri
alınabilir işlemler kaydeder: Component değerlerinin önceki ve sonraki hâlleri, ortaya çıkan ya da kaybolan
Entity'lerin anlık görüntüleri ve dosya değişiklikleri. Başarısız olan bir komut ve her dry run geri alınır;
tamamlanan bir komut etiketi, kaynağı (`ui`, `inspector`, `gizmo`, `rpc:<client>`, ...) ve öncesi ile
sonrasındaki seçimiyle bir undo adımı olur.

- Bir Entity'yi silmek alt Entity'lerini de siler ve başka yerlerde onlara işaret eden Entity alanlarını null
  yapar; undo aynı id'leri ve referansları geri getirir.
- Her şeyi geri almak sahneyi byte byte geri verir; her şeyi yeniden yapmak son sahneyi verir.
- Geçmiş en fazla 256 MiB tutar (`EditorContextOptions::undoBudget`); eski adımlar bir `history.trimmed`
  event'iyle atılır.
- Seçim yapmak bir undo adımı değildir, ama undo ve redo kendi adımlarının seçimini geri yükler.
- Kaydetmek undo durumunu hatırlar; o duruma geri dönmek sahneyi yeniden temiz yapar.

### Değişiklikleri gruplamak

`transaction.begin {"label": "Build board"}` bir transaction açar: aynı çağıranın sonraki geri alınabilir
komutları hemen uygulanır ama `transaction.commit` çalışınca tek bir undo adımı olur; `transaction.cancel`
ise hepsini geri alır ve seçimi geri yükler. Bir transaction onu açana aittir (`CommandOptions::caller`: RPC
istemcileri için bağlantı, editörün kendisi için 0). Açık olduğu sürece:

- diğer çağıranların geri alınabilir komutları `busy` (1002, `retryable`) ile başarısız olur; okumak ve seçmek
  çalışmaya devam eder;
- sahneyi değiştiren, kaydeden ya da geçmişte gezinen komutlar (`scene.new`, `scene.open`, `scene.save`,
  `scene.save_as`, `project.open`, `project.create`, `project.recover`, `history.list` dışındaki `history.*`;
  `outsideTransactions` bayrağı) herkes için başarısız olur;
- her komut yine revizyonu artırır, yani `expectRevision` içeride de çalışır;
- RPC bağlantısı kapanınca editör transaction'ı iptal eder.

### Batch'ler

`editor.batch` bir komut listesini sırayla tek transaction ve tek undo adımı olarak çalıştırır. `as` ile
adlandırılan bir adım sonraki adımlarda kullanılabilir: onların parametrelerindeki `"$name.path.to.field"`
string'i o adımın sonucundaki değere dönüşür (dizi elemanları indisle; `"$$"` düz bir `$` yazar,
`"$selection"` anlamını korur).

```json
{"label": "Board", "steps": [
  {"as": "board", "method": "entity.create", "params": {"name": "Board", "parent": "/Game"}},
  {"method": "entity.create", "params": {"name": "Cell 1", "parent": "$board.entity.id"}},
  {"method": "selection.set", "params": {"entities": ["$board.entity.path"]}}
]}
```

Hiçbir adım çalışmadan önce hepsi denetlenir (metot adları, `as` adları, parametre nesneleri); async komutlar,
`transaction.*` ve iç içe batch'ler adım olamaz. Atomik bir batch (varsayılan) bir adım başarısız olunca her
şeyi geri alır ve o adımın hatasını işaretçisi `/steps/<i>/params` altında olacak şekilde döndürür;
`"atomic": false` ile başarısız adımlar `steps[i].error` içinde bildirilir ve diğerleri korunur. Aynı
çağıranın açık bir transaction'ı içindeki batch ona katılır; başarısız atomik bir batch yalnızca kendi
adımlarını geri alır. Bir batch'in dry run'ı geri alınabilir adımları bir deneme transaction'ında
(`TransactionInfo::trial`) çalıştırır ve sonunda revizyonu değiştirmeden iptal eder; ne geri alınabilir ne de
salt okunur olan adımları yalnızca doğrular. Adımlar çağrının kapısıyla (`CommandOptions::gate`) çalışır,
yani RPC izin modu her birine uygulanır.

### Kendi adımlarını geri almak

`history.undo {"scope": "mine"}`, çağıranın kaynağına ait son adımları, ardından başka kaynakların adımları
gelmiş olsa bile geri alır; yeter ki o sonraki adımlar bağımsız olsun (`findOverlap()`): bir Entity'nin aynı
Component'ini değiştirmemiş, adımın değiştirdiği ya da referans verdiği bir Entity'yi oluşturmamış veya
silmemiş, aynı dosyaya ya da ayara dokunmamış olmalılar. Aksi hâlde `conflict` (1203) ile başarısız olur ve
hiçbir şeyi değiştirmez. Geri alınan adım redo tarafına geçer; `history.redo` onu yeniden uygular.

### Async komutlar

Birkaç frame süren komutlar `CommandRegistry::addAsync()` ile kaydedilir: handler'ları parametreleri doğrular
ve bir `CommandTask` döndürür; editör onu her frame'de bir `CommandProgress` ile bir adım ilerletir.
`executeJson()` onları reddeder; `startJson()` `update()` ile ilerletilen ve iptal için yok edilen bir
`RunningCommand` döndürür. Async komutlar geri alınabilir olamaz. İlki `editor.wait {"frames": 60}`.

```cpp
const illusion::JsonDocument params = std::move(illusion::JsonDocument::parse(R"({"frames": 60})").value());
auto started = editor.startJson("editor.wait", params.root());
if (started)
{
    std::unique_ptr<illusion::editor::RunningCommand> running = std::move(started).value();
    // Once per frame, on the editor's thread:
    auto step = running->update(); // std::nullopt while it runs; the result JSON or an error at the end
}
```

## Hatalar

Başarısızlıklar; bir tür, İngilizce bir mesaj, geçersiz değerler ve öneriler içeren bir `CommandError`
döndürür. Türlerin JSON-RPC ve MCP aralıklarının dışında sabit kodları vardır:

| Kod | Tür | Kod | Tür |
| --- | --- | --- | --- |
| 1000 | `unauthenticated` | 1203 | `conflict` |
| 1001 | `permission_denied` | 1204 | `ambiguous` |
| 1002 | `busy` | 1205 | `precondition_failed` |
| 1003 | `timeout` | 1300 | `io_error` |
| 1004 | `cancelled` | 1301 | `build_failed` |
| 1100 | `wrong_mode` | 1302 | `module_error` |
| 1200 | `not_found` | 1400 | `unsupported` |
| 1201 | `already_exists` | 1401 | `limit_exceeded` |
| 1202 | `validation_failed` | 1500 | `internal` |

## Değişiklikler ve event'ler

Her commit, undo ve redo sahne revizyonunu artırır. `changes().changesSince(revision, out)`, bir istemcinin
hatırladığı revizyondan bu yana hangi Entity'lerin oluşturulduğunu, silindiğini veya değiştiğini (ve hangi
Component'lerin), ayrıca hangi sahne ayarlarının değiştiğini döndürür; istemcinin sahneyi yeniden okuması gerekiyorsa `false` döner. `events()`;
`scene.changed`, `selection.changed`, `history.changed`, `history.trimmed`, `scene.opened`, `scene.saved` ve
`scene.autosaved` event'lerini her `endFrame()` çağrısında bir kez iletir; komut kaydedilen bir frame'in
sonunda da `commands.changed` (`{"commands": <sayı>}`) gönderir.

## Çökme journal'ı ve kurtarma

Bir proje açıkken uygulanan her değişiklik `.illusion/journal/<session>.jsonl` dosyasına eklenir ve her
karenin sonunda diske yazılır. Düzgün bir kapanış dosyayı siler; geride kalan bir journal çöken bir oturuma
aittir. `project.recover` (veya `EditorContext::recover()`) o journal'ın başladığı sahneyi açar ve journal'ı
tek bir undo adımı olarak yeniden oynatır. Kaydedilmemiş değişiklikleri olan bir sahne ayrıca her 60 saniyede
`.illusion/autosave/` içine kaydedilir; dönüşümlü üç kopya tutulur.

## Transform'lar

`transform` komutları Entity'leri Component değerlerini elle yazmadan yerleştirir:

| Komut | Ne yapar |
| --- | --- |
| `transform.get` | Dünya uzayındaki ya da parent'a göre yerleşimi verir; dönüşü ayrıca derece cinsinden Euler açılarıyla verir. |
| `transform.set` | Konumu, dönüşü ve ölçeği dünya uzayında (varsayılan) ya da parent'a göre ayarlar. |
| `transform.translate` | Bir öteleme kadar taşır: dünya eksenleri, parent'ın eksenleri (`local`) ya da Entity'nin kendi eksenleri (`self`) boyunca. |
| `transform.rotate` | Her Entity'yi kendi konumu, hepsinin merkezi (`center`) ya da bir nokta (`point`) etrafında döndürür. |
| `transform.scale` | Ölçeği çarpar; bir pivot verilirse konumlar da ondan uzaklaşır. |
| `transform.reset` | Parent'a göre konumu, dönüşü ve ölçeği sıfırlar. |
| `transform.snap_to_grid` | Dünya konumlarını bir grid adımının en yakın katına yuvarlar (varsayılan olarak gizmo'nun taşıma adımı). |

Dönüşler quaternion `[x, y, z, w]` (`rotation`) ya da derece cinsinden Euler açılarıdır (`euler`;
`quatFromEuler()` gibi önce yaw, sonra pitch, sonra roll uygulanır). Komutlar bir çocuk için `LocalTransform`
değerini (uzay `local` ise tam olarak) ve türetilmiş `Transform` değerini yazar; bir kök için `Transform`
yazar ve `LocalTransform` değerini ona eşit tutar. Atalarından biriyle birlikte listelenen bir Entity bir kez,
atasıyla birlikte hareket eder.

```json
{"entities": ["/Game/Board", "/Game/Player"], "euler": [0, 90, 0], "pivot": "center"}
```

Editör her değişiklikten sonra ve bir sahne açıldığında her çocuğun dünya `Transform` değerini
`LocalTransform` değerinden yeniden türetir (`EntityIndex::updateTransforms()`); böylece bir sonraki komut
güncel dünya yerleşimlerini görür: `keepWorld` ile `hierarchy.set_parent`, yükledikten hemen sonra bile bir
Entity'yi olduğu yerde tutar. Orada yalnızca türetilmiş değerler yazılır; kaydedilen değerler yalnızca
komutlarla değişir, böylece undo tam kalır.

## Pano

`clipboard.copy` Entity'leri alt Entity'leriyle birlikte scene v2 JSON olarak editörün panosuna koyar ve bu
metni döndürür; kopyalanan kökler dünya yerleşimlerini korur. `clipboard.paste` onları yeni id'lerle,
verilmişse bir `parent` altında yeniden oluşturur ve seçer. Kopyalanan Entity'ler arasındaki Entity alanları
yeni kopyalara işaret eder; kopyanın dışına işaret eden alanlar null olur. `clipboard.cut` tek bir undo
adımında kopyalar ve siler. `clipboard.paste`, `text` içinde scene JSON da alır; böylece agent'lar hazırlanmış
Entity'leri ekleyebilir.

## Asset'ler ve referanslar

`asset` komutları `assets:/` altındaki dosyalarla çalışır: `asset.list` (`Folder`, `Scene`, `Material`,
`Model`, `Texture`, `Environment`, `AudioClip` ve `Other` türleriyle), `asset.info`, `asset.create_folder`,
`asset.move`, `asset.delete`, `asset.references` ve `asset.dependencies`.

Referanslar reflection ile bulunur: kaydedilen her Component'in, sahne ayarlarının, `.material` dosyalarının
ve proje ayar dosyalarının her `AssetRef` alanı. `ReferenceIndex`, `assets:/` altındaki her sahneyi ve
Material'i ve `settings:/` altındaki her ayar dosyasını okur (uzantıların büyük/küçük harfi fark etmez) ve
yalnızca içeriği değişen dosyaları yeniden ayrıştırır. Açık sahne dosyasından değil, düzenlenen World'den
okunur; böylece kaydedilmemiş değişiklikler de sayılır. Manifest'in başlangıç sahnesi de `project.illusion`
kaynaklı bir referans sayılır. `assets:/models/ship.glb` referansı alt Asset'leriyle (`#mesh/0`) de eşleşir;
bir klasör de içindeki her şeyle eşleşir.

- `asset.move` bir dosyayı ya da klasörü taşır ve ona yapılan her referansı tek bir undo adımında yeniden
  yazar: kapalı sahnelerde, Material'lerde ve ayar dosyalarında (yeniden yazılır) ve açık sahnede (onunla
  birlikte kaydedilir).
- `asset.delete`, silinen yolun dışındaki bir şey ona referans verdikçe reddeder; `force` ile yine de siler ve
  boşta kalan referansları döndürür. Undo dosyaları geri getirir.
- İki komut da açık sahnenin dosyasını taşımaz ya da silmez: önce sahneyi başka bir adla kaydedin.
  Başlangıç sahnesi de taşınamaz.

Türleri kayıtlı olmayan Component'ler ve ayarlar (dosyadaki hâlleriyle tutulur) aranmaz; `asset.references`
okuyamadığı dosyalar için uyarı verir.

## Material'ler

`material.create`, `material.get` ve `material.set`, `.material` dosyalarını `MaterialAsset` reflection'ı
üzerinden okur ve yazar: `material` altında `MaterialData` alanları, `textures` altında Texture yolları.
`material.set` birleştirir; yazan iki komut da geri alınabilir dosya değişiklikleridir.

```json
{"path": "assets:/materials/red.material", "value": {"material": {"baseColorFactor": "#cc2222", "roughnessFactor": 0.4}}}
```

## Ayarlar

Ayar türleri, `EditorContext::settings()` içine kaydedilen reflection'lı struct'lardır; her biri sahneler ya da
proje içindir:

```cpp
illusion::reflectStruct<game::Difficulty>("game.Difficulty").field("enemies", &game::Difficulty::enemies);
editor.settings().add("game.Difficulty", illusion::editor::SettingsScope::Scene);
editor.settings().add("game.Controls", illusion::editor::SettingsScope::Project, "settings:/controls.json");
```

- **Sahne ayarları** sahne dosyasının `settings` bölümünde durur. `scene.settings_get` kayıtlı her türü tüm
  alanlarıyla (sahne ayarlamıyorsa varsayılanlarla), türü bilinmeyen ayarları da kaydedildikleri gibi
  listeler; `scene.settings_set` bir yamayı bir undo adımı olarak birleştirir (`replace` diğer alanları
  sıfırlar, `null` ayarı kaldırır). Türü bilinmeyen ayarlar değişmeden yeniden kaydedilir.
- **Proje ayarları** `settings:` altında kendi dosyalarında durur; `illusion.settings` biçiminde, tür ve veri
  sürümüyle, böylece eski dosyalar taşınır (migrate). `project.settings_get` ve `project.settings_set` aynı
  şekilde çalışır; `null` dosyayı siler. Proje ayarları Entity referansı tutamaz.

## Log

Editör, sürecin en yeni log mesajlarını tutar (`EditorContextOptions::logCapacity`, varsayılan 1.024).
`log.read` onları en eskiden başlayarak, `minLevel`, `category` ya da `contains` ile süzerek döndürür:
`since` yoksa en yeni `limit` tanesini, `since` varsa ondan sonraki en eskileri; böylece dönen `last`
değerini yeniden `since` olarak vermek hiçbirini kaybetmeden okumaya devam eder. `log.clear` o ana kadar
loglananları gizler; `log.set_level` sürecin logladığı en düşük seviyeyi ayarlar.

## Komut yazmak

Bir handler, parametre struct'ını alıp sonuç struct'ını ya da bir hatayı döndüren bir fonksiyondur. World'ü
okur ve yalnızca `CommandContext::edit()` üzerinden değiştirir:

```cpp
struct NudgeParams
{
    std::string entity;
    f32 distance = 0.0f;
};

Expected<EntityResult, CommandError> nudge(CommandContext& context, const NudgeParams& params)
{
    Expected<Entity, CommandError> entity = context.entity(params.entity, "/entity");
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    const std::string value = "{\"position\":[" + std::to_string(params.distance) + ",0,0]}";
    const JsonDocument patch = std::move(JsonDocument::parse(value).value());
    if (auto moved = context.edit().setComponent(*entity, componentId<Transform>(), patch.root()); !moved)
    {
        return unexpected(std::move(moved).error());
    }
    return EntityResult{context.summary(*entity)};
}

reflectStruct<NudgeParams>("game.NudgeParams").field("entity", &NudgeParams::entity).field("distance", &NudgeParams::distance);
editor.commands().add<&nudge>({.name = "game.nudge", .title = strings::kNudgeTitle, .description = strings::kNudgeDescription,
                               .agentSummary = "Moves an entity along x. Example: {\"entity\": \"/Board\", \"distance\": 1}.",
                               .category = "Game", .flags = {.undoable = true}, .permission = PermissionClass::Edit});
```
