# Editör Penceresi

`illusion_editor` editör penceresini açar: bir menü çubuğu, bir araç çubuğu, yerleştirilmiş paneller ve bir
durum çubuğu. Pencerenin yaptığı her şey bir [Editor command](../../reference/editor-commands.md) çalıştırır;
böylece bir kişi ve bir yapay zekâ ajanı tek bir API kullanır: bir menü öğesi, bir araç çubuğu düğmesi ve bir
kısayol, bir komutu sabit parametrelerle çalıştıran *UI eylemleridir* ve ajanlar pencerenin kendisini `ui.*`
komutlarıyla yönetebilir.

```bash
illusion_editor --project games/tetris
```

`--project` olmadan Welcome penceresi açılır. Editörü derlemek ve başlatmak için [Başlarken](../../getting-started.md),
temalar, dil ve kısayollar için [Tercihler](preferences.md) sayfasına bakın.

## Pencerenin düzeni

| Bölüm | Gösterdiği |
| --- | --- |
| Menü çubuğu | File, Edit, View, Entity, Assets, Play, Modules, Tools, Window ve Help. Komutu sonraki bir aşamada gelen öğeler devre dışıdır; ipuçları o aşamayı söyler. |
| Araç çubuğu | Solda dönüşüm araçları, ortada Play, Pause, Step ve Simulate, sağda yerleşim, ajan çipi, bildirimler ve arama. |
| Paneller | [Outliner ve Inspector](scene-editing.md), Scene, Game, Assets, Console, History, AI Activity, Profiler, Rendering ve Project Settings, pencereye yerleştirilmiş olarak. Sonraki aşamaların panelleri neyi göstereceklerini söyler. |
| Durum çubuğu | Seçim, sahne revizyonu ve kayıt durumu, frame süresi, dil, bağlı ajanlar (ve bekleyen onaylar) ve oynatma durumu. |

Bir paneli sekmesinin kapatma düğmesiyle kapatın, Window menüsünden yeniden açın (`ui.show_panel`).

## Yerleşimler

Bir yerleşim panelleri düzenler. Beş yerleşim hazır gelir:

| Yerleşim | Kullanım |
| --- | --- |
| `default` | Solda Outliner, sağda Inspector sekmeleri, altta araç panelleri. |
| `level-design` | Solda Assets'in üstünde Outliner, büyük bir Scene görünümü, altta Console ve History. |
| `debug` | Altta büyük Console ve Profiler, Inspector'ın yanında History. |
| `profiling` | Alt yarıda Profiler, sağda Rendering ayarları. |
| `agent` | Bir ajanın çalışmasını izlemek için sağda AI Activity ve History. |

Birini araç çubuğunun yerleşim düğmesinden ya da Window > Layouts'tan seçin. **Save layout** geçerli düzeni
kullanıcınız için bir adla kaydeder; **Reset layout** etkin yerleşimin hazır düzenini geri getirir. Düzen
editör kapanırken kaydedilir ve bir sonraki açılışta geri gelir; bir proje `settings/editor_layout.ini`
içinde varsayılan bir düzen getirebilir, sizin kendi düzeniniz yoksa o kullanılır.

```json
{"method": "ui.layout_apply", "params": {"name": "agent"}}
```

## Komut paleti

Her komutu ve menü eylemini başlığıyla, kendi dilinizde ya da İngilizce aramak için **Ctrl+Shift+P**
(ya da **Ctrl+K**) tuşlarına basın. Entity'leri listelemek için aramaya `@` ile, tercih sayfalarını listelemek
için `>` ile başlayın; **Ctrl+P** (Go to Anything) paleti Entity'lerle açar. **Yukarı** ve **Aşağı** seçer,
**Enter** çalıştırır, **Escape** kapatır.

Eylemin vermediği parametreleri gerektiren bir komut, parametre şemasından üretilen bir form açar: metinler
için metin alanları, sayı alanları, bayraklar için onay kutuları, numaralandırmalar için listeler ve geri
kalan her şey için bir JSON alanı. Ajanlar paleti `ui.palette` ile açar.

## Console

Console editörün logunu gösterir. Düzeye göre süzün (Debug, Info, Warnings, Errors), metinde arayın,
tekrarlanan mesajları sayılı tek bir satırda toplayın ve zaman damgalarını gösterin. Bir mesajın kaynak
bağlantısı `file:line` bilgisini panoya kopyalar. **Clear**, `log.clear` komutunu çalıştırır.

Alttaki satır bir komut çalıştırır: önce adı, sonra JSON olarak parametreleri.

```text
entity.create {"name": "Board"}
```

## History

History geri alma adımlarını listeler; oraya atlamak için bir satıra tıklayın (`history.jump`). Bir rozet her
adımı kimin yaptığını gösterir: pencerede yapılan adımlar için **You**, bir ajanın adımları için istemcinin adı.
**Undo *client*'s changes**, sizin adımlarınız daha sonra gelse bile, aynı şeylere dokunmadıkları sürece o
ajanın tüm adımlarını geri alır.

## AI Activity

AI Activity paneli editöre bağlı ajanları gösterir: istemci ve ajan adları, MCP protokol dönemi ve istek
sayısı. **Pause** bir ajanın isteklerini siz devam ettirene kadar bekletir; **Disconnect** bağlantısını kapatır.
Alttaki akış her çağrıyı parametreleri, süresi ve sonucuyla, en yenisi başta listeler; bir ajanın son
değişikliğinin yanındaki **Undo** onu geri alır.

Üstteki izin modu tüm ajanlar için geçerlidir:

| Mod | Ajanların yapabileceği |
| --- | --- |
| Read only | yalnızca okuma. |
| Ask | okuma ve düzenleme; yıkıcı, dosya, Build ve sistem komutları onayınızı bekler. |
| Automatic | her şeyi çalıştırma. |

Ask modunda onay gerektiren bir istek sağ üst köşede bir istem ve bir bildirim gösterir; panel bekleyen her
isteği listeler. **Allow once** isteği çalıştırır, **Allow for session** ayrıca o tür komutlara ajanın
bağlantısı için bundan sonra da izin verir, **Deny** isteği `permission_denied` ile yanıtlar. Bir istek
beklerken ajanın sonraki istekleri onun arkasında bekler; diğer ajanlar devam eder. Bir batch (`editor.batch`)
tüm adımları için bir kez sorar. Onaylar bilerek komut değildir: hiçbir istemci kendi isteklerini onaylayamaz.

## Bildirimler

Bildirimler sonuçları ve hataları birkaç saniye gösterir; araç çubuğundaki zil son 64 bildirimi içeren
bildirim merkezini açar. Ajanlar `ui.notify` ile bildirim gösterebilir.

## Projeler

Welcome penceresi son projeleri listeler, bir şablondan proje oluşturur (`project.create`) ve birini açmak için
çalışma alanının klasörlerinde gezinir (`project.open`). Çalışma alanının dışındaki projeler listelenir ama
buradan açılamaz; başka bir alan seçmek için editörü `--workspace` ile başlatın. Help > About sürümü,
Help > Third-party licenses editörün kullandığı kütüphanelerin ve yazı tiplerinin lisanslarını gösterir.

## Ekran görüntüleri

`editor.screenshot` pencereyi bir PNG olarak döndürür (MCP istemcilerine görsel içerik olarak). GPU'su olan
penceresiz bir editör arayüzü ekran dışında istenen boyutta çizer; varsayılan 1920x1080'dir (`--ui-size`).

```json
{"method": "editor.screenshot", "params": {"width": 1280, "height": 720}}
```

## Henüz yok

Pencerede gamepad ile gezinme, bir ajanın seçimini viewport'ta izleme, kaynak dosyaları açan External Tools
ve paletin `#` Asset araması sonraki aşamalarla gelir (bkz.
[yol haritası](https://github.com/oguzhanaknc/illusion/blob/main/ROADMAP.md)).
