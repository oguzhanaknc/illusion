# Oyun UI'ı

Bir oyunun UI'ı Entity'lerden oluşur. **Canvas**, `UiCanvas` taşıyan bir Entity'dir; altındaki `UiElement`
taşıyan Entity'ler onun elemanlarıdır: anchor'lar ve offset'lerle, bir `UiStack` ile ya da bir `UiGrid` ile
yerleştirilir; görüntü (`UiImage`) ve metin (`UiText`) olarak çizilir. `UiWorld`, bir `GameSession` içinde
elemanları yerleştirir ve her karede bir çizim listesi kaydeder; dışa aktarılmış player ve editörün Game
görünümü bunu `UiRenderer` ile sahnenin üzerine çizer (ADR 0057). UI Component'lerinin `Transform`'u yoktur
ve diğer tüm Component'ler gibi sahnelerde ve prefab'lerde saklanırlar.

Header'lar: `<illusion/ui/UiComponents.hpp>`, `<illusion/ui/UiGeometry.hpp>`, `<illusion/ui/UiWorld.hpp>`,
`<illusion/ui/UiWidgets.hpp>`

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
| `modal` | Duraklatma menüsü içindir: bir eleman gösterdiği sürece yalnızca o ve üstündeki canvas'lar input alır, oyunun diğer input action'ları hiçbir şey görmez ([Modal canvas'lar](#modal-canvases)) |

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
`world.markChanged<T>()` çağırır; bir Entity oluşturmak ya da Component eklemek kendini duyurur. Sahnenin zaten
içerdiği UI'ı değiştirmek için modül bunun yerine `UiService` kullanır ([UI'ı script'lemek](#scripting-the-ui)).

## Düğmeler, anahtarlar ve kaydırıcılar {#buttons-toggles-and-sliders}

`UiButton`, `UiToggle` veya `UiSlider` ile birlikte `UiInteractable` (`illusion/ui/UiWidgets.hpp`), bir öğenin
`ui.navigate`, `ui.submit` ve `ui.cancel` action'ları üzerinden fareye, klavyeye ve gamepad'lere tepki vermesini sağlar
([Input ve odak](#input-and-focus)). Bir widget; görünümünü veren bir `UiImage`, bir `UiInteractable` ve üç widget
Component'inden biriyle donatılmış bir elemandır; hepsi `UiElement` ister, widget Component'leri ise
`UiInteractable` ister.

### UiInteractable

Bir elemanı üzerine gelinebilir, basılabilir ve odaklanabilir yapar. Geçerli durumunun tint'i, elemanın
`UiImage` tint'iyle çarpılır:

| Alan | Anlamı |
| --- | --- |
| `enabled` | Varsayılan olarak açık. Devre dışı bir eleman input'u yok sayar, odaklanamaz ve `disabledTint` gösterir |
| `focusable` | Varsayılan olarak açık. Klavye ve gamepad'lerle gezinerek erişilebilir; işaretçi yine de ona basabilir |
| `hoveredTint` | İşaretçinin altında (varsayılan 1,15, 1,15, 1,15, 1) |
| `pressedTint` | İşaretçi ya da submit tarafından basılı tutulurken (varsayılan 0,8, 0,8, 0,8, 1) |
| `focusedTint` | Gezinmeyle odaklanmışken (varsayılan 1,15, 1,15, 1,15, 1) |
| `disabledTint` | `enabled` kapalıyken (varsayılan 0,5, 0,5, 0,5, 0,5) |
| `up`, `down`, `left`, `right` | Gezinme için açık komşular: o yönde odağı alacak Entity |

Durumların bir önceliği vardır: önce devre dışı, sonra basılı, sonra odaklı, sonra üzerine gelinmiş; hiçbirinde
olmayan eleman boyanmaz. Boş olan ya da odağı alamayan bir komşu, o yöndeki en yakın odaklanabilir elemana
düşer ([Input ve odak](#input-and-focus)). Runtime Component'i `UiInteraction` (`hovered`, `pressed`,
`focused`) her etkileşimli elemana eklenir ve UI tarafından her karede yazılır.

### UiButton

Bir tag'dir. Düğme etkinleştirildiğinde `Clicked` olayını bildirir: işaretçi üzerinde basılıp bırakılır ya da
odaktayken `ui.submit` basılıp bırakılır.

### UiToggle

Etkinleştirmenin değiştirdiği bir değer, `on`. Anahtarın **ilk çocuğu** yalnızca anahtar açıkken gösterilir:
onu onay işareti yapın. Gizliyken işaretçi tarafından da vurulmaz. Değerin değişmesi, 1 (açık) ya da 0 (kapalı)
değeriyle bir `ValueChanged` olayı bildirir.

### UiSlider

Bir aralıkta, sürükleyerek ya da ekseni boyunca gezinerek değişen bir değer:

| Alan | Anlamı |
| --- | --- |
| `value` | Değer; `min` ile `max` arasında tutulur |
| `min` | Solda (ya da üstte) değer |
| `max` | Sağda (ya da altta) değer |
| `step` | Değerler `min`'den başlayarak bunun katlarına oturur; 0 (varsayılan) sürekli demektir |
| `axis` | `Horizontal` (varsayılan) veya `Vertical` |
| `navigationStep` | Bir gezinme basışının aralığa oranı olarak değişimi; `step` 0 iken kullanılır (varsayılan 0,1) |

Kaydırıcı iki çocuğu yerleştirir. **İlk çocuğu** dolgudur: kaydırıcının başından değere kadar uzatılır.
**İkinci çocuğu** tutamaçtır: eksen boyunca değerin üzerine ortalanır ve kendi placement'ının boyutunu ve çapraz
eksendeki konumunu korur. İkiden sonraki çocuklar sıradan elemanlardır. Sürükleme, değeri işaretçinin kaydırıcı
boyunca konumundan belirler; düğme basılıyken işaretçi kaydırıcıdan çıksa da sürer.

## Input ve odak {#input-and-focus}

UI, işaretçiyi ve üç input action'ını okur. `GameSession` bunları Play modunda her update'te doldurur
(`UiWidgets.hpp` içindeki `UiInput`; testler elle doldurup `UiWorld::update(frame, input)` çağırır).

**İşaretçi.** İşaretçi, altındaki en üstteki etkin etkileşimli elemana vurur:

- Sonraki canvas'lar (daha yüksek `order`), hiyerarşide sonraki düğümler ve sonraki kardeşler üsttedir.
- Vurma testi her elemanın dönmesini ve ölçeğini izler; `clipChildren` taşıyan bir atanın dikdörtgeninin dışındaki
  nokta, onun altındaki hiçbir şeye vurmaz.
- `opacity` değeri 0 olan elemanlara, gizli bir anahtar çocuğunun altındakilere ve devre dışı elemanlara vurulmaz;
  devre dışı bir düğme işaretçinin arkasındaki elemana ulaşmasına izin verir. `UiInteractable`'sız elemanlar
  (düz bir görüntü) işaretçiyi asla engellemez.
- Pencerenin odağı yoksa ya da imleç hedefin dışındaysa veya yakalanmışsa işaretçi yoktur; o zaman hiçbir şeyin
  üzerine gelinmez.

Birincil düğmeyi bir elemanın üzerinde basmak ona basar ve odaklanabiliyorsa odağı verir (değeri 0 olan bir
`FocusChanged` olayı). Aynı elemanın üzerinde bırakmak onu etkinleştirir. Basılan bir kaydırıcı, düğme
bırakılana kadar işaretçiyi izler.

**Action'lar.** Projenin input Asset'inde yoksa `GameSession::create` bu action'ları varsayılan atamalarla ekler:

| Action | Tür | Varsayılan atamalar |
| --- | --- | --- |
| `ui.navigate` | `Vector2` | `Keys.Up.Down.Left.Right`, `Keys.W.S.A.D`, `Stick.Left`, `Stick.DPad` |
| `ui.submit` | `Button` | `Key.Enter`, `Key.Space`, `Gamepad.South` |
| `ui.cancel` | `Button` | `Key.Escape`, `Gamepad.East` |

Bunlar sıradan action'lardır: diğerleri gibi yeniden atayın ([Input action'ları](actions.md#yeniden-atama)) ya da
projenizin input Asset'inde başka atamalarla tanımlayın; Asset'te zaten bulunan bir action değiştirilmez.
Showcase projesinin `assets/input/showcase.input` dosyası bir örnektir.

**Gezinme.** `ui.navigate` değerinin 0,5'i aşan sapması bir yön sayılır; büyük eksen kazanır. Odak, yön başladığında
bir kez hareket eder; yön basılı tutuldukça 0,4 saniye sonra başlayarak her 0,12 saniyede tekrarlar. Hiçbir şey
odakta değilken ilk hareket (ya da ilk `ui.submit`), odaklanabilen en üstteki canvas'ta hiyerarşi sırasındaki
ilk odaklanabilir elemanı odaklar. Aksi halde odak, odaktaki elemanın o yöndeki açık komşusuna gider; yoksa (ya da
odağı alamayan bir komşu varsa) merkezi o yönde en yakın olan odaklanabilir elemana gider. Odaktaki bir kaydırıcı,
ekseni boyunca olan yönleri kendisi alır: değerini `step` kadar, `step` 0 ise aralığın `navigationStep` oranı kadar
değiştirir (sağ ve aşağı artırır); diğer yönler odağı ondan uzaklaştırır.

**Submit ve cancel.** `ui.submit` basılı tutulduğu sürece odaktaki elemana basar ve odak hâlâ o elemandayken
bırakılınca onu etkinleştirir. `ui.cancel`, basıldığı karede bir `Cancelled` olayı bildirir; Entity'si odaktaki
elemanın canvas'ıdır, hiçbir şey odakta değilse modal canvas'tır. UI bunun dışında bir şey yapmaz: menüyü
kapatmak oyunun kararıdır.

`UiWorld::focused()` odaktaki elemanı verir, `setFocus()` onu ayarlar (örneğin yeni açtığınız menünün ilk düğmesine
odaklanmak için); kaldırılan, devre dışı bırakılan, gizlenen ya da artık odaklanamayan bir elemandaki odak bırakılır.

## Modal canvas'lar {#modal-canvases}

`UiCanvas::modal`, bir duraklatma menüsü ya da diyalog içindir. Modal bir canvas en az bir eleman gösterdiği sürece
(etkindir ve üzerinde bir eleman yerleştirilmiştir):

- İşaretçi yalnızca bu canvas'a ve üstündeki canvas'lara (daha yüksek `order`) vurabilir ve odak yalnızca onlara
  gelebilir; altındaki oyunun kendi HUD'ı etkisizdir.
- Session oyunun input action'larını maskeler: `InputActions::setExclusivePrefix("ui.")` çağırır; böylece yalnızca
  adı `ui.` ile başlayan action'lar input alır, diğerleri bırakılmış okunur. Menü açıkken modüller hareket,
  zıplama ya da ateş okumaz.

`UiWorld::modalActive()`, son update'in böyle bir eleman yerleştirip yerleştirmediğini söyler. Maske son UI
update'ini izler; yani canvas göründükten bir update sonra başlar ve gizlendikten ya da devre dışı bırakıldıktan
bir update sonra biter. Elemanı olmayan modal bir canvas hiçbir şey yapmaz.

## Olayları okumak {#reading-events}

`UiWorld`'ün `events()` listesi, son update'te olanları sırasıyla verir:

| `UiEventType` | Ne zaman | `value` |
| --- | --- | --- |
| `Clicked` | Bir `UiButton` etkinleştirildi | 0 |
| `ValueChanged` | Bir `UiToggle` ya da `UiSlider` değişti | Yeni değer (anahtar için 0 ya da 1) |
| `FocusChanged` | Bir eleman odağı aldı | Gezinme taşıdıysa 1, işaretçi taşıdıysa 0 |
| `Cancelled` | `ui.cancel` basıldı | 0 (`entity` bir canvas ya da null) |

Bunları host kodundan, session'ın update'inden sonra okuyun. Liste bir sonraki update'te değiştirilir; bu yüzden
her update'te bir kez okuyun:

```cpp
(*session)->update(frameSeconds);
for (const illusion::UiEvent& event : (*session)->ui().events())
{
    if (event.type == illusion::UiEventType::Clicked && event.entity == startButton)
    {
        startGame();
    }
    else if (event.type == illusion::UiEventType::ValueChanged && event.entity == volumeSlider)
    {
        setVolume(event.value);
    }
}
```

Bir update'te `UiWorldDesc::maxEvents`'i aşan olaylar atılır ve `UiWorldStatistics::droppedEvents` içinde sayılır.

Oyun modülleri aynı listeyi `UiService::pollEvents()` ile okur ([UI'ı script'lemek](#scripting-the-ui)).
Durumu kendi World'lerinden de okuyabilirler: `UiToggle::on` ve `UiSlider::value` değiştiklerinde yerinde
yazılır ve runtime Component'i `UiInteraction`, bir elemanın üzerine gelinip gelinmediğini, basılı ya da odakta
olup olmadığını söyler.

```cpp
const illusion::UiSlider& slider = world.get<illusion::UiSlider>(volumeSlider);
const bool musicOn = world.get<illusion::UiToggle>(musicToggle).on;
const bool held = world.get<illusion::UiInteraction>(startButton).pressed;
```

## UI'ı script'lemek {#scripting-the-ui}

Bir oyun modülü UI'ı `UiService` (`illusion/ui/UiService.hpp`, `USES ui`; ADR 0057) ile değiştirir ve okur.
Host onu, session'ın `UiWorld`'ü üzerinde `UiWorldService` ile uygular; modül onu diğer servisler gibi bulur ve
pointer'ı kontrol eder:

```cpp
illusion::UiService* ui = context.service<illusion::UiService>();
```

Her fonksiyon bir elemanın Entity'sini alır; ölü bir Entity için ya da fonksiyonun ihtiyaç duyduğu Component'e
sahip olmayan bir Entity için false (ya da hiçbir şey) döndürür. Hiçbir şey modülü geri çağırmaz: modül ister ve
olayları poll eder.

| Fonksiyon | Amaç |
| --- | --- |
| `find` | Bir kökün altında (ya da çizim sırasıyla herhangi bir canvas'ta) verilen `Name`'e sahip ilk Entity |
| `setText`, `setTextKey` | Bir `UiText`'in metni; düz ya da string table anahtarıyla |
| `setVisible`, `isVisible`, `setInteractable` | Bir elemanı ve torunlarını göstermek ya da gizlemek, gösterilip gösterilmediğini sormak, etkinleştirmek ya da devre dışı bırakmak |
| `setToggle`, `toggle`, `setSliderValue`, `sliderValue` | Bir `UiToggle` ile bir `UiSlider`'ın değerleri |
| `setFocus`, `focused` | Odaktaki eleman |
| `rect` | Son yerleşimin dikdörtgeni, canvas birimi cinsinden |
| `invalidateLayout` | UI'a Component'lerin doğrudan yazıldığını bildirir |
| `pollEvents` | UI'ın son update'indeki olaylar |
| `setLanguage`, `language` | Oyunun dili |

### Elemanları bulmak {#finding-elements}

`find(root, name)`, `root` ve torunları arasında, hiyerarşi sırasıyla, `Name` Component'i `name` olan ilk
Entity'yi verir; gizli olanlar da dahildir. `root` null ise her canvas'ı arar; en düşük `order`'a sahip canvas
önce gelir. Hiçbir şey eşleşmezse null bir Entity döndürür. Elemanları bir kez, `start()` içinde bulun ve
Entity'leri bir Component'te tutun ya da her update'te yeniden bulun: bir isim hiyerarşide bir gezinti
maliyetindedir, bir Entity ise hiçbir şeye mal olmaz.

### Metin ayarlamak {#setting-text}

`setText(entity, "Score 120")` düz metni gösterir ve elemanın `key`'ini temizler. `setTextKey()`, string table'ın
bir mesajını oyunun geçerli dilinde, adlandırılmış argümanlarla biçimlendirerek gösterir ve dil değiştiğinde onu
izlemeyi sürdürür. Argüman, dilin çoğul kurallarının ve sayı biçimlerinin uygulandığı
`UiTextArgument::number(name, value)` ya da `UiTextArgument::string(name, text)` biçimindedir. En fazla 8
argüman olabilir, her metin en fazla 255 bayt; bunun ötesinde `setTextKey()` false döndürür.

```json
"showcase.hud.frames": "{frames, plural, one {# frame} other {# frames}}"
```

```cpp
const illusion::Entity counter = ui->find({}, "Counter");
const illusion::UiTextArgument frames[] = {illusion::UiTextArgument::number("frames", 120.0)};
ui->setTextKey(counter, "showcase.hud.frames", frames); // "120 frames"; 1 shows "1 frame"
```

Türkçe dosya `tr.json` aynı anahtarı kendi mesajıyla taşır: `{frames, plural, one {# kare} other {# kare}}`
(mesaj söz dizimi için [Yerelleştirme](localization.md) sayfasına bakın). Argümanlar, elemanın runtime Component'i
`UiTextArguments` içinde tutulur.

### Görünürlük, etkileşim ve değerler {#visibility-interaction-and-values}

`setVisible(entity, false)` bir elemanı ve altındaki her şeyi gizler (`Hierarchy.hpp` içindeki `setEnabled()` gibi
`Inactive` tag'ini koyar); `isVisible()`, eleman ya da bir atası gizliyse false verir. `setInteractable()`,
`UiInteractable::enabled` değerini yazar. `setToggle()` ve `setSliderValue()` bir widget'ın değerini ayarlar
(kaydırıcı değeri input'taki gibi sınırlanır ve oturtulur) ve olay bildirmez: değeri ayarlayan modülün bunu
duyması gerekmez. `toggle()` ve `sliderValue()` değerleri geri okur.

### Odak ve yerleşim {#focus-and-layout}

`setFocus(entity)` odağı taşır; örneğin yeni açtığınız menünün ilk düğmesine. Eleman şu an odaklanamıyorsa false
döndürür; `setFocus({})` odağı temizler. `rect(entity)`, son yerleşimin elemana verdiği dikdörtgendir, canvas
birimi cinsinden.

`UiService` değişikliklerini host üzerinden yapar; bu yüzden onlar `world.markChanged()` olmadan yerleşime
ulaşır. UI Component'lerini `World`'ü üzerinden doğrudan yazan bir modül, yukarıda anlatıldığı gibi
`world.markChanged<T>()` çağırır ya da bir yazma grubundan sonra bir kez `invalidateLayout()` çağırır.

### Olayları poll etmek {#polling-events}

`pollEvents(events)`, UI'ın son update'indeki olayları verdiğiniz bir span'e kopyalar ve kaç tane olduğunu
döndürür. UI, session'ın adım sırasında modüllerden sonra çalışır; bu yüzden bunlar önceki karenin olaylarıdır.
Döndürülen toplam span'den büyük olabilir: en fazla `events.size()` tanesini okuyun ve span'i bir karede
beklediğiniz en çok olaya göre boyutlandırın. Olaylar [Olayları okumak](#reading-events) bölümündekilerdir ve
`ui.cancel` `Cancelled` olarak gelir.

### Dil {#language}

`setLanguage("tr")`, oyunu projenin `assets:/locales` içinde dosyası bulunan bir locale'e geçirir; dosya yoksa
false döndürür. Anahtarı olan her `UiText` onu izler. `language()` geçerli BCP 47 etiketidir ve bir sonraki
`setLanguage()` çağrısına kadar geçerlidir.

### Örnek: bir duraklatma menüsü {#example-a-pause-menu}

Sahnede `PauseMenu` adlı, başlangıçta gizli, modal bir canvas ve iki düğme, `Resume` ile `Quit`, vardır. Modül,
`ui.cancel` basılınca menüyü gösterir, Resume ile yeniden gizler ve Quit ile oyunu, host'undan
`SceneService::requestExit()` ile isteyerek bitirir: player o update'ten sonra kapanır ve editörün Play modu
durur.

```cmake
illusion_add_module(pause SOURCES game/Pause.cpp USES runtime ui)
```

```cpp
#include <illusion/core/Module.hpp>
#include <illusion/runtime/SceneService.hpp>
#include <illusion/ui/UiService.hpp>

#include <algorithm>
#include <array>
#include <span>

namespace
{

bool loadModule(illusion::ModuleContext&)
{
    return true;
}

void updateModule(illusion::ModuleContext& context, illusion::f64)
{
    illusion::UiService* ui = context.service<illusion::UiService>();
    illusion::SceneService* scenes = context.service<illusion::SceneService>();
    if (ui == nullptr || scenes == nullptr)
    {
        return; // this World was attached without them
    }
    const illusion::Entity menu = ui->find({}, "PauseMenu");
    const illusion::Entity resume = ui->find(menu, "Resume");
    const illusion::Entity quit = ui->find(menu, "Quit");

    std::array<illusion::UiEvent, 16> events{};
    const illusion::u32 count =
        std::min<illusion::u32>(ui->pollEvents(events), static_cast<illusion::u32>(events.size()));
    for (const illusion::UiEvent& event : std::span(events).first(count))
    {
        if (event.type == illusion::UiEventType::Cancelled && !ui->isVisible(menu))
        {
            ui->setVisible(menu, true);
            ui->setFocus(resume);
        }
        else if (event.type == illusion::UiEventType::Clicked && event.entity == resume)
        {
            ui->setVisible(menu, false);
        }
        else if (event.type == illusion::UiEventType::Clicked && event.entity == quit)
        {
            scenes->requestExit();
        }
    }
}

constexpr illusion::ModuleApi kPause =
    illusion::makeModuleApi("pause", "1.0.0", {.load = &loadModule, .update = &updateModule});

} // namespace

ILL_MODULE(pause, kPause)
```

Menü görünürken canvas'ı modaldır: oyunun diğer action'ları bırakılmış okunur; bu yüzden modülün hareket
tuşları için kendi kontrolüne ihtiyacı yoktur ([Modal canvas'lar](#modal-canvases)).

## Henüz olmayanlar {#what-is-not-there-yet}

Bu paket UI çizer, input'a tepki verir ve oyun modüllerinin onu script'lemesini sağlar. Sonraki paketler metin
alanlarını, kaydırmayı ve düğme glyph'lerini, Scene görünümünde canvas editörünü ve geçişleri ile animasyonu
ekler. İlk sürümün sınırları
([ADR 0057](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0057-game-ui.md)):

- Metin shaping olmadan soldan sağa çizilir: Arapça, İbranice, Hint yazı sistemleri ve ligatürler
  desteklenmez, sağdan sola metin de desteklenmez ve CJK, font atlası için fazla büyük karakter aralıkları
  gerektirir.
- Metinde dış çizgi, gölge ve gradyan henüz yoktur.
- Canvas'lar ekranı kaplar; dünyaya yerleştirilen bir UI (bir karakterin üzerindeki etiket) kapsanmaz.
