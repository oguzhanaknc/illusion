# Showcase Projesi

`samples/showcase`, sahnesi (`assets:/scenes/everything.scene`) motorun kaydedilen her Component'ini kullanan ve
her alanına varsayılanından farklı bir değer veren bir editör projesidir: kameralar (perspektif ve
ortografik), procedural sky ile directional, point ve sky ışıkları, yerleşik şekillerden ve bir glTF modelinden
(ikinci bir submesh ile) Mesh'ler, bir hiyerarşi, devre dışı bir Entity, her collider şekli (Mesh Asset'lerinden
yapılan bir üçgen Mesh ve bir convex hull dahil), statik, kinematik, dinamik ve sensor gövdeler, bir karakter,
her türden joint, her bus'ta her zayıflamayla ve bir koniyle ses kaynakları ve biri override'lı iki prefab
örneği. Welcome penceresinden ya da `--project samples/showcase` ile açın.

Asset'leri `tools/make_assets.py` tarafından sayılardan üretilir (iki mesh'li bir model ve bir ton); bu yüzden
proje hiçbir üçüncü taraf lisansı taşımaz. Sahne elle yazılmaz: `build.jsonl` onu bir ajanın yapacağı gibi
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
(WASD, sol çubuk) eylemlerini ekler, sayılardan bir çan sesi üretir ve mikseri ayarlar. Her karede Player
karakterini yürütür ve zıplatır, belirli karelerde Ball, Crate, Rock, Drum ve Door'u iter, ışın ve şekil
atar, temasları ve Trigger'a girenleri sayar, bir şey sertçe düştüğü yerde çan çalar ve 120. karede bir
fener prefab'ı oluşturur. Sona doğru yerçekimini azaltır ve Crate'in zeminin içinden düşmesine izin verir.
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
