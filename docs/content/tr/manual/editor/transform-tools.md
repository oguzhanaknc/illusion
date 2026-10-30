# Dönüşüm Araçları

Scene görünümü seçili Entity'lerin üzerine bir gizmo çizer. Tutamaçlarından birini sürüklemek seçimi taşır,
döndürür ya da ölçekler; sürüklemenin tamamı tek bir geri alma adımıdır. Her parçası aynı zamanda bir
[Editor command](../../reference/editor-commands.md) olduğu için bir yapay zekâ ajanı da aynı aracı, eksenleri,
pivotu ve yakalamayı seçer ve Entity'leri `transform.*` komutlarıyla hareket ettirir.

## Araçlar

Araç düğmeleri araç çubuğunun solundadır; tuşlar klavye bir metin alanında değilken çalışır.

| Tuş | Araç | Tutamaçlar |
| --- | --- | --- |
| **Q** | Seç | Gizmo yok: sol sürükleme bir seçim kutusu çizer. |
| **W** | Taşı | Oklar bir eksen boyunca, kareler bir düzlemde, ortadaki kare görünüm düzleminde taşır. |
| **E** | Döndür | Halkalar bir eksen çevresinde, dış halka görünüm yönü çevresinde döndürür; halkaların içi serbestçe döndürür (trackball). |
| **R** | Ölçekle | Kutular bir eksen boyunca, ortadaki kutu her yöne eşit ölçekler. |
| **T** | Evrensel | Taşıma okları ve kareleri, döndürme halkaları ve eşit ölçekleme kutusu bir arada. |

Blender keymap'i bunların yerine **W** ile seçer, **G**, **R** ve **S**'yi ise modal dönüşümler için kullanır
(bkz. [Tercihler](preferences.md)).

İmlecin altındaki tutamaç temanın hover rengiyle çizilir ve seçim ne kadar uzakta olursa olsun gizmo ekranda
aynı boyutta kalır. Halkaların arka yarıları soluk çizilir ve tutulamaz. Kameraya doğru bakan bir eksen ve
kenarından görülen bir düzlem silikleşir, çünkü onlar boyunca sürüklemek kararsız olurdu. Eksen renkleri
`colorBlindAxes` tercihini izler (Okabe-Ito renkleri).

**Alt** basılıyken sol sürükleme kameraya gider; böylece gizmo orbit yapmanın önüne hiç geçmez. Tutamaçların
dışında başlayan sol sürükleme, Seç aracındaki gibi bir seçim kutusu çizer.

## Sürüklemek

Sürükleme bir transaction içinde çalışır: düğme basılı tutulduğu sürece seçim imleci izler, düğmeyi bırakmak
tek bir geri alma adımı oluşturur (History panelinde `gizmo` kaynağından Taşı, Döndür ya da Ölçekle etiketiyle).
**Esc** ya da sağ fare düğmesi sürüklemeyi iptal eder ve her Entity'yi tam olarak eski yerine koyar.

Seçimde bir ebeveyn ve onun çocukları varsa yalnızca ebeveyn taşınır, çocukları onu izler. Etkin Entity en son
seçilendir.

## Yazılan sayılar

Bir sürükleme ya da modal dönüşüm sürerken bir sayı yazmak imlecin yerini alır: seçim tam olarak o kadar taşınır,
döner ya da ölçeklenir ve sayı imlecin yanında gösterilir. Rakamlar ve bir nokta bir sayı oluşturur, **-** işaretini
çevirir, **Backspace** son karakteri siler ve **Tab** sonraki sayıya geçer.

| Tutamaç | Sayılar |
| --- | --- |
| Taşıma oku | Eksen boyunca mesafe. |
| Taşıma karesi | Düzlemin eksenleri boyunca x, y, z sırasıyla iki mesafe. |
| Ortadaki kare (görünüm düzlemi) | Gizmo'nun x, y ve z eksenleri boyunca üç mesafe. |
| Halka, görünüm halkası, trackball | Derece cinsinden açı (trackball görünüm yönü çevresinde döner). |
| Ölçek kutusu, ortadaki kutu | Ölçek çarpanı (en az 0.01). |

**Enter** sürüklemeyi düğme bırakılmadan onaylar; bütün rakamları silmek imlece geri döner.

## Modal dönüşümler

Modal dönüşüm, Blender'daki **G**, **R** ve **S** gibi, hiçbir düğme basılı tutmadan seçimi imleçle taşır,
döndürür ya da ölçekler. İmlecin olduğu yerde, imlecin altındaki Scene görünümünde başlar:

| Tuş (illusion keymap'i) | Blender keymap'i | İmleci izleyen |
| --- | --- | --- |
| **Shift+W** | **G** | Konum, görünüm düzleminde. |
| **Shift+E** | **R** | Dönüş, imleç pivotun çevresinde dolaştıkça görünüm yönü çevresinde. |
| **Shift+R** | **S** | Ölçek, imlecin pivota uzaklığıyla. |

Sürerken **X**, **Y** ya da **Z** onu dünya eksenine kısıtlar; aynı tuşa yeniden basmak etkin Entity'nin kendi
eksenlerini kullanır, üçüncü basış kısıtı kaldırır. Tuşla birlikte **Shift** onu diğer iki eksenin düzlemine
kısıtlar (dönüş o zaman tuşun ekseni çevresinde olur). Kısıtlı eksenler görünüm boyunca çizilir ve tuşlar imlecin
yanında listelenir. Yakalama ve yazılan sayılar sürüklemedeki gibi çalışır. **Enter** ya da sol tıklama onaylar
(tek geri alma adımı); **Esc** ya da sağ tıklama her şeyi eski yerine koyar.

## Eksenler

Araçların yanındaki eksen düğmesi Taşı ve Döndür gizmo'larının yönünü seçer:

| Eksenler | Yön |
| --- | --- |
| Dünya | Sahnenin x, y ve z eksenleri. |
| Yerel | Etkin Entity'nin kendi eksenleri. |
| Görünüm | Kameranın eksenleri: x sağa, y yukarı, z izleyiciye doğru. |

Ölçekle gizmo'su her zaman etkin Entity'nin kendi eksenlerini kullanır, çünkü başka eksenler boyunca ölçekleme
Entity'yi çarpıtırdı.

## Pivot

Pivot düğmesi gizmo'nun durduğu ve döndürme ile ölçeklemenin çevresinde yapıldığı noktayı seçer:

| Pivot | Nokta |
| --- | --- |
| Orta nokta | Seçili Entity'lerin ortalama konumu. |
| Etkin Entity | Etkin Entity'nin konumu. |
| Ayrı merkezler | Her Entity kendi konumu çevresinde döner ve ölçeklenir; gizmo orta noktada durur. |
| Sınır kutusu merkezi | Seçili Entity'lerin konumlarını çevreleyen kutunun merkezi. |

## Yakalama

Grid düğmesi yakalamayı açar; yanındaki ok adımları ayarlar. Sürükleme sırasında **Ctrl**'yi basılı tutmak
ayarı tersine çevirir: yakalama kapalıyken sürükleme yakalar, açıkken serbestçe hareket eder.

| Adım | Varsayılan | Aralık | Ne yakalanır |
| --- | --- | --- | --- |
| Taşıma adımı | 0.5 | 0.001 ile 1.000 dünya birimi | Gizmo'nun her ekseni boyunca taşınan mesafe. |
| Döndürme adımı | 15° | 0.01° ile 180° | Bir halkanın, görünüm halkasının ya da trackball'un açısı. |
| Ölçekleme adımı | 0.1 | 0.001 ile 10 | Ölçek çarpanı: 1, 1.1, 1.2 ... |

Taşıma yakalaması sürüklemenin başladığı yerden sayar. Entity'leri grid'in kendisine oturtmak için dünya
konumlarını adımın en yakın katına yuvarlayan `transform.snap_to_grid` kullanılır.

## Yerleştirme

Entity menüsü ve aşağıdaki komutlar Entity'leri çizdiklerinin çevresindeki kutulara göre yerleştirir (kendi
Mesh'leri ve çocuklarının Mesh'leri); hiçbir şey çizmeyen bir Entity konumu olarak sayılır. GPU'suz bir editörde
her Entity konumu olarak sayılır.

| Eylem | Tuş | Ne yapar |
| --- | --- | --- |
| Zemine bırak | **End** | Seçili her Entity, kutusunun altı aşağıdaki başka bir Entity'nin en yüksek kutusuna, yoksa y = 0 zemin düzlemine oturana kadar düşer (ya da yükselir). Gizli ve etkin olmayan Entity'ler yüzey sayılmaz. Fizik collider'ları kullanılmaz. |
| Görünüme hizala | **Ctrl+Shift+F** | Seçimi Scene kamerasının dönüşüne çevirir, böylece görünümün baktığı yere bakar (-z'si boyunca); `move` onu kameranın gözüne de koyar, örneğin bir Camera'yı mevcut görünümden yöneltmek için. |
| Hizala | | Entity'leri tek bir eksen boyunca sıraya dizer: en küçük, orta ya da en büyük kenarları hepsini çevreleyen kutunun ya da yerinde kalan son listelenen (etkin) Entity'nin aynı kenarına gelir. |
| Dağıt | | Üç ya da daha fazla Entity'yi tek bir eksen boyunca, yerinde kalan ilk ve son Entity arasında eşit aralıklarla yerleştirir: merkezleri arasında eşit mesafe ya da kutuları arasında eşit boşluk. |

Her biri tek bir geri alma adımıdır.

## Komutlar

| Komut | Ne yapar |
| --- | --- |
| `gizmo.get` | Araç, eksenler, pivot ve yakalama. |
| `gizmo.set_tool` | `select`, `translate`, `rotate`, `scale` ya da `universal`. |
| `gizmo.set_space` | `world`, `local` ya da `view`. |
| `gizmo.set_pivot` | `median`, `active`, `individual` ya da `bounds_center`. |
| `gizmo.set_snap` | Yakalamanın açık ya da kapalı olması ve `translate`, `rotate`, `scale` adımları. |
| `gizmo.modal` | Editördeki kişi için modal bir `translate`, `rotate` ya da `scale` başlatır. |
| `transform.snap_to_grid` | Entity'lerin dünya konumlarını bir grid'e yuvarlar (tek geri alma adımı). |
| `transform.drop_to_ground` | Entity'leri altlarındaki kutulara (`onto`: `surfaces`) ya da zemin düzlemine (`plane`) bırakır. |
| `transform.align_to_view` | Entity'leri bir viewport'un kamerasına çevirir; `move` onları gözüne de yerleştirir. |
| `transform.align` | Entity'leri `axis` boyunca `min`, `center` ya da `max` kenarlarına göre, `selection`'a ya da `active` Entity'ye (`to`) hizalar. |
| `transform.distribute` | Entity'leri `axis` boyunca `centers` ya da `gaps` ile aralıklandırır. |

Gizmo ayarları editör oturumuna aittir: sahneye kaydedilmez ve geri alma adımı değildir. Entity'leri taşımak
isteyen bir ajan gizmo'yu sürüklemez; aynı pivotlarla `transform.translate`, `transform.rotate` ya da
`transform.scale` çağırır:

```json
{"method": "gizmo.set_snap", "params": {"enabled": true, "translate": 0.25}}
{"method": "transform.snap_to_grid", "params": {"entities": ["$selection"]}}
{"method": "transform.rotate", "params": {"entities": ["$selection"], "euler": [0, 90, 0], "pivot": "center"}}
{"method": "transform.drop_to_ground", "params": {"entities": ["$selection"]}}
{"method": "transform.align", "params": {"entities": ["/Crate 1", "/Crate 2"], "axis": "x", "edge": "min"}}
{"method": "transform.distribute", "params": {"entities": ["/Post 1", "/Post 2", "/Post 3"], "axis": "z", "mode": "gaps"}}
```
