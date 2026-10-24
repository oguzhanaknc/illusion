# Tercihler

File > Preferences (**Ctrl+,**) editörün kullanıcınız için nasıl görünüp davrandığını değiştirir. Her
değişiklik `preferences.set` komutunu çalıştırır; böylece ajanlar da tercihleri okuyup değiştirebilir
(`preferences.get`). Tercihler kullanıcı dizininde `preferences.json` olarak kaydedilir:

| Sistem | Kullanıcı dizini |
| --- | --- |
| Windows | `%APPDATA%\illusion\editor` |
| macOS | `~/Library/Application Support/illusion/editor` |
| Linux | `$XDG_DATA_HOME/illusion/editor` (varsayılan `~/.local/share/illusion/editor`) |

Kaydedilmiş yerleşimler yanındaki `layouts/` klasöründe durur. Başka bir dizin kullanmak için editörü
`--user-dir <dir>` ile başlatın; penceresiz ya da `--deterministic` bir editör, bir dizin verilmedikçe
tercihlerini bellekte tutar.

```json
{"method": "preferences.set", "params": {"value": {"theme": "light", "uiScale": 1.25}}}
```

Değer bir birleştirme yamasıdır: değinmediği alanlar değerlerini korur (`"replace": true` son projeler dışında
onları sıfırlar). Geçersiz değerler alanın JSON Pointer'ıyla başarısız olur, ör. `/value/uiScale`.

## Sayfalar

| Sayfa | Tercihler |
| --- | --- |
| Appearance | `theme` (`dark`, `light`, `high-contrast`), `accent` (`azure`, `violet`, `teal`, `amber`), `uiScale` (0.75 ile 2 arası, monitörün ölçeğiyle çarpılır), `reduceMotion`, `colorBlindAxes` (X, Y ve Z eksenleri için Okabe-Ito renkleri). |
| Language | `locale`: İngilizce, sözde dil `en-XA` (yerleşimleri denemek için aksanlı, daha uzun metin) ve `editor-data/locales` içindeki her dil dosyası. Dil bir sonraki frame'de değişir. |
| Keyboard | `keymap` preset'i ve `shortcuts`, aşağıya bakın. |
| AI Agents | `agentPermissions` (`read-only`, `ask`, `auto`; varsayılan `ask`) ve `approvalPopups`. Bkz. [Editör Penceresi](interface.md#ai-activity). |
| Autosave | `autosaveInterval` (saniye, hiçbir zaman için 0, en çok 3.600) ve `autosaveCopies` (1 ile 20 arası). |
| External Tools | `codeEditor`: kaynak dosyaları açan komut (sonraki bir aşamanın modül iş akışı kullanır). |

Dosya ayrıca `recentProjects` (açılan son on proje) ve `layout` (en son kullanılan yerleşim) bilgilerini tutar.

## Kısayollar

Her menü öğesi ve araç çubuğu düğmesi `edit.undo` gibi sabit bir id'si olan bir UI eylemidir. Bir keymap
preset'i eylemlere tuş kombinasyonlarını verir:

| Preset | `illusion`'dan farkları |
| --- | --- |
| `illusion` | Varsayılanlar: Ctrl+Z, Ctrl+Y, Ctrl+D, Delete, F2, Ctrl+Shift+P, Ctrl+P, araçlar için Q/W/E/R/T, oynatmak için F5. |
| `unity` | Ctrl+P oynatır, Ctrl+Shift+P duraklatır, Ctrl+Alt+P bir adım ilerler, Ctrl+K Go to Anything'dir. |
| `unreal` | Alt+P oynatır, Alt+S simüle eder, Pause duraklatır, ters tırnak Console'u açar. |
| `blender` | Shift+D çoğaltır, X siler, G/R/S taşır, döndürür ve ölçekler, F3 arar, Shift+A bir Entity ekler. |

Keyboard sayfasında bir eylemi yeniden atamak için kombinasyonuna tıklayıp yeni tuşlara basın; **Escape** iptal
eder. **Reset** preset'in kombinasyonuna döner, **Remove** eylemi kombinasyonsuz bırakır. İki eyleme atanmış
kombinasyonlar çakışma olarak listelenir ve tabloda işaretlenir. Kombinasyonlar değiştiriciler ve bir tuş olarak
yazılır; tuş [Pencereler ve Input](../platform.md) sayfasındaki gibi adlandırılır:

```json
{"value": {"keymap": "blender", "shortcuts": [{"action": "edit.redo", "chord": "Ctrl+Shift+Z"}]}}
```
