# Scene Viewport'u

Scene paneli açık sahneyi editör kamerasından gösterir; bu kamera editöre aittir, sahnenin parçası değildir.
Panel sahneyi motorun renderer'ı ile çizer, bir zemin grid'i ve seçimin çevresine bir outline ekler ve
Entity'leri tıklamayla seçer. Panelin yaptığı her şey aynı zamanda bir [Editor command](../../reference/editor-commands.md)
olduğu için bir yapay zekâ ajanı da kamerayı aynı şekilde hareket ettirebilir, gölgelendirmeyi değiştirebilir
ve Entity seçebilir.

`--no-gpu` ile başlatılan penceresiz bir editörün viewport'u yoktur: panel boş bir durum gösterir ve
`viewport.*` komutları 1400 (`unsupported`) koduyla başarısız olur.

## Kamerayı hareket ettirmek

Editör kamerası bir pivot noktasının çevresinde döner. Varsayılan `illusion` gezinmesinde fare denetimleri
şunlardır:

| Girdi | Eylem |
| --- | --- |
| **Alt** + sol sürükleme | Pivotun çevresinde orbit. |
| Orta sürükleme | Pan: imlecin altındaki nokta imleci izler. |
| Tekerlek | Pivota yaklaşan ya da uzaklaşan dolly. |
| **Alt** + sağ sürükleme | Dolly. |
| Sağ sürükleme | Etrafa bakmak; basılı tutulurken **W A S D** hareket ettirir, **Q E** aşağı ve yukarı götürür (**Shift** daha hızlıdır, tekerlek hızı değiştirir). |
| **F** | Seçimi çerçeveler (hiçbir şey seçili değilse her şeyi). |
| **Shift+F** | Her şeyi çerçeveler. |

Çerçeveleme ve standart görünümler kamerayı kısa bir animasyonla taşır; `reduceMotion` tercihi kamerayı
hemen taşır. Kamera hareket ederken uçuş tuşları eylem çalıştırmasın diye klavye kısayolları duraklatılır.

### Gezinme preset'leri

`viewportNavigation` tercihi (Preferences, Keyboard sayfası) başka araçların fare denetimlerini seçer.
Varsayılan `keymap`, keymap preset'ini izler. Tekerlek her preset'te dolly yapar, **F** çerçeveler ve **Alt** +
sol sürükleme orbit yapar.

| Preset | Sol sürükleme | Orta sürükleme | Sağ sürükleme |
| --- | --- | --- | --- |
| `illusion`, `unity` | Seçim (kutu) | Pan | Uçuş (**Alt**: dolly) |
| `unreal` | Yürüme: döner ve zemin boyunca ileri ya da geri gider | Pan | Uçuş (**Alt**: dolly) |
| `blender` | Seçim (kutu) | Orbit (**Shift**: pan, **Ctrl**: dolly) | Hiçbir şey |

### Görünüm küpü ve eksen gezgini

Bir Scene görünümünün sağ üst köşesinde kamerayla birlikte dönen bir küp ve onun altında üç eksen görünür.
Küpün bir yüzüne tıklamak kamerayı o ortografik görünüme (Top, Front, Right...) taşır; bir kenara ya da köşeye
tıklamak perspektif kamerayı onun boyunca bakacak şekilde çevirir. Küpü sürüklemek pivotun çevresinde orbit
yapar. Bir eksenin ucuna tıklamak o eksen boyunca bakar: dolu uçlar pozitif eksenler, içi boş olanlar negatif
eksenlerdir. Eksenler temanın eksen renklerini kullanır.

## Game görünümü ve ek viewport'lar

**Game** paneli sahneyi ana kamerasından (`Camera` içeren ilk etkin Entity) oyuncunun göreceği gibi gösterir:
grid, outline ve yalnızca editöre ait Entity'ler olmadan. Sahnede kamera yoksa panel bunu söyler. Editör
kamerası komutları ve görünüm menüsü yalnızca Scene görünümlerinde çalışır.

Her viewport'un **Aspect** menüsü görüntünün biçimini belirler: **Free** paneli doldurur, ön ayarlar (16:9,
16:10, 4:3, 21:9, 1:1) o biçimdeki en büyük görüntüyü panele sığdırır, sabit çözünürlükler (640 × 360,
1280 × 720, 1920 × 1080) ise panel boyutu ne olursa olsun tam o kadar piksel çizer. Görüntü ortalanır ve
çevresine bantlar eklenir.

Scene araç çubuğundaki **New viewport**, her birinin kendi editör kamerası olan en fazla üç Scene görünümü
daha açar (Scene 2, 3, 4); bunlar ana görünümün kamerası ve ayarlarıyla başlar. Pencereyi kapatmak viewport'u
kapatır.

## Kamera yer imleri

**Ctrl+Shift+1** ile **Ctrl+Shift+9** bir Scene görünümünün editör kamerasını bir yer imine kaydeder;
**Ctrl+1** ile **Ctrl+9** kamerayı oraya geri götürür. Yer imleri Scene görünümleri arasında ortaktır ve editör
kapanana kadar saklanır.

## Seçmek

Bir tıklama imlecin altındaki Entity'yi seçer; **Shift** + tıklama onu seçime ekler, **Ctrl** + tıklama ekler ya
da çıkarır. Arka plana tıklamak seçimi temizler. Bir Entity'nin kenarına 2 piksel uzaklıktaki tıklamalar da onu
bulur. Aynı noktaya yeniden tıklamak imlecin altındaki bir sonraki Entity'yi önden arkaya doğru seçer; böylece
başka Entity'lerin arkasında kalanlar da seçilebilir. Outliner'da gizlenen ya da kilitlenen Entity'ler asla
seçilmez.

Bir gizmo tutamacında başlamayan sol sürükleme bir seçim kutusu çizer ve kutunun içinde pikseli olan her
Entity'yi seçer; **Shift** onları ekler, **Ctrl** seçimlerini tersine çevirir. Seçimi taşıyan, döndüren ve
ölçekleyen gizmo'lar [Dönüşüm Araçları](transform-tools.md) sayfasında anlatılır.

İmlecin altındaki Entity, tıklanmadan önce temanın hover renginde ince bir outline alır. Mesh'i olmayan
Entity'ler bulundukları yerde bir simge (billboard) gösterir: kameralar, yönlü, nokta ve gökyüzü ışıkları, ses
kaynakları ve ses dinleyicileri. Bir simgeye tıklamak Entity'sini seçer; seçili simgeler seçim renginde çizilir.

### Bir alan için Entity seçmek

Inspector'daki bir `Entity` alanının yanındaki damlalık düğmesi seçmeyi başlatır: bir Scene görünümünde bir
Entity'ye ya da bir simgeye yapılan sonraki tıklama o Entity'yi alana yazar (tek bir undo adımı olarak) ve Scene
görünümü beklerken bir ipucu gösterir. Arka plana tıklamak beklemeyi sürdürür; **Esc** iptal eder.

### Ajanı izlemek

**Follow agent** açıkken (AI Activity paneli ya da `followAgent` tercihi), bir yapay zekâ ajanı seçimi her
değiştirdiğinde ana Scene görünümü seçimi çerçeveler; böylece editörün başındaki kişi ajanın ne üzerinde
çalıştığını görür.

## Görselleştiriciler

Görselleştiriciler kendi görüntüsü olmayan Component'lerin biçimini sahnenin üzerine çizgiler olarak çizer:

| Görselleştirici | Çizdiği |
| --- | --- |
| Lights | Nokta ışıklarının menzil küresi; yönlü ışıkların yönü. |
| Cameras | Her `Camera`'nın görüş frustum'u, Game görünümünün en-boy oranında. |
| Colliders | Her `Collider`'ın kutusu, küresi, kapsülü, silindiri ya da sınırları; sensörler başka bir renkte. |
| Characters | Her `CharacterController`'ın kapsülü. |
| Joints | Her `Joint`'in bağlantı noktası ve ekseni, sınırları ve bağlı gövdeye giden çizgiyle. |
| Audio | Uzamsal `AudioSource`'ların en küçük ve en büyük uzaklık küreleri ve konileri. |

Varsayılan olarak yalnızca seçili Entity'leri çizerler; Visualisers menüsündeki **All** her Entity'yi çizer.
Gizli ve etkin olmayan Entity'ler asla çizilmez.

Bir `Camera` seçiliyken Scene görünümünün sağ alt köşesi o kameranın gördüğünün küçük bir resmini gösterir
(**Camera preview**). **Stats**, sol üst köşede kare süresini, draw call'ları, üçgenleri, görünen nesneleri,
gölge draw call'larını ve görüntü boyutunu gösterir.

## Araç çubuğu

| Menü | İçerik |
| --- | --- |
| **Shading** | Lit, Unlit, Albedo, Normals, Roughness, Metallic, Ambient occlusion, Shadow cascades, Motion vectors ve wireframe katmanı. |
| **Overlays** | Grid, seçim outline'ı, hover, simgeler, görünüm küpü, kamera önizlemesi, istatistikler ve **Visualisers** menüsü (her tür için bir öğe ve Selected ya da All). Game görünümünde yalnızca outline, hover ve istatistikler vardır. |
| **View** | Perspektif ve pivotun çevresinde ortografik Top, Bottom, Front, Back, Left ve Right görünümleri. |
| Frame | Seçimi çerçeveler. |
| **Aspect** | Free, en-boy oranı ön ayarları ve sabit çözünürlükler. |
| New viewport | Başka bir Scene görünümü açar (yalnızca ana Scene panelinde). |

Grid zemin düzleminde durur (ortografik yan görünümlerde kameraya bakan düzlemde). Aralığı uzaklığa göre onar
katlık adımlarla uyum sağlar; x ve z eksenleri temanın eksen renkleriyle çizilir (renk körlüğü ön ayarı
Okabe-Ito renklerini kullanır).

Panel yeniden boyutlandırılırken görüntü, boyut 150 ms boyunca değişmeyene ya da fare düğmesi bırakılana kadar
esnetilir; sonra viewport yeni boyutta çizer.

## Komutlar

| Komut | Yaptığı iş |
| --- | --- |
| `viewport.list` | Viewport'ları boyutları, kameraları ve ayarlarıyla listeler. |
| `viewport.camera_get` / `viewport.camera_set` | Editör kamerasını okur ya da taşır: pivot, uzaklık, yaw, pitch, görüş alanı, projeksiyon veya pivota bakan bir göz konumu. |
| `viewport.view` | Standart bir görünüm: `perspective`, `top`, `bottom`, `front`, `back`, `left`, `right`. |
| `viewport.frame` | Entity'leri, seçimi ya da her şeyi çerçeveler. |
| `viewport.set_mode` / `viewport.set_overlays` | Gölgelendirme ve wireframe; katmanlar ve görselleştiriciler. |
| `viewport.project` | Bir Entity'nin ya da bir noktanın göründüğü piksel. |
| `viewport.pick` | Bir pikseldeki Entity (asenkron); isteğe bağlı olarak seçimi değiştirir. |
| `viewport.pick_rect` | Bir dikdörtgenin içinde pikseli olan her Entity (asenkron); isteğe bağlı olarak seçimi değiştirir. |
| `viewport.screenshot` | Viewport görüntüsünün PNG'si (asenkron), MCP görüntüsü olarak. |
| `viewport.sample` | Birkaç pikselin ortalama rengi: RGBA ve ton, doygunluk, parlaklık (asenkron). |
| `viewport.create` / `viewport.close` | Ek bir Scene görünümü açar ya da kapatır (`scene-2` ile `scene-4`). |
| `viewport.set_aspect` | Free, bir en-boy oranı ön ayarı ya da sabit bir `width` ve `height`. |
| `camera.bookmark_set` / `camera.bookmark_go` | Editör kamerasını 1-9 numaralı yuvaya kaydeder ya da oraya geri götürür. |
| `render.stats` | Bir viewport'un son karesindeki kare sayısı, görünen nesneler, draw call'lar ve üçgenler. |
| `render.gpu_info` / `render.diagnostics` | GPU ve sürücü; doğrulamanın açık olup olmadığı ve kaç hata bildirdiği. |

Bir ajan ekrandaki bir Entity'yi `viewport.project` ile bulur ve pikseli `viewport.pick`'e verir:

```json
{"method": "viewport.project", "params": {"entity": "/Game/Board"}}
{"method": "viewport.pick", "params": {"x": 640, "y": 360, "select": "replace"}}
```

Her komut bir `viewport` kimliği alır (varsayılan `scene`; `game` Game panelidir). Bir ajan oyuncunun
gördüğünü, Game görünümünün çözünürlüğünü sabitleyip bir ekran görüntüsü alarak ve bir renk örnekleyerek
denetler:

```json
{"method": "viewport.set_aspect", "params": {"viewport": "game", "aspect": "fixed", "width": 640, "height": 360}}
{"method": "viewport.screenshot", "params": {"viewport": "game"}}
{"method": "viewport.sample", "params": {"viewport": "game", "x": 320, "y": 180, "radius": 2}}
```

Bir ajan her Entity için collider görselleştiricilerini ve istatistikleri şöyle açar:

```json
{"method": "viewport.set_overlays", "params": {"visualisers": {"colliders": true, "audio": false}, "visualiserScope": "all", "stats": true}}
```

Seçimler, ekran görüntüleri ve örnekler, paneli gizliyken viewport'u kendileri çizer ve viewport'un Asset'leri
yüklenene kadar bekler; böylece bitmiş görüntüyü görürler.

Pikseller viewport görüntüsünün sol üst köşesinden sayılır, y aşağı doğrudur; boyutunu `viewport.list` verir.
Kamera, gölgelendirme ve katmanlar editör durumudur: sahneye kaydedilmez ve undo geçmişine girmez.
