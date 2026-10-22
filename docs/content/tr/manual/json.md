# JSON ve Expected

illusion; sahne dosyaları, proje ayarları, araç protokolleri (JSON-RPC, MCP) ve metin tabloları için
JSON okur ve yazar. JSON API'si asla exception fırlatmaz: her hata bir dönüş değeridir. Ayrıştırıcıyı
gizli tutar; böylece hiçbir üçüncü taraf header'ı sizin kodunuza ulaşmaz.

Header'lar: `#include <illusion/core/Json.hpp>` ve `#include <illusion/core/Expected.hpp>`

## Expected

C++20'de `std::expected` yoktur; bu yüzden core kütüphanesi, kurtarılabilir biçimde başarısız
olabilen işlemler için `Expected<T, E>` sunar. Ya `T` tipinde bir değer ya da `E` tipinde bir hata
tutar:

```cpp
illusion::Expected<illusion::u16, std::string> parsePort(std::string_view text)
{
    illusion::u16 port = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), port);
    if (error != std::errc() || end != text.data() + text.size())
    {
        return illusion::unexpected(std::format("'{}' is not a port number", text));
    }
    return port;
}

const auto port = parsePort(argument);
if (!port)
{
    ILL_LOG_ERROR("Net", "{}", port.error());
    return;
}
connect(*port);
```

| Üye | Anlamı |
| --- | --- |
| `hasValue()`, `explicit operator bool` | Bir değer tutuluyorsa true. |
| `value()`, `operator*`, `operator->` | Değer. Bir hata tutuluyorsa assert tetiklenir. |
| `error()` | Hata. Bir değer tutuluyorsa assert tetiklenir. |
| `valueOr(fallback)` | Değer; hata durumunda `fallback`. |

`Expected<void, E>`, değer olmadan başarıyı bildirir. `T` ve `E` nothrow-movable olduğunda `Expected`
da öyledir ve `constexpr` kodda çalışır. Kurtarılabilir hatalar için bunu, `bool`'u veya
`std::optional`'ı kullanın; motor kodu asla exception fırlatmaz.

## JSON okuma

`JsonDocument::parse()` belgeyi ya da bayt ofseti, satır, sütun ve bir mesaj içeren bir `JsonError`
döndürür:

```cpp
auto parsed = illusion::JsonDocument::parse(text);
if (!parsed)
{
    const illusion::JsonError& error = parsed.error();
    ILL_LOG_ERROR("Project", "project.json:{}:{}: {}", error.line, error.column, error.message);
    return;
}
const illusion::JsonValue root = parsed->root();
const std::string_view name = root.find("name").asString().value_or("Untitled");
const illusion::i64 width = root.pointer("/window/size/0").asI64().value_or(1280);
for (const illusion::JsonValue scene : root.find("scenes").elements())
{
    // ...
}
for (const illusion::JsonMember member : root.find("settings").members())
{
    // member.key, member.value
}
```

Okuyucu katıdır. Şunları reddeder:

- standart JSON (RFC 8259) olmayan her şey: yorumlar, sondaki virgüller, tek tırnaklar, NaN ve Infinity,
  baştaki sıfırlar, bayt sırası işareti (BOM);
- geçersiz UTF-8 ve belgeden sonra gelen içerik;
- bir nesnede tekrarlanan anahtarlar (hata, tekrarlanan anahtarı gösterir);
- `maxDepth`'ten (varsayılan 128) derin iç içe yapılar ve `maxBytes`'tan (64 MiB) büyük girdiler. İkisi
  de belge oluşturulmadan önce kontrol edilir; bu yüzden fazla büyük veya fazla derin bir girdi, metni
  üzerinden en fazla bir geçişe mal olur.

Sınırları `JsonReadOptions` ile değiştirin. `allowDuplicateKeys = true` tekrarlanan anahtarları kabul
eder; aramalar o zaman ilkini döndürür.

```cpp
illusion::JsonReadOptions options;
options.maxDepth = 512;
options.maxBytes = 256 * 1024 * 1024;
auto large = illusion::JsonDocument::parse(text, options);
```

## Değerler

`JsonValue`, bir belgenin içine bakan ucuz bir görünümdür: belge yaşadığı sürece geçerlidir (belgeyi
taşımak onu geçersiz kılmaz). Eksik bir değer hata değil, **absent** (yok) değerdir; böylece aramalar
arada kontrol olmadan zincirlenir: herhangi bir adım eksikse `root.find("a").find("b")[3]` absent olur.

| Fonksiyon | Sonuç |
| --- | --- |
| `type()`, `isNull()`, `isBool()`, `isInteger()`, `isReal()`, `isNumber()`, `isString()`, `isArray()`, `isObject()`, `isAbsent()` | Değerin türü. |
| `asBool()`, `asI64()`, `asU64()`, `asF64()`, `asString()` | `std::optional` olarak içerik; tür uymuyorsa veya bir tamsayı aralık dışındaysa boş. `asF64()` tamsayıları da kabul eder. |
| `size()` | Bir dizinin elemanları veya bir nesnenin üyeleri. |
| `operator[](index)`, `find(key)` | Bir eleman veya üye ya da absent bir değer. |
| `elements()`, `members()` | Dosya sırasıyla aralıklar. |
| `pointer("/a/0")` | Bir JSON Pointer (RFC 6901): `~0`, `~` demektir; `~1`, `/` demektir; hatalı pointer'lar absent olur. |
| `dump()` | Tanılama için sıkışık metin. |

Tamsayılar, `f64`'ün hassasiyet kaybettiği 2^53'ün üzerinde de dahil olmak üzere `i64` ve `u64`
aralığının tamamında tam değerlerini korur.

## JSON yazma

`JsonMutDocument` bir belge oluşturur. Metinler ve anahtarlar kopyalanır:

```cpp
illusion::JsonMutDocument doc;
illusion::JsonMutValue root = doc.makeObject();
doc.setRoot(root);
root.add("name", doc.makeString("Crate"));
root.add("mass", doc.makeF32(12.5f));
illusion::JsonMutValue size = root.addArray("size");
size.push(doc.makeF32(1.0f)).push(doc.makeF32(2.0f)).push(doc.makeF32(1.0f));

std::string text;
if (const auto written = doc.write(text); !written)
{
    ILL_LOG_ERROR("Tools", "{}", written.error().message);
}
```

`add()` bir üye ekler ve yeni bir anahtar bekler; `set()` var olan bir üyeyi değiştirir; `remove()`
onu siler. `copy()`, ayrıştırılmış bir belgedeki bir `JsonValue`'nun derin kopyasını oluşturur; böylece
bir belge okunabilir, düzenlenebilir ve geri yazılabilir. `find()` ile `asBool()`, `asI64()`, `asU64()`,
`asF64()` ve `asString()` okuyucuları `JsonValue`'daki gibi çalışır; örneğin reflection migration'larında.
Bir değer onu oluşturan belgeye aittir: onu
başka bir belgeye eklemek assert tetikler. `makeF32()`, aynı `f32` olarak geri okunan en
kısa metni yazar (`0.10000000149011612` değil, `0.1`).

`write(out, options)`, `out`'un içeriğini değiştirir. Sonlu olmayan sayılar ve geçerli UTF-8 olmayan
metinler için yazma başarısız olur. `JsonValue::write()`, ayrıştırılmış değerleri aynı şekilde yazar.

| `JsonWriteOptions` | Etkisi |
| --- | --- |
| `pretty = true` (varsayılan) | Kanonik düzen: iki boşluk girinti, skaler dizileri tek satırda, sonda bir satır sonu. |
| `pretty = false` | Sıkışık: hiç boşluk yok. |
| `sortKeys = true` | Nesne üyeleri, her seviyede anahtarlarının baytlarına göre sıralanır. |

Pretty çıktı, sahne dosyalarının biçimidir. Sayılar, aynı değere geri okunan en kısa metinle yazılır;
böylece belgeler bit bit aynı geri döner:

```json
{
  "name": "Crate",
  "mass": 12.5,
  "size": [1.0, 2.0, 1.0]
}
```

## Bellek

Ayrıştırma ve oluşturma, belleklerini heap yerine bir `LinearAllocator`'dan alabilir:

```cpp
illusion::LinearAllocator arena(1024 * 1024);
auto parsed = illusion::JsonDocument::parse(text, arena);

illusion::JsonMutDocument reply(arena);
```

Ayrıştırılmış bir belge, metnin bir kopyasını ve her değer (anahtar, skaler, dizi, nesne) için 16 bayt
tutar. Tipik belgeler metnin boyutunun 3 ila 5 katına, tek basamaklı sayılardan oluşan diziler ise 11
katına kadar ihtiyaç duyar. Arena dolduğunda `parse()` "out of memory" ile başarısız olur.
`JsonMutDocument` ise absent değerler döndürür, hatayı hatırlar (`failed()`) ve `write()` bunu bildirir;
böylece bir oluşturucu yalnızca sonda tek bir kontrole ihtiyaç duyar.

Arena sıfırlandıktan veya belgenin altına geri sarıldıktan sonra belgeyi ve değerlerini kullanmayın. Böyle
bir belgeyi yok etmek veya ona atama yapmak sorun değildir: arena belleğine hiç dokunmaz; bu yüzden bir
kare arenası, içinde oluşturulan belgeler kapsam dışına çıkmadan önce sıfırlanabilir.

Yazma, çıktı metninin kapasitesini yeniden kullanır ve çalışma belleğini stack'te tutar; bu yüzden aynı
metne tekrar yazmak heap'e dokunmaz. Yalnızca 64 seviyeden derin iç içe belgeler ve `sortKeys` ile aynı
anda yazılan nesnelerde 128'den fazla üye heap'ten bellek ayırmayı gerektirir.

## Hatalar ve thread'ler

`JsonError::offset`, `line` ve `column` (1'den başlar, bayt olarak sayılır) girdideki sorunu gösterir.
Yazma hatalarında yalnızca bir mesaj bulunur. `JsonMutDocument` thread-safe değildir; aynı
`JsonDocument`'ı istediğiniz sayıda thread aynı anda okuyabilir.

Tasarım için bkz. [ADR 0028](../architecture/decisions.md).
