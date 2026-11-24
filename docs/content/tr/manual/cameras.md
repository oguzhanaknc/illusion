# Kameralar

Bir oyun World'ünü bir `Camera` Component'i üzerinden gösterir. O Camera'yı oyun kodunda hareket ettirmek
yerine sahneye **sanal kameralar** koyun; her biri veriyle tarif edilen bir çekimdir: neyi izlediği, nasıl
hareket ettiği, nereye baktığı. Gerçek Camera üzerindeki bir **kamera beyni** en iyi sanal kamerayı
gösterir ve başka biri devraldığında geçiş yapar ([ADR 0064](../architecture/decisions.md)). `CameraWorld`
(`illusion/camera/CameraWorld.hpp`) bunları bir oyun session'ının [Kameralar adımında](runtime.md#adm-sras)
çalıştırır.

## Sanal kameralar

`VirtualCamera` Component'i bir Entity'yi çekim yapar. Beyin, Entity'si `Inactive` olmayan etkin kameralar
arasından `priority` değeri en yüksek olanı gösterir; öncelikler eşitse en son etkinleşen kazanır.

| Alan | Anlamı |
| --- | --- |
| `priority`, `enabled` | Hangi kameranın canlı olduğu |
| `follow` | Gövde aşamasının hedefi; boşsa kameranın kendi Entity'si |
| `lookAt` | Nişan aşamasının hedefi; boşsa `follow` hedefi |
| `fovY`, `nearPlane`, `dutch` | Beynin Camera'ya verdiği lens; `dutch` görüntüyü yatırır |
| `customBlend`, `blendIn` | Beynin çift için bir geçişi yoksa bu kamera canlı olduğunda kullanılan geçiş |

Aynı Entity'deki diğer kamera Component'leri onun aşamalarıdır: en fazla bir **gövde** (kameranın nerede
olduğu), en fazla bir **nişan** (nereye baktığı) ve isteğe bağlı **gürültü**. Gövdesi olmayan kamera
Entity'sinin `Transform`'unda durur; nişanı olmayan kamera gövdenin dönüşünü korur. İkinci bir gövde ya da
nişan log'a bir kez yazılır ve yok sayılır. Bir eklemi izlemek için ona `BoneAttachment` ile bağlı bir
Entity'yi izleyin ([socket'ler](animation.md#socketler)).

Bir update'te yalnızca canlı kamera ile bir geçişin başladığı kamera değerlendirilir. Önceki update'te
değerlendirilmemiş bir kamera sönümlemesine baştan başlar; böylece canlı olduğunda hedefine atlar.

### Takip

`CameraFollow` kamerayı hedeften `offset` uzaklıkta tutar; hedefin uzayında (hedefle döner ve onun
dönüşünü alır) ya da dünya uzayında, her eksen için bir yumuşatma süresiyle (`damping`).

### Yörünge

`CameraOrbit` kamerayı hedefin `pivotOffset` üstündeki bir pivotun çevresinde, sınırlar içinde yaw ve pitch
ile bir uzaklıkta döndürür. Vector action `lookAction` onu döndürür (tam sapmada saniyede `lookSpeed`
radyan), axis action `zoomAction` uzaklığı değiştirir.

### Yaylı kol

`CameraSpringArm` kamerayı hedefin omzundan (`shoulderOffset`, `side`, `armLength`) uzanan bir kolun ucuna
koyar. Her update'te kol boyunca `collisionLayers` fizik katmanlarına karşı `collisionRadius` yarıçaplı bir
küre atılır; küre hedefin kendi gövdelerinden geçer ve kol duvarların dışında kalmak için kısalır
(`pullInSpeed`, `recoverySpeed`). `alignToTarget` açıkken yaw'ı hedefin baktığı yöne görelidir;
`lookAction` onu döndürür.

### Dolly

`CameraDolly` kamerayı `path` boyunca, kameranın Entity'sinin uzayındaki teğetli Hermite noktalarında,
`position` konumuna (nokta indisi artı kesir) ya da `NearestToTarget` ile hedefe en yakın noktaya taşır.

### Bakış

`CameraLookAt` kamerayı bakış hedefine artı `targetOffset` konumuna sönümlenerek döndürür.

### Kompozisyon

`CameraComposer` hedefi `screenPosition` konumunda tutar (her eksende -1 ile 1 arası): ölü bölgenin içinde
kamera kıpırdamaz, yumuşak bölgenin içinde sönümlenerek geri döner ve hedef yumuşak bölgeden hiç çıkmaz.

## Rig'ler

Rig, bir stili oluşturan aşamaları olan bir sanal kameradır. Çoğu oyunun ihtiyaç duyduğu üçü aşağıda.
`follow` ve `lookAt` alanları, hedef için `entity.create`'in döndürdüğü `id` değerini alır.

**Takip kamerası.** Hedefin arkasında sabit bir offset'te bir follow gövdesi ve göğsüne bakan bir look-at
nişanı:

```json
{"method": "entity.create", "params": {"name": "Chase", "components": {
  "illusion.VirtualCamera": {"priority": 10, "follow": "1f3a9c52e0b7d468", "lookAt": "1f3a9c52e0b7d468"},
  "illusion.CameraFollow": {"offset": [0, 2, 6], "damping": [0.2, 0.2, 0.2]},
  "illusion.CameraLookAt": {"targetOffset": [0, 1.5, 0]}}}}
```

**Omuz üstü kamera.** Fare ya da sağ çubuğun döndürdüğü (`lookAction`) ve duvarların kısalttığı bir yaylı kol;
oyuncu nişan alırken ona daha yüksek bir `priority` verin:

```json
{"method": "entity.create", "params": {"name": "Shoulder", "components": {
  "illusion.VirtualCamera": {"priority": 5, "follow": "1f3a9c52e0b7d468", "fovY": 0.9},
  "illusion.CameraSpringArm": {"shoulderOffset": [0.6, 1.6, 0], "armLength": 2.5, "lookAction": "camera.look"}}}}
```

**Ray kamerası.** Hedefin yakınında kalan bir parkur boyunca dolly; yandan kaydırmalı oyunlar ya da bir
ara sahne çekimi için, composer hedefi merkezin soluna yerleştirir:

```json
{"method": "entity.create", "params": {"name": "Rail", "components": {
  "illusion.VirtualCamera": {"priority": 1, "follow": "1f3a9c52e0b7d468"},
  "illusion.CameraDolly": {"mode": "NearestToTarget", "path": [
    {"position": [-8, 2, 6], "tangent": [8, 0, 0]}, {"position": [8, 2, 6], "tangent": [8, 0, 0]}]},
  "illusion.CameraComposer": {"screenPosition": [-0.2, 0]}}}}
```

`Camera`'nın Entity'sine bir `CameraBrain` koyun. `priority` değeri en yüksek kamera canlıdır; yani takip
kamerası (10) gösterilir, diğer ikisi bekler; `camera.activate` ya da `CameraService::activate()` başka birini
onun üstüne çıkarır. Beynin bir çift geçişi, yeni kameranın kendi `blendIn` değerini geçersiz kılar: bu,
takip kamerasından omuz kamerasına 0,35 sn'de hızlı başlayan bir eğriyle, geri kalan her şeyde 0,8 sn'de
geçer.

```json
{"method": "component.set", "params": {"entity": "/Camera", "type": "illusion.CameraBrain", "value": {
  "defaultBlend": {"style": "EaseInOut", "duration": 0.8},
  "blends": [{"from": "7c20d4e1a95b3f68", "to": "b6094a7d12ce8f35", "blend": {"style": "Curve", "duration": 0.35,
    "control1": [0.2, 0.0], "control2": [0.0, 1.0]}}]}}}
```

Bir modül oyun çalışırken aynı rig'leri sürer: nişan alırken omuz kamerası zoom yapar, bir düğme omzu
değiştirir ve kol yansır. `swapShoulder()`, yaylı kolu olmayan bir kamerada `CameraError::NoStage` ile
başarısız olur.

```cpp
if (illusion::CameraService* cameras = context.service<illusion::CameraService>())
{
    static_cast<void>(cameras->setFieldOfView(shoulder, aiming ? 0.7f : 0.9f));
    if (swapPressed)
    {
        static_cast<void>(cameras->swapShoulder(shoulder));
    }
}
```

## Gürültü ve sarsıntılar

`CameraNoise` elde çekim salınımı ekler: her eksende `seed` ile fazlanan iki sinüs. Bir `CameraImpulse`
Entity'si `radius` içindeki her canlı kamerayı sarsar; konumunda en güçlüdür ve `duration` boyunca söner.
Oyun başladığında (`playOnStart`), sonra her `interval` saniyede bir tetiklenir. Kod bir sarsıntıyı
`CameraWorld::shake()` ile başlatır:

```cpp
illusion::CameraShake shake;
shake.origin = explosion;
shake.strength = 0.8f;
shake.radius = 15.0f;
shake.duration = 0.6f;
static_cast<void>(session.cameras().shake(shake));
```

## Kamera beyni

Gerçek `Camera`'nın Entity'sindeki bir `CameraBrain`, her update'te canlı sanal kameradan Camera'nın dünya
`Transform`'unu (ebeveyni varsa `LocalTransform`'unu da) ve `fovY` ile `nearPlane` değerlerini yazar. Başka
bir kamera canlı olduğunda çiftin geçişiyle geçer: `blends` içinde `from` ve `to` değerleri eşleşen ilk
kayıt (boş olan her kamerayla eşleşir), yoksa yeni kameranın `blendIn` değeri, o da yoksa `defaultBlend`.
Bir geçiş bir stil (`Cut`, `Linear`, `EaseInOut` ya da kübik Bezier `Curve`) ve bir süredir. Üçüncü bir
kamerayla kesilen geçiş, Camera'nın bulunduğu yerden devam eder. Camera'yı kendisi hareket ettiren oyun kodu,
Camera'nın beyni olduğunda bunu bırakmalıdır.

## Bir modülden

Bir oyun modülü `CameraService`'i `context.service<illusion::CameraService>()` ile bulur
(`illusion_add_module`'de `USES camera`). `activate()` ile bir kamerayı diğerlerinin üstüne çıkarır,
`deactivate()` ile önceliği geri verir, kameraları hedeflere çevirir, bir orbit'i ya da spring arm'ı
döndürür, omzu değiştirir, görüş alanıyla zoom yapar, shake'leri ve impulse'ları başlatır, canlı kamerayı
ve geçişin ilerlemesini okur. [Scripting API referansı](../reference/scripting-api.md#cameraservice) her
fonksiyonu ve hatalarını listeler.

## Editörde

Dört `camera.*` [Editor commands](../reference/editor-commands.md) kameraların içine bakar ve onları sürer;
insanlar (command palette) ve AI agent'lar için aynı şekilde:

| Komut | Ne yapar |
| --- | --- |
| `camera.list` | Sahnenin sanal kameraları (en fazla 200): `priority`, `enabled`, `inactive` olup olmadıkları, derece ve metre cinsinden lens, gövde ve nişan aşaması (`Follow`, `Orbit`, `SpringArm`, `Dolly` ya da `None`; `LookAt`, `Composer` ya da `None`), `noise`, `follow` ve `lookAt` hedefleri ve bir beynin onları şimdi gösterip göstermediği (`live`); varsayılan geçişleri ve çift geçiş sayılarıyla beyinler; `CameraImpulse` kaynaklarının sayısı |
| `camera.status` | Bir play oturumunda her beynin gösterdiği: `live` kamerası, geçtiği kamera (`from`), geçişin 0'dan 1'e `progress` değeri, canlı kameranın `weight` değeri ve Camera'ya yazdığı `position`, `rotation` (pitch, yaw, roll, derece) ve `fieldOfView`, ayrıca son update'in kaç kamera değerlendirdiği |
| `camera.activate` | Bir kameraya diğer her etkin kameradan bir fazla `priority` verir; böylece beyin onu gösterir. Kaydedilen `VirtualCamera`'yı düzenler; bu yüzden geri alınabilir ve oyun o kamera canlıyken başlar. Kamera etkin olmalı ve `Inactive` olmamalıdır |
| `camera.preview` | Game görünümlerini bir sanal kameranın (`entity`) içinden baktırır, ya da `clear` ile yeniden ana Camera'dan |

```json
{"call": "camera.list", "params": {}}
{"call": "camera.activate", "params": {"entity": "/Rigs/Shoulder"}}
{"call": "camera.preview", "params": {"entity": "/Rigs/Shoulder"}}
{"call": "camera.preview", "params": {"clear": true}}
{"call": "play.step", "params": {"steps": 30}}
{"call": "camera.status", "params": {}}
```

`camera.list` ve `camera.status`, bir oyun çalışırken play World'ünde, aksi halde edit World'ünde çalışır.
Aşamaları yalnızca bir play oturumu çalıştırır; bu yüzden edit World'ünde `camera.status` `live` değerini
false verir ve bunu `note` içinde söyler; `play.step` ve ardından `camera.status` bir geçişi ya da yaylı kolu
deterministik biçimde gösterir. `camera.activate` ve `camera.preview` oyun oynarken de her zaman edit
World'ünde çalışır; oyun bıraktıkları yerden başlar. Çalışan oyunun kamerasını dışarıdan değiştirmek için
bir `VirtualCamera`'nın `priority` değerini play World'ünde çalışan `component.set_field` ile ayarlayın ya da
bir modülden `CameraService::activate()` çağırın. Geçersiz parametreler JSON Pointer'larıyla bildirilir:
`VirtualCamera` olmayan, devre dışı ya da etkin olmayan bir Entity için `/entity`, `entity` ile birlikte
verilen `clear` için `/clear`.

### Viewport'larda

Scene görünümünün **Cameras** görselleştiricisi (`viewport.set_overlays` ile `{"visualisers": {"cameras": true}}`,
varsayılan olarak açık) her `VirtualCamera`'nın frustum'unu da çizer: seçili olanlarınkini ya da **All** ile
hepsininkini, kameranın `Transform` ve lensinde, mavi renkle. Bir play oturumunda bir beynin gösterdiği
kamera her zaman, beynin değerlendirdiği pozda, sönümleme ve sarsıntısıyla sarı çizilir; bir geçişin
başladığı kamera turuncu çizilir; böylece bir geçiş, yer değiştiren iki frustum olarak görünür. Önizlenen
kamera da sarıdır.

**Game** görünümü beynin çıktısını ek bir ayar olmadan gösterir: bir play oturumunda beyin `Camera`'sına
yazar; Game görünümü de geçişler dahil oyuncunun gördüğünün aynısıdır. Play oturumunun dışında kameralar
durur, çünkü aşamaları yalnızca bir oyunda çalışır; `camera.preview` Game görünümünü bir sanal kameranın
içinden, Entity'sinin durduğu yerden ve lensiyle baktırır; bu, sahnede ana bir `Camera` olmadan sabit bir
çekimi ya da bir rayın başlangıç noktasını yerleştirmeye yeter. Önizleme editor durumudur: kaydedilmez, geri
alınmaz ve bir play oturumu onun yerine beyninin yazdığını gösterir.

## Performans

Geliştirme masaüstünde ölçüldü (Release):

- Hepsi bir gövde aşaması (follow, orbit, kesmeli yaylı kol, dolly), bir nişan aşaması, gürültü ve bir geçiş
  çalıştıran 100 sanal kameranın tek bir update'i 53 µs sürer (Benchmark `camera/stages-100`, bütçe
  400 µs).
- 100 sanal kamerası ve canlı kamerasını her on update'te bir değiştiren bir beyni olan bir World'de
  `CameraWorld::update()` 2,1 µs sürer (Benchmark `camera/world-100`, bütçe 20 µs): yalnızca canlı kamera ve
  bir geçişin başladığı kamera değerlendirilir; bu yüzden maliyet kamera sayısını değil beyin sayısını izler.
- World kameralarını bir kez gördükten sonra bir update'te hiçbir şey allocate edilmez; yaylı kol canlı kol
  başına bir küre cast'ine mal olur.
