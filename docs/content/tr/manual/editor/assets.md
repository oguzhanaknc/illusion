# Asset Browser

Asset Browser, projenin `assets:` klasörünü gezilebilir bir grid veya liste olarak gösterir: thumbnail'ler,
arama, sıralama ve tür filtresi ile. Her dosya işlemi bir [Editör komutu](../../reference/editor-commands.md)
çalıştırır; böylece insanlar ve yapay zekâ ajanları yeniden adlandırma, taşıma, silme ve reimport işlemlerini
aynı API üzerinden yapar (ADR 0045).

## Gezinme

Sol tarafta projenin bütün **klasörleri** (derinliğe göre girintili) ve **favoriler** listelenir. Bir klasöre
tıklamak onu açar; favori bir klasör açılır, favori bir dosya ise kendi klasörünü açıp dosyayı seçer. Araç
çubuğundaki geri, ileri ve üst düğmeleri geçmişte hareket eder, breadcrumb'lar da doğrudan bir üst klasöre
atlar. Geçerli klasör, breadcrumb'ların bittiği klasördür.

**Refresh** mount'u yeniden tarar. Editör kataloğu normalde file watching ile güncel tutar, bu yüzden bu düğme
yalnızca bir watcher'ın göremediği klasörler (örneğin bir ağ sürücüsü) içindir.

## Grid ve liste

**Grid view** her asset için bir kare gösterir: varsa bir thumbnail, yoksa türün ikonu, altında da adı. **List
view** ad, tür ve boyut sütunlu bir tablo gösterir. Görünümü değiştirmek, ada, türe, boyuta veya değişiklik
zamanına göre sıralamak ve türe göre filtrelemek için araç çubuğunu kullanın. Arama alanı geçerli klasörü ada
göre filtreler.

Thumbnail'ler, `asset.thumbnail` komutunun `thumbnails:` mount'una yazdığı PNG dosyalarıdır; browser görünen
asset'lerin thumbnail'lerini her karede birkaç tane olacak şekilde ister. Bu yüzden thumbnail'i olmayan bir
asset önce ikonunu gösterir, resim üretilince belirir. Değişen bir asset otomatik olarak yeni bir thumbnail
alır (anahtarı içeriğini ve import ayarlarını içerir).

Bir asset'i seçmek için tıklayın (Ctrl-tıklama ile birkaçını seçin). Çift tıklamak onu açar: bir klasör açılır,
bir sahne editörde açılır, başka bir dosya da [External Tools](preferences.md) code editor'ında
açılır.

## Dosya işlemleri

Menüsü için bir asset'e sağ tıklayın veya araç çubuğunu kullanın:

| İşlem | Ne yapar |
| --- | --- |
| **Open** | Asset'i çift tıklama gibi açar. |
| **Reimport** | Bir texture veya model'i yeniden import eder (bkz. aşağıdaki import ayarları). |
| **Rename** (F2) | Yeniden adlandırır ve ona yapılan bütün referansları günceller. |
| **Move to folder…** | Taşır ve ona yapılan bütün referansları günceller. |
| **Delete** | Siler; undo dosyayı geri getirir. Bir şey ona referans veriyorsa **Delete anyway** siler ve referansları dangling bırakır. |
| **Reveal in the file manager** | Sistemin dosya yöneticisinde gösterir (`editor.reveal`). |
| **Copy path** | Sanal yolunu panoya kopyalar. |
| **New folder** | Geçerli klasörde bir klasör oluşturur. |

Rename ve move tek bir `asset.move`'dur; silme `asset.delete`'dir. Her biri tek bir undo adımıdır ve işlemin
değiştirdiği dosyalar raporlanır; böylece bir move'un neleri düzelttiğini görürsünüz.

## Import ayarları

Bir texture veya model seçerek nasıl import edildiğini düzenleyin. Asset'in yanındaki `<file>.import.json`
sidecar'ı varsayılandan farklı ayarları tutar; **Apply** onu yazar (bütün ayarlar varsayılana dönünce siler),
**Revert** disktekini yeniden yükler.

- **Textures** (`.png`, `.jpg`, `.jpeg`, `.tga`, `.bmp`, `.hdr`): color space (automatic, sRGB veya linear),
  mipmap üretilip üretilmeyeceği ve piksel cinsinden maksimum boyut.
- **Models** (`.gltf`, `.glb`): pozisyonlara ve node çevirilerine uygulanan uniform scale.

Import ayarları paneli, seçili asset'e kaç dosyanın referans verdiğini de söyler.

## Komut paletinden asset bulma

Komut paletinde `#` yazın (veya paleti `"mode": "assets"` ile açın) ve her asset'i yoluna göre arayın. Birini
seçmek Asset Browser'ı onun klasöründe, o seçili olarak açar.
