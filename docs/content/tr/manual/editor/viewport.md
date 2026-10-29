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

## Seçmek

Bir tıklama imlecin altındaki Entity'yi seçer; **Shift** + tıklama onu seçime ekler, **Ctrl** + tıklama ekler ya
da çıkarır. Arka plana tıklamak seçimi temizler. Bir Entity'nin kenarına 2 piksel uzaklıktaki tıklamalar da onu
bulur. Aynı noktaya yeniden tıklamak imlecin altındaki bir sonraki Entity'yi önden arkaya doğru seçer; böylece
başka Entity'lerin arkasında kalanlar da seçilebilir. Outliner'da gizlenen ya da kilitlenen Entity'ler asla
seçilmez.

## Araç çubuğu

| Menü | İçerik |
| --- | --- |
| **Shading** | Lit, Unlit, Albedo, Normals, Roughness, Metallic, Ambient occlusion, Shadow cascades, Motion vectors ve wireframe katmanı. |
| **Overlays** | Grid ve seçim outline'ı. |
| **View** | Perspektif ve pivotun çevresinde ortografik Top, Bottom, Front, Back, Left ve Right görünümleri. |
| Frame | Seçimi çerçeveler. |

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

Bir ajan ekrandaki bir Entity'yi `viewport.project` ile bulur ve pikseli `viewport.pick`'e verir:

```json
{"method": "viewport.project", "params": {"entity": "/Game/Board"}}
{"method": "viewport.pick", "params": {"x": 640, "y": 360, "select": "replace"}}
```

Pikseller viewport görüntüsünün sol üst köşesinden sayılır, y aşağı doğrudur; boyutunu `viewport.list` verir.
Kamera, gölgelendirme ve katmanlar editör durumudur: sahneye kaydedilmez ve undo geçmişine girmez.
