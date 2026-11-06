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
