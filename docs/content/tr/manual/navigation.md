# Navigasyon

Navigasyon bir karakterin zemin üzerinde yol bulmasını sağlar. illusion, bir seviyenin collider'larından
Recast Navigation ile bir navigasyon mesh'i (navmesh) üretir ([ADR 0062](../architecture/decisions.md)) ve
onu bir `.navmesh` dosyasında saklar.

!!! note "Bugün neler var"
    Bu sayfa kaydedilen Component'leri ve bake işlemini anlatır. Navmesh üzerinde yürüyen agent'lar, yol
    sorguları, kalabalık (crowd), scripting API'si ve editör komutları sonraki paketlerle gelir: sorgulu
    `NavWorld` (NAV-1b), crowd ve `NavAgent` hareketi (NAV-1c), oyun modülleri için `NavService` (NAV-1d)
    ve dokümantasyonlu editör komutları (NAV-1e). O zamana kadar `illusion/nav/NavBuild.hpp` içindeki C++
    fonksiyonlarıyla bake edebilirsiniz.

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

Bir `NavAgent`, `agentType`'ının navmesh'inde `destination`'a yürür. Component ve çalışma zamanı durumu
`NavAgentState` şimdiden kayıtlıdır, ama henüz hiçbir şey agent'ları hareket ettirmez (NAV-1c).

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

## Performans

Geliştirme masaüstünde ölçüldü:

- 10.000 üçgenli bir sahnenin 64 x 64 hücrelik tek bir tile'ını bake etmek 2,8 ms sürer (Benchmark
  `nav/bake-tile`, bütçe 20 ms).
- 256 m x 256 m'yi, 729 tile ile Job System üzerinde paralel bake etmek 99 ms sürer (Benchmark
  `nav/bake-256m`, bütçe 600 ms).
