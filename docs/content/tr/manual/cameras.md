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
