# Editör Mimarisi

Editör, deponun oyunların hiçbir zaman link etmediği ayrı bir parçasıdır. Bu sayfa editörün nasıl
düzenlendiğini, motorla arasındaki sınırın nasıl korunduğunu ve üzerine kurulduğu UI temelini anlatır.
Editör yapım aşamasındadır (M7 kilometre taşı); bu sayfa onunla birlikte büyür.

## Ağaç ve hedefler

Editör kodu `editor/` altında yaşar:

| Yol | Hedef | İçerik |
| --- | --- | --- |
| `editor/thirdparty` | `illusion::imgui`, `illusion::implot` | Editörün yapılandırma başlığıyla Dear ImGui (docking) ve ImPlot |
| `editor/strings` | `illusion::editor_strings` | Editörün İngilizce metni (`editor/data/locales/en.json`), string tablosu olarak |
| `editor/core` | `illusion::editor_core` | Penceresiz editör çekirdeği: projeler, düzenlenen sahne, tipli komutlar, transaction'lar, undo, journal'lar (GPU, Dear ImGui veya socket yok) |
| `editor/ui` | `illusion::editor_ui` | Backend'ler, tema, fontlar, ikonlar, kare başına metin, widget kiti, ekran dışı ve GPU'suz kareler (`OffscreenUi`, `HeadlessUi`), `UiDriver` |
| `editor/shell` | `illusion::editor_shell` | Editör penceresinin arayüzü: menüler, araç çubuğu, yerleşimler, palet, konsol, geçmiş, AI Activity, tercihler, proje pencereleri |
| `editor/tools/ui_gallery` | `illusion_ui_gallery` | Tasarım incelemeleri ve golden görüntü testleri için widget kataloğu |
| `editor/tools/command_reference` | `illusion_editor_command_reference` | Üretilen [komut referansını](../reference/editor-commands.md) yazar ve kontrol eder |
| `editor/data` | `editor-data/` | Temalar, locale'ler ve proje şablonları; fontlarla birlikte editör çalıştırılabilirlerinin yanına kopyalanır |
| `editor/tests`, `editor/benchmarks` | | Editör testleri ve Benchmark'ları |

`ILLUSION_BUILD_EDITOR` bu ağacı ekler; illusion en üst düzey proje olduğunda açıktır. Editör
çalıştırılabilirleri `<build>/bin/<Config>/` klasörüne yazılır; yanlarındaki `editor-data/` klasörünü editör
salt okunur olarak `editor:` adıyla bağlar.

## Export sınırı

Oyunlar asla editör kodu içermemelidir ([ADR 0030](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0030-editor-tree-and-export-boundary.md)).
Bunu üç kontrol sağlar:

- **Configure sırasında.** Editör hedefleri `illusion_add_editor_library()` ve
  `illusion_add_editor_executable()` ile oluşturulur. Bütün hedefler oluştuktan sonra CMake diğer her hedefin
  link bağımlılıklarını dolaşır ve ulaştığı her editör hedefinin yolunu göstererek başarısız olur:

```text
Games and the engine must not depend on editor code (see ADR 0030):
  'my_engine_lib' links the editor-only target 'illusion_editor_ui' (my_engine_lib -> helper -> illusion_editor_ui)
```

- **CI'da.** `python tools/check_layering.py`, `editor/` dışındaki editör ve Dear ImGui include'larını ve
  editör hedef adlarını reddeder; **Runtime-only build** işi motoru editör kapalıyken derler ve test eder.
- **Tasarım gereği.** Motor kodu editörün varlığını hiçbir zaman kontrol etmez. Paneller editör durumunu
  yalnızca okur ve komut gönderir; insanlar ve yapay zekâ ajanları aynı komutları kullanır.

## UI yığını

Editör arayüzü, docking dalıyla Dear ImGui'yi ve kendi backend'lerimizi kullanır
([ADR 0031](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0031-editor-ui-stack.md)):

| Parça | Görevi |
| --- | --- |
| `UiContext` | Bir pencerenin Dear ImGui ve ImPlot bağlamları; ayarlar bellekte tutulur; Dear ImGui bellek ayırmalarını sayar |
| `UiPlatformBackend` | EventBus'taki pencere event'lerini Dear ImGui'ye iletir; imleç, pano ve IME isteklerini pencereye geri uygular |
| `UiRenderer` | Dear ImGui'yi NVRHI ile çizer; dinamik font atlası; editörün kaydettiği Texture'lar (viewport'lar) |
| `Theme` | Renk token'larını ve tasarım ölçülerini bir Dear ImGui stiline dönüştürür |
| `FontManager` | Roboto, Roboto Mono ve Material Symbols; kullanılan boyutta ihtiyaç anında rasterleştirilir |
| `FrameText` | Kararlı id'li, yerelleştirilmiş etiketler; her karede bir arena içinde oluşturulur |
| `Widgets.hpp` | Widget kiti: ikon butonları, toggle'lar, segmentli seçiciler, arama alanları, vektör alanları, Component kartları, özellik tabloları, Asset kutucukları, toast'lar |
| `UiDriver` | Testler ve otomasyon için öğeleri kararlı id ile bulur; tıklar, sürükler ve yazar |

Editör arayüzünün bir karesi:

```cpp
backend.newFrame(deltaSeconds);
ImGui::NewFrame();
text.reset();
drawPanels(kit);            // widgets read state and submit commands
ImGui::Render();

commands->open();
commands->clearTextureFloat(uiTarget, nvrhi::AllSubresources, nvrhi::Color(0.0f));
renderer->render(commands, uiFramebuffer, ImGui::GetDrawData());
renderer->present(commands, uiTarget, swapchain->currentFramebuffer());
commands->close();
```

### Renk hattı

Dear ImGui'nin renkleri ve harmanlaması gamma uzayını varsayar; bu yüzden arayüz, sRGB kodlu değerler tutan
bir `RGBA8_UNORM` UI hedefine çizilir. `UiRenderer::present()` hedefi swapchain'e kopyalar: `_SRGB`
swapchain'lerde değerleri önce çözer, böylece donanımın kodlaması onları değiştirmeden geri yazar; UNORM
swapchain'lerde değerleri olduğu gibi kopyalar. İkisi de aynı renkleri gösterir ve bunu bir GPU testi
doğrular. Ekran görüntüleri doğrudan UI hedefinden okunur.

### Kararlı id'ler

Etkileşimli her öğenin etiketi `"<yerelleştirilmiş metin>###<kararlı id>"` biçimindedir. Dear ImGui yalnızca
`###` sonrasını hash'ler; böylece öğe her dilde id'sini, kaydedilmiş durumunu ve docking konumunu korur;
testler ve ajanlar da ona aynı id ile ulaşır:

```cpp
if (ImGui::Button(text.label(strings::kCommonSave, "inspector.save")))   // "Save###inspector.save"
{
    save();
}
ui::vec3Field(kit, "inspector.transform.position", position);            // items ".x", ".y", ".z"
```

Id'ler küçük harfli ve noktayla ayrılmıştır, panelden aşağı doğru gider: `outliner.search`,
`inspector.transform.position.x`.

## Tema

Bir tema 28 renk token'ı içeren bir JSON dosyasıdır; her tema her token'ı tanımlar:

```json
{"format": "illusion.theme", "version": 1, "name": "illusion Dark", "highContrast": false,
 "tokens": {"bg.base": "#0F1115", "bg.panel": "#16191F", "text.primary": "#E3E6EB", "accent": "#4C8DFF"}}
```

| Grup | Token'lar |
| --- | --- |
| Yüzeyler | `bg.base`, `bg.panel`, `bg.raised`, `bg.input`, `bg.hover`, `bg.active`, `border`, `separator` |
| Metin | `text.primary`, `text.secondary`, `text.disabled` |
| Vurgu | `accent`, `accent.fill`, `accent.text`, `accent.subtle` |
| Durum | `success`, `warning`, `error`, `info`, `play.tint` |
| Viewport | `viewport.selection`, `viewport.hover`, `axis.x`, `axis.y`, `axis.z`, `axis.cb.x`, `axis.cb.y`, `axis.cb.z` |

Editör **illusion Dark** (varsayılan), **illusion Light** ve **High Contrast** temalarıyla gelir. Testler
WCAG kontrast oranlarını zorunlu kılar: metin her yüzeyde en az 4.5:1 (High Contrast'ta 7:1), grafikler en
az 3:1. Vurgu hazır ayarları (Azure, Violet, Teal, Amber) yalnızca vurgu token'larını değiştirir.

Ölçüler 4 px'lik bir ızgarayı izler: pencere boşluğu 10, frame boşluğu 8×5, öğe aralığı 8×6; pencereler ve
popup'lar için 6, frame'ler için 4, sekmeler için 5 köşe yuvarlaması; kutular yerine 1 px ayırıcılar ve
etkin sekmede 2 px'lik bir vurgu çizgisi. `Theme::apply()`, token'lardan biri, ölçek ya da DPI değiştiğinde
bütün stili token'lardan ve ölçek (kullanıcının UI ölçeği çarpı monitörün içerik ölçeği) ile çarpılmış
ölçülerden yeniden kurar.

## Fontlar ve ikonlar

| Rol | Font | Boyut |
| --- | --- | --- |
| Gövde | Roboto Regular | 15 px |
| Altyazı | Roboto Regular | 13 px |
| Güçlü (başlıklar, sekmeler, butonlar) | Roboto Medium | 15 px |
| Başlık | Roboto Bold | 17 px |
| Mono (konsol, kod, sayılar) | Roboto Mono | 14 px |
| İkon (araç çubukları) | Material Symbols Rounded | 20 px |

Material Symbols her metin fontuna birleştirilir; böylece etiketler ikonla metni karıştırabilir. Fontlar
kullanılan boyutta ihtiyaç anında rasterleştirilir; UI ölçeği ve DPI yalnızca `ImGuiStyle::FontScaleDpi`
değerini değiştirir.

İkon sabitleri sabitlenmiş codepoints dosyasından üretilir; bilinmeyen bir ikon derleme hatasıdır:

```cpp
#include <illusion/editor/ui/Icons.hpp>

ui::iconButton(kit, icons::kPlayArrow, "toolbar.play", text.format(strings::kToolPlay), "Ctrl+P");
const std::optional<ui::Icon> icon = ui::findIcon(componentMeta.icon);   // names known at run time
```

## UI galerisi

`illusion_ui_gallery` her widget'ı, tipografiyi ve ikonları, örnek verilerle editör tarzı bir yerleşimi
gösterir. Tasarım incelemelerinin referansıdır:

```bash
illusion_ui_gallery --theme light --scale 1.5
illusion_ui_gallery --headless --page editor --locale en-XA --screenshot editor.png
```

Pencerede F2 temayı, F3 sayfayı, F4 dili (İngilizce ya da en-XA pseudo-locale) değiştirir. Editör GPU
testleri galeri görüntülerini `editor/tests/data/golden` içindeki golden görüntülerle karşılaştırır; CI de
her görüntüyü `ui-captures` artifact'ı olarak yayımlar.

## Editör çekirdeği

`illusion::editor_core`, penceresi olmayan editördür. `EditorContext`; projenin `VirtualFileSystem`'ini, açık
sahneyi tutan tek bir World'ü, `EntityIndex`'i (id'ler, yollar ve isimler), `Selection`'ı, `UndoStack`'i,
`ChangeJournal`'ı, `EventHub`'ı ve `CommandRegistry`'yi sahiplenir
([ADR 0036](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0036-editor-command-core.md)). Her
değişiklik bir komuttan geçer:

```text
UI widget / shortcut / palette / RPC / MCP
        |  execute<Result>(name, params)  or  executeJson(name, json)
        v
EditorContext::run()  -- validate params (reflected structs, JSON Pointers, suggestions)
        |  begin transaction, run the handler
        v
WorldEditor           -- validated primitives, each recording reversible operations
        |  commit: UndoStep (merge keys, budget), crash journal line, change journal revision, events
        v
World + project files
```

İşlemler; Component değerlerinin önceki ve sonraki hâlleri (scene v2 JSON), ortaya çıkan ya da kaybolan
Entity anlık görüntüleri, sahne ayarlarının değerleri ve `.illusion/trash` içindeki blob'larla desteklenen dosya
değişiklikleridir. Undo, redo, başarısız komutların ve dry run'ların geri alınması ve çökme sonrası kurtarma
hepsi bunları yeniden oynatır; böylece her şey geri alındıktan sonraki sahne, öncekiyle byte byte aynıdır.
Editör her değişiklikten sonra çocukların dünya Transform'larını yeniden türetir; yalnızca sahne dosyalarının
tutmadığı bu türetilmiş değerleri yazar, böylece işlemleri yeniden oynatmak tam kalır.

Bir transaction birden çok komuta da yayılabilir (`transaction.begin`, `editor.batch`): her komut yine kendi
işlemlerini commit eder, journal'a yazar ve bir revizyonu bitirir, ama işlemler geçmiş yerine açık
transaction'da toplanır ve commit'te tek bir undo adımı olur. Transaction bir çağırana aittir; böylece bir RPC
bağlantısının kendi transaction'ı olur ve diğer çağıranlar o bitene kadar bekler (`busy`).
`history.undo {"scope": "mine"}` bir kaynağın adımını, başkalarının sonraki bağımsız adımlarının ötesinden,
önce en üste taşıyarak geri alır; bağımsız adımların sırası değiştirilebildiği için sonuç kesindir. Async
komutlar `RunningCommand::update()` ile her frame'de bir adım ilerleyen görevlerdir; RPC sunucusu onları
`pump()` içinde ilerletir ve `rpc.progress` bildirir.

World'ün yanında iki parça daha vardır: `SettingsCatalog` (sahnelerin ve projenin ayar olarak tuttuğu
reflection'lı struct'lar) ve `assets:/` altındaki her sahne ve Material dosyasının Asset referanslarını bilen
`ReferenceIndex` (reflection ile bulunur, yalnızca bir dosyanın içeriği değişince yeniden ayrıştırılır); böylece
`asset.move` onları yeniden yazabilir, `asset.delete` de reddedebilir. Kılavuz
[kullanımını](../manual/editor/index.md) anlatır.

## Editör kabuğu

`illusion::editor_shell` pencerenin arayüzüdür (`EditorShell`). Editör durumunu yukarıdaki UI yığınıyla çizer
ve yalnızca komutlarla değiştirir: her menü öğesi, araç çubuğu düğmesi ve kısayol, bir komutu sabit
parametrelerle çalıştıran bir `UiAction`'dır (`editor_core` içindedir; böylece eylem tablosu, keymap preset'leri
ve test'leri pencere gerektirmez) ve palet formları parametrelerin reflection bilgisinden kurar. Kabuk, `ui.*`
komutlarının ve `editor.screenshot`'ın arkasındaki `UiHost`'tur ve `EditorContext::setUi()` ile kaydedilir;
o olmadan (`--no-gpu`) bu komutlar `unsupported` ile başarısız olur.

```text
illusion_editor main loop (windowed)
  Platform::pollEvents / waitEvents   <- RpcServer wakes it (Platform::wake)
  RpcServer::pump(8 ms)               -- requests, approvals held for the window
  EditorShell::beginFrame             -- preferences (theme, scale, locale, keymap), pending layouts
  ImGui::NewFrame, EditorShell::draw  -- menus, toolbar, dock space, panels, palette, prompts, toasts
  ImGui::Render, OffscreenUi::renderTo(swapchain), screenshots (OffscreenUi::capture)
  EditorContext::endFrame             -- events, journal, autosave
```

Tercihler çekirdekte reflection'lı bir struct'tır (kullanıcı dizininde `preferences.json`, `preferences.set` ile
değiştirilir). Yerleşimler, açık panelleri tutan bir `[IllusionShell][Panels]` bölümüyle birlikte Dear ImGui
ayar metnidir; beş hazır yerleşim `DockBuilder` ile kurulur. AI Activity paneli `RpcServer`'ı (istemciler,
etkinlik halkası, onaylar) `stateVersion()` değiştiğinde yenilediği kopyalar üzerinden okur; böylece boşta
frame'ler bellek ayırmaz. GPU'su olan penceresiz bir editör aynı kabuğu bir `OffscreenUi` içinde çalıştırır ve
yalnızca `editor.screenshot`'ı yanıtlamak için çizer. Kılavuz [pencereyi](../manual/editor/interface.md) anlatır.

## MCP köprüsü

`illusion_mcp`, bir MCP istemcisi ile editör arasında duran ayrı bir süreçtir
([ADR 0039](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0039-mcp-bridge.md)):

```text
MCP client --stdio--> StdioTransport --> McpServer (illusion::mcp_protocol) --> EditorToolProvider --> RpcClient --> editor
                                            |  era, tools, results, resources       |  launch/attach, crash relaunch,
                                            |  progress, cancellation, prompts      |  rpc.progress, rpc.cancel, log ring
```

`illusion::mcp_protocol` saftır (yalnızca core Json): `McpServer` her seferinde bir satır alır ve bir
`McpTransport` üzerinden yazar; komutları bir `ToolProvider` çalıştırır. Araçları `rpc.discover`'ın OpenRPC
belgesinden üretir, bu yüzden hiçbir komutu bilmesi gerekmez. `illusion::editor_client`, editör tarafını
(`EditorToolProvider`) ve kabul çalıştırıcısının `--via-mcp` için kullandığı köprünün bir MCP istemcisini
(`McpClient`) barındırır. Bir araç çağrısı `McpServer::handle()` içinde çalışır; beklerken sağlayıcı
`CallControl::service()` çağırır; bu çağrı iptalleri ve ping'leri hemen işler, diğer istekleri sonraya bırakır.
Sonucu [yapay zekâ ajanları kılavuzu](../manual/editor/ai-agents.md) anlatır.

## Performans

- Boşta geçen UI kareleri bellek ayırmaz: etiketler kare başına bir arenada oluşturulur, renderer buffer'ları
  yalnızca büyür ve bir test galeri kareleri boyunca hem genel hem Dear ImGui bellek ayırmalarını sayar.
- `editor/shell-idle-frame` tüm editör penceresinin 1920x1080'de boşta bir karesini ölçer (yaklaşık 32 µs;
  bütçe 300 µs); bir test de 120 ısınma karesinden sonra boşta kabuk karelerinin bellek ayırmadığını kontrol eder.
- `ui/gallery-frame` Benchmark'ı en yoğun galeri sayfasının bir karesinin CPU maliyetini ölçer (bir masaüstü
  makinede yaklaşık 40 µs; bütçe 400 µs).
- Editör çekirdeğinin Benchmark'ları: `editor/component-set` (birleştirilen tek bir `component.set`),
  `editor/undo-capture-delete` (bir alt ağacı silip geri getirmek, Entity başına), `editor/scene-query`
  (aranan Entity başına), `editor/transform-translate` (bir gizmo sürüklemesi gibi dört seviye derinlikte
  birleştirilen tek bir `transform.translate`) ve `editor/reference-refresh` (referans indeksi için değişmemiş
  bir sahne dosyasını denetlemek), `editor/batch-step` (`$` referanslı bir `editor.batch` adımı, batch ile
  birlikte geri alınarak) ve `editor/undo-own-check` (kendi adımlarını geri almadan önce sonraki bir adımı
  denetlemek).
- `mcp/translate`, köprünün sunucusu üzerinden anında yanıt veren bir sağlayıcıyla tek bir MCP `tools/call`
  çağrısını ölçer: ayrıştırma, aracı bulma, sonucu yazma (bir masaüstü makinede yaklaşık 1,6 µs; bütçe 10 µs).

## Erişilebilirlik

Editör tam klavye gezinmesi, High Contrast teması, renk körlüğüne uygun eksen ayarı (eksen tutamaçlarında
harfler de bulunur), %75 ile %200 arası UI ölçeği ve azaltılmış hareket sunar. Dear ImGui arayüzü ekran
okuyuculara açmaz.
