# Scene Viewport'u

Scene paneli açık sahneyi editör kamerasından gösterir; bu kamera editöre aittir, sahnenin parçası değildir.
Panel sahneyi motorun renderer'ı ile çizer, bir zemin grid'i ve seçimin çevresine bir outline ekler ve
Entity'leri tıklamayla seçer. Panelin yaptığı her şey aynı zamanda bir [Editor command](../../reference/editor-commands.md)
olduğu için bir yapay zekâ ajanı da kamerayı aynı şekilde hareket ettirebilir, gölgelendirmeyi değiştirebilir
ve Entity seçebilir.

`--no-gpu` ile başlatılan penceresiz bir editörün viewport'u yoktur: panel boş bir durum gösterir ve
`viewport.*` komutları 1400 (`unsupported`) koduyla başarısız olur.

## Kamerayı hareket ettirmek

Editör kamerası bir pivot noktasının çevresinde döner. Fare denetimleri şunlardır:

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

Sol sürükleme bir seçim kutusu çizer ve kutunun içinde pikseli olan her Entity'yi seçer; **Shift** onları
ekler, **Ctrl** seçimlerini tersine çevirir.

## Araç çubuğu

| Menü | İçerik |
| --- | --- |
| **Shading** | Lit, Unlit, Albedo, Normals, Roughness, Metallic, Ambient occlusion, Shadow cascades, Motion vectors ve wireframe katmanı. |
| **Overlays** | Grid ve seçim outline'ı. |
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
| `viewport.set_mode` / `viewport.set_overlays` | Gölgelendirme ve wireframe; grid ve seçim outline'ı. |
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

Seçimler, ekran görüntüleri ve örnekler, paneli gizliyken viewport'u kendileri çizer ve viewport'un Asset'leri
yüklenene kadar bekler; böylece bitmiş görüntüyü görürler.

Pikseller viewport görüntüsünün sol üst köşesinden sayılır, y aşağı doğrudur; boyutunu `viewport.list` verir.
Kamera, gölgelendirme ve katmanlar editör durumudur: sahneye kaydedilmez ve undo geçmişine girmez.
