# Rastgele Sayılar

Core kütüphanesinde görevleri farklı iki rastgelelik kaynağı vardır: oyun mantığı, prosedürel içerik,
araçlar ve test'ler için hızlı ve deterministik bir üreteç olan `Random` ile işletim sisteminden tahmin
edilemez baytlar isteyen `secureRandomBytes()`.

Header: `#include <illusion/core/Random.hpp>`

## Deterministik: Random

`Random`, splitmix64 ile tohumlanan xoshiro256** üretecidir. Aynı tohum her platformda, derleyicide ve
Build tipinde aynı diziyi verir; bu yüzden bir bölümü, bir ganimet tablosunu veya başarısız bir test'i
tekrar oynatmak için tohum yeterlidir:

```cpp
illusion::Random random(levelSeed);
const illusion::u64 card = random.below(52);       // uniform in [0, 52)
const illusion::i64 damage = random.range(10, 20); // uniform in [10, 20], both inclusive
const illusion::f32 jitter = random.uniformF32();  // uniform in [0, 1)
if (random.chance(0.05))
{
    spawnRareItem();
}
```

| Fonksiyon | Sonuç |
| --- | --- |
| `next()` | 64 rastgele bit. |
| `below(n)` | `[0, n)` aralığında yanlılık olmadan düzgün dağılımlı; `n` 0'dan büyük olmalıdır. |
| `range(min, max)` | `[min, max]` aralığında düzgün dağılımlı; `i64` aralığının tamamında çalışır. |
| `uniform01()`, `uniformF32()` | En üstteki 53 (veya 24) bitten `[0, 1)` aralığında düzgün dağılımlı. |
| `boolean()`, `chance(p)` | Hilesiz bir yazı tura ve `p` olasılıkla true. |
| `state()`, `setState()` | Anlık görüntü alma ve geri yükleme, örneğin kayıtlı oyunlar veya rollback için. |

`Random`, `std::uniform_random_bit_generator` gereksinimlerini karşılar; bu yüzden `std::shuffle` ve
standart dağılımlarla da çalışır. Standart dağılımların her standart kütüphanede farklı uygulandığını
unutmayın: her platformda aynı olması gereken sonuçlar için `Random`'ın kendi fonksiyonlarını kullanın.

Bir üreteç thread-safe değildir. Her thread'e veya Job'a, üst bir üreteçten tohumlanan kendi üretecini
verin:

```cpp
illusion::Random parent(worldSeed);
illusion::Random chunkRandom(parent.next());
```

## Güvenli: secureRandomBytes

Bir saldırganın tahmin etmemesi gereken her şey için `secureRandomBytes()` kullanın: oturum token'ları,
makineler arasında çakışmaması gereken kimlikler, anahtarlar ve nonce'lar. Windows'ta `BCryptGenRandom`,
Linux'ta `getrandom`, macOS'ta `arc4random_buf` kullanır:

```cpp
std::array<std::byte, 16> token{};
if (!illusion::secureRandomBytes(token))
{
    ILL_LOG_ERROR("Net", "the system random source failed");
    return;
}
```

Yalnızca sistem kaynağı başarısız olursa false döndürür; bu durumda buffer kullanılmamalıdır.
Thread-safe'tir, `Random`'dan çok daha yavaştır ve tekrarlanamaz. Oyun mantığında asla kullanmayın.
