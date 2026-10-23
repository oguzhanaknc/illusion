# Editör Çekirdeği

Başlık: `#include <illusion/editor/core/EditorContext.hpp>` (hedef `illusion::editor_core`, yalnızca editör)

Editör çekirdeği, penceresi olmayan editördür: açık proje, düzenlenen sahne, seçim ve bunları değiştiren
her komut. Paneller, kısayollar ve komut paleti onun komutlarını çağırır; AI ajanları da aynı komutları RPC
ve MCP üzerinden çağırır (M7j'den itibaren). Her şey penceresiz çalışır; testler de onu böyle sürer.

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
Component'lerin) döndürür; istemcinin sahneyi yeniden okuması gerekiyorsa `false` döner. `events()`;
`scene.changed`, `selection.changed`, `history.changed`, `history.trimmed`, `scene.opened`, `scene.saved` ve
`scene.autosaved` event'lerini her `endFrame()` çağrısında bir kez iletir.

## Çökme journal'ı ve kurtarma

Bir proje açıkken uygulanan her değişiklik `.illusion/journal/<session>.jsonl` dosyasına eklenir ve her
karenin sonunda diske yazılır. Düzgün bir kapanış dosyayı siler; geride kalan bir journal çöken bir oturuma
aittir. `project.recover` (veya `EditorContext::recover()`) o journal'ın başladığı sahneyi açar ve journal'ı
tek bir undo adımı olarak yeniden oynatır. Kaydedilmemiş değişiklikleri olan bir sahne ayrıca her 60 saniyede
`.illusion/autosave/` içine kaydedilir; dönüşümlü üç kopya tutulur.

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
