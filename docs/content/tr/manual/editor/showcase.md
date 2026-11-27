# Showcase Projesi

`samples/showcase`, sahnesi (`assets:/scenes/everything.scene`) motorun kaydedilen her Component'ini kullanan ve
her alanına varsayılanından farklı bir değer veren bir editör projesidir: kameralar (perspektif ve
ortografik), procedural sky ile directional, point ve sky ışıkları, yerleşik şekillerden ve bir glTF modelinden
(ikinci bir submesh ile) Mesh'ler, bir hiyerarşi, devre dışı bir Entity, her collider şekli (Mesh Asset'lerinden
yapılan bir üçgen Mesh ve bir convex hull dahil), statik, kinematik, dinamik ve sensor gövdeler, bir karakter,
her türden joint, her bus'ta her zayıflamayla ve bir koniyle ses kaynakları ve biri override'lı iki prefab
örneği. Welcome penceresinden ya da `--project samples/showcase` ile açın.

Aynı zamanda ana Camera'nın tamamını gördüğü küçük bir oyun alanıdır: zemine ve basamaklı bloklara düşen
şekiller, çekmecesi dışarı kayıp geri giren bir masa, bir lambanın altında bir trigger pedi, kasasında
menteşeli bir kapı, ucundan bir ip sarkan bir sarkaç ve inip çıkan bir platform. Collider'ının kendine ait
yerleşik bir Mesh'i olmayan gövdeler, collider boyutunda bir çocuk Entity alır ("Door Mesh", "Pendulum Mesh",
...); böylece collider'lar tam boyutlarını korur ve joint anchor'ları ölçeklenmemiş gövde uzayında kalır.

Asset'leri `tools/make_assets.py` tarafından sayılardan üretilir (iki mesh'li bir model, beş ses: müzik,
rüzgâr, bir lamba uğultusu, bir selamlama ve bir tık, ve bir nine-slice panel görüntüsü); bu yüzden proje, font dışında hiçbir üçüncü taraf lisansı taşımaz: `assets/fonts/Roboto-Regular.ttf`
(Apache-2.0, lisansı `assets/fonts/LICENSE.txt` içinde). Sahne elle yazılmaz: `build.jsonl` onu bir ajanın yapacağı gibi
Editor commands ile kurar ve kaydeder. Aynı dosya, duman sprite'ına `asset.set_import_settings` ile bir içe
aktarma ayarları sidecar'ı (`"quality": "high"`) ve `project.settings_set` ile varsayılan olmayan bir pencere
(1600x900, yeniden boyutlandırılamaz; `settings/window.json`) ile bulut yansıtması kapalı kayıtlı oyunlar
(`settings/save.json`) da verir. Yeniden kurmak için:

```bash
python samples/showcase/tools/make_assets.py
illusion_ctl --launch illusion_editor --editor-arg --headless --editor-arg --no-gpu --editor-arg --deterministic --editor-arg --project --editor-arg samples/showcase replay samples/showcase/build.jsonl
```

Bir test (`TestShowcase.cpp`) projenin bir kopyasını açar; kaydedilen bir Component ya da alanlarından biri
sahnede yoksa veya varsayılanında bırakılmışsa ya da projede eksik Asset'ler veya çözümlenmemiş prefab'lar
varsa başarısız olur. Bu yüzden yeni bir Component ya da alan `build.jsonl`'da bir değer ister. Üç parçacık
emitter'ı `ParticleEmitter`'ı kapsar: trigger pedinin üzerinde bir kıvılcım çeşmesi, blokların yanında
oynatılmayı bekleyen bir toz emitter'ı (`playOnStart` kapalı) ve GPU'da simüle edilen 20.000 ateş böceklik bir
alan. `Column`, `Animator`'ı kapsar: animasyon grafı `assets/animations/column.animgraph` onu `sway`'in 0,4
saniyesinde tutan ve eğik çizilen skinned bir sütun (`sway` ve `lean` klipli `assets/models/column.gltf`,
`tools/make_assets.py` yazar), bkz. [Animasyon](../animation.md#animasyon-graflar); çocuğu `Column Lamp`,
sütunun `top` eklemindeki küçük bir küreyle `BoneAttachment`'ı kapsar ([socket'ler](../animation.md#socketler)).
Kameranın arkasındaki küçük iki bacaklı figür `Strider` (`assets/models/strider.gltf`), `LookAt`'i (sarkacı
izler), `TwoBoneIk`'i (sol ayağı ve pole'ü `Strider Knee Pole` olan `Strider Right Leg` çocuğundaki sağ ayağı)
ve `FootPlacement`'ı kapsar ([look-at](../animation.md#look-at), [two-bone IK](../animation.md#two-bone-ik),
[foot placement](../animation.md#foot-placement)).
Yanındaki `Small Strider` (`assets/models/small_strider.gltf`, Strider'ın kendi clip'i olmayan, yeniden adlandırılmış, daha küçük bir kopyası) Strider'ın `idle` clip'ini retarget ederek oynatır ([retargeting](../animation.md#retargeting)); çocuğu `Small Strider Hat`, küçük rig'de joint adı olarak bulunmayan `head` bone adıyla bağlanır.
Bir `Wind` Entity'si tozu (`windFactor`) avlunun öbür ucuna savurur. Devre dışı `Dusk Sky`
Entity'si gün batımında bir `SkyAtmosphere` tutar: gökyüzünü ve güneşi hareketli olanlarla değiştirmek için onu
etkinleştirin; devre dışı `Film Look` ise derecelendirilmiş, vinyetli bir görüntü için `PostEffects`
tutar. Devre dışı `Morning Mist`, alçakta süzülen bir sis için bir `VolumetricFog` tutar. `HUD` Entity'si her UI
Component'ini kullanan bir oyun UI'ıdır: nine-slice görüntülü bir durum paneli ve bir metin stack'i (biri düz,
biri `assets/locales/en.json` ve `tr.json` içindeki bir anahtarla yerelleştirilmiş), renkli hücrelerden bir grid ve
döndürülmüş, ölçeklenmiş, kırpılmış bir kadran; bkz. [Oyun UI'ı](../ui.md). Play sırasında Game görünümünde görünür. Diğerlerinin doğusunda, x = 30'daki Nav yard navigasyon Component'lerini kapsar: `Nav Floor` ve `Nav Step` zemindir, `Nav Surface` Entity'si onu agent türü 1 için bake eder ve `assets/navigation/yard.navmesh` dosyasını yükler, `Nav Hole` ondan bir kare keser, `Nav Ladder` zemini basamağın tepesine bağlar, carve eden `Nav Barrel` yolu kapatır ve `Nav Walker` avlunun karşısında bir hedefi olan bir agent'tır ([Navigasyon](../navigation.md)).

## Showcase modülü

Projenin oyun kodu olan `modules/showcase`, sahneyi [scripting API](../../reference/scripting-api.md)'nin
tamamıyla oynatır: `ModuleContext`'in ve on host servisinin her fonksiyonu `showcase.cpp`'den çağrılır.
Oturum başlarken mesh geometrisinden bir zemin ve bir mücevher kurar, Jump (Space, gamepad South) ve Move
(WASD, sol çubuk) eylemlerini ekler, sayılardan bir çan sesi üretir ve mikseri ayarlar. Bir tuşa basana ya da
bir çubuğu oynatana kadar Player trigger pedinin üzerinden kendi kendine ileri geri yürür; sonra WASD ya da sol
çubuk onu yürütür, Space ya da South zıplatır. Ped üzerindeyken üstteki lamba yanar ve pede her bastığında
Voice onu selamlar. Menteşe motoru Door'u her dört saniyede bir açıp yeniden kapatır (oyun kodu `motorSpeed`'i
her an değiştirebilir), çekmece kayar, platform inip çıkar ve Ball, Crate, Rock ile Drum belirli karelerde
itilir; Ball 90. karede bir buçuk katına büyür. Modül ayrıca ışın ve şekil atar, temasları ve Trigger'a
girenleri sayar, bir şey sertçe düştüğü yerde çan çalar ve ilk seferinde tozu kaldırır, Player pede bastığında
kıvılcım yağdırır, kıvılcım çeşmesini 270. karede durdurup 290. karede yeniden başlatır, 120. karede bir fener
prefab'ı oluşturur, 200. kareden
itibaren bir saniyeliğine Ay yerçekimine geçer ve 240. kareden sonra Player'ın Crate'in içinden geçmesine izin
verir (katmanları artık çarpışmaz). HUD'ı `UiService` ile yönetir: kare sayacı çoğul içeren bir string table
mesajıdır, modül paleti gizler ve gösterir, odağı taşır, widget'ları okur ve olayları poll eder;
`ui.cancel` basılınca (Escape ya da gamepad'de East) oyunu `SceneService::requestExit()` ile bitirir.
`AnimationService` ile Column'un grafını oynatır: `sway` ile `lean` harmanını bir parametreyle eğer, 30. karede
bir trigger ile Column'u eğdirir, 150. kareden itibaren onu yeniden sakinleştirir, grafın `peak` olaylarını
sayar ve `top` ekleminin yerini `jointTransform()` ile okur. 30. karede Strider lambaya döner
(`setLookAtTarget()`), sol ayağı bir noktaya uzanır (`setIkTarget()`) ve sağ ayağının IK'si kapanır
(`setIkWeight()`). `NavService` ile 5. karede bahçenin navmesh'ini sorgular, 10. karede Nav Walker'ı hedefine
gönderir, durumunu ve kalan mesafesini okur, `Arrived` olaylarını sayar, 200. karede onu başa ışınlar ve 280. karede durdurur.
Gördükleri, Showcase Entity'sinin `showcase.Stats` Component'inde tutulur.

**Modules → Build Modules** (`module.build`) ile Build edin ve **Play**'e basın:

```json
{"method": "module.build", "params": {}}
{"method": "play.start", "params": {"mode": "play", "fixedStep": true, "seed": 7}}
```

Modül yalnızca kare numarasına ve girdiye göre karar verir; bu yüzden sabit adımlı oturumlar birebir
tekrarlanır. Üç kontrol showcase'i motorla uyumlu tutar:

- `python tools/check_api_coverage.py` (CI), "Scripting API" Doxygen grubunun bir fonksiyonu
  `showcase.cpp`'de çağrılmıyorsa başarısız olur.
- `tests/showcase/TestShowcaseModule.cpp` modülü sayaçlı proxy'lerle sarılmış gerçek servisler üzerinde 300
  kare çalıştırır ve hiç çağrılmayan bir fonksiyon varsa başarısız olur. İkinci bir test aynı girdiyle iki
  sabit adımlı oturum çalıştırır ve kaydedilen durumlarının hash'ini karşılaştırır.
- `TestShowcaseGpu.cpp` sahneyi ana Camera'sından bir golden görüntüyle karşılaştırır (`showcase-game.png`,
  `ILLUSION_UPDATE_GOLDENS=1` ile yeniden yazılır).

Proje ayrıca dışa aktarılmış bir oyun olarak da derlenir, `showcase_game` target'ı (`samples/CMakeLists.txt`):
modülü statik bağlanır, verisi çalıştırılabilir dosyanın yanına paketlenir ve [player](../runtime.md#oyuncu-player)
çalıştırır. `TestShowcaseGame.cpp` onu 300 sabit adım çalıştırır ve ekran görüntüsünü editörün Game görünümünün
golden görüntüsüyle karşılaştırır.
