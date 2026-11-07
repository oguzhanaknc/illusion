# Teknoloji Demosu Projesi

`samples/techdemo`, gündüzden geceye dönen bir orman açıklığında birinci şahıs bir yürüyüştür: ağaçlar ve
çimenler rüzgarda eğilir, alacakaranlıkta ateş böcekleri çıkar, bir kamp ateşi çıtırdar ve ağaçları aydınlatır,
neon halkalar uğuldar, ışıldayan küreler ittiğinizde yuvarlanır. [SkyAtmosphere](../rendering.md#gece-ve-gunduz),
[Wind](../rendering.md#ruzgar), [GPU parçacıkları](../particles.md#gpu-simulasyonu), fizik, ses ve
[fareyle bakış](../actions.md#fareyle-bakis) bir araya gelir.

![Sabah on birde açıklık](../../images/techdemo/day.png)

![Aynı görünüm gece: ateş böcekleri, fenerler, neon ve kamp ateşi](../../images/techdemo/night.png)

## Çalıştırma

Bitkilerin, kayaların ve fenerin modelleri ve dokuları Poly Haven'dan CC0 asset'lerdir. Depoda değildirler:
önce onları indirin (yaklaşık 28 MB), sonra projeyi açın:

```bash
python samples/techdemo/tools/fetch_assets.py
illusion_editor --project samples/techdemo
```

Modülü **Modules → Build Modules** ile derleyin ve **Play**'e basın. Fareyi yakalamak için Game görünümüne
tıklayın (Shift+F1 serbest bırakır).

| Eylem | Klavye ve fare | Gamepad |
| --- | --- | --- |
| Bakmak | fare | sağ çubuk |
| Yürümek | W A S D | sol çubuk |
| Koşmak | Shift | sol çubuk düğmesi |
| Zıplamak | Space | South (A, Cross) |
| Zamanı hızlandırmak (×20) | T | North (Y, Triangle) |

Tam bir gün altı dakika sürer.

## İçinde ne var

- **Arazi**: tepelerle çevrili düz bir açıklık; üçgen mesh collider'ı vardır, Poly Haven orman zemini dokusuyla
  kaplıdır.
- **Ağaçlar**: üç prosedürel geniş yapraklı ağaç (kabuk ve yaprak kartı Mesh'leri), açıklığın çevresinde 46
  tane; Material'leri rüzgarda salınır ve titrer (`windSway`, `windFlutter`). Yaklaşık 5.000 çimen öbeği,
  eğrelti ve çalı da onunla eğilir.
- **Gökyüzü**: saatini modülün ilerlettiği bir `SkyAtmosphere`; güneşi gündüzü aydınlatır ve gölgeleri düşürür,
  ay geceyi.
- **Kamp ateşi**: alev, köz ve duman emitter'ları (közler ve duman rüzgarla sürüklenir), titreyen, alevlerle
  birlikte oynayan ve yumuşak, hareketli [gölgeler](../rendering.md#point-light-shadows) düşüren bir nokta
  ışık ve döngüsel bir çıtırtı.
- **Gece**: 4.000 GPU ateş böceği, alacakaranlıkta parlayan ve gölge düşüren üç fener, cırcır böcekleri.
- **Neon**: camgöbeği, eflatun ve mor renklerde bir kapı, üç halka ve çubuklar; bloom oluşturan HDR ışımayla.
- **Fizik**: küreler ve fenerler oyuncunun ittiği dinamik gövdelerdir. Arazinin collider'ı hazır olana kadar
  kinematik kalırlar; böylece o pişirilirken içinden düşmezler.

## Nasıl kurulur

Poly Haven dosyaları dışındaki her şeyi sabit seed'lerle script'ler üretir:

- `tools/make_assets.py` araziyi, ağaçları, neonu, kütükleri, dokularını, sprite'ları ve sesleri yazar;
  `terrain_height()` fonksiyonunu sağlar.
- `tools/make_scene.py`, Material'leri ve sahneyi oluşturan editör komutlarını, `build.jsonl`'u yazar; her şeyi
  `terrain_height()` ile yere yerleştirir.
- `modules/techdemo` oyun modülüdür: yürüme, bakma, saat, gece efektleri, titreyen ışıklar.

Sahneyi değiştirmek için bir script'i düzenleyin ve yeniden kurun:

```bash
python samples/techdemo/tools/make_assets.py
python samples/techdemo/tools/make_scene.py
illusion_ctl --launch illusion_editor --editor-arg --headless --editor-arg --no-gpu --editor-arg --project --editor-arg samples/techdemo replay samples/techdemo/build.jsonl
```

`tools/test_tools.py` script'leri test eder; CI onu çalıştırır.
