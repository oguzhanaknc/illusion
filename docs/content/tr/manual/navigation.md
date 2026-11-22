# Navigasyon

Navigasyon bir karakterin zemin üzerinde yol bulmasını sağlar. illusion, bir seviyenin collider'larından
Recast Navigation ile bir navigasyon mesh'i (navmesh) üretir ([ADR 0062](../architecture/decisions.md)) ve
onu bir `.navmesh` dosyasında saklar.

!!! note "Bugün neler var"
    Bu sayfa kaydedilen Component'leri, bake işlemini ve çalışma zamanı navmesh'leri, sorguları ve crowd'u
    olan `NavWorld`'ü anlatır. Oyun modülleri için `NavService` (NAV-1d) ve dokümantasyonlu editör komutları
    (NAV-1e) sonraki paketlerle gelir.

## Surfaces

Bir `NavMeshSurface` tek bir navmesh'tir: bir agent türü için yürünebilir zemin; Entity'nin konumu
etrafındaki bir kutunun içindeki collider'lardan üretilir (dönüş yok sayılır).

| Alan | Anlamı |
| --- | --- |
| `size` | Kutunun tam boyutu, m cinsinden |
| `agentType` | Onu kullanan agent'lar, [0, 8) aralığında |
| `agentRadius` | Duvarlardan boşluk, m |
| `agentHeight` | Gereken baş boşluğu, m |
| `agentMaxClimb` | Çıkılan en yüksek basamak, m |
| `agentMaxSlope` | Yürünebilir en dik eğim, radyan (derece olarak gösterilir, varsayılan 50°) |
| `build` | Aşağıdaki bake ayarları |
| `layerMask` | Okuduğu collider'ların fizik katmanları (bit i = katman i) |
| `areaCosts` | 16 alanın her birinde bir metre yürümenin maliyet çarpanı, en az 1 |
| `data` | Bake edilmiş `.navmesh` dosyası; yoksa World yüklenirken bake edilir |
| `rebuildAtRuntime` | Oyun çalışırken değişen kaynakların tile'larını yeniden üret |

Bake, kutunun içindeki, katmanı `layerMask`'te olan ve gövdesi Static ya da Kinematic olan (ya da hiç
RigidBody'si olmayan) her Collider'ın üçgenlerini toplar. Dynamic gövdeler zemin sayılmaz. `Inactive`
tag'li Entity'ler atlanır. Convex hull ve Mesh collider'ları için Mesh'in yüklenmiş olması gerekir; bake
atlamak zorunda kaldıklarını sayar.

## Settings

`NavMeshSurface::build` Recast ayarlarını tutar. Hücreler (cell), bake'in çalıştığı voxel'lerdir.

| Alan | Varsayılan | Anlamı |
| --- | --- | --- |
| `cellSize` | 0.15 | Yatay voxel boyutu, m |
| `cellHeight` | 0.1 | Dikey voxel boyutu, m |
| `tileSize` | 64 | Hücre cinsinden tile genişliği, [16, 1024] aralığında |
| `minRegionArea` | 1 | Daha küçük adalar atılır, m² |
| `mergeRegionArea` | 4 | Daha küçük bölgeler komşularına katılır, m² |
| `maxEdgeLength` | 12 | Sınır boyunca en uzun poligon kenarı, m; 0 sınırsız |
| `maxSimplificationError` | 1.3 | Sadeleştirilmiş sınırın voxel'lerden en büyük uzaklığı, hücre |
| `detailSampleDistance` | 6 | Detay mesh'inin yükseklik örneklerinin aralığı, hücre; 0 hiç |
| `detailSampleMaxError` | 1 | Detay mesh'inin en büyük yükseklik hatası, hücre |

Daha küçük hücreler geometriyi daha yakından izler ve daha çok zaman ile bellek harcar. Aralık dışı bir
değer bake'in başarısız olmasına ve ayarın adının bildirilmesine yol açar.

## Areas

Bir `NavArea`, Entity'nin Collider'ının üçgenlerini ya da Entity etrafındaki bir kutuyu [0, 16)
aralığında bir alan türüyle işaretler. Alan 0 varsayılan yürünebilir zemindir. Agent'lar ucuz alanları
(`areaCosts`) tercih eder ve bazılarıyla sınırlanabilir (`NavAgent::areaMask`).

| Alan | Anlamı |
| --- | --- |
| `area` | Alan türü |
| `walkable` | False, üçgenleri ya da kutuyu navmesh'ten keser |
| `volume` | Collider yerine `size` kutusunu kullan (tam boyut, m; yalnızca +y etrafında dönüş) |

## Links

Bir `NavLink`, Entity'nin konumundan `end`'e (Entity'nin yerel uzayında) giden bir off-mesh bağlantıdır;
agent türü `agentTypeMask`'te olan surface'lerin navmesh'ine bake edilir.

| Alan | Anlamı |
| --- | --- |
| `kind` | `Jump` (parabol), `Ladder`, `Drop`, `Door` ya da `Custom` |
| `bidirectional` | Bitişten başlangıca da geçilebilir |
| `radius` | Uçların navmesh'e yapıştığı mesafe, m |
| `height` | Jump: parabolün yüksek uca göre yüksekliği, m |
| `seconds` | Geçiş süresi; 0 agent'ın hızından türetilir |
| `area` | Bağlantının alan türü |
| `agentTypeMask` | Navmesh'i onu alan agent türleri (bit i = tür i) |

## Obstacles

Bir `NavObstacle`, agent'ların kaçındığı bir şeydir. Bir kutu (`size`, m) ya da dik bir silindirdir
(`radius` ve `height`, m). `carve` ayarlandığında, `moveThreshold` (m) kadardan fazla hareket edip
`settleSeconds` süre durduktan sonra, `rebuildAtRuntime`'lı surface'lerde kapladığı tile'lardan da
kesilir. Bake, carve eden bir obstacle'ı yürünemez bir alan hacmi gibi ele alır.

## Agents

Bir `NavAgent`, `agentType`'ının navmesh'inde `destination`'a yürür. `NavWorld` ona ilk update'inde bir
crowd slot'u verir ve çalışma zamanı durumu `NavAgentState`'i ekler.

| Alan | Anlamı |
| --- | --- |
| `radius`, `height` | Kaçınma boyutu, m |
| `maxSpeed` | m/s |
| `maxAcceleration` | m/s² |
| `turnSpeed` | Yönelişin dönüşü, rad/s |
| `stoppingDistance` | Vardı sayılan hedefe uzaklık, m |
| `avoidance` | `None`, `Low`, `Medium` ya da `High` |
| `priority` | [0, 99] aralığında; düşük değerlerden yüksek olanlar kaçınır |
| `separationWeight` | Komşulardan ayrı durma gücü |
| `areaMask` | Yürüyebildiği alanlar (bit i = alan i) |
| `drive` | Onu kim hareket ettirir: `Character`, `Transform`, `RootMotion` ya da `None` |
| `faceMovement` | Hareket yönüne doğru +y etrafında dön |
| `speedParameter` | Animasyon grafının hızına ayarlanan float parametresi; boşsa yok |
| `destination` | Dünya uzayında gittiği yer; yoksa yerinde durur |

### Drive'lar

`drive`, agent'ı kimin hareket ettirdiğini söyler:

| Drive | Onu kim hareket ettirir |
| --- | --- |
| `Character` | `CharacterController`'ı. Navigasyon yalnızca `NavAgentState::velocity`'yi yazar; oyun oturumu yatay kısmını fiziğe verir ve dikey hızı korur. Sonraki update fiziğin karakteri koyduğu yeri okur; böylece çarpışmalar, basamaklar ve eğimler korunur. |
| `Transform` | Navigasyon: Transform'u navmesh yüzeyinde hareket ettirir. Fiziksiz crowd'lar için. |
| `RootMotion` | Animator'ün root motion'ı. Navigasyon Entity'yi yoluna doğru döndürür ve hızını ayarlar. |
| `None` | Oyun kodu. Navigasyon yolu, durumu ve istenen hızı tutar. |

Root motion ötelemesi `Character` ve `Transform` agent'larına uygulanmaz. `faceMovement`, Entity'nin +z
eksenini `turnSpeed` ile harekete doğru döndürür; `RootMotion` agent'ları her zaman döner.

### Crowd

`advance` ile her update şu aşamaları çalıştırır:

1. Slot'lar ve konumlar World'ün thread'inde eşitlenir; değişen hedefler bir yol ister.
2. En fazla `maxPathRequestsPerUpdate` yol araması slot sırasıyla başlar. Her biri bitene kadar update
   başına `maxPathIterations` A* adımıyla bir Job olarak çalışır.
3. Agent'ların update başındaki konumlarından bir yakınlık ızgarası kurulur.
4. Agent başına Job'lar: yol koridoru agent'ı izler, ardından köşeler, yakındaki duvarlar, en fazla
   `maxNeighbours` komşu, istenen hız, ayrışma ve engelden kaçınma gelir.
5. Agent başına Job'lar: ivme, hareket ve ayrışma (`Transform`), link geçişi.
6. `NavAgentState`, Transform'lar ve olaylar World'ün thread'inde Entity sırasıyla yazılır.

Her Job aşaması update başındaki durumu okur ve yalnızca kendi agent'ını yazar; bu yüzden sabit adımlı
bir çalışma her Worker sayısında aynı sonucu verir. Bir agent, `priority` değeri kendisininkinden yüksek
olmayan komşulardan ve yakınındaki her `NavObstacle`'dan kaçınır. `Inactive` etiketli Entity'ler
slot'larını korur ve hareket etmez. `advance` olmadan (Play modu dışındaki editör) agent'lar slot ve durum
alır ama hareket etmez.

Hedefe ulaşamayan bir yol kısmidir: agent yolun sonuna yürür ve durumu `Partial` olur. Navmesh'ten
yarıçapı artı 1 m'den uzak bir agent `OffMesh` olur ve geri döndüğünde yolunu yeniden arar. Vardıktan
sonra itilen bir agent geri yürür.

### Link geçişi

Bir agent'ın yolu bir `NavLink`'e ulaştığında navigasyon Transform'unu link boyunca taşır (fizik bir
karakteri peşinden ışınlar): `Jump` için tepesi yüksek ucun `height` kadar üstünde olan bir parabol,
`Ladder` için önce dikey sonra yatay (tırmanış alçak uçta olur), diğerleri için düz bir çizgi. Geçiş
`seconds` sürer; 0 ise link'in uzunluğunun `maxSpeed`'e bölümü kadar. Bu sırada `NavAgentState::link`
link'in Entity'sidir.

### Durum ve olaylar

| `NavAgentStatus` | Anlamı |
| --- | --- |
| `Idle` | Hedef yok |
| `Pending` | Bir yolu ya da navmesh'inin yüklenmesini bekliyor |
| `Moving` | Bir yolu izliyor |
| `Partial` | Hedefe en yakın ulaşılabilir noktaya giden bir yolu izliyor |
| `Arrived` | Hedefin `stoppingDistance` yakınında |
| `Failed` | Yol yok: hedef navmesh üzerinde değil ya da hiçbir yol ona yaklaştırmıyor |
| `OffMesh` | Yönlendirilemeyecek kadar navmesh'ten uzak; döndüğünde yeniden arar |

`events()` son update'in olaylarını Entity sırasıyla döndürür: `Arrived`, `PathFailed`, `LinkEntered` ve
`LinkExited` (link'in Entity'si ve türüyle). En fazla `maxEvents` tutulur; diğerleri
`NavStatistics::droppedEvents`'te sayılır.

```cpp
navigation.setDestination(guard, door); // saved in NavAgent::destination
navigation.update(step, &jobs, true);
for (const illusion::NavEvent& event : navigation.events())
{
    if (event.type == illusion::NavEventType::LinkEntered && event.kind == illusion::NavLinkKind::Door)
    {
        openDoor(event.link);
    }
}
navigation.stop(guard);             // slows to a stop
navigation.warp(guard, spawnPoint); // moves at once and searches the path again
```

`findSlot()` bir agent'ın slot'unu, `pathCorners(slot)` önündeki köşeleri döndürür. Kayıtlı bir oyun
yalnızca `destination`'ı geri getirir: agent yüklemeden sonra yolunu yeniden arar.

### Oyun oturumunda

`GameSession` bir `NavWorld`'e sahiptir (`navigation()`). Navigation adımı Saves'ten sonra ve
Animation'dan önce çalışır: `NavWorld::update()` (sabit adımlarla önce bake ve yüklemeleri bekler),
ardından oturum `Character` agent'larına hızlarını verir ve `speedParameter`'ları ayarlar. Crowd yalnızca
oturum başlatılmışken ilerler. AI adımı hemen ondan önce çalışır: behaviour tree'lerin hareketleri aynı
update'te agent'ların hedefleri olur.

### Bir modülden

Bir oyun modülü navigasyona [`NavService`](../reference/scripting-api.md#navservice) ile (`USES nav`) ulaşır:
`NavWorld`'ün sorgularının aynıları, bir agent'ın Entity'si için `setDestination()`, `stop()` ve `warp()`,
`status()` ve `remainingDistance()`, ve son update'in olayları için `pollEvents()`. Her çağrı assert etmek
yerine `NavError` taşıyan bir `Expected` döndürür.

```cpp
#include <illusion/core/Module.hpp>
#include <illusion/nav/NavService.hpp>

#include <array>

void update(illusion::ModuleContext& context, illusion::f64)
{
    illusion::NavService* nav = context.service<illusion::NavService>();
    if (nav == nullptr)
    {
        return; // this World was attached without navigation
    }
    if (needsOrders)
    {
        const illusion::Expected<void, illusion::NavError> sent = nav->setDestination(guard, door);
        if (!sent)
        {
            ILL_LOG_INFO("Game", "guard: {}", illusion::navErrorName(sent.error()));
        }
        needsOrders = false;
    }
    if (nav->status(guard).valueOr(illusion::NavAgentStatus::Idle) == illusion::NavAgentStatus::Arrived)
    {
        openDoor();
    }
    std::array<illusion::NavEvent, 8> events{};
    const illusion::u32 count = nav->pollEvents(events);
    for (illusion::u32 i = 0; i < count && i < events.size(); ++i)
    {
        if (events[i].type == illusion::NavEventType::PathFailed)
        {
            giveUp(events[i].agent);
        }
    }
}
```

## Baking

Bake, `illusion/nav/NavBuild.hpp` içindeki iki saf fonksiyondur:

```cpp
auto geometry = illusion::collectNavGeometry(world, surface, findMesh);
if (geometry)
{
    auto data = illusion::bakeNavMesh(*geometry, &jobs);
    if (data)
    {
        writeFile("assets:/navigation/forest.navmesh", illusion::encodeNavMeshData(*data));
    }
}
```

`collectNavGeometry()` üçgenleri, alanları, alan hacimlerini, link'leri ve carve eden obstacle'ları dünya
uzayında toplar. `findMesh`, bir convex hull ya da Mesh Collider'ın Mesh'ini döndürür; yüklenmediyse null.

`bakeNavMesh()` surface'i `tileSize` hücre genişliğinde tile'lardan oluşan bir ızgara olarak üretir; tile
başına bir Job. Baytlar her Worker sayısında aynıdır ve aynı girdi için her çalıştırmada aynıdır.

Her tile, kendisiyle ve sınırıyla örtüşen girdinin ve ayarların bir hash'ini saklar. Bir değişiklikten
sonra `rebakeStaleTiles()` hash'leri karşılaştırır ve yalnızca hash'i farklı olan tile'ları yeniden üretir
(ızgara ya da ayarlar değiştiyse hepsini). Tek bir kutuyu taşımak yalnızca dokunduğu tile'ları değiştirir.

## `.navmesh` dosyası

`encodeNavMeshData()` dosyayı yazar, `decodeNavMeshData()` okur. Dosya bir magic, kapsayıcı sürümü
(`kNavMeshFormatVersion`, şimdi 1) ve Detour tile sürümünü, agent profilini ve ayarları, her tile'ın
verisi ve hash'iyle tile ızgarasını, bake edilmiş link'leri ve bir xxhash64 sağlama toplamı içerir. Okuma
şu hatalardan biriyle başarısız olur:

| Hata | Anlamı |
| --- | --- |
| `NotNavMesh` | Magic yok |
| `Version` | Başka bir kapsayıcı sürümü |
| `DetourVersion` | Başka bir Detour tile sürümüyle bake edilmiş: yeniden bake edin |
| `Truncated` | Dosya erken bitiyor ya da bir boyut alanı aralık dışı |
| `Checksum` | Sağlama toplamı uyuşmuyor |
| `Invalid` | Bir değer aralık dışı (tile koordinatları, ayarlar, yinelenen tile) |

## Editor'de

Editor, navmesh'leri dört [Editor commands](../reference/editor-commands.md) ile bake eder ve inceler;
bunları insanlar (command palette) ve AI agent'lar ortak kullanır. Editor, düzenlenen sahne üzerinde, her
surface'in navmesh'ini yükleyen ya da bake eden ve hiç ilerletilmeyen bir `NavWorld` tutar: agent'ları
hareket etmez.

`nav.bake` bir `NavMeshSurface`'i (`surface`) ya da sahnedeki her surface'i bake eder ve
`NavMeshSurface::data` alanını yazdığı `.navmesh` dosyasına ayarlar. `data`'sı olmayan bir surface
`assets:/navigation/<entity adı>.navmesh` dosyasını ya da çağrının `path` değerini alır. Komut asenkrondur,
çünkü önce Mesh Collider'ların model'leri yüklenir.

```json
{"method":"nav.bake","params":{"surface":"/Nav Surface"}}
```

- **Yalnızca değişen tile'lar.** Dosya varsa yalnızca girdi hash'i farklı olan tile'lar yeniden üretilir
  (`rebakeStaleTiles()`); `force` hepsini üretir. Sonuç, her surface için tile'ları ve yeniden üretilen
  tile'ları listeler.
- **İki kez bake etmek hiçbir şeyi değiştirmez.** Aynı girdi için baytlar aynıdır. Baytları değişmeyen bir
  dosya yazılmaz ve ikinci bake hiçbir tile'ı yeniden üretmez.
- **Geri alma.** Önce dosya yazılır, sonra `data` geri alınabilir bir adımla ayarlanır. Adımı geri almak
  `data`'yı eski haline getirir ve dosyayı bırakır.
- **Hatalar.** `NavMeshSurface` olmayan bir hedef, `assets:/` içinde `.navmesh` dosyası olmayan bir `path`
  ya da aynı dosyayı yazacak iki surface, hiçbir şey bake edilmeden önce hata verir.

`nav.status`, surface'leri agent türleri, veri dosyaları ve navmesh'lerinin tam olup olmadığıyla listeler;
tile, polygon, bekleyen ve eski tile'ları ve başarısız surface'leri sayar. `nav.path` bir agent türü için iki
nokta arasında yol bulur ve köşe noktalarını, uzunluğunu ve kısmi olup olmadığını döndürür. Navmesh'e 2 m'den
(yukarı ve aşağı 4 m) uzak bir nokta, `/from` ya da `/to` adını veren bir hata verir. Bir seviyenin bağlı
olduğunu denetlemek için kullanın.

```json
{"method":"nav.path","params":{"from":[24,0,-6],"to":[35,1,5],"agentType":1}}
```

`nav.debug`, navigasyonu `drawDebug()` ile Scene görünümlerinde gösterir: alana göre renklenen navmesh
polygon'ları (`navMesh`), tile sınırları (`tiles`), link'ler, agent'lar, yollar ve obstacle'lar. Editor
durumudur, geri alınamaz ve vermediğiniz bayraklar değerini korur.

```json
{"method":"nav.debug","params":{"enabled":true,"tiles":true}}
```

`showcase` projesi navmesh'ini `build.jsonl` içindeki bir `nav.bake` adımıyla bake eder; dosyayı yeniden
oynatmak `assets/navigation/yard.navmesh` dosyasını yeniden üretir.

## Çalışma zamanı

`NavWorld` (`illusion/nav/NavWorld.hpp`) her `NavMeshSurface` için döşemeli (tiled) bir navmesh tutar. World
ve `AssetStreamer` ile oluşturun, her adımda bir kez `update()` çağırın:

```cpp
illusion::NavWorld navigation(world, &streamer);
while (running)
{
    streamer.update();
    navigation.update(step, &jobs, true);
}
navigation.waitIdle(&jobs);
```

- **Yükleme.** `update()` her surface'in `data` dosyasını streamer üzerinden yükler (decoder
  `illusion.navmesh`).
- **Bake.** `data`'sı olmayan ya da dosyası bulunmayan veya reddedilen bir surface bunun yerine bake edilir.
  Eksik veya reddedilen dosya tek bir log satırı yazar ve `NavStatistics::failures` içinde sayılır. Streamer
  yoksa surface'ler her zaman bake edilir.
- **Eskimiş tile'lar.** Yüklenen bir tile'ın girdisi hash'iyle artık uyuşmuyorsa, `rebuildAtRuntime` açıksa
  tile yeniden üretilir. Kapalıysa `NavStatistics::staleTiles` içinde sayılır ve bir kez loglanır.
- **Çalışırken değişiklikler.** `rebuildAtRuntime` açıkken, değişen Transform, Collider, RigidBody, NavArea
  veya NavLink'in ve durulmuş bir carve eden NavObstacle'ın dokunduğu tile'lar yeniden üretilir. Bir Mesh
  Collider'ın kullandığı model dosyasındaki değişiklik yeniden bake başlatmaz.
- **`invalidate(bounds)`** bir `Aabb` ile kesişen tile'ları eskimiş işaretler. World'ün göremediği bir girdi
  değişikliğinde kullanın.
- **Canlı yeniden yükleme.** Bir `.navmesh` dosyasını kaydetmek surface'inin tile'larını değiştirir.
- **Agent türü başına bir surface.** Birden fazla surface aynı agent türünü kullanırsa en düşük Entity
  indeksli olan kazanır; diğerleri bir kez loglanır ve yok sayılır.
- **Build'ler.** Arka planda aynı anda en fazla `NavWorldDesc::maxTileBuildsInFlight` tile üretilir. Biten
  tile'lar bir `update()`'in başında, build'lerin başlama sırasıyla yerine konur.
- **`waitIdle(&jobs)`**, hiçbir surface verisini ya da tile'larını beklemeyene kadar
  `update(0, jobs, false)` çağırır. `isReady(agentType)` bir navmesh'in tüm tile'lara sahip olup olmadığını
  söyler.

`statistics()` `surfaces`, `tiles`, `polygons`, `pendingTiles`, `staleTiles`, `tileBuilds`, `failures`
ve crowd için `agents`, `moving`, `pathSearches` ve `droppedEvents` değerlerini verir.

## Sorgular

Sorgular `const`'tır, World'ün thread'inde çalışır ve hiç allocation yapmaz. Her sorgu bir `NavQueryFilter`
alır:

| Alan | Varsayılan | Anlamı |
| --- | --- | --- |
| `agentType` | 0 | Sorulan navmesh |
| `areaMask` | tüm alanlar | Sorgunun geçebileceği alanlar (bit i = alan i) |
| `searchExtents` | (2, 4, 2) | Bir noktanın poligonu için aranan kutunun yarı boyutu, m cinsinden |

```cpp
illusion::NavQueryFilter filter;
filter.agentType = 1;
filter.areaMask = ~(1u << 2); // keep out of area 2

std::array<illusion::Vec3, 32> corners;
auto path = navigation.findPath(from, to, corners, filter);
if (path)
{
    // corners[0 .. path->count) is the route; path->total, path->length and path->partial describe all of it
}
else if (path.error() == illusion::NavError::NoPath)
{
    // the points are on the navmesh but nothing reachable is nearer to `to` than `from`
}
```

`findPath()` en kısa yolun köşe noktalarını yazar; ilki navmesh üzerindeki başlangıçtır. Hedefe
ulaşılamıyorsa yol kısmidir (partial): hedefe en yakın ulaşılabilir noktada biter ve `partial` true olur.
`NoPath` yalnızca ulaşılabilir hiçbir şey hedefe başlangıçtan daha yakın değilse döner. Yoldan küçük bir
çıktı span'i `count` kadar nokta verir; `total` ve `length` yine tüm yolu anlatır.

```cpp
auto ground = navigation.nearestPoint(position, filter);
auto sight = navigation.raycast(from, to, filter);
if (sight && sight->hit)
{
    // sight->point, sight->normal and sight->fraction describe the wall
}
auto spot = navigation.randomPoint(center, 5.0f, 1234, filter);
```

- `nearestPoint()`, `searchExtents` içinde konuma en yakın navmesh noktasını verir.
- `raycast()` navmesh yüzeyinde yürür ve karşılaştığı ilk duvarı bildirir: `hit`, yürüyüşün durduğu `point`,
  duvarın yatay `normal`'i ve yolun yürünen kısmı `fraction`.
- `randomPoint()`, merkezden ulaşılabilir ve yarıçap içinde bir navmesh noktası verir. Aynı seed aynı noktayı
  verir.

`areaCosts` ve `areaMask` sorgu anında uygulanır: `areaCosts` değiştirmek yeniden build gerektirmez.

### Hatalar

| `NavError` | Anlamı |
| --- | --- |
| `NoAgent` | Entity'de `NavAgent` yok ya da Entity canlı değil |
| `NoNavMesh` | O agent türünün navmesh'i henüz yüklenmedi |
| `OffMesh` | Bir noktanın `searchExtents` içinde navmesh poligonu yok |
| `NoPath` | Noktalar navmesh üzerinde ama onları bağlayan bir şey yok |
| `InvalidValue` | Bir değer sonlu değil ya da aralık dışı (agent türü, negatif yarıçap, boş span) |

### Debug çizimi

`drawDebug()` çizgileri bir `NavDebugSink`'e gönderir; editörün viewport'u onu kendi debug draw'ına uyarlar.

```cpp
struct LineSink : illusion::NavDebugSink
{
    void line(const illusion::Vec3& a, const illusion::Vec3& b, const illusion::Vec4& color) override;
};

illusion::NavDebugOptions options;
options.tiles = true;
options.agentTypeMask = 1u << 1;
LineSink sink;
navigation.drawDebug(options, sink);
```

`agents` her agent'ın çemberini, yüksekliğini ve hızını, `paths` ise önündeki köşeleri çizer.

## Performans

Geliştirme masaüstünde ölçüldü:

- 10.000 üçgenli bir sahnenin 64 x 64 hücrelik tek bir tile'ını bake etmek 2,8 ms sürer (Benchmark
  `nav/bake-tile`, bütçe 20 ms).
- 256 m x 256 m'yi, 729 tile ile Job System üzerinde paralel bake etmek 99 ms sürer (Benchmark
  `nav/bake-256m`, bütçe 600 ms).
- 1.000 yol bulmak 13 ms sürer (Benchmark `nav/find-path-1000`: 729 tile ve 225 duvarlı sahnede ortalama
  96 m uzunluk, bütçe 80 ms).
- Yürüyen 500 agent'lık bir crowd'u Job System ile güncellemek 0,3 ms sürer (Benchmark `nav/crowd-500`,
  bütçe 2 ms).
