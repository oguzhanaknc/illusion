# Teknoloji Demosu Projesi

`samples/techdemo`, gündüzden geceye dönen bir köknar ormanındaki açıklıkta birinci şahıs bir yürüyüştür:
köknarlar, çimenler ve eğreltiler rüzgarda eğilir, alacakaranlıkta alçalan güneş gövdelerin arasından parlar,
ateş böcekleri çıkar, bir kamp ateşi çıtırdar ve ağaçları aydınlatır, neon halkalar uğuldar, ışıldayan küreler
ittiğinizde yuvarlanır, açıklığın kenarında yaşayan bir yaratık sizi gördüğünde ya da duyduğunda peşinize
düşer. [SkyAtmosphere](../rendering.md#gece-ve-gunduz), [Wind](../rendering.md#ruzgar),
[GPU parçacıkları](../particles.md#gpu-simulasyonu), fizik, ses, [fareyle bakış](../actions.md#fareyle-bakis),
navigasyon, yapay zeka ve animasyon bir araya gelir.

![Sabah on birde açıklık](../../images/techdemo/day.png)

![Beş buçuk: güneş köknarların ardında batıyor](../../images/techdemo/dusk.png)

![Aynı görünüm gece: ateş böcekleri, fenerler, neon ve kamp ateşi](../../images/techdemo/night.png)

## Çalıştırma

Ağaçlar, bitkiler, kayalar, kuru odunlar, eşyalar ve orman zemini Poly Haven'dan CC0 asset'lerdir. Depoda
değildirler: önce onları indirin (bir kez, yaklaşık 900 MB), sonra projeyi açın:

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

Escape (ya da East) duraklatma menüsünü açar: Resume, Save game, Load game, ana ses düzeyi, dil ve Quit. Menü
görünürken saat durur. Oyuncu, gökyüzünün saati, küreler ve fenerler [`Saved` tag'ini](../saved-games.md) taşır;
bu yüzden **Save game** bulunduğunuz yeri, günün saatini ve eşyaları ittiğiniz yerleri tek bir slot'ta saklar,
**Load game** bir slot olana kadar soluktur. Hızlar, ateşböcekleri ve sesler yüklemeden sonra baştan başlar.

## İçinde ne var

- **Arazi**: tepelerle çevrili düz bir açıklık; üçgen mesh collider'ı vardır, Poly Haven'ın çam ormanı iğne
  yaprağı örtüsüyle kaplıdır.
- **Orman**: 110 uzun köknar (13 ile 19 m), 80 genç köknar, 14 geniş yapraklı ağaç ve 36 fidan; hepsi Poly
  Haven modelleridir ve her birinin bir silindir collider'ı vardır. Bir ağacın tüm parçaları boyuna göre
  seçilmiş tek bir `windSway` paylaşır; böylece gövde ve iğneler birlikte eğilir. Yapraklar ve iğneler ayrıca
  titrer (`windFlutter`). Yapraklar, iğneler ve bodur bitkiler alçak güneşin ışığını geçirir
  (`diffuseTransmission`); güneşe karşı sarımsı yeşil parlarlar.
- **Bodur bitkiler**: sabit bir seed ile dağıtılmış yaklaşık 14.000 çimen öbeği, eğrelti, çalı, ısırgan, çiçek
  (kırlangıçotu, karahindiba, cezayir menekşesi), yabani ot, kök, kuru dal ve kabuk. Yalnızca uzaktan görülen
  tepelerde bitki örtüsü daha seyrektir.
- **Kayalar ve kuru odun**: tepelerde kaya yüzleri, iri kayalar, yosunlu taşlar, devrik kütükler, ağaç
  kütükleri ve kök yığınları; convex hull collider'larla katıdırlar.
- **Gökyüzü**: saatini modülün ilerlettiği bir `SkyAtmosphere`; güneş neon kapının ardında batar, gündüzü
  aydınlatır ve gölgeleri düşürür, ay geceyi. `VolumetricFog` onun ışığını gövdelerin arasında ışık huzmelerine
  çevirir.
- **Kamp ateşi**: taş halkanın içinde közlerin üzerine yaslanmış odunlar, alev, köz ve duman emitter'ları
  (közler ve duman rüzgarla sürüklenir), titreyen, alevlerle birlikte oynayan ve yumuşak, hareketli
  [gölgeler](../rendering.md#point-light-shadows) düşüren bir nokta ışık ve döngüsel bir çıtırtı. Oturmak için iki
  devrik gövde ve bir kütük, bir odun yığını ve kütüğe saplanmış bir balta.
- **Gece**: 5.000 GPU ateş böceği, alacakaranlıkta parlayan ve gölge düşüren üç fener, cırcır böcekleri.
- **Neon**: camgöbeği, eflatun ve mor renklerde bir kapı, üç halka ve çubuklar; bloom oluşturan HDR ışımayla.
- **Fizik**: küreler ve fenerler oyuncunun ittiği dinamik gövdelerdir. Arazinin collider'ı hazır olana kadar
  kinematik kalırlar; böylece o pişirilirken içinden düşmezler.
- **Yaratık**: açıklığın kenarında, kamp ateşinin arkasında ini olan kurda benzer bir hayvan (aşağıda).

### Yaratık

Yaratık ininin çevresinde dolaşır, sizi gördüğünde ya da duyduğunda peşinize düşer, sizi kaybettiğinde evine
döner. Motorun [navigasyonu](../navigation.md), [yapay zekası](../ai.md) ve [animasyonuyla](../animation.md)
yapılmıştır:

- **Navmesh**: *Clearing Navigation* Entity'sinin açıklığın üzerinde 40 × 40 m'lik bir `NavMeshSurface`'i
  vardır; `build.jsonl` onu `assets/navigation/clearing.navmesh` dosyasına bake eder (depoda). *Campfire Area*,
  ateşin çevresinde alan 1'den bir `NavArea` hacmidir ve surface bu alanın maliyetini 25 yapar: yaratık, başka
  yol yoksa, ateşin üzerinden değil çevresinden yürür.
- **Gövde**: `idle`, `walk` ve `run` clip'leriyle kutulardan rig'li bir model (`assets/models/creature.gltf`).
  Animasyon grafiği (`assets/animations/creature.animgraph`) onları `speed` parametresine göre karıştırır;
  `NavAgent` bu parametreyi her güncellemede kendi hızına ayarlar (`speedParameter`), böylece bacaklar zemine
  ayak uydurur.
- **Hareket**: `Character` drive'lı bir `NavAgent`, `CharacterController`'ını yönlendirir. Modül ona dolaşırken
  1,4 m/s, kovalarken 4,2 m/s verir.
- **Duyular**: bir `AiPerception` önündeki 9 m'yi 120 derecelik bir koni içinde görür, ağaçlar ve kayalar görüşü
  keser; sesleri de duyar. Oyuncunun bir `AiStimulus`'u vardır; modül her ayak sesini `AiService` üzerinden bir
  ses olarak bildirir: yürürken 7 m, koşarken 15 m içinde duyulur.
- **Davranış**: bir `BehaviorAgent`, öncelik sırasıyla üç dallı `assets/ai/creature.behavior`'ı çalıştırır:
  *chase* (oyuncuya koş ve hırla; bu, modülün yanıtladığı bir action'dır), *return* (sizi en son sezdiği yeri
  ara, kokla, sonra eve dön) ve *wander* (ininin 4 m yakınında rastgele bir nokta, sonra bir mola). `home`
  anahtarı, ajanın bir başlangıç değeri olan inidir.

Sizi kovalarken HUD bunu söyler. Duraklatma menüsü saati durdurduğu gibi onu da durdurur: modül
`BehaviorAgent`'ını kapatır ve `NavAgent`'ını durdurur, menü kapanınca devam etmesine izin verir. Oyuncu gibi o
da arazinin collider'ını bekler.

### Hafifletilmiş ağaçlar

Poly Haven ağaçları yakın çekim için yapılmıştır: bir köknarda dört milyon iğne üçgeni vardır. Bu yüzden
`fetch_assets.py` indirilen dosyaların yanına onların hafif kopyalarını (`*_lite.gltf`) yazar. İğneler ve
yapraklar birbiriyle vertex paylaşmayan kartlardır; script kartların eşit dağılmış bir kısmını tutar ve
düşürdüğü yaprak alanını telafi etmek için her birini kendi merkezi etrafında büyütür; gövdeler ve dallar
olduğu gibi kalır. Motorun [levels of detail](../rendering.md#levels-of-detail)'i bu kartları uzaklıkla
birlikte daha da seyreltir; oyuncunun asla yaklaşamadığı tepelerdeki köknarların `lodBias`'ı 2'dir, böylece
daha erken seyrelirler; yalnızca açıklığa yakın ağaçların iğneleri gölge düşürür. Script ayrıca modellerin
doku dönüşümlerini (`KHR_texture_transform`) UV'lerine işler.

Tüm bunlarla 1280 × 720'de, ateşin ve üç fenerin gölgeleriyle bir gece karesi, masaüstü bir RTX 4070 SUPER
GPU'sunda yaklaşık 30 ms sürer (gölgeler 12 ms, derinlik 6 ms, gölgelendirme 11 ms).

## Nasıl kurulur

Poly Haven dosyaları dışındaki her şeyi sabit seed'lerle script'ler üretir:

- `tools/fetch_assets.py` sabitlenmiş Poly Haven dosyalarını indirir (boyut ve SHA-256 denetlenir), her
  bitkinin renk ve alfa haritalarını RGBA yaprak dokularında birleştirir ve hafif ağaçları yazar.
- `tools/make_assets.py` araziyi, neonu, kütükleri, sprite'ları, sesleri ve animasyon grafiği ile davranış
  ağacıyla birlikte yaratığı yazar; `terrain_height()` fonksiyonunu sağlar.
- `tools/make_scene.py`, Material'leri ve sahneyi oluşturan editör komutlarını, `build.jsonl`'u yazar; her şeyi
  `terrain_height()` ile yere yerleştirir. Yaratık, navmesh'i ve HUD'un uyarısı en sona gelir; böylece geri
  kalan her şeyin id'leri aynı kalır.
- `modules/techdemo` oyun modülüdür: yürüme, bakma, saat, gece efektleri, titreyen ışıklar, yaratığın duyduğu
  ayak sesleri, hızları ve hırlamaları.

Sahneyi değiştirmek için bir script'i düzenleyin ve yeniden kurun:

```bash
python samples/techdemo/tools/make_assets.py
python samples/techdemo/tools/make_scene.py
illusion_ctl --launch illusion_editor --editor-arg --headless --editor-arg --no-gpu --editor-arg --deterministic --editor-arg --project --editor-arg samples/techdemo replay samples/techdemo/build.jsonl
```

`tools/test_tools.py` script'leri test eder; CI onu çalıştırır.
