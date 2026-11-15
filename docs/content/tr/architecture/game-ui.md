# Oyun UI'ı Mimarisi

Bu sayfa oyun UI'ının nasıl kurulduğunu anlatır: hangi kütüphane ne yapar, bir frame'de ne olur ve onu hızlı
tutan kurallar nelerdir. Nasıl kullanılacağı [Oyun UI'ı kılavuzunda](../manual/ui.md) anlatılır; karar ve
seçenekleri [ADR 0057](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0057-game-ui.md)'dedir.

## Katmanlar

Her katman yalnızca bu listede kendisinden önce gelenlere bağlıdır:

1. **`engine/assets`: `Font`.** Bir CPU Asset'i. `Font::bake()` bir `.ttf` ya da `.otf` dosyasını glyph
   advance'ları, sınırları ve em cinsinden kerning ile tek kanallı bir signed distance field atlas'ına çevirir.
   Diğer Asset'ler gibi cook edilir ve GPU kodu içermez.
2. **`engine/ui`: geometri, draw list, Component'ler, `UiWorld`, `UiService`.** `UiGeometry.hpp` canvas ölçeğini
   ve yerleşim matematiğini, `UiDrawList.hpp` toplu quad'ları, `UiComponents.hpp` ve `UiWidgets.hpp` kaydedilen
   `illusion.Ui*` Component'lerini, `UiWorld.hpp` System'i (layout, etkileşim, event'ler, geçişler) ve
   `UiService.hpp` scripting API'yi adapter'ı `UiWorldService` ile birlikte barındırır. Bu kütüphanede GPU kodu
   yoktur; oyun modülleri başlıklarını `USES ui` ile alır.
3. **`engine/render`: `UiRenderer` ve `UiRenderAssets`.** `UiRenderer::render()` bir `UiDrawData`'yı bir renk
   hedefine çizer. `UiRenderAssets` font ve resimleri `AssetStreamer` ile yükler ve renderer'a kaydeder; UI
   kütüphanesinin tanımladığı `UiAssets` arayüzünü uygular, böylece kütüphane hiçbir GPU türü görmez.
4. **`engine/runtime`: `GameSession`.** Çalışan bir oyun bir `UiWorld`'e sahiptir, onu oturumun adım sırasında
   çalıştırır, input'unu platformdan ve input action'larından doldurur ve oyunun `Localization`'ını
   (`assets:/locales`) tutar.
5. **Host'lar.** Dışa aktarılmış player (`engine/player`) ve editörün Game view'ı (`editor/play`,
   `editor/viewport`) hedef boyutu `GameSession::setUiFrame()` ile verir, sahneden sonra `UiRenderer::render()`
   çağırır ve sonucu gösterir.

## Bir frame

`GameSession::update()` UI'ı oyun modüllerinden ve hiyerarşi adımından sonra çalıştırır; böylece bir modülün
değişiklikleri aynı frame'de görünür:

1. **Layout, yalnızca bir şey değiştiyse.** `UiWorld`, UI Component'lerinin, hiyerarşinin ve `Inactive`
   tag'lerinin `World::componentVersion()` değerlerini, dili ve hedef boyutu son layout'unkilerle karşılaştırır.
   Hiçbir şey değişmediyse son sonucu yeniden kullanır (`ui/layout-reused`). Aksi halde yerleşimleri, stack'leri
   ve grid'leri öğe başına bir `UiComputedRect`'e çözer.
2. **Etkileşim.** Input varsa, işaretçi hit test'i (döndürme, ölçek, clip'ler ve modal canvas'lara saygı duyan en
   üstteki etkileşimli öğe), `ui.navigate`, `ui.submit` ve `ui.cancel` ile odak ve gezinme, text field'lar,
   kaydırma ve geçişler layout üzerinde çalışır ve güncellemenin event'leri `UiService::pollEvents()` için
   toplanır.
3. **Draw list.** System canvas'ları `order` sırasıyla, öğeleri hiyerarşi sırasıyla gezer ve dikdörtgenleri,
   resimleri, nine-slice panelleri ve glyph'leri önceden ayrılmış `UiDrawList`'e quad olarak kaydeder. Aynı
   Texture, clip ve metin efektine sahip ardışık quad'lar tek bir komutu paylaşır.
4. **Pass.** Host `UiWorld::drawData()` verisini `UiRenderer::render()`'a verir; o da `SceneRenderer`'ın
   doldurduğu renk hedefinin üstüne, **tonemapping ve anti-aliasing'den sonra**, premultiplied alpha blending ile
   ve derinlik olmadan çizer. TAA ve upscaler'lar UI'ı hiç görmez, ekran görüntüleri onu içerir.

Headless bir oturum (testler, editör komut çekirdeği) çizmeden layout yapar ve input'u işler. Game view'ın
düzenlenen World'ü önizlemek için kullandığı input'suz güncelleme layout yapar ve kaydeder, ama hiçbir geçişi
ilerletmez ve kaydedilen hiçbir Component'e yazmaz (`UiWorldDesc::preview`).

## Öğeler neden Entity'dir

Bir canvas `UiCanvas` taşıyan bir Entity'dir ve öğeleri onun altındaki Entity'lerdir. Sahneler ve prefab'lar
onları kaydeder, reflection Inspector'larını çizer, genel `entity.*` ve `component.*` komutları ile MCP araçları
onları düzenler, undo ve hot reload onlar üzerinde çalışır ve layout bitişik Component'ler üzerinde bir
System'dir. Ayrı bir UI belgesi bunların hepsini yeniden yazmak anlamına gelirdi. Bedeli, bir UI Entity'sinin
`Transform`'u olmaması (motorun diğer System'leri onu atlar) ve uzun bir listenin çok sayıda Entity olmasıdır;
tartılan seçenekler [ADR 0057](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0057-game-ui.md)'dedir.

## Allocation ve performans kuralları

- **Sabit bir frame'de allocation yok.** Draw list'in ve event listesinin dizileri `UiWorld` oluşturulurken
  `UiWorldDesc` ile boyutlandırılır (varsayılan olarak güncelleme başına 16.384 öğe, 64 iç içe seviye ve 256
  event). Sığmayan şey atlanır ya da bırakılır ve `UiWorldStatistics`'te sayılır, hiçbir zaman büyütülmez.
  Testler, input'lu olanı da içeren sabit bir frame'in allocation'larını sayar.
- **Layout yalnızca değişimde çalışır.** Bir UI Component'ine referansla yazmak `world.markChanged<T>()` gerektirir;
  `UiService` bunu modüller için yapar.
- **Geçişler yalnızca çizer.** Layout'u asla değiştirmezler; varmış bir geçiş hiçbir şeye mal olmaz.
- **Pass kendi başına allocation yapmaz.** NVRHI, frame başına iki buffer yüklemesi için küçük ve sabit sayıda
  allocation yapar; bir test bu sayının artmadığını doğrular. `UiRenderer`, vertex, index ve Texture için sabit
  kapasitelerle oluşturulur.
- **Batch'leri yalnızca durum böler.** Öncekinden farklı bir Texture, clip dikdörtgeni ya da metin konturu yeni bir
  draw call başlatır; bir HUD bunlardan bir avuçtur.

Benchmark'lar ve bütçeleri (birim başına nanosaniye; bir masaüstü sonucunun kabaca 5 ila 10 katı olarak
belirlenir; bkz. [Profiling](../manual/profiling.md#performans-butceleri)):

| Benchmark | Birim | Bütçe | Geliştirme makinesinde ölçülen |
| --- | --- | --- | --- |
| `ui/text-layout` | glyph | 1.000 ns | kaydedilmedi |
| `ui/draw-list-quads` | quad | 250 ns | kaydedilmedi |
| `ui/layout` | öğe, 1.000 öğe | 5.000 ns | 544 ns |
| `ui/layout-reused` | öğe, 1.000 öğe | 3.000 ns | 408 ns |
| `ui/hit-test` | 1.000 düğmeli güncelleme | 400.000 ns | 58 µs |
| `ui/set-text-key` | çağrı | 20.000 ns | 2,6 µs |
| `ui/scroll-1000` | güncelleme | 300.000 ns | 43 µs |
| `ui/transitions-1000` | 1.000 çalışan geçişli güncelleme | 500.000 ns | 81 µs |
| `ui/render-hud` (GPU) | 195 quad'ın `render()`'ı, submit ve bekleme dahil | 500.000 ns | 67 µs |
| `gameui/pick-and-handles` (editör) | seçme ve tutamaç hit test'leri | 20.000 ns | 3,3 µs |

## Editör tarafı

Editör aynı kütüphaneleri bağlar; hiçbir oyun editörü bağlamaz (ADR 0030).

- **Game view**, düzenlenen World'ün UI'ını bir preview `UiWorld` ile (`UiWorldDesc::preview`) çizer; view'ın
  boyutuna göre layout yapılır ve sahneden sonra bir `UiRenderer` ile çizilir. Bir oyun çalışırken oturumun
  kendi UI'ını çizer. Scene view UI çizmez.
- **Komutlar.** Game UI kategorisi: `gameui.create`, `gameui.layout`, `gameui.click`, `gameui.focus`,
  `gameui.pick`, `gameui.place` ve `gameui.set_anchors` (toplam 193 komut); her biri aynı zamanda bir MCP aracıdır.
  World'ü yalnızca `WorldEditor` üzerinden değiştirirler, bu yüzden hepsi geri alınabilir; `gameui.click` ve
  `gameui.focus` çalışan oyunun oturumuna etki eder.
- **Yerleşim matematiği.** `editor/core` içindeki `UiPlacementEdit.hpp` GPU'dan bağımsızdır: 16 anchor ön ayarı,
  bir dikdörtgeni koruyan ya da ona ulaşan yerleşimler, hesaplanmış bir layout'ta seçme, tutamaç hit test'leri ve
  sürüklemeler. Pencerenin Game view'ı ve komutlar onu paylaşır; bu yüzden fareyle bir sürükleme ile bir
  `gameui.place` çağrısı aynı sonucu verir.
- **Tutamaçlar ve işaretçiler** `canvas.selection`, `canvas.handle` ve `canvas.anchor` tema token'larıyla, Game
  view'ın resminin üstüne editörün kendi UI'ı tarafından çizilir; oyunun draw list'i hiçbir zaman editör
  grafiği içermez.

## Açık konular

Roadmap'te ve ADR 0057'de kayıtlıdır:

- Sağdan sola metin ve shaping, dinamik atlas sayfalarıyla CJK; metin seçimi ve pano.
- Başlıklar büyük boyutlarda yumuşak görünürse multi-channel distance field'lar.
- Dünya uzayında UI (bir karakterin üstünde etiket).
- UI pass'inin HDR çıkışı ve onu render ölçeğinde değil upscale'den sonra çizmek (ikisi de M9 ile).
- Küçük bir öğeyi çerçevelemek için Game view'ı yakınlaştırmak ve kaydırmak, ve Scene view'da UI.
- Etiket yerine glyph resimleri ve geri dönüştürülen listeler.
- Bir UI prefab'ını örneklemek için UI'a özel bir fonksiyon (bugün `SceneService::instantiate()` yapar).
- Farklı ölçekli canvas'lar tek bir draw list'i ilk canvas'ın birimlerinde paylaşır ve yalnızca o canvas glyph'leri
  piksellere hizalar; pass'te sıfır allocation kalıcı olarak eşlenmiş buffer'lar gerektirir.
