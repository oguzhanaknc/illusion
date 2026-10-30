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

Blender keymap'i bunların yerine **W**, **G**, **R** ve **S** kullanır (bkz. [Tercihler](preferences.md)).

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

## Komutlar

| Komut | Ne yapar |
| --- | --- |
| `gizmo.get` | Araç, eksenler, pivot ve yakalama. |
| `gizmo.set_tool` | `select`, `translate`, `rotate`, `scale` ya da `universal`. |
| `gizmo.set_space` | `world`, `local` ya da `view`. |
| `gizmo.set_pivot` | `median`, `active` ya da `individual`. |
| `gizmo.set_snap` | Yakalamanın açık ya da kapalı olması ve `translate`, `rotate`, `scale` adımları. |
| `transform.snap_to_grid` | Entity'lerin dünya konumlarını bir grid'e yuvarlar (tek geri alma adımı). |

Gizmo ayarları editör oturumuna aittir: sahneye kaydedilmez ve geri alma adımı değildir. Entity'leri taşımak
isteyen bir ajan gizmo'yu sürüklemez; aynı pivotlarla `transform.translate`, `transform.rotate` ya da
`transform.scale` çağırır:

```json
{"method": "gizmo.set_snap", "params": {"enabled": true, "translate": 0.25}}
{"method": "transform.snap_to_grid", "params": {"entities": ["$selection"]}}
{"method": "transform.rotate", "params": {"entities": ["$selection"], "euler": [0, 90, 0], "pivot": "center"}}
```
