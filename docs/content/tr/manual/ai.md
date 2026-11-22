# AI

illusion oyun karakterlerini behaviour tree'lerle yönetir. Bir tree, bir `.behavior` dosyasıdır: türlü
key'lerden oluşan bir blackboard ve bir agent'ın tick tick ne yapacağına karar veren bir node ağacı
([ADR 0062](../architecture/decisions.md)). Hareket [Navigasyon](navigation.md) üzerinden gider.

!!! note "Bugün neler var"
    Bu sayfa `.behavior` formatını, blackboard'u, tick'i, AI Component'lerini, `AiWorld`'ü ve oyun modülleri
    için `AiService`'i anlatır. `ai.*` Editor commands (NAV-2d) sonraki paketle gelir.

## The `.behavior` file

Dosya bir JSON nesnesidir. Bilinmeyen anahtarlar hatadır; `AssetKind::BehaviorTree`, `.behavior`
uzantısının Asset türüdür.

| Anahtar | Anlamı |
| --- | --- |
| `format` | `"illusion.behavior"` olmalı |
| `version` | 1 olmalı (`kBehaviorTreeVersion`) |
| `blackboard` | İsteğe bağlı key dizisi, en çok 32 (`kMaxBlackboardKeys`) |
| `root` | Kök node, zorunlu; tree'de en çok 1.024 node olur (`kMaxBehaviorNodes`) |

```json
{
  "format": "illusion.behavior",
  "version": 1,
  "blackboard": [
    {"name": "target", "type": "Entity"},
    {"name": "home", "type": "Vector3", "default": [0, 0, 0]},
    {"name": "ammo", "type": "Int", "default": 3}
  ],
  "root": {"name": "guard brain", "selector": [
    {"guard": {"key": "target", "compare": "IsSet", "abort": "LowerPriority",
      "child": {"sequence": [
        {"moveTo": {"key": "target", "acceptance": 1.5}},
        {"timeLimit": {"seconds": 3, "child": {"action": "bite"}}},
        {"setValue": {"key": "target", "value": null}}
      ]}}},
    {"guard": {"key": "ammo", "compare": "Greater", "value": 0,
      "child": {"cooldown": {"seconds": 5, "child": {"action": "reload"}}}}},
    {"sequence": [
      {"wander": {"key": "home", "radius": 8}},
      {"wait": {"seconds": 2, "deviation": 1}}
    ]}
  ]}
}
```

## The blackboard

`blackboard`'un her girdisinin benzersiz bir `name`'i, bir `type`'ı ve isteğe bağlı bir `default`'u vardır.

| Tür | Dosyada `default` | `default` yoksa |
| --- | --- | --- |
| `Bool` | `true` ya da `false` | `false` |
| `Int` | -2147483648 ile 2147483647 arasında bir tam sayı | 0 |
| `Float` | Sonlu bir sayı | 0 |
| `Vector3` | 3 sonlu sayıdan oluşan bir dizi | Atanmamış |
| `Entity` | Yok: bir dosya Entity adlandıramaz | Atanmamış |

`Bool`, `Int` ve `Float` key'leri her zaman atanmıştır. Bir `Vector3` ya da `Entity` key'i atanmamış
olabilir; artık yaşamayan bir Entity atanmamış sayılır. Çalışan bir agent'ın key'lerini oyun doldurur.

### Comparisons

`guard` ve `condition`, bir key'i adını verdikleri `compare` ile karşılaştırır. Karşılaştırma key'in
türüne uymalıdır, yoksa parse başarısız olur.

| `compare` | Uygulandığı tür | `value` |
| --- | --- | --- |
| `IsSet`, `IsNotSet` | `Vector3`, `Entity` | Verilemez |
| `Equal`, `NotEqual` | `Bool`, `Int`, `Float` | Zorunlu |
| `Greater`, `Less` | `Int`, `Float` | Zorunlu |
| `GreaterEqual`, `LessEqual` | Yalnızca `Int` | Zorunlu |

## Nodes

Bir node, anahtarı tam olarak bir node türü olan bir nesnedir; isteğe bağlı bir `name` alabilir (durum ve
hata ayıklama için boş olmayan bir etiket; varsayılanı türün adıdır). Tür anahtarının değeri aşağıdaki
alanları taşır. Sayılar sonlu olmalıdır.

```json
{"name": "chase", "moveTo": {"key": "target"}}
```

### Composites

| Node | Değer | Anlamı |
| --- | --- | --- |
| `sequence` | En az bir node'luk dizi | Çocukları biri başarısız olana kadar sırayla çalıştırır |
| `selector` | En az bir node'luk dizi | Çocukları biri başarılı olana kadar sırayla çalıştırır |
| `parallel` | `{"successes", "children"}` | Her tick'te her çocuğu çalıştırır. `children` en az bir node'luk bir dizidir; `successes` (varsayılan 0: hepsi, çocuk sayısını aşamaz) kaçının başarılı olması gerektiğidir. Bu artık mümkün olmadığı anda başarısız olur ve bittiğinde hâlâ çalışan çocukları abort eder |

### Decorators

Bir decorator'ın tek bir `child` node'u vardır.

| Node | Alanlar | Anlamı |
| --- | --- | --- |
| `inverter` | `child` | Çocuğunun başarısını ve başarısızlığını değiştirir |
| `succeeder` | `child` | Çocuğu bittiğinde başarılı olur |
| `failer` | `child` | Çocuğu bittiğinde başarısız olur |
| `repeat` | `count`, `child` | Çocuğunu `count` kez çalıştırır (varsayılan 0: sonsuz); çocuk başarısız olursa o da başarısız olur |
| `retry` | `count`, `child` | Çocuğunu başarısızlıktan sonra en çok `count` kez yeniden çalıştırır (varsayılan 0: sonsuz): `count` 2, 3 denemeye izin verir |
| `cooldown` | `seconds`, `child` | Çocuğu bittikten sonra `seconds` boyunca başarısız olur; `seconds` 0'dan büyüktür |
| `timeLimit` | `seconds`, `child` | `seconds` sonra başarısız olur ve çocuğunu abort eder; `seconds` 0'dan büyüktür |
| `guard` | `key`, `compare`, `value`, `abort`, `child` | Koşul sağlandığı sürece çocuğunu çalıştırır; yoksa başarısız olur |

`count`, 0 ile 4294967295 arasında bir tam sayıdır. `guard` için `key`, `compare` ve `value`
[Comparisons](#comparisons) bölümündeki gibidir; `abort` aşağıdaki kiplerden biridir (varsayılan `None`).

### Leaves

| Node | Alanlar | Anlamı |
| --- | --- | --- |
| `wait` | `seconds`, `deviation` | `seconds` (en az 0) kadar, en çok `deviation` (varsayılan 0, en çok `seconds`) artı eksi ile çalışır |
| `moveTo` | `key`, `acceptance` | Bir `Vector3` key'ine ya da bir `Entity` key'inin konumuna gider, `acceptance` m içinde varır (0'dan büyük, varsayılan 1) |
| `wander` | `key`, `radius` | `Vector3` key'inin `radius` m (zorunlu, 0'dan büyük) çevresindeki rastgele bir navmesh noktasına gider; key yoksa agent'ın tree'nin ilk tick'indeki konumunun çevresindeki bir noktaya |
| `stop` | yok (`{}`) | Agent'ı durdurur ve başarılı olur |
| `setValue` | `key`, `value` | `value`'yu key'e yazar ve başarılı olur. `Vector3` 3 sayılık bir dizi, `Bool`, `Int` ve `Float` kendi türlerini alır; `null` bir `Vector3` ya da `Entity` key'ini temizler |
| `condition` | `key`, `compare`, `value` | Koşul sağlanırsa başarılı olur, yoksa başarısız |
| `action` | Bir ad dizesi | Oyun kodu: o adlı action'ı bir modül bitirene kadar çalışır, sonra başarılı ya da başarısız olur |

### Abort modes

| `abort` | Anlamı |
| --- | --- |
| `None` | Koşul yalnızca guard başlarken denetlenir |
| `Self` | Guard koşulu her tick denetler ve yanlış olunca çalışan çocuğunu abort eder |
| `LowerPriority` | `Self` gibi; ayrıca koşul yanlıştan doğruya geçince selector'ının çalışan sonraki kardeşini de abort eder ve devralır |

`LowerPriority` yalnızca ebeveyni bir `selector` olan guard'da kullanılabilir. Sadece yanlıştan doğruya
geçişte devralır: zaten doğru olan bir koşul hiçbir şeyi kesmez.

### Errors

`parseBehaviorTree()` ilk sorunu, hatalı değerin JSON yoluyla başlayan bir iletiyle döndürür:

```text
root.selector[0].guard.key: unknown key 'targt'
root.sequence[1].wait.deviation: must be at most seconds
```

Geçersiz JSON satır ve sütunuyla bildirilir; `format`'ı olmayan ya da başka bir `version`'ı olan dosya
herhangi bir node okunmadan reddedilir. `writeBehaviorTree()` bir tree'yi geri yazar ve
`parseBehaviorTree()` onu değişmeden okur.

## Ticking a tree

Tick, düz veri üzerinde iki saf fonksiyondur; bu yüzden bir World olmadan çalışır. Her agent'ın bir
`BehaviorTreeState`'i vardır; bir `BehaviorContext` yaprakların davranmasını sağlar (agent nerede, hareket
istekleri, action'lar).

```cpp
auto tree = illusion::parseBehaviorTree(text);
if (tree)
{
    illusion::BehaviorTreeState state = illusion::makeBehaviorTreeState(*tree, 1234);
    state.blackboard[tree->findKey("home")].vector = spawnPoint;
    state.blackboard[tree->findKey("home")].set = true;
    illusion::BehaviorStatus status = illusion::tickBehaviorTree(*tree, state, 1.0f / 60.0f, context);
}
```

`makeBehaviorTreeState()` her node'u idle, key'leri varsayılanlarında ve seed ile tohumlanmış bir
`Random` taşıyan bir state döndürür. `resetBehaviorTree()` node'ları idle'a, key'leri varsayılanlarına,
tick sayısını ve süreyi 0'a döndürür ve `Random`'ı korur; hâlâ bekleyen istekleri durdurmaz, bunu çağıran
yapar. `tickBehaviorTree()` kökün durumunu döndürür. Biten bir kök bir sonraki tick'te yeniden başlar.

Node'ların bir tick'teki davranışı:

- **Durum.** `Success` ve `Failure`, node yeniden başlayana kadar üzerinde kalır.
- **Repeat ve Retry.** Çocuklarını tick başına en çok bir kez çalıştırırlar; bu yüzden hemen başarılı olan
  bir çocuk tek bir tick içinde döngüye giremez.
- **TimeLimit.** Yalnızca çocuğunun hâlâ çalıştığı tick'leri sayar.
- **Cooldown.** Çocuğunun bitişinden itibaren tree zamanını sayar (tree başladığından beri tick'lenen
  saniyeler).
- **MoveTo.** Agent hedefin `acceptance` içindeyse hemen başarılı olur. Çalışırken, hedef son istekten beri
  `acceptance`'tan fazla hareket ettiyse yeni bir hareket ister. Key atanmamışsa ya da Entity yoksa
  başarısız olur.
- **Wander.** Key yoksa agent'ın tree'nin ilk tick'indeki konumunun çevresinde dolaşır. Her hareket
  `kWanderAcceptance` (0,5 m) kullanır. Context nokta bulamazsa başarısız olur.
- **Action.** Action'ı ilk tick'inde başlatır ve sonucu en erken bir sonraki tick'te bildirir.
- **Wait.** Deviation, state'in `Random`'ından çekilir; aynı seed aynı bekleyişleri tekrarlar.
- **Abort'lar.** Abort edilen bir node, hareketini ya da action'ını context üzerinden durdurur.

## Agents

`BehaviorAgent` Component'i, Entity'si için bir tree çalıştırır; genellikle onu hareket ettiren bir
`NavAgent` ile birlikte.

| Alan | Anlamı |
| --- | --- |
| `tree` | `.behavior` dosyası |
| `enabled` | Oyun çalışırken tick'lenir |
| `tickInterval` | Tick'ler arası saniye; 0 her update'te tick'ler |
| `blackboard` | Tree'nin varsayılanlarının yerine geçen başlangıç değerleri (`BlackboardOverride`: `key`, Bool, Int ve Float için `number`, Vector3 için `vector`, Entity için `entity`) |

`AiWorld` bir World'ün AI'ını tutar. Her update'te her tree dosyasını `AssetStreamer` üzerinden bir kez
yükler (decoder `illusion.behavior`), her agent'a bir slot ve runtime `AiAgentState` verir (slot'u, son
tick'ten sonraki durum, çalışan node ve algının hedefi) ve oyun çalışırken algılar ve etkin tree'leri Job
olarak tick'ler. Bir tree varsayılanları ve agent'ın başlangıç değerleriyle başlar; `Random`'ı agent'ın
`EntityGuid`'inden tohumlanır, böylece bir sahne aynı beklemeleri ve gezinmeleri tekrarlar. `tickInterval`'ı
olan tree'ler her aralıkta bir kez, son tick'lerinden beri geçen süreyle tick'lenir; çok sayıda agent aynı
update'te tick'lenmesin diye aralığa yayılırlar. `Inactive` etiketli Entity'ler durumlarını korur ve
tick'lenmez.

Hareketler [`NavWorld`](navigation.md) üzerinden gider: bir `moveTo` `NavAgent`'ın hedefini ayarlar ve agent
`acceptance` içine vardığında başarılı olur; bir `wander` yarıçapı içinde ulaşılabilir bir navmesh noktası
seçer. `NavAgent`'ı olmayan bir agent'ın hareketleri başarısız olur.

```cpp
illusion::AiWorld ai(world, navigation, &streamer);
ai.update(step, &jobs, &physics, true); // after the modules, before navigation
for (const illusion::AiActionRequest& request : ai.actions())
{
    if (request.phase == illusion::AiActionPhase::Started && request.name == "greet")
    {
        ai.finishAction(request.entity, request.action, true);
    }
}
```

### Actions

Bir `action` node'u oyun kodundan bir şey yapmasını ister. `actions()` son update'in başlattığı ya da abort
ettiği action'ları slot sırasıyla listeler; `finishAction()` sonucu bildirir, node onu bir sonraki tick'inde
görür. `setValue()` ve `value()` bir agent'ın blackboard'undaki bir key'i yazar ve okur, `restart()` tree'yi
başlangıç blackboard'uyla kökten yeniden başlatır.

### Live reload

Bir `.behavior` dosyası değiştiğinde agent'ları yeni tree ile yeniden başlar; çalışan action'ları abort
edilmiş olarak bildirilir. Artık ayrıştırılamayan bir dosya eski tree'yi korur ve hatayı log'a yazar.

## Perception

`AiPerception` bir Entity'nin görmesini ve duymasını sağlar. `faceMovement`'lı bir `NavAgent`'ın baktığı
gibi +z ekseni boyunca bakar.

| Alan | Anlamı |
| --- | --- |
| `sightRadius` | m cinsinden; 0 kördür |
| `sightAngle` | Radyan cinsinden tam koni açısı (120°) |
| `eyeHeight` | Gözün Entity'nin konumundan yüksekliği, m |
| `occluderMask` | Görüşü engelleyen fizik katmanları |
| `hearing` | Gürültülerin yarıçapının çarpanı; 0 sağırdır |
| `memorySeconds` | Artık algılamadığı şeyi ne kadar hatırladığı |
| `detectTeams` | Fark ettiği takımlar (bit i = takım i) |
| `targetKey` | Tree'sinin seçilen hedefe ayarlanan Entity key'i, hedef unutulunca temizlenir |
| `positionKey` | Tree'sinin hedefin bilinen son konumuna ayarlanan Vector3 key'i |

Her update'te bir algılayıcı konisindeki uyaranları toplar (Job olarak; en çok
`AiWorldDesc::maxSightCandidates`, en yakın önce), ardından gözden görüş hattını bir `PhysicsWorld`
raycast'iyle sınar. Raycast'ler update başına `AiWorldDesc::maxSightRaysPerUpdate` ile sınırlıdır ve sırayla
dağıtılır, böylece her algılayıcı sırasını alır; bu update'te sırası gelmeyen son sonucunu korur.
`reportNoise()` ile bildirilen gürültüler bir sonraki update'te `radius` çarpı `hearing` içinde duyulur.
Algılanan şey `memorySeconds` boyunca hatırlanır; `perceived()` onu en yakın önce listeler. Hedef şu an
algılanan en yakın şeydir, yoksa en yeni hatıradır. `targetKey` ile
`{"key": "target", "compare": "IsSet", "abort": "LowerPriority"}` gibi bir guard, bir hedef görülür görülmez
gezinmeyi keser.

### Stimuli

`AiStimulus` bir Entity'yi algılayıcıların gördüğü bir şey yapar: `team` (`kMaxAiTeams`, 32'nin altında),
`offset` (bakılan nokta, Entity'nin konumuna göre) ve `visible` (saklanırken false).

Geometri `illusion/ai/Perception.hpp` içinde saf fonksiyonlar olarak durur:

- `inSightCone(eye, forward, radius, angle, point)`, `point` `eye`'ın `radius` içinde ve birim `forward`
  çevresindeki tam açısı `angle` olan koninin içindeyse doğrudur (radyan; 2 pi ya da fazlası her yönü
  görür). İki sınır da dahildir ve gözün kendisi içeridedir. Görüş hattını denetlemez.
- `hearsNoise(listener, hearing, noise)`, gürültü `radius` çarpı `hearing` içindeyse doğrudur (dahil).
  `hearing` 0 iken hiçbir şey duyulmaz.

### In a game session

`GameSession` bir `AiWorld`'e sahiptir (`ai()`). AI adımı Saves'ten sonra ve Navigation'dan önce çalışır,
böylece bir tick'in hareketleri aynı update'te hedef olur; sabit adımlarla önce navmesh'leri ve tree
yüklemelerini bekler. Tree'ler yalnızca oturum başlatılmışken tick'lenir; `drawDebug()` görüş konilerini,
görüleni, hatıraları ve gürültüleri çizer.

## Bir modülden

Bir oyun modülü `AiService`'i `context.service<illusion::AiService>()` ile bulur (`illusion_add_module`'da
`USES ai`). Tree'nin okuduğu blackboard key'lerini yazar, tree'nin istediği action'ları yürütür ve noise
bildirir. [Scripting API referansı](../reference/scripting-api.md#aiservice) her fonksiyonu ve hatalarını
listeler.

## Performance

Geliştirme masaüstünde ölçüldü: 1.000 tree'yi bir kez tick'lemek 33 µs sürer (Benchmark `ai/tick-1000`,
bütçe 250 µs). 50 hareketli uyaran arasında 200 algılayıcı ve 256 görüş raycast'iyle bir `AiWorld`
update'i yaklaşık 0,13 ms (`ai/perception-200`, bütçe 0,75 ms; ADR 0062 1 ms'yi hedefler), 30 node'luk bir
tree çalıştıran 1.000 agent'lık bir update yaklaşık 0,19 ms sürer (`ai/world-1000`, bütçe 1,3 ms); ikisi de
Job System üzerinde.
