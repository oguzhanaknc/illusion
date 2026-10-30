# Yapay Zekâ Ajanları

Claude Code gibi bir yapay zekâ ajanı editörü, editörün Model Context Protocol (MCP) köprüsü olan
`illusion_mcp` üzerinden yönetir. Her editör komutu bir MCP aracıdır (tool); böylece bir ajan, editörün kendi
panellerinin yapabildiği her şeyi yapabilir: proje ve Entity oluşturmak, Component ayarlamak, nesneleri
taşımak, geri almak, kaydetmek ve sonucu incelemek. Tasarım:
[ADR 0039](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0039-mcp-bridge.md).

```text
Claude Code  --MCP (stdio)-->  illusion_mcp  --editor protocol-->  illusion_editor
```

Köprü ayrı bir süreçtir. Proje için zaten çalışan bir editöre bağlanır ya da penceresiz bir editör başlatır;
editör çökerse onu yeniden başlatır.

## Claude Code kurulumu

`project.create` ile oluşturulan bir proje, editörün yanındaki `illusion_mcp`'yi gösteren bir `.mcp.json`
içerir. Claude Code'u proje dizininde başlatın ve sorulduğunda `illusion` sunucusunu onaylayın.

Var olan bir proje için sunucuyu kendiniz ekleyin:

```bash
claude mcp add --transport stdio illusion -- /path/to/build/bin/Release/illusion_mcp --project .
```

ya da proje kökünde `.mcp.json` yazın:

```json
{"mcpServers": {"illusion": {"type": "stdio", "command": "/path/to/illusion_mcp", "args": ["--project", "."], "timeout": 600000}}}
```

[İlk ajan oturumunuz](../../tutorials/first-agent-session.md) eğitimi bir oturumu adım adım anlatır.

## Köprü seçenekleri

| Seçenek | Anlamı |
| --- | --- |
| `--project <dir>` | Proje: endpoint dosyası (bağlanma) ya da açılacak proje (başlatma). |
| `--auto` | Varsayılan: projenin editörü `--rpc-listen` ile çalışıyorsa ona bağlanır, yoksa bir tane başlatır. |
| `--attach` | Yalnızca bağlanır; çalışan editör yoksa başarısız olur. |
| `--launch` | Her zaman penceresiz bir editör başlatır (logu köprünün stderr'ine gider). |
| `--headless`, `--no-gpu`, `--deterministic`, `--seed <n>` | Başlatılan editöre iletilir. |
| `--permissions read-only\|ask\|auto` | Başlatılan editörün izin modu. |
| `--editor <path>`, `--editor-arg <arg>` | Başka bir editör yürütülebiliri, ek editör argümanları. |
| `--tools all\|core\|<domain>,...` | Hangi komutların araç olacağı (aşağıya bakın). |
| `--record <file.jsonl>` | Her MCP mesajını zaman damgası ve yönüyle kaydeder. |
| `--page-size <n>` | Liste sayfası başına araç, kaynak ve istem sayısı (varsayılan 100). |
| `--log-level trace\|debug\|info\|warn\|error\|off` | Köprünün log seviyesi (stderr). |

stdout yalnızca MCP mesajlarını taşır; her log satırı stderr'e gider. Köprü stdin'i kapanınca sona erer ve
başlattığı editörden kapanmasını ister.

## Araçlar

Her komut, `.` yerine `_` konmuş adıyla bir araç olur: `entity.create` → `entity_create`,
`project.file_read` → `project_file_read`. Bir aracın açıklaması komutun örnekli özetidir; ardından
parametreleri (türler, aralıklar, enum değerleri, zorunlu ya da isteğe bağlı), izin sınıfı ve uzun sürüp
sürmediği gelir. Girdi ve çıktı şemaları komutun JSON Schema'larıdır; `readOnlyHint`, `destructiveHint` ve
`idempotentHint` ipuçları komutun bayraklarından gelir.

`--tools core` yalnızca `editor`, `project`, `scene`, `entity`, `component`, `hierarchy`, `transform`,
`history` ve `log` komutlarını sunar ve ajanın bağlamını küçük tutar; `--tools entity,scene` alan seçer. İki
kaçış yolu her zaman vardır:

- `illusion_call {"method": "asset.list", "params": {}}` araç olarak sunulsun ya da sunulmasın her komutu
  çalıştırır.
- `illusion_batch`, `editor` alanı seçilmediğinde `editor.batch` komutunu çalıştırır.

Editörün komutları değiştiğinde (bir modül yenilerini kaydettiğinde) istemciler
`notifications/tools/list_changed` alır.

## Sonuçlar ve hatalar

Bir sonuç, komutun tam sonucunu `structuredContent` olarak ve en fazla yaklaşık 4 KB'lık bir metin özetini
taşır. Özet kesildiğinde bunu belirtir; aracın `limit` ve `cursor` parametreleri varsa daha azını isteyin.
Görüntüler (ekran görüntüleri, önizlemeler) uzun kenarı en fazla 1024 piksel olan PNG içerik olarak gelir.

Başarısız bir komut `isError: true` ile şunu döndürür:

```json
{"code": 1200, "kind": "not_found", "message": "no entity is named 'Board'", "path": "/entity", "hint": "Look up existing names with scene_tree, entity_find, component_types or asset_list.", "errors": [{"path": "/entity", "message": "no entity is named 'Board'"}], "suggestions": [], "retryable": false}
```

`code` ve `kind` [editör protokolündekilerle](../../reference/editor-protocol.md#hatalar) aynıdır; `path`
hatalı parametreyi gösterir. Bilinmeyen araçlar ve bozuk mesajlar bunun yerine JSON-RPC hataları alır.

## Kaynaklar ve istemler

| Kaynak | İçerik |
| --- | --- |
| `illusion://guide` | İş akışı, birimler, Entity referansları, geri alma, izinler, sınırlar. |
| `illusion://commands` | Her komut, araç adı ve özetiyle. |
| `illusion://project` | Açık proje (`project.info`). |
| `illusion://scene/current` | Etkin sahnenin Entity ağacı (`scene.tree`). |
| `illusion://scene/summary` | Yol, revizyon, Entity sayısı, içerik hash'i, kaydedilmemiş durum (`scene.info`). |
| `illusion://log/recent` | Editörün son 50 log mesajı. |
| `illusion://components/{stableName}` | Bir Component türünün şeması, ör. `illusion://components/illusion.Transform`. |

`new_game` (`name`, `idea`) ve `add_component_type` (`name`, `fields`) istemleri bir ajanı bu görevlere doğru
araçlarla başlatır.

## İzin modları

Editörün izin modu, ajanın neyi çalıştırabileceğine karar verir:

| Mod | Ajan şunu yapabilir |
| --- | --- |
| `read-only` | yalnızca okuma (sahne, Entity'ler, dosyalar). |
| `ask` | okuma ve düzenleme; dosya, Build, yıkıcı ve sistem komutları bir kişinin onayını ister (editörün penceresi yokken reddedilir). Editör penceresinin varsayılanı. |
| `auto` | her şeyi çalıştırma. Penceresiz editörün varsayılanı. |

Editör penceresinde onay gerektiren bir çağrı bekler: editörün başındaki kişi bir istem görür ve çağrıya bir
kez izin verir, bu tür komutlara oturumun geri kalanında izin verir ya da reddeder. AI Activity paneli bağlı
ajanları ve her çağrıyı parametreleri, süresi ve sonucuyla gösterir; kişi buradan bir ajanı duraklatabilir,
bağlantısını kesebilir ve modu değiştirebilir (bkz. [Editör Penceresi](interface.md#ai-activity)). Bekleyen bir
çağrı ilerleme göstermez; ajanın sonraki çağrıları onun arkasında bekler. **Follow agent** açıkken Scene
görünümü ajanın yaptığı her seçimi çerçeveler (bkz. [Scene Viewport'u](viewport.md#ajan-izlemek)).

Reddedilen bir çağrı `permission_denied` türüyle başarısız olur. Bir ajanın yaptığı her değişikliğin kaynağı
`rpc:illusion_mcp` olur; böylece `history.undo {"scope": "mine"}` yalnızca ajanın adımlarını geri alır,
editörün istemci listesi (`editor.clients`) ve etkinlik halkası (`editor.activity`) kimin ne yaptığını
gösterir.

## Sınırlar

| Sınır | Değer |
| --- | --- |
| Bir sonucun metin özeti | yaklaşık 4 KB (tam sonuç `structuredContent` içindedir) |
| Görüntü boyutu | uzun kenarda 1024 piksel |
| Liste sayfası başına araç | 100 (`--page-size`) |
| `.mcp.json` içindeki araç zaman aşımı | 600 s |
| Mesaj boyutu | 16 MiB |

## Çökmeler ve kurtarma

Başlatılan bir editör beklenmedik biçimde kapanırsa, o sırada çalışan çağrı çıkış kodu ve editörün son 50 log
satırıyla `editor_crashed` türünü döndürür. Sonraki çağrı editörü aynı projeyle (ya da ajanın en son
oluşturduğu veya açtığı projeyle) yeniden başlatır, çökme günlüğünü `project.recover` ile yeniden oynatır ve
sonuca bununla ilgili bir not ekler. Bağlanılan bir editör kaybolursa `editor_unavailable` gelir; sonraki
çağrı yeniden bağlanır.

## Sorun giderme

| Sorun | Neye bakılmalı |
| --- | --- |
| Sunucu başlamıyor | `.mcp.json` içindeki komutu bir terminalde çalıştırın: hatalar stderr'dedir. Çıkış kodu 1, editöre ulaşılamadığı ya da başlatılamadığı anlamına gelir; `--project` ve `--editor` değerlerini kontrol edin. |
| `MCP_PROTOCOL_NEGOTIATION` ya da `Unsupported protocol version` (-32022) | İstemci, köprünün konuşmadığı bir protokol sürümü istedi. Köprü 2025-06-18 ve 2025-11-25 (`initialize` ile) ve 2026-07-28 (durumsuz) konuşur; istemciyi ya da köprüyü güncelleyin. |
| `send initialize first` (-32600) | Bir istemci `initialize` olmadan ve modern `_meta` sürümü olmadan istek gönderdi. |
| Araçlar eksik | `--tools` değerini kontrol edin; `illusion_call` her komuta ulaşır. |
| Çağrılar `permission_denied` ile başarısız | Editör `read-only` ya da `ask` modunda çalışıyor ya da editörün başındaki kişi çağrıyı reddetti; `--permissions auto` ile başlatın ya da modu AI Activity panelinde değiştirin. |
| Bir çağrı uzun süre bekliyor | Editör penceresi bir kişinin onayını bekliyor (`ask` modu); AI Activity paneline bakın. |
| Metin özetleri `truncated` ile bitiyor | Sonuç `structuredContent` içinde eksiksizdir; isteği `limit` ile daraltın. |
| `editor_crashed` | Editörün son log satırları hatanın içindedir; yeniden başlatıp kurtarmak için tekrar çağırın. |
| Düzenlemeler `play.stop` sonrasında kayboluyor | Bir play oturumu çalışıyordu: Entity ve Component komutları play World'ü değiştirdi. `"world": "edit"` verin ya da durdurmadan önce değerleri `play.keep_changes` ile kopyalayın (bkz. [Play Modu](play-mode.md)). |

Her mesajı görmek için bir oturumu `--record session.jsonl` ile kaydedin.
