# Random Numbers

The core library has two sources of randomness with different jobs: `Random`, a fast deterministic
generator for gameplay, procedural content, tools and tests, and `secureRandomBytes()`, which asks the
operating system for unpredictable bytes.

Header: `#include <illusion/core/Random.hpp>`

## Deterministic: Random

`Random` is xoshiro256** seeded through splitmix64. The same seed gives the same sequence on every
platform, compiler and build type, so a seed is enough to replay a level, a loot table or a failing
test:

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

| Function | Result |
| --- | --- |
| `next()` | 64 random bits. |
| `below(n)` | Uniform in `[0, n)` without bias; `n` must be greater than 0. |
| `range(min, max)` | Uniform in `[min, max]`; works for the whole `i64` range. |
| `uniform01()`, `uniformF32()` | Uniform in `[0, 1)` from the top 53 (or 24) bits. |
| `boolean()`, `chance(p)` | A fair coin, and true with probability `p`. |
| `state()`, `setState()` | Snapshot and restore, e.g. for save games or rollback. |

`Random` satisfies `std::uniform_random_bit_generator`, so it also works with `std::shuffle` and the
standard distributions. Note that the standard distributions are implemented differently by each
standard library: for results that must match on every platform, use `Random`'s own functions.

A generator is not thread-safe. Give each thread or Job its own, seeded from a parent generator:

```cpp
illusion::Random parent(worldSeed);
illusion::Random chunkRandom(parent.next());
```

## Secure: secureRandomBytes

Use `secureRandomBytes()` for anything an attacker must not predict: session tokens, identifiers that
must not collide across machines, keys and nonces. It uses `BCryptGenRandom` on Windows, `getrandom` on
Linux and `arc4random_buf` on macOS:

```cpp
std::array<std::byte, 16> token{};
if (!illusion::secureRandomBytes(token))
{
    ILL_LOG_ERROR("Net", "the system random source failed");
    return;
}
```

It returns false only if the system source fails; the buffer must then not be used. It is thread-safe,
much slower than `Random` and not reproducible. Never use it for gameplay.
