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

Asset'leri `tools/make_assets.py` tarafından sayılardan üretilir (iki mesh'li bir model ve beş ses: müzik,
rüzgâr, bir lamba uğultusu, bir selamlama ve bir tık); bu yüzden proje hiçbir üçüncü taraf lisansı taşımaz. Sahne elle yazılmaz: `build.jsonl` onu bir ajanın yapacağı gibi
Editor commands ile kurar ve kaydeder. Yeniden kurmak için:

```bash
python samples/showcase/tools/make_assets.py
illusion_ctl --launch illusion_editor --editor-arg --headless --editor-arg --no-gpu --editor-arg --deterministic --editor-arg --project --editor-arg samples/showcase replay samples/showcase/build.jsonl
```

Bir test (`TestShowcase.cpp`) projenin bir kopyasını açar; kaydedilen bir Component ya da alanlarından biri
sahnede yoksa veya varsayılanında bırakılmışsa ya da projede eksik Asset'ler veya çözümlenmemiş prefab'lar
varsa başarısız olur. Bu yüzden yeni bir Component ya da alan `build.jsonl`'da bir değer ister.

## Showcase modülü

Projenin oyun kodu olan `modules/showcase`, sahneyi [scripting API](../../reference/scripting-api.md)'nin
tamamıyla oynatır: `ModuleContext`'in ve dört host servisinin her fonksiyonu `showcase.cpp`'den çağrılır.
Oturum başlarken mesh geometrisinden bir zemin ve bir mücevher kurar, Jump (Space, gamepad South) ve Move
(WASD, sol çubuk) eylemlerini ekler, sayılardan bir çan sesi üretir ve mikseri ayarlar. Bir tuşa basana ya da
bir çubuğu oynatana kadar Player trigger pedinin üzerinden kendi kendine ileri geri yürür; sonra WASD ya da sol
çubuk onu yürütür, Space ya da South zıplatır. Ped üzerindeyken üstteki lamba yanar ve pede her bastığında
Voice onu selamlar. Menteşe motoru Door'u her dört saniyede bir açıp yeniden kapatır (oyun kodu `motorSpeed`'i
her an değiştirebilir), çekmece kayar, platform inip çıkar ve Ball, Crate, Rock ile Drum belirli karelerde
itilir; Ball 90. karede bir buçuk katına büyür. Modül ayrıca ışın ve şekil atar, temasları ve Trigger'a
girenleri sayar, bir şey sertçe düştüğü yerde çan çalar, 120. karede bir fener prefab'ı oluşturur, 200. kareden
itibaren bir saniyeliğine Ay yerçekimine geçer ve 240. kareden sonra Player'ın Crate'in içinden geçmesine izin
verir (katmanları artık çarpışmaz). Gördükleri, Showcase Entity'sinin `showcase.Stats` Component'inde tutulur.

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
