# Editör Protokolü

Protokol sürümü `0.3.0` (semver; `1.0` M7x ile gelir). Tasarım: ADR 0037. Nasıl kullanılır:
[Otomasyon](../manual/editor/automation.md). Yapay zekâ ajanları onu MCP köprüsü üzerinden kullanır
([Yapay zekâ ajanları](../manual/editor/ai-agents.md)).

## Çerçeveleme

Satır başına bir UTF-8 JSON nesnesi, `\n` ile biter (öncesindeki `\r` yok sayılır, boş satırlar atlanır),
satır sonu hariç en fazla 16 MiB. Daha uzun bir satır -32600 alır ve bağlantı kapanır. JSON-RPC batch dizileri
-32600 ile reddedilir.

## İstekler ve yanıtlar

```json
{"jsonrpc": "2.0", "id": 7, "method": "entity.create", "params": {"name": "Board", "clientToken": "c1-0007"}}
{"jsonrpc": "2.0", "id": 7, "result": {"entity": {"id": "9f2c41aa0b7d3e15", "name": "Board", "path": "/Board"}, "_editor": {"revision": 42, "transaction": 17, "dryRun": false, "operations": 2, "warnings": []}}}
```

- `id` bir string, sayı ya da null'dır ve değişmeden geri gelir. `id`'si olmayan bir mesaj bir bildirimdir
  (notification): çalışır ama hiçbir zaman yanıt almaz, hata bile.
- `params` bir nesnedir (adlandırılmış parametreler) ya da yoktur.
- Bir bağlantının yanıtları isteklerinin sırasıyla gelir; async komutların yanıtları hariç: onlar komut
  bitince, `rpc.progress` bildirimlerinden sonra gelir ve sonraki istekler onları beklemez.
- Her komut sonucunun bir `_editor` üyesi vardır: çağrıdan sonraki sahne revizyonu, undo transaction'ı (yoksa
  0), dry run olup olmadığı, temel işlem sayısı ve uyarılar.
- Değişiklik yapan komutlar ayrıca `dryRun`, `mergeKey`, `expectRevision` ve `clientToken` alır (bkz.
  [editör çekirdeği](../manual/editor/index.md#komutlar)).

## Hatalar

```json
{"jsonrpc": "2.0", "id": 7, "error": {"code": 1200, "message": "entity '/Nope' does not exist", "data": {"kind": "not_found", "errors": [], "suggestions": [], "retryable": false}}}
```

| Kod | Anlamı |
| --- | --- |
| -32700 | Satır JSON değil ya da geçerli UTF-8 değil (`id` null). |
| -32600 | Bir JSON-RPC 2.0 istek nesnesi değil, bir batch dizisi ya da 16 MiB'ı aşan bir satır. |
| -32601 | Böyle bir metot yok; `data.suggestions` yakın adları listeler. |
| -32602 | `params` bir nesne değil. |
| -32603 | Sunucuda bir hata. |
| 1000 | `unauthenticated`: önce doğru token ile kimlik doğrulayın. |
| 1001 | `permission_denied`: izin modu komuta izin vermiyor. |
| 1002 | `busy`: istek kuyruğu dolu ya da başka bir bağlantının açık transaction'ı var (`retryable`). |
| 1004 | `cancelled`: `rpc.cancel` isteği bitirdi. |
| 1003–1500 | Komut hataları (`ErrorKind`): `timeout`, `wrong_mode`, `not_found`, `already_exists`, `validation_failed` (`data.errors[{path, message}]` ile), `conflict`, `ambiguous`, `precondition_failed`, `io_error`, `build_failed`, `module_error`, `unsupported`, `limit_exceeded`, `internal`. |

## Taşıyıcılar

| Taşıyıcı | Başlatma | Kimlik doğrulama |
| --- | --- | --- |
| stdio | `--rpc-stdio` | Yok: boruları üst süreç sahiplenir. |
| `AF_UNIX` soketi | `--rpc-listen [<socket>]` | Endpoint dosyasındaki token ile `rpc.authenticate`. |

Endpoint dosyası `<project>/.illusion/run/editor-endpoint.json`: `{pid, protocolVersion, editorVersion,
transport, address, token}`; yalnızca sahibine açık, atomik olarak yazılır ve çıkışta silinir. Soket
`<project>/.illusion/run/editor.sock` dosyasıdır; bu yol 103 bayttan uzunsa (ya da Windows'ta ASCII değilse)
`XDG_RUNTIME_DIR`, `TMPDIR` ya da temp dizinindeki `illusion-<hash>.sock` olur.

## Sunucu metotları

| Metot | Parametreler | Sonuç |
| --- | --- | --- |
| `rpc.authenticate` | `token` (64 hex basamak), `client` (kaynaklar için ad, `[A-Za-z0-9._-]`, en fazla 64) | `{protocolVersion, editorVersion, client}` |
| `rpc.discover` | — | Her metodun OpenRPC 1.3 belgesi |
| `editor.subscribe` | `topics` (dizi; boş: hepsi), `mode` (`push` ya da `pull`, varsayılan `push`) | `{topics, mode}` |
| `editor.unsubscribe` | — | `{}` |
| `editor.poll_events` | `limit` (varsayılan 256) | `{events: [{topic, payload}], overflow}` |
| `editor.set_client` | `client` (kaynak `rpc:<client>`), `agent`, `agentVersion`, `era` (her biri isteğe bağlı, en fazla 64 bayt) | `{connection, client, origin}` |
| `editor.clients` | — | `{clients: [{connection, client, agent, agentVersion, era, origin, requests}]}` |
| `editor.activity` | `since` (sıra numarası, varsayılan 0), `limit` (varsayılan 256) | `{entries: [{seq, connection, client, method, code, micros, revision}]}`, en eskisi önce |
| `editor.quit` | — | `{}`; editör yanıttan sonra kapanır (izin sınıfı `system`) |
| `rpc.cancel` | `id` (bu bağlantının bir isteğinin) | id ile gönderildiyse `{}`; genellikle bir bildirim |

Bir soket istemcisinin ilk mesajı 2 saniye içinde `rpc.authenticate` olmalıdır; aksi halde 1000 alır ve
bağlantı kapanır. Push olayları `{"method": "event", "params": {"topic", "payload"}}` bildirimleridir.
Async komutlar, ilerleme değiştiğinde frame başına en fazla bir kez `{"method": "rpc.progress", "params":
{"id", "progress", "total", "message"}}` gönderir (`message` yalnızca varsa). Komutlar bağlantıyı çağıran
olarak çalışır: bir bağlantının transaction'ı (`transaction.begin`) bağlantı kapanınca iptal edilir ve
`history.undo {"scope": "mine"}` bağlantının kaynağı `rpc:<client>` adımlarını geri alır. `editor.batch`
adımları izin moduna tek tek denetlenir. `editor.activity` bütün bağlantıların son 256 isteğini tutar
(başarıda `code` 0'dır; `editor.activity`, `editor.clients` ve `editor.poll_events` dışarıda bırakılır); MCP
köprüsü kendini `editor.set_client` ile adlandırır, böylece değişikliklerinin kaynağı `rpc:illusion_mcp` olur.
Diğer her metot bir [editör komutudur](editor-commands.md).

## Sınırlar

| Sınır | Değer | Aşıldığında |
| --- | --- | --- |
| Mesaj boyutu | 16 MiB | -32600, bağlantı kapanır |
| Bağlantılar | 8 | 1401, bağlantı kapanır |
| Bağlantı başına bekleyen istek (çalışan async komutlar dahil) | 64 | 1401 (`retryable`) |
| Toplam bekleyen istek | 256 | 1002 (`retryable`) |
| Pull aboneliği başına saklanan olay | 1.024 | `overflow: true` |
| İstemci başına hatırlanan `clientToken` | 1.024 | en eskisi unutulur |
| `editor.batch` adımları | 1.000 | 1202 |
| `editor.wait` frame'leri | 100.000 | 1202 |
| Bağlantı başına gönderilmemiş yanıt | 64 MiB | bağlantı kapanır |

## İzin modları

| Mod | Okuma | Düzenleme | Yıkıcı, Dosya, Build, Sistem |
| --- | --- | --- | --- |
| `read-only` | salt okunur komutlar | reddedilir | reddedilir |
| `ask` | izin verilir | izin verilir | onay gerekir (penceresizde reddedilir) |
| `auto` | izin verilir | izin verilir | izin verilir |
