# Kayıtlı oyunlar

Kayıtlı oyun, oyuncu oynarken değişenleri saklar ve bir sonraki sefer bunları level'in üzerine geri koyar
(ADR 0058). Kalıcı olması gereken Entity'leri `Saved` etiketiyle işaretlersiniz; bir oyun modülü slot'ları
`SaveService` ile yazar, yükler ve siler. Slot dosyalarının checksum'ı vardır, bir çökme dosyayı yarım
bırakmayacak şekilde yazılır, bir yedek tutulur, bir görüntü taşıyabilir ve bir mağaza varsa mağazanın bulutuna
yansıtılır.

Header'lar: `#include <illusion/save/SaveService.hpp>` ve `#include <illusion/save/SaveComponents.hpp>`
(kütüphane `illusion::save`; modüller `USES save` kullanır)

```cmake
illusion_add_module(mygame SHARED SOURCES mygame.cpp USES runtime save)
```

## Saved etiketi

Inspector'da ya da kodda bir Entity'ye `Saved` Component'ini (`illusion.Saved`, boş bir etiket) ekleyin; kayıt o
Entity'yi **ve tüm alt Entity'lerini** içerir:

```cpp
illusion::registerSaveComponents(); // once, with the other Components
world.emplace<illusion::Saved>(player);
```

Kayıtlı bir Entity'nin çocuğunun kendi etiketine ihtiyacı yoktur. Kayıtlı alt ağaçların dışındaki hiçbir Entity
yazılmaz: level kendi sahne dosyasından gelir ve yükleme kaydı onun üzerine uygular.

## Neler kaydedilir, neler kaydedilmez

Bir kayıt, kayıtlı her Entity'nin bir sahne dosyasında bulunacak tüm Component'lerini içerir: `Transform`,
fizik gövdeleri, ışıklar ve `reflect<T>("mygame.Name")` ile yansıtılmış kendi Component'leriniz. Yüklemeden
sonra da kalması gereken oyun durumu bu yüzden kayıtlı bir Entity üzerindeki yansıtılmış bir Component'tir:

```cpp
struct Health
{
    float current = 100.0f;
};

illusion::reflect<Health>("mygame.Health").field("current", &Health::current);
```

Kaydın içermedikleri:

- Kayıtlı alt ağaçların dışındaki Entity'ler ve level'in kendisi.
- Runtime durumu: `reflectRuntime<T>()` ile yansıtılmış Component'ler, renderer'ın, fiziğin ve sesin runtime
  Component'leri (yüklemeden sonra yeniden türetilir) ve modülünüzün değişkenlerinin durumu.
- Dinamik gövdelerin hızları, canlı parçacıklar, çalan sesler ve süren UI geçişleri. Bunlar kayıtlı
  Component'lerin söylediği değerlerden yeniden başlar.

Yükleme kayıtlı kümeyi değiştirir: hem World'de hem slot'ta bulunan bir Entity kayıtlı değerleri alır, yalnızca
World'de olan yok edilir (kayıtlı oyunda yok edilmişti), yalnızca slot'ta olan yaratılır (kayıtlı oyunda
doğmuştu). Oyun önce doğru level'i yükler; hangi level olduğu metadata'nın `custom` metnine aittir.

Kayıtlı Component'ler sahne dosyaları gibi sürümlenir ve taşınır ([Reflection](reflection.md#surumler-ve-migrationlar)):
yeniden adlandırılmış bir alan yüklenir, oyunun artık sahip olmadığı bir türün Component'i log'da bir uyarıyla
atılır.

## Slot'lar ve metadata

Bir slot'un adı 1 ila 32 ASCII harf, rakam, `_` ve `-` karakterinden oluşur (`isValidSaveSlotName()`); kaydederken
verdiğiniz ve slot'u yüklemeden okuyabildiğiniz metadata'sı vardır:

| Alan | Anlamı |
| --- | --- |
| `title` | Oyuncu için metin, UTF-8, en çok 128 bayt |
| `playSeconds` | Oynanan süre, negatif olamaz |
| `gameVersion` | Sizin seçtiğiniz ve geri aldığınız bir sayı, yüklemeden sonraki kendi düzeltmeleriniz için |
| `custom` | Sizin tanımladığınız en çok 1024 bayt UTF-8, örneğin yüklenecek level |

Host `savedAt` (Unix epoch'tan beri saniye, UTC) ve engine'in sürümünü ekler. `SaveService::slots()` listeyi
ada göre sıralı olarak bellekten verir, bu yüzden her karede çağrılabilir; her kaydın bir `SaveSlotState`'i
vardır:

| Durum | Anlamı |
| --- | --- |
| `Ok` | Slot dosyası geçerli |
| `Backup` | Dosya yok ya da bozuk ve yedeği geçerli; yükleme yedeği okur |
| `Corrupt` | Ne dosya ne de yedek okunabiliyor |
| `NewerVersion` | Dosya container'ın daha yeni bir sürümüyle yazılmış ve yüklenemez |

## Kaydetme ve yükleme

`save()`, `load()` ve `remove()` bir istek kuyruğa alır ve hemen döner. Hemen yalnızca `InvalidName`,
`InvalidValue` (metadata çok uzun ya da UTF-8 değil) ya da `Unavailable` (kayıt dizini yok) ile başarısız olurlar.
`save()` World'ü onu çağıran update'in sonunda yakalar; dosya host'un save thread'inde kodlanır, denetlenir ve
yazılır, bu yüzden oyun hiçbir zaman diski beklemez.

```cpp
if (illusion::SaveService* saves = context.service<illusion::SaveService>())
{
    if (savePressed)
    {
        static_cast<void>(saves->save("slot1", {.title = "Chapter 2", .playSeconds = played, .gameVersion = 3}));
    }
    if (loadPressed && saves->slot("slot1").has_value())
    {
        static_cast<void>(saves->load("slot1"));
    }
}
```

İstekler çağrı sırasıyla çalışır; bu yüzden aynı slot için `save()` ardından `load()`, ilkinin yazdığını okur.

## Olaylar

Bir isteğin sonucu sonraki bir update'te `SaveEvent` olarak gelir. `pollEvents()` son update'in olaylarını
döndürür; bir update'teki her çağrı aynı olayları verir, bu yüzden bir modülün birkaç yeri onları
okuyabilir:

```cpp
std::array<illusion::SaveEvent, 8> events{};
const illusion::u32 count = std::min<illusion::u32>(saves->pollEvents(events), events.size());
for (const illusion::SaveEvent& event : std::span(events).first(count))
{
    switch (event.type)
    {
    case illusion::SaveEventType::Saved:
        break;
    case illusion::SaveEventType::Loaded:
    case illusion::SaveEventType::LoadedBackup:
        findMyEntitiesAgain();
        break;
    case illusion::SaveEventType::Removed:
        break;
    case illusion::SaveEventType::Failed:
        ILL_LOG_WARN("Game", "slot {} failed: {}", event.slot, illusion::saveErrorName(event.error));
        break;
    }
}
```

`Failed` bir `SaveError` taşır: `NotFound` (bu adda slot yok), `Corrupt`, `NewerVersion`, `TooLarge` (64 MiB'ın
üstünde bir payload), `IoError` ya da `InvalidScene`. Başarısız bir yükleme World'de hiçbir şeyi değiştirmez.
`busy()` bir istek olayını henüz teslim etmediği sürece true'dur.

## Entity handle'ları yüklemede değişir

Yükleme, her sahne yüklemesi gibi kayıtlı Entity'leri yok eder ve yeniden yaratır; bu yüzden modülünüzün bir
Component'te ya da değişkende tuttuğu `Entity` handle'ı `Loaded` ya da `LoadedBackup` olayından sonra ölüdür.
Entity'leri o noktada, ada ya da bir etiket Component'ine göre yeniden bulun. Kayıtlı alt ağaçların dışındaki
Entity'lerin handle'ları geçerli kalır. Bunların dışında olup `Entity` alanı kayıtlı bir Entity'yi gösteren bir
Entity de ölü bir handle tutar; bu yüzden böyle referansları kayıtlı alt ağaçların içinde tutun.

## Yedekler ve bozuk dosyalar

Slot dosyaları aynı dizinde geçici bir dosyaya yazılır, aygıta flush edilir ve slot'un üzerine yeniden
adlandırılır; bu yüzden bir çökme ya da elektrik kesintisi önceki kaydı bırakır, yarım bir dosya bırakmaz. Bir
slot değiştirilmeden önce geçerli dosyası `<slot>.save.bak` olur. Dosyanın checksum'ı vardır; magic numarası,
boyutu ya da checksum'ı yanlış olan dosya `Corrupt` sayılır ve asla uygulanmaz. Dosyası yok ya da bozuk olan bir
slot yüklenirken yedek okunur ve `LoadedBackup` bildirilir; böylece oyun oyuncuya söyleyebilir. Container'ın
daha yeni bir sürümünden gelen dosya `NewerVersion`'dır; saklanır, yedekle değiştirilmez ve yüklenmez.

Bir çökmenin yarıda kestiği yazmanın artık `*.tmp-*` dosyaları yok sayılır ve henüz silinmez.

## Slot görüntüleri

Kaydedilen bir slot bir görüntü alır: sunulan son kare 16:9'a kırpılıp 320 x 180'e ölçeklenir ve slot
dosyasının yanına `<slot>.png` olarak yazılır. `SaveSlotInfo::screenshot` onun sanal yoludur, slot'ta
görüntü yoksa boştur; bir `UiImage` onu gösterebilir. Player kendi görünümünü, Play modu Game görünümünü geri okur;
yalnızca bir kayıt kareyi geri okur. Görüntü atomik yazmanın parçası değildir: bir çökmeden sonra slot önceki
kaydının görüntüsünü gösterebilir. Buluttan gelen slot yeniden kaydedilene kadar görüntüsüz kalır.

!!! note "Görüntü önbelleği"
    UI bir görüntüyü yoluyla önbelleğe alır; bu yüzden `SaveSlotInfo::screenshot` gösteren bir `UiImage`, slot
    yeniden kaydedildikten sonra da slot'un ilk görüntüsünü tutar.

## Dosyalar nerede durur

| Host | Dizin |
| --- | --- |
| Export edilmiş player | `user:/saves` ([Dosya Sistemi](filesystem.md); Windows'ta `%APPDATA%\<organization>\<product>\saves`) |
| Editor'de Play modu | Projenin `cache:/saves` dizini (`.illusion/cache/saves`); her projenin kendi slot'ları vardır ve export onları içermez |

Bir slot `<slot>.save` dosyasıdır; yanında `<slot>.save.bak` ve `<slot>.png` bulunur. Oyun dizini seçmez, yalnızca
slot'lara ad verir.

## Buluta yansıtma

Oyunun bulut dosyalarını uzakta tutan bir mağazası varsa (Steam; null mağaza bulut dosyalarını zaten yerel dosya
olarak tutar) ve `SaveSettings::cloud` açıksa, `SaveService` slot dosyalarını yansıtır: yazılan her slot buluta
`<slot>.save` olarak kopyalanır, silinen her slot oradan da silinir. Oturum başlarken `savedAt` değeri yerel
slot'unkinden yeni olan bir bulut dosyası onun yerine geçer ve yerel dosya yedek olur; daha eski ya da aynı
yaştaki bir bulut dosyası hiçbir şeyi değiştirmez. Bulut hataları yalnızca log'a yazılır ve bir kaydı asla
başarısız yapmaz. Ayar `settings:/save.json` dosyasıdır:

```json
{
  "format": "illusion.settings",
  "type": "illusion.SaveSettings",
  "version": 1,
  "value": {"cloud": false}
}
```

Mağazalar hakkında daha fazlası [Oyun Runtime'ı](runtime.md#buluttaki-kaytl-oyunlar) sayfasında. Bulut
dosyasından daha yeni olan ya da bulutta bulunmayan yerel bir slot oturum başlarken yüklenmez; bir sonraki
kayıtla yüklenir.

## Editor commands

Kayıt dizini olan bir oyunu Play modu çalıştırırken [Editor commands](../reference/editor-commands.md)
`saves.list`, `saves.write`, `saves.load` ve `saves.remove` aynı service'e ulaşır; böylece bir agent ve bir test,
kayıtlı oyunları bir oyun modülü gibi kullanır. Hiçbir oturum çalışmıyorsa `wrong_mode` ile, bir `simulate`
oturumunda `precondition_failed` ile başarısız olurlar. `saves.list` her slot'u başlığı, oynama süresi, oyun
sürümü, custom metni, `savedAt`, durumu ve görüntü yoluyla döndürür. Diğer üçü asenkrondur: isteği kuyruğa
alırlar ve onu bitiren olayla (`Saved`, `Loaded`, `LoadedBackup` ya da `Removed`) yanıt verirler, ya da
`SaveError`'ın adıyla başarısız olurlar. Oyun istekleri update ederken gönderir; bu yüzden duraklatılmış bir oyun
yalnızca `play.step`'ten sonra yanıt verir.

```json
{"method": "saves.write", "params": {"slot": "slot1", "title": "Chapter 2", "playSeconds": 3600, "gameVersion": 3}}
{"method": "saves.load", "params": {"slot": "slot1"}}
{"method": "saves.list", "params": {}}
```

Oturumu durdurmak play World'ünü atar; bu yüzden bir yükleme çalışan oyunu değiştirir, açık sahneyi asla.
Slot'lar siz silene kadar projenin cache'inde kalır.
