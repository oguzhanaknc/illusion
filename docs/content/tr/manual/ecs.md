# ECS

illusion oyun durumunu bir Entity Component System (ECS) içinde saklar. **Entity** hafif bir
tanıtıcıdır (handle), **Component** bir Entity'ye eklenmiş düz veridir; System'ler ise belirli
Component'lere sahip tüm Entity'ler üzerinde **Query** çalıştıran fonksiyonlardır.

Header: `#include <illusion/ecs/World.hpp>`

## Component'ler

Taşınabilir (movable) her C++ tipi Component olabilir. Component'leri küçük ve davranıştan arındırılmış
tutun:

```cpp
namespace game
{
struct Position
{
    float x = 0, y = 0, z = 0;
};

struct Velocity
{
    float x = 0, y = 0, z = 0;
};

struct Player // empty type: a tag
{
};
} // namespace game
```

Kurallar:

- Move constructor ve destructor exception fırlatmamalıdır.
- Her Component tipinin benzersiz, tam nitelikli (fully qualified) bir adı olmalıdır. Component'leri
  anonim namespace'lerde veya fonksiyonların içinde tanımlamayın.
- Boş tipler **tag**'dir: bellek kaplamazlar ve filtreleme için kullanılırlar.
- 64 byte'a kadar alignment desteklenir.

Component tipleri ilk kullanıldıklarında otomatik olarak kaydedilir. Component'leri sahne
dosyalarına kaydetmek için onları yansıtın ([Reflection](reflection.md) ve [Sahneler](scenes.md) sayfalarına bakın).

## Entity'ler

```cpp
illusion::World world;

illusion::Entity player = world.create(game::Position{}, game::Velocity{1, 0, 0}, game::Player{});
illusion::Entity empty = world.create();

world.emplace<game::Velocity>(empty, 0.0f, 1.0f, 0.0f); // add, constructing in place
world.set(empty, game::Position{5, 0, 0});              // add or assign
game::Position& position = world.get<game::Position>(empty);
bool moving = world.has<game::Velocity>(empty);
world.remove<game::Velocity>(empty);

world.destroy(empty);
bool alive = world.isAlive(empty); // false: the handle is stale
```

Bir Entity handle'ı bir indeks ve bir generation içerir. Bir Entity yok edildiğinde yuvası yeni bir
generation ile yeniden kullanılır; böylece eski handle'lar `isAlive()` ile güvenle tespit edilir.

Handle'lar yalnızca onları oluşturan World ve süreç içinde geçerlidir. Kaydedilen veya sahne dosyaları ya
da editör gibi dışarıdan başvurulan Entity'ler ayrıca kalıcı bir id, yani `EntityGuid` Component'ini, ve
isteğe bağlı olarak bir `Name` taşır; bkz. [Entity id'leri](scenes.md#entity-idleri).

!!! warning "Referanslar kısa ömürlüdür"
    `get()`, `tryGet()` ve `emplace()` tarafından döndürülen referans ve pointer'lar bir sonraki yapısal
    değişiklikte (Entity oluşturma veya yok etme, Component ekleme veya çıkarma) geçersiz olur.

## Query'ler

Bir Query, listelenen Component'lerin tümüne sahip her Entity'yi ziyaret eder. Salt okunur erişimi
`const` ile belirtin:

```cpp
world.query<game::Position, const game::Velocity>().each(
    [](game::Position& position, const game::Velocity& velocity) {
        position.x += velocity.x;
        position.y += velocity.y;
        position.z += velocity.z;
    });
```

Çağrılabilir nesne ilk parametre olarak Entity'yi de alabilir: `[](illusion::Entity e, game::Position& p)`.

Filtreler, erişilmeyen Component'lere göre Entity seçer:

```cpp
auto players = world.query<game::Position>().with<game::Player>();
auto others = world.query<game::Position>().without<game::Player>();
illusion::u32 playerCount = players.count();
```

Eşleşen Archetype'lar World başına önbelleğe alınır; bu yüzden her karede Query oluşturmak bellek
ayırmaz.

### Chunk iterasyonu

`eachChunk()` size her seferinde bir Chunk'ın bitişik dizilerini verir; bu, SIMD ve derleyicinin otomatik
vektörleştirmesi için en uygun yapıdır:

```cpp
world.query<game::Position, const game::Velocity>().eachChunk(
    [](illusion::ChunkView<game::Position, const game::Velocity>& chunk) {
        std::span<game::Position> positions = chunk.get<game::Position>();
        std::span<const game::Velocity> velocities = chunk.get<const game::Velocity>();
        for (illusion::u32 i = 0; i < chunk.size(); ++i)
        {
            positions[i].x += velocities[i].x;
        }
    });
```

### Paralel iterasyon

`parallelEach()` Chunk'ları Job System üzerinde işler ve tüm Chunk'lar bittiğinde döner:

```cpp
illusion::JobSystem jobs;
world.query<game::Position, const game::Velocity>().parallelEach(
    jobs, [](game::Position& position, const game::Velocity& velocity) { position.x += velocity.x; });
```

Çağrılabilir nesne eşzamanlı çalışır; yalnızca kendisine verilen Component'lere ve thread'ler arasında
paylaşılması güvenli olan verilere dokunmalıdır.

C++ tipleri yerine `ComponentId`'lerle çalışan serializer'lar ve araçlar aynı Chunk'ları
`World::forEachMatching()` ile dolaşır; bkz. [Tipi silinmiş erişim](scenes.md#tipi-silinmis-erisim).

## İterasyon sırasında yapısal değişiklikler

Bir Query dolaşırken Entity oluşturmak veya yok etmek, Component eklemek veya çıkarmak yasaktır (Debug'da
bir assert tetiklenir). Değişiklikleri bir `CommandBuffer`'a kaydedin ve sonrasında uygulayın:

```cpp
illusion::CommandBuffer commands;
world.query<const game::Position>().each([&](illusion::Entity e, const game::Position& p) {
    if (p.y < -100.0f)
    {
        commands.destroy(e);
    }
});
commands.apply(world);
```

`CommandBuffer`; `destroy()`, `set()`, `remove<T>()` ve `create(components...)` destekler. Komutlar
kaydedildikleri sırayla çalışır. Buffer belleğini kullanımlar arasında korur; bu yüzden System başına
bir buffer'ı yeniden kullanmak kararlı durumda bellek ayırmaz.

## Yapısal sürüm

`World::structuralVersion()`, Entity kümesi veya bir Entity'nin Component kümesi her değiştiğinde bir
artar: bir Entity oluşturmak veya yok etmek, Entity'de olmayan bir Component eklemek, olan birini
çıkarmak ve boş olmayan bir World'ü temizlemek; doğrudan ya da bir `CommandBuffer` üzerinden. Component
değerlerini yazmak onu değiştirmez. Araçlar, önbelleklerini yalnızca World'ün düzeni değiştiğinde yeniden
oluşturmak için bunu kullanır:

```cpp
if (world.structuralVersion() != m_outlinerVersion)
{
    rebuildOutliner(world);
    m_outlinerVersion = world.structuralVersion();
}
```

`World::componentVersion<T>()` aynı fikri tek bir Component türü için uygular: bir Entity `T` kazandığında
veya kaybettiğinde ve kod `world.markChanged<T>()` ile `T` değerlerini yazdığını bildirdiğinde artar.
Aşağıdaki `HierarchyIndex` gibi tek bir türden oluşturulan önbellekler, ilgisiz değişikliklerin kendilerini
geçersiz kılmaması için bunu kullanır.

## Transform hiyerarşisi

Header: `#include <illusion/ecs/Hierarchy.hpp>`

`Hierarchy` Component'i olan bir Entity'nin bir ebeveyni vardır (ya da bir köktür) ve kardeşleri arasında
bir sırası vardır. `LocalTransform`'u onu ebeveynine göre yerleştirir. `Transform`, her System'in okuduğu
dünya uzayındaki yerleşim olarak kalır: `updateTransformHierarchy()` onu her karede bir kez
`LocalTransform`'dan türetir.

```cpp
illusion::World world;
illusion::HierarchyIndex hierarchy;

const illusion::Entity car = world.create(illusion::Name{"Car"}, illusion::Transform{});
const illusion::Entity wheel = world.create(illusion::Name{"Wheel"}, illusion::Transform{});
if (auto attached = illusion::setParent(world, wheel, car); !attached)
{
    ILL_LOG_WARN("Game", "cannot attach: {}", illusion::hierarchyErrorMessage(attached.error()));
}

// Every frame, after game code and physics moved entities and before rendering:
world.get<illusion::Transform>(car).position.x += 1.0f;
illusion::updateTransformHierarchy(world, hierarchy, &jobs); // the wheel follows the car
```

- `setParent(child, parent, mode, index)`, `child`'ı `parent`'ın altına (null Entity onu kök yapar),
  kardeşleri arasında `index` konumuna taşır (`kLastSibling` sona ekler). `ParentMode::KeepWorld` çocuğu
  dünyadaki yerinde tutar ve `LocalTransform`'unu hesaplar; `KeepLocal` `LocalTransform`'u korur ve çocuğu
  yeni ebeveyniyle birlikte taşır. Eksik `Hierarchy`, `LocalTransform` veya `Transform` eklenir. Döngüler ve
  ölü Entity'ler bir `HierarchyError` ile reddedilir ve hiçbir şeyi değiştirmez.
- `detach()` bir Entity'yi kök yapar, `reorder()` onu kardeşleri arasında taşır ve `destroyRecursive()` onu
  tüm torunlarıyla birlikte yok eder. Kardeş sıraları sıkı kalır (0, 1, 2, ...).
- `parentOf()` ebeveyni döndürür. Bu fonksiyonlar `Hierarchy`'si olan Entity'leri tarar; bu yüzden her
  karede her Entity için değil, düzenleme ve oyun olayları için düşünülmüştür.
- Bir kök için doğruluk `Transform`'dadır ve `LocalTransform`'u (varsa) ona eşit tutulur. `Transform`'u
  olmayan bir ebeveyn birim dönüşüm sayılır.
- Çalışma zamanı etiketi `WorldSpaceAuthority` olan bir Entity, bir System'in yazdığı `Transform`'u korur ve
  `LocalTransform`'u ondan hesaplanır; böylece çocukları onu izler. Fizik bu etiketi dinamik gövdelere ve
  karakterlere ekler.
- Yerleşimler konum, dönüş ve ölçek olarak birleşir (`combine()`, tersi `relativeTo()`). Konumlar tamdır.
  Bir `Transform` kaymayı (shear) ifade edemez; bu yüzden düzgün olmayan ölçekli bir ebeveynin altındaki
  döndürülmüş bir çocuğun dönüşü ve ölçeği, matrislerinin çarpımına yalnızca yaklaşır.

`HierarchyIndex` çocuk listelerini, derinlik öncelikli ön sırayı (pre-order) ve derinlik seviyelerini tutar.
Yalnızca hiyerarşi değiştiğinde (`componentVersion<Hierarchy>()`) veya `Hierarchy`'si olmayan bir ebeveyn
öldüğünde yeniden oluşturulur; bu yüzden `update()` her karede ucuzdur ve `Hierarchy`'si olmayan
Entity'lerin hiçbir maliyeti yoktur. Ağacı dolaşmak için kullanın:

```cpp
hierarchy.update(world);
for (const illusion::Entity root : hierarchy.roots())
{
    hierarchy.depthFirst(root, [&](illusion::Entity entity, illusion::u32 depth) {
        drawOutlinerRow(entity, depth);
    });
}
```

Çocuklar önce `Hierarchy::order`'a, sonra `EntityGuid`'e göre sıralanır; `Hierarchy`'si olmayan kökler en
sona gelir. `Hierarchy` değerlerini bu fonksiyonlar yerine doğrudan yazan kod, ardından
`world.markChanged<illusion::Hierarchy>()` çağırmalıdır.

## Disabled ve Inactive

`setEnabled(world, entity, false)`, sahnelere kaydedilen `Disabled` etiketini ekler. Bir sonraki
`updateTransformHierarchy()` çağrısında Entity ve tüm torunları çalışma zamanı etiketi `Inactive`'i alır;
etkinleştirildiklerinde veya devre dışı alt ağaçtan çıkarıldıklarında onu yeniden kaybederler. Motor
System'leri Inactive Entity'leri atlar:

- renderer Inactive Mesh'leri, ışıkları ve gökyüzü ışıklarını çizmez;
- fizik gövdelerini, karakterlerini ve eklemlerini kaldırır ve etkin olduklarında güncel `Transform`'dan
  yeniden oluşturur (hızlar sıfırdan başlar);
- ses kaynaklarını durdurur; `playOnStart` olan kaynaklar etkin olduklarında yeniden başlar.

Oyun System'leri de onları atlamalıdır:

```cpp
world.query<Enemy, const illusion::Transform>().without<illusion::Inactive>().each(
    [](Enemy& enemy, const illusion::Transform& transform) { enemy.think(transform); });
```

`isEnabledInHierarchy()`, ebeveyn zincirini dolaşarak bir sonraki güncellemeden önce hemen yanıt verir.

## Nasıl çalışır

Tam olarak aynı Component tipi kümesine sahip Entity'ler bir **Archetype**'ı paylaşır. Bir Archetype,
Entity'lerini 16 KiB'lık **Chunk**'larda saklar; bir Chunk içinde her Component tipinin kendi sıkı
paketlenmiş dizisi vardır (structure of arrays). Bir Component eklemek veya çıkarmak Entity'yi başka bir
Archetype'a taşır. Bir Entity silindiğinde Archetype'ın son Entity'si boşluğa taşınır; böylece Chunk'lar
yoğun kalır.

Bu tasarımın gerekçesi için bkz. [ADR 0004](../architecture/decisions.md).

## Performans notları

- Çok sayıda `get()` çağrısı yerine Query'leri tercih edin: Query'ler bitişik bellek üzerinden akar.
- Her karede çok sayıda Entity'ye sık sık Component ekleyip çıkarmaktan kaçının; durum sık değişiyorsa
  bir tag veya bayrak alanı kullanın.
- Salt okunur Component'leri Query'lerde `const` olarak işaretleyin. Bu, niyeti belgeler ve ileride
  scheduler'ın System'leri paralel çalıştırmasına olanak tanıyacaktır.
