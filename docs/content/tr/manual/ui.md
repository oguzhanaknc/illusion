# Oyun UI'ı

Bir oyunun UI'ı Entity'lerden oluşur. **Canvas**, `UiCanvas` taşıyan bir Entity'dir; altındaki `UiElement`
taşıyan Entity'ler onun elemanlarıdır: anchor'lar ve offset'lerle, bir `UiStack` ile ya da bir `UiGrid` ile
yerleştirilir; görüntü (`UiImage`) ve metin (`UiText`) olarak çizilir. `UiWorld`, bir `GameSession` içinde
elemanları yerleştirir ve her karede bir çizim listesi kaydeder; dışa aktarılmış player ve editörün Game
görünümü bunu `UiRenderer` ile sahnenin üzerine çizer (ADR 0057). UI Component'lerinin `Transform`'u yoktur
ve diğer tüm Component'ler gibi sahnelerde ve prefab'lerde saklanırlar.

Header'lar: `<illusion/ui/UiComponents.hpp>`, `<illusion/ui/UiGeometry.hpp>`, `<illusion/ui/UiWorld.hpp>`

Kütüphane: `illusion::ui` (oyun modülleri header'ları `USES ui` ile alır)

Tüm boyutlar **canvas birimi** cinsindendir: orijin sol üst köşedir ve y aşağı doğrudur. Canvas, birimlerini
hedefin piksellerine eşler; böylece bir UI her çözünürlükte aynı görünür (aşağıya bakın).

## Canvas'lar ve ölçekleme {#canvases-and-scaling}

Canvas bir UI'ın köküdür. `scale` alanı (`UiCanvasScale`) canvas birimlerinin hedefe nasıl eşlendiğini belirler:

| `UiScaleMode` | Bir canvas birimi |
| --- | --- |
| `Fit` (varsayılan) | Hedef genişliği / `referenceSize.x` ile hedef yüksekliği / `referenceSize.y` değerlerinin küçüğü kadar piksel: referans boyutun tamamı her zaman sığar |
| `MatchHeight` | Hedef yüksekliği / `referenceSize.y` piksel: canvas her zaman `referenceSize.y` birim yüksekliğindedir, genişliği en-boy oranını izler |
| `MatchWidth` | Hedef genişliği / `referenceSize.x` piksel: canvas her zaman `referenceSize.x` birim genişliğindedir |
| `Pixels` | Bir piksel |

`referenceSize`, UI'ın tasarlandığı boyuttur (varsayılan 1920 x 1080). `userScale`, oyuncunun erişilebilirlik
için seçebildiği ek bir çarpandır; 0,5 ile 3 arasına sıkıştırılır ve `Pixels` dahil her modu çarpar.
`canvasMetrics()` bir hedef için sonucu verir: `pixelsPerUnit`, birim cinsinden canvas `size` ve `safeArea`.

| `UiCanvas` alanı | Anlamı |
| --- | --- |
| `scale` | Yukarıdaki `UiCanvasScale` |
| `order` | Daha yüksek `order`'lı canvas'lar üstte çizilir; eşit değerlerde Entity sırası korunur |
| `useSafeArea` | Varsayılan olarak açık: canvas, hedefin tamamı yerine ekranın güvenli kenar boşlukları içine yerleşir (TV overscan'i, telefon çentiği) |
| `modal` | Duraklatma menüsü içindir: gösterilirken oyunun diğer input action'ları girdi görmez. Saklanır ama henüz hiçbir şey onu okumaz; sonraki bir paketin widget'ları okuyacak |

Host, hedef boyutunu ve güvenli olmayan kenarları piksel olarak `GameSession::setUiFrame()` ile verir
(player ve Game görünümü verir). `Inactive` tag'li Entity'ler ve torunları yerleştirilmez ve çizilmez; yani
bir canvas'ı devre dışı bırakmak tüm UI'ı gizler.

## Yerleştirme {#placement}

`UiElement::placement`, bir elemanı ebeveyninin dikdörtgeninin içine (canvas'ın çocuğu için canvas'ın alanı)
iki anchor ve iki offset ile koyar:

| Alan | Anlamı |
| --- | --- |
| `anchorMin` | Sol üst köşenin ebeveyn içinde nereye çapalandığı: (0, 0) ebeveynin sol üstü, (1, 1) sağ altıdır |
| `anchorMax` | Sağ alt köşe için aynısı |
| `offsetMin` | Sol üst köşenin anchor'ından uzaklığı, canvas birimi cinsinden |
| `offsetMax` | Sağ alt köşenin anchor'ından uzaklığı |

Her köşe `parent.min + anchor * parent.size + offset` olur. Anchor'lar eşitse eleman, ebeveynin bir noktasında
sabit boyutludur; farklıysa ebeveynle birlikte esner. `resolvePlacement()` dikdörtgeni hesaplar; `max` hiçbir
zaman `min`'den önce olmaz.

**Örnek 1: sağ üst köşede bir skor**, 296 x 64 birim, üst ve sağ kenardan 24 birim uzakta:

```cpp
illusion::UiPlacement topRight;
topRight.anchorMin = topRight.anchorMax = {1.0f, 0.0f};
topRight.offsetMin = {-320.0f, 24.0f};
topRight.offsetMax = {-24.0f, 88.0f};
const illusion::UiRect rect = illusion::resolvePlacement(topRight, {{0.0f, 0.0f}, {1920.0f, 1080.0f}});
// rect: min (1600, 24), max (1896, 88)
```

**Örnek 2: alt kenar boyunca bir çubuk**; 72 birim yüksekliğinde, alt kenarın 24 birim üstünde ve canvas
genişliği ne olursa olsun sol ve sağ kenardan 24 birim içeride:

```cpp
illusion::UiPlacement bottomBar;
bottomBar.anchorMin = {0.0f, 1.0f};
bottomBar.anchorMax = {1.0f, 1.0f};
bottomBar.offsetMin = {24.0f, -96.0f};
bottomBar.offsetMax = {-24.0f, -24.0f};
const illusion::UiRect rect = illusion::resolvePlacement(bottomBar, {{0.0f, 0.0f}, {1920.0f, 1080.0f}});
// rect: min (24, 984), max (1896, 1056)
```

Bir `UiStack`'in ya da `UiGrid`'in çocuğu o ebeveyn tarafından yerleştirilir ve `placement`'ı yok sayar; yalnızca
`size`'ı kullanır. `size` orada elemanın canvas birimi cinsinden boyutudur; sıfır olan eksen içeriğin boyutunu
(bir `UiText` ya da görüntü) alır veya esner.

## Görüntüler {#images}

`UiImage`, elemanın dikdörtgeninin üzerine bir Texture çizer; `image` boşsa düz bir renk çizer.

| Alan | Anlamı |
| --- | --- |
| `image` | Texture Asset'i (`assets:/textures/panel.png`); boşsa `tint` düz renk olarak çizilir |
| `tint` | Görüntüyle çarpılan doğrusal RGBA; alfası ve elemanın `opacity` değeri onu soldurur |
| `border` | Görüntü pikseli cinsinden nine-slice kenarlıkları (`left`, `top`, `right`, `bottom`); hepsi sıfırsa görüntünün tamamı esner |
| `borderScale` | Kenarlıkların görüntü pikseli başına canvas birimi: 2, 8 piksellik kenarlığı 16 birim genişliğinde çizer |
| `preserveAspect` | Görüntüyü en-boy oranını koruyarak elemana sığdırır; kenarlıklar yok sayılır |

Nine-slice panel, köşelerini ve kenarlarını her boyutta keskin tutar: dört köşe `border * borderScale` birim
olarak çizilir, kenarlar bir eksen boyunca, orta kısım iki eksen boyunca esner.

Hâlâ yüklenmekte olan bir görüntü, gelene kadar hiçbir şey çizmez.

## Metin {#text}

`UiText`, elemanın dikdörtgeninde bir [font Asset'i](assets.md#fontlar) ile metin çizer; bu, projenin
Asset'lerindeki bir `.ttf` ya da `.otf` dosyasıdır. Varsayılan karakter aralıkları Basic Latin, Latin-1 ve
Latin Extended-A'yı kapsar; yani Türkçe ve çoğu Avrupa dili çalışır.

| Alan | Anlamı |
| --- | --- |
| `text` | UTF-8 metin; `key` boşsa gösterilir |
| `key` | Oyunun Localization'ındaki bir string tablo anahtarı; `text` yerine onun mesajı gösterilir |
| `font` | Font Asset'i (`assets:/fonts/Roboto-Regular.ttf`); yoksa hiçbir şey çizilmez |
| `size` | Canvas birimi cinsinden em boyutu |
| `color` | Doğrusal RGBA |
| `align` | Yatay: `Start`, `Center` veya `End` |
| `verticalAlign` | Elemanda dikey: `Top`, `Middle` veya `Bottom` |
| `wrap` | Varsayılan olarak açık: satırlar elemanın genişliğinde kırılır; kapalıysa tek satır kalır |
| `lineSpacing` | Fontun satır yüksekliğinin çarpanı (1, fontun kendi değeridir) |
| `letterSpacing` | Glyph başına ek ilerleme, em cinsinden |

Editör şablonlarından oluşturulan yeni projeler `assets/fonts/Roboto-Regular.ttf` dosyasını içerir
(Apache-2.0; lisansı yanındadır); böylece `UiText` hemen çalışır. Başka herhangi bir `.ttf` de aynı şekilde
çalışır.

### Düz ve yerelleştirilmiş metin {#literal-and-localized-text}

Düz bir `text` yazıldığı gibi gösterilir. Bir `key` ise oyunun Localization'ında aranır ve yerine onun mesajı
çizilir (`GameSession::localization()`; etkin dili izler ve dil değişince yerleşim güncellenir). Oturum,
oluşturulurken projenin `assets:/locales/*.json` dosyalarını okur: `en.json`'un İngilizce metni tabandır ve
diğer her dosya bir dil ekler. Tabloların bilmediği bir anahtar, anahtarın kendisini gösterir; bu
`UiWorldStatistics::missingKeys` içinde sayılır.

```json
{
  "format": "illusion.locale",
  "version": 1,
  "locale": "en",
  "displayName": "English",
  "plural": "en",
  "number": {"decimal": ".", "group": ","},
  "strings": {
    "showcase.hud.title": "Showcase HUD"
  }
}
```

`assets/locales/tr.json` aynı anahtarları çevirileriyle taşır ([Yerelleştirme](localization.md) sayfasına bakın):

```json
{
  "format": "illusion.locale",
  "version": 1,
  "locale": "tr",
  "displayName": "Türkçe",
  "plural": "tr",
  "number": {"decimal": ",", "group": "."},
  "fallback": "en",
  "strings": {
    "showcase.hud.title": "Vitrin Arayüzü"
  }
}
```

`key` değeri `showcase.hud.title` olan bir `UiText` bu durumda "Showcase HUD" gösterir; etkin dil `tr` ise
"Vitrin Arayüzü" gösterir.

## Stack'ler ve grid'ler {#stacks-and-grids}

Bir elemandaki `UiStack`, çocuklarını art arda yerleştirir:

| Alan | Anlamı |
| --- | --- |
| `axis` | `Vertical` (varsayılan, yukarıdan aşağı) veya `Horizontal` (soldan sağa) |
| `spacing` | Çocuklar arasındaki boşluk, canvas birimi cinsinden |
| `padding` | Elemanın kenarlarının içindeki boşluk (`left`, `top`, `right`, `bottom`) |
| `crossAlign` | Yöne dik yerleşim: `Start`, `Center`, `End` veya `Stretch` (varsayılan: stack'in iç boyutunu doldurur) |

Eksen boyunca bir çocuk `size` değerini, yoksa içeriğinin boyutunu (metin stack'in iç genişliğinde kırılır),
o da yoksa esneyen diğer çocuklarla kalan alanı paylaşır. Eksene dik yönde `crossAlign`'ı izler.

`UiGrid`, çocukları eşit hücrelerden oluşan satırlara yerleştirir; soldan sağa, sonra aşağı: satır başına
`columns` hücre (en az 1), her hücre için `cellSize`, sütunlar ve satırlar arasında `spacing` ve `padding`.
`size`'ı olan bir çocuk hücresinde o boyutta ortalanır, yoksa hücreyi doldurur. Bir stack ya da grid, kendi
stack'leri ve grid'leri olan başka elemanlar içerebilir.

## Döndürme, ölçek, opaklık ve kırpma {#rotation-scale-opacity-and-clipping}

`UiElement`'in şu alanları, bir elemanın ve torunlarının nasıl çizileceğini değiştirir; yerleşimin onları
nereye koyduğunu değiştirmez:

| Alan | Anlamı |
| --- | --- |
| `pivot` | Döndürme ve ölçeğin merkezi, elemanın dikdörtgeninde: (0.5, 0.5) ortadır |
| `rotation` | Pivot etrafında saat yönünde döndürme, radyan cinsinden (Inspector'da derece olarak gösterilir) |
| `scale` | Pivot etrafında görsel ölçek; yerleşim bunu görmez |
| `opacity` | 0 ile 1 arası; elemanın ve her torunun alfasını çarpar |
| `clipChildren` | Torunlar elemanın dikdörtgeninin dışına çizilmez |

Döndürme, ölçek ve opaklık hiyerarşi boyunca birikir. Kırpma bir dikdörtgendir: kaydırma alanları ve dolan
çubuklar için kullanın, yuvarlak şekiller için değil.

## Çizim sırası {#draw-order}

Canvas'lar artan `order` sırasıyla çizilir (eşit değerlerde Entity indeksine göre). Bir canvas içinde elemanlar
hiyerarşi sırasıyla çizilir: önce ebeveyn, sonra çocukları ve sonraki kardeşler öncekilerin üzerine. Bir elemanı
öne almak için onu `<illusion/ecs/Hierarchy.hpp>` içindeki `reorder()` ile kardeşlerinin sonrasına taşıyın.

## Editörde HUD kurmak {#building-a-hud-in-the-editor}

1. `HUD` adlı bir Entity oluşturun ve Inspector'da `UI canvas` Component'ini ekleyin.
2. Outliner'da çocuk Entity'ler oluşturun (`HUD`'un üzerine sürükleyin) ve her birine `UI element`, ardından
   `UI image`, `UI text`, `UI stack` veya `UI grid` ekleyin (bu dördü `UI element` gerektirir).
3. Alanları ayarlayın: yerleşim anchor'ları ve offset'leri, font ve Texture Asset'leri (Asset Browser'dan alanların
   üzerine sürükleyin), renkler ve boyutlar.
4. **Play**'e basın: UI, Game görünümünde çizilir. Scene görünümü henüz UI çizmez; sonucu Game görünümünde
   kontrol edin; `"viewport": "game"` ile `viewport.screenshot` komutu onun resmini alır.

`samples/showcase` projesi, her UI Component'ini ve her alanı kullanan bir HUD içerir; sahne `build.jsonl` ile
kurulur ([Showcase Projesi](editor/showcase.md) sayfasına bakın).

## Bir modülden HUD kurmak {#building-a-hud-from-a-module}

Bir modül aynı Entity'leri Component'leriyle oluşturur. Header'ları almak için `USES ui` ile derleyin:

```cmake
illusion_add_module(hud SOURCES game/Hud.cpp USES ui)
```

```cpp
#include <illusion/core/Module.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/ui/UiComponents.hpp>

#include <string>
#include <utility>

namespace
{

illusion::Entity g_score;

void start(illusion::ModuleContext& context)
{
    illusion::World& world = context.world();
    const illusion::Entity canvas = world.create(illusion::UiCanvas{});

    illusion::UiElement placement;
    placement.placement.anchorMin = placement.placement.anchorMax = {1.0f, 0.0f};
    placement.placement.offsetMin = {-320.0f, 24.0f};
    placement.placement.offsetMax = {-24.0f, 88.0f};
    illusion::UiText text;
    text.text = "Score 0";
    text.font = illusion::AssetRef{"assets:/fonts/Roboto-Regular.ttf"};
    text.align = illusion::UiTextAlign::End;
    g_score = world.create(placement, std::move(text));
    illusion::setParent(world, g_score, canvas);
}

void showScore(illusion::World& world, int score)
{
    world.get<illusion::UiText>(g_score).text = "Score " + std::to_string(score);
    world.markChanged<illusion::UiText>();
}

} // namespace
```

`UiWorld` yerleşimi yalnızca bir UI Component'i, hiyerarşi, `Inactive` tag'leri, dil ya da hedef değiştiğinde
yeniden hesaplar. Bir UI Component'ini referansla yazan kod, `Hierarchy`'de olduğu gibi sonrasında
`world.markChanged<T>()` çağırır; bir Entity oluşturmak ya da Component eklemek kendini duyurur.

## Henüz olmayanlar {#what-is-not-there-yet}

Bu paket UI çizer; input'a tepki vermez. Sonraki paketler widget'ları (düğmeler, kaydırıcılar, odak ve gezinme,
fare ve gamepad'lerden input; `modal` bunu kullanacak), oyun modülleri için `UiService`'i, Scene görünümünde
canvas editörünü ve geçişleri ile animasyonu ekler. İlk sürümün sınırları
([ADR 0057](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0057-game-ui.md)):

- Metin shaping olmadan soldan sağa çizilir: Arapça, İbranice, Hint yazı sistemleri ve ligatürler
  desteklenmez, sağdan sola metin de desteklenmez ve CJK, font atlası için fazla büyük karakter aralıkları
  gerektirir.
- Metinde dış çizgi, gölge ve gradyan henüz yoktur.
- Canvas'lar ekranı kaplar; dünyaya yerleştirilen bir UI (bir karakterin üzerindeki etiket) kapsanmaz.
