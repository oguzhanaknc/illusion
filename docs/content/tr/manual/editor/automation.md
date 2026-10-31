# Otomasyon

Her editör komutu başka bir süreçten çağrılabilir: AI ajanları, betikler, CI işleri ve MCP köprüsü editörün
kendi panelleriyle aynı komutları kullanır. Editör JSON-RPC 2.0 konuşur; her satırda bir JSON nesnesi, stdin
ve stdout üzerinden ya da yerel bir soket üzerinden. Ağda hiçbir şey dinlemez.

## Editörü penceresiz çalıştırmak

Penceresiz (`--headless`) `illusion_editor` yalnızca editör protokolüne hizmet verir:

```bash
illusion_editor --headless --workspace ~/games --rpc-stdio
```

| Seçenek | Anlamı |
| --- | --- |
| `--headless` | Pencere yok. Bu seçenek olmadan editör penceresi açılır (bkz. [Editör Penceresi](interface.md)). |
| `--project <dir>` | Başlarken bu projeyi açar. |
| `--workspace <dir>` | `project.create` ve `project.open` komutlarının çalışabileceği dizin (varsayılan: `--project`'in üst dizini ya da geçerli dizin). |
| `--rpc-stdio` | Üst sürece stdin ve stdout üzerinden hizmet verir. stdin kapanınca editör sona erer. |
| `--rpc-listen [<socket>]` | Yerel istemcilere bir `AF_UNIX` soketi üzerinden hizmet verir (varsayılan: projenin içinde, aşağıya bakın). |
| `--permissions read-only\|ask\|auto` | İstemcilerin neleri çalıştırabileceği (varsayılan `auto`). |
| `--deterministic`, `--seed <n>` | Tekrarlanabilir oturumlar için tohumlanmış Entity id'leri ve sabit 1/60 s frame adımı. |
| `--no-gpu` | GPU olmadan çalışır; GPU gerektiren komutlar (`editor.screenshot`, `ui.*` komutları) `unsupported` ile başarısız olur. GPU varken arayüz ekran dışında çalışır. |
| `--ui-size <w>x<h>` | `editor.screenshot` için ekran dışı arayüzün boyutu (varsayılan 1920x1080). |
| `--user-dir <dir>` | Tercihler ve kaydedilmiş yerleşimler (varsayılan: bellekte tutulur). |
| `--frames <n>` | `n` frame sonra durur. |

Log stderr'e gider; böylece stdout yalnızca protokolü taşır.

## illusion_ctl

`illusion_ctl` komut satırı istemcisidir. Tek bir komut için penceresiz bir editör başlatır ya da çalışan
bir editöre bağlanır:

```bash
illusion_ctl --launch build/bin/Release/illusion_editor --editor-arg --workspace --editor-arg ~/games \
    call project.create '{"directory": "tetris", "name": "Tetris"}'
illusion_ctl --project ~/games/tetris call scene.info
illusion_ctl --project ~/games/tetris subscribe scene.changed history.changed --count 10
illusion_ctl --project ~/games/tetris dump-catalog > catalog.json
```

| Komut | Ne yapar |
| --- | --- |
| `call <method> [<params>]` | Bir komutu çalıştırır ve sonucunu yazar (başarısız olursa çıkış kodu 1, hata stderr'de). |
| `batch <file.jsonl> [--keep-going]` | Satır başına bir `{"method", "params"}` nesnesini sırayla çalıştırır. |
| `subscribe [<topic>...] [--count <n>]` | Editör olaylarını geldikçe yazar. |
| `dump-catalog` | Her metodun OpenRPC belgesini yazar (`rpc.discover`). |
| `replay <transcript> [--checks <file>]` | Bir transcript'i başlatılan bir editörde yeniden oynatır (bkz. [kabul testleri](#kabul-testleri)). |
| `check <file>` | Bir kontrol dosyasını başlatılan bir editörde çalıştırır. |

`replay` ve `check`, her adımı bunun yerine köprü üzerinden bir MCP araç çağrısı olarak çalıştırmak için
`--via-mcp <illusion_mcp> [--era legacy|modern]` alır (bkz. [Yapay zekâ ajanları](ai-agents.md)).

Bağlantılar: `--launch <editor>` (her editör argümanı için `--editor-arg` ile), `--project <dir>` (projenin
endpoint dosyası), `--endpoint <file>` ya da `--socket <path> --token <token>`.

## Python istemcisi

`tools/editor/illusion_rpc.py` yalnızca Python standart kütüphanesine ihtiyaç duyar:

```python
from illusion_rpc import EditorClient, RpcError

with EditorClient.launch("build/bin/Release/illusion_editor", ["--workspace", "/tmp/games"]) as editor:
    editor.call("project.create", directory="tetris", name="Tetris")
    board = editor.call("entity.create", name="Board")["entity"]
    try:
        editor.call("entity.get", entity="/Nope")
    except RpcError as error:
        print(error.code, error.data["kind"])  # 1200 not_found
```

`event` ve `rpc.progress` gibi bildirimler `call()` beklerken `editor.notifications` içinde birikir.
`EditorClient.connect_project(path)` çalışan bir editöre endpoint dosyası üzerinden bağlanır.

## Soketler ve endpoint dosyası

`--rpc-listen` ve bir projeyle editör `<project>/.illusion/run/editor.sock` adresinde dinler ve
`<project>/.illusion/run/editor-endpoint.json` dosyasını yazar:

```json
{"pid": 4242, "protocolVersion": "0.1.0", "editorVersion": "0.1.0", "transport": "unix", "address": "/home/me/games/tetris/.illusion/run/editor.sock", "token": "<64 hex digits>"}
```

`--project` olmadan bir soket yolu verin; endpoint dosyası o zaman onun yanına `<socket>.endpoint.json`
olarak yazılır (`illusion_ctl --endpoint` ile kullanın). Run dizinini ve dosyayı yalnızca geçerli kullanıcı
okuyabilir. Bir soket istemcisi ilk mesaj olarak 2 saniye
içinde `rpc.authenticate {"token": ..., "client": "my-tool"}` göndermelidir; istemci adı undo geçmişinde
`rpc:my-tool` kaynağı olarak görünür. Proje yolu bir soket adresi için fazla uzunsa (ya da Windows'ta ASCII
değilse) soket, kullanıcıya özel runtime ya da temp dizinindeki `illusion-<hash>.sock` dosyasına taşınır;
endpoint dosyası her zaman yerini söyler. Dosya editör kapanınca silinir; istemciler editör süreci artık
olmayan bir dosyayı yok sayar.

## İzinler ve güvenlik

| Mod | Çalıştırdıkları |
| --- | --- |
| `read-only` | Yalnızca salt okunur komutlar. |
| `ask` | Okuma ve düzenleme komutları; yıkıcı, dosya, Build ve sistem komutları bir kişinin onayını gerektirir, bu yüzden penceresiz bir editör onları reddeder. |
| `auto` | Her şey (penceresiz varsayılan). |

Reddedilen çağrılar `permission_denied` (1001) ile başarısız olur. Dosya komutları proje mount'larının, proje
komutları da çalışma alanının içinde kalır. Kabuk çalıştıran bir komut yoktur.

!!! warning "Ajanlar kod çalıştırabilir"
    `module.build` ve `module.load`, bir ajanın yazdığı C++ kodunu derler ve editör sürecinin içinde yükler
    (bkz. [Proje Modülleri](project-modules.md)). Güvenmediğiniz ajanlar için `read-only` ya da `ask` kullanın.

## Olaylar

`editor.subscribe {"topics": ["scene.changed"], "mode": "push"}`, editörün her olay için frame ve konu başına
bir kez bildirim göndermesini sağlar:

```json
{"jsonrpc": "2.0", "method": "event", "params": {"topic": "scene.changed", "payload": {"revision": 42}}}
```

`pull` modunda editör `editor.poll_events` için en fazla 1.024 olay saklar; daha fazlası gelirse yanıt
`"overflow": true` der ve istemci sahneyi yeniden okur. Konular arasında `scene.changed`, `scene.opened`,
`scene.saved`, `selection.changed`, `history.changed`, `scene.autosaved` ve `commands.changed` vardır; boş liste
hepsi demektir. `commands.changed` sonrasında (örneğin bir modül komut kaydettiğinde) sunucunun metot listesi
ve `rpc.discover` yeni komutları içerir.

Olaylar frame sonunda yayımlanır; bu yüzden bir komutun yanıtı, olaylarının da hazır olduğunu garanti
etmez. Deterministik bir pull için komuttan sonra `editor.wait {"frames": 1}` çağrısının tamamlanmasını
bekleyin, ardından `editor.poll_events` çağırın. Ek senkron çağrılar bir frame sınırını garanti etmez.

## Async komutlar ve iptal

`async` bayraklı komutlar (`editor.list_commands` bayrağı gösterir) birkaç editör frame'i sürer. Editör onlara
bittiklerinde yanıt verir, arada istek id'siyle `rpc.progress` bildirimleri gönderir; bağlantının sonraki
istekleri onları beklemez. id ile gönderilen `rpc.cancel` çalışan bir async komutu bitirir ya da hâlâ kuyrukta
olan bir isteği atlatır; istek `cancelled` (1004) ile yanıtlanır:

```json
{"jsonrpc": "2.0", "id": 7, "method": "editor.wait", "params": {"frames": 600}}
{"jsonrpc": "2.0", "method": "rpc.progress", "params": {"id": 7, "progress": 1, "total": 600}}
{"jsonrpc": "2.0", "method": "rpc.cancel", "params": {"id": 7}}
{"jsonrpc": "2.0", "id": 7, "error": {"code": 1004, "message": "editor.wait was cancelled", "data": {"kind": "cancelled", "errors": [], "suggestions": [], "retryable": false}}}
```

## Batch'ler ve transaction'lar

`editor.batch` birkaç komutu, bir adımın sonucundan sonrakine `$` referanslarıyla, tek undo adımı olarak
çalıştırır ([editör çekirdeği](index.md#batchler)). Daha uzun bir iş için istemci bağlantısına ait bir
transaction açabilir; bu sırada diğer istemciler sahneyi değiştiremez (`busy`, yeniden denenebilir) ve
bağlantı kapanırsa editör transaction'ı geri alır:

```python
editor.call("transaction.begin", label="Build board")
board = editor.call("entity.create", name="Board")["entity"]
editor.call("entity.create", name="Cell 1", parent=board["id"])
editor.call("transaction.commit")  # one undo step; transaction.cancel would revert both
editor.call("history.undo", scope="mine")  # undoes it even if other clients edited since
```

## Yeniden denemeler ve eşzamanlılık

- Değişiklik yapan bir çağrıdaki `clientToken` onu yeniden denemeye güvenli kılar: editör her istemcinin son
  1.024 token'ını hatırlar ve tekrarlanan birine ilk yanıtla karşılık verir.
- `expectRevision`, siz okuduktan sonra başka biri sahneyi değiştirdiyse `conflict` (1203) ile başarısız olur.
- `history.undo {"scope": "mine"}` kendi son adımınızı diğer istemcilerin sonraki bağımsız adımlarının
  ötesinden geri alır; onlar aynı şeylere dokunduysa `conflict` ile başarısız olur.
- `retryable: true` taşıyan `busy` (1002) ve `limit_exceeded` (1401) "sonra yeniden deneyin" demektir: editör
  8 bağlantı, bağlantı başına bekleyen 64 istek ve toplamda 256 istek kabul eder.

## Kabul testleri

Ajan kabul testi, editör çağrılarından oluşan bir transcript'i yeniden oynatır ve sonucu kontrol eder
(`tests/acceptance/tetris`). Bir transcript'in her satırı bir adımdır:

```json
{"call": "entity.create", "params": {"name": "Board"}, "capture": {"board": "/entity/id"}, "expect": {"result": {"entity": {"path": "/Board"}}}}
{"call": "entity.get", "params": {"entity": "${board}"}}
{"restart": {"arguments": ["--project", "${root}"]}}
```

`capture` bir yanıttaki değerleri JSON Pointer ile kaydeder; `${name}` onları sonra kullanır. `expect` yanıtı
eşleştirir: nesneler en azından verilen üyelere sahipse eşleşir; `$approx`, `$regex`, `$length`, `$exists`,
`$oneOf`, `$contains`, `$min` ve `$max` eşleşmeyi gevşetir. `"requires": "gpu"` ile işaretli bir adım,
GPU'suz bir editörde `otherwise` ile eşleşmelidir (varsayılan: hata 1400).

`image` içeren bir adım, yanıtındaki ekran görüntüsünü transcript'in yanındaki golden bir PNG ile karşılaştırır.
Bir kanalı `channelTolerance` değerinden fazla sapan piksel farklı sayılır; piksellerin `maxDifferent`
oranından fazlası farklıysa ya da `notUniform` ayarlıyken görüntü tek düz bir renkse adım başarısız olur.
`ILLUSION_UPDATE_GOLDENS=1` golden'ı yeniden yazar, `ILLUSION_UI_CAPTURE_DIR` ise her görüntüyü oraya da
kaydeder:

```json
{"call": "viewport.screenshot", "params": {"viewport": "game"}, "requires": "gpu", "image": {"golden": "golden/board.png", "channelTolerance": 8, "maxDifferent": 0.02, "notUniform": true}}
```

Şöyle çalıştırın:

```bash
illusion_ctl --launch build/bin/Release/illusion_editor --editor-arg --deterministic --editor-arg --no-gpu \
    replay tests/acceptance/tetris/scripted.jsonl --checks tests/acceptance/tetris/checks.jsonl --temp-workspace \
    --var 'tetrisModule="build/editor/tests/Release/illusion_tetris_module.dll"'
```

`--var name=JSON` ilk adımdan önce bir `${name}` tanımlar; Tetris kontrolleri (C5) başvuru modülü
`tests/acceptance/tetris/module/Tetris.cpp`'yi `${tetrisModule}` yolundan yükler, Play modunu `fixedStep`
ile başlatır ve oyunu `input.inject` ile `play.step` üzerinden oynar. CTest modülü (`illusion_tetris_module`)
build eder ve yolunu verir.

CTest senaryoyu MCP köprüsü üzerinden iki protokol çağında da çalıştırır
(`--via-mcp` ile `acceptance.tetris.board.mcp-legacy` ve `acceptance.tetris.board.mcp-modern`).

Protokolün kendisi [editör protokolü başvurusunda](../../reference/editor-protocol.md) anlatılır.
