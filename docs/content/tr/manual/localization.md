# Yerelleştirme

illusion, kullanıcıya görünen metinleri string tabloları üzerinden gösterir: her metnin sabit bir anahtarı
vardır, İngilizce metni programa derlenir ve çeviriler çalışma anında yüklenen JSON dosyalarıdır. Yeni bir
dil eklemek için ne kod değişikliği ne de yeni bir Build gerekir.

Header'lar: `#include <illusion/text/Localization.hpp>` (yapı taşları için `MessageFormat.hpp`,
`PluralRules.hpp`, `SearchFold.hpp`, `FuzzyMatcher.hpp`, `Utf8.hpp`). Kütüphane: `illusion::text`.

## String tabloları

Bir tablo, İngilizce kaynağı içeren bir locale dosyasıdır; genellikle onu kullanan kodun yanındaki
`data/locales/en.json`:

```json
{
  "format": "illusion.locale",
  "version": 1,
  "locale": "en",
  "displayName": "English",
  "plural": "en",
  "number": {"decimal": ".", "group": ","},
  "strings": {
    "game.hud.ammo": "{count, plural, =0 {Out of ammo} one {# round} other {# rounds}}",
    "game.menu.start": {"text": "Start", "comment": "Main menu: starts a new game"}
  }
}
```

Anahtarlar ASCII'dir, noktayla ayrılır ve sıralıdır: `^[a-z][a-z0-9_]*(\.[A-Za-z0-9_]+)+$`. İlk parça
tablonun adıdır. `comment`, bağlamı çevirmenlere açıklar.

Tabloyu CMake'te tanımlayın. Build sırasında `illusion_textc` onu `TextKey` sabitlerinden oluşan bir
header'a ve İngilizce metni içeren bir kaynak dosyaya dönüştürür:

```cmake
illusion_add_string_table(my_game
    TABLE game
    SOURCE data/locales/en.json
    NAMESPACE game::strings
    HEADER game/GameStrings.hpp)
```

Sabitin adı, anahtarın ilk parçası çıkarılmış PascalCase hâlidir: `game.hud.ammo`,
`game::strings::kHudAmmo` olur. Yanlış yazılmış bir anahtar derlenmez.

## Metin gösterme

`Localization` tabloları ve etkin dili tutar. Her tabloyu kaydedin, çevirileri yükleyin ve UI kodu
çalışmadan önce her karede bir kez `applyPendingLocale()` çağırın:

```cpp
illusion::text::Localization localization;
localization.addTable(game::strings::kTable);
if (const auto added = localization.addLocale(readText("locales/tr.json")); !added)
{
    ILL_LOG_ERROR("Game", "tr.json:{}: {}", added.error().line, added.error().message);
}
localization.setLocale(*illusion::text::LocaleId::parse("tr-TR"));

// every frame:
localization.applyPendingLocale();

std::string label;
illusion::text::TextOutput out(label);
(void)localization.format(game::strings::kHudAmmo, {{"count", ammo}}, out);
```

`setLocale()` bir sonraki `applyPendingLocale()` çağrısında etkili olur; böylece tek bir kare asla iki dili
karıştırmaz. `TextOutput` metni bir `LinearAllocator` içinde de oluşturabilir; bu, kare başına UI metnini
heap'ten uzak tutar:

```cpp
illusion::text::TextOutput frameText(frameArena);
(void)localization.format(game::strings::kMenuStart, {}, frameText);
drawLabel(frameText.view());
```

## Mesaj sözdizimi

Desenler ICU MessageFormat'ın bir alt kümesini kullanır:

| Sözdizimi | Anlamı |
| --- | --- |
| `{name}` | Bir argüman: bir metin ya da yerel ayarın ayırıcılarıyla bir sayı. |
| `{n, number}` | Bir sayı. |
| `{n, plural, =0 {...} one {...} other {...}}` | Çoğul biçimler; `#` sayının kendisidir. `=N` tam eşleşir. |
| `{s, select, female {...} male {...} other {...}}` | Metin değerine göre seçim. |
| `''` | Tek bir kesme işareti. |
| `'{'`, `'}'`, `'#'` | Değişmez süslü parantezler ve çoğul biçimler içinde değişmez `#`. |

Çoğul ve seçim yapıları bir `other` dalı gerektirir ve en fazla sekiz seviye iç içe geçebilir. Cümleleri
metinleri birleştirerek kurmayın: cümlenin tamamına tek bir anahtar verin ve parçaları argüman olarak geçin.

Biçimlendirme her zaman mesajın tamamını yazar. Eksik bir argüman `{name}` olarak yazılır ve bir hata
değeri olarak bildirilir; hiçbir şey exception fırlatmaz veya assert tetiklemez. Desenler, bir tablo veya
locale eklendiğinde derlenir.

## Çoğullar ve sayılar

Her dilin CLDR çoğul kuralları vardır: İngilizcede `one` ve `other`, Türkçede `one` (n = 1) ve `other`,
Rusçada `one`, `few`, `many` ve `other`. Yerleşik kurallar en, de, tr, fr, es, it, pt, ru, uk, pl, cs, ar, ja,
zh ve ko dillerini kapsar. Bir locale dosyası kuralını `plural` alanında belirtir.

Sayılar gruplanarak ve en fazla üç ondalık basamakla gösterilir: `1234.5`, İngilizcede `1,234.5`,
Türkçede `1.234,5` olur. Gösterilen basamaklar çoğul biçimi de belirler; bu yüzden `1.0` (`1` olarak
gösterilir) 1'in biçimini alır. Düzenlenebilir sayı alanları gösterim biçimini kullanmamalıdır; değişmez
`.` ayırıcısını kullanırlar.

## Çeviriler

Bir çeviri, yerel ayarının adını taşıyan (`tr.json`, `pt-BR.json`) ve aynı anahtarları içeren bir locale
dosyasıdır:

```json
{
  "format": "illusion.locale",
  "version": 1,
  "locale": "tr",
  "displayName": "Türkçe",
  "plural": "tr",
  "number": {"decimal": ",", "group": "."},
  "fallback": "en",
  "strings": {
    "game.hud.ammo": {"text": "{count, plural, =0 {Mermi bitti} one {# mermi} other {# mermi}}", "source": "75071fcff137de6a"},
    "game.menu.start": {"text": "Başla", "source": "6f75a38f09135d29"}
  }
}
```

`source`, çevirinin yapıldığı İngilizce metnin xxHash64 değeridir (16 küçük harfli onaltılık basamak).
İngilizce metin değiştiğinde kontrol, çeviriyi eskimiş olarak bildirir. `fonts`, dilin ihtiyaç duyduğu ek
yedek fontları listeleyebilir.

Her anahtar ayrı ayrı çözülür: yerel ayar (`tr-TR`), üst yerel ayarları (`tr`), dosyalarının `fallback`
değeri ve son olarak her zaman eksiksiz olan İngilizce. İngilizceye düşen bir anahtar Debug seviyesinde bir
kez loglanır; `missingKeyCount()` bunları sayar. Deseni derlenmeyen veya İngilizce metinde olmayan
argümanlar kullanan çeviriler bir uyarıyla atlanır.

Yeni bir dil eklemek için:

1. `en.json` dosyasını `<locale>.json` olarak kopyalayın.
2. `locale`, `displayName`, `plural` ve `number` alanlarını ayarlayın.
3. Metinleri çevirin ve her birinin `source` hash'ini kaydedin.
4. `illusion_textc check <dizin>` komutunu çalıştırın.
5. Dosyayı `addLocale()` ile yükleyin. Kod değişikliği gerekmez.

## Sahte yerel ayar (pseudo-locale)

`en-XA` yerel ayarı, her İngilizce metni sahte yerelleştirilmiş gösterir: `[Šåvé Šçéñé~~~]`. Harfler aksan
alır, metin yaklaşık %35 uzar ve köşeli parantez içine alınır; argümanlar bozulmaz. Bir tabloda olmayan
metinleri (düz kalırlar) ve daha uzun çevirileri kaldıramayan yerleşimleri bulmak için kullanın.

## Locale dosyalarını kontrol etme

`illusion_textc check [--strict] <dizin>...`, her dizindeki `en.json` dosyasını ve tüm çevirileri kontrol
eder. CTest `text.locales`, bunu her Build işinde bütün string tabloları için çalıştırır:

| Hata | Uyarı (`--strict` ile hata) |
| --- | --- |
| Katı olmayan JSON (yorumlar, sondaki virgüller) | Eksik anahtar (İngilizce gösterilir) |
| Geçersiz veya sırasız anahtarlar | Eskimiş ya da eksik `source` hash'i |
| Ayrıştırılamayan bir desen | |
| İngilizcede olmayan bir anahtar | |
| İngilizceden farklı argüman adları | |
| Yerel ayarın kuralındaki biçimlerden birini içermeyen bir çoğul | |

## Arama

`searchFold()`, yerel ayara bağlı olmadan büyük/küçük harf farkını ve aksanları kaldırır; böylece I, ı, İ ve
i birbirini eşler, ş de s ile eşleşir. `FuzzyMatcher`, komut paletleri ve arama kutuları için adayları
sıralar: desenin karakterleri sırayla geçmelidir; kelime başlarındaki, camelCase tümseklerindeki ve art
arda gelen eşleşmeler daha yüksek puan alır.

```cpp
illusion::text::FuzzyMatcher matcher(query);
for (const Command& command : commands)
{
    if (const auto score = matcher.score(command.title))
    {
        results.push_back({*score, &command});
    }
}
```

Kullanıcılar iki dilde de arayabilsin diye hem yerelleştirilmiş hem de İngilizce başlığı eşleyin.

## Input etiketleri

Input kütüphanesinin, tuş ve gamepad glyph etiketleri için bir tablosu vardır. `glyphLabelKey()` bir
binding'in etiket anahtarını verir, `glyphLabel(binding, family, localization)` ise etiketi etkin dilde
biçimlendirir (`<illusion/input/InputStrings.hpp>` içindeki `illusion::input::strings::kTable` tablosunu
kaydedin). İngilizce `glyphLabel(binding, family)` değişmedi.

## Sınırlamalar

Metin şekillendirilmez ve çift yönlü yerleşimi yoktur; bu yüzden Arapça, İbranice ve karmaşık yazılar doğru
gösterilemez. Yapay zekâ ajanlarına yönelik metinler (protokol hataları, komut açıklamaları) her zaman
İngilizcedir.

Tasarım için bkz. [ADR 0029](../architecture/decisions.md).
