# Event'ler ve Zaman

Header: `#include <illusion/core/EventBus.hpp>`, `<illusion/core/Time.hpp>`

## Event bus

`EventBus`, System'leri birbirine bağımlı kılmadan birbirine bağlar. Bir event düz bir struct'tır;
abone olanlar o tip için bir handler kaydeder.

```cpp
struct PlayerDied
{
    illusion::Entity player;
    illusion::u32 score;
};

illusion::EventBus bus;
illusion::Subscription sub = bus.subscribe<PlayerDied>([](const PlayerDied& e) {
    showGameOver(e.score);
});

bus.publish(PlayerDied{player, 1200}); // handlers run now
bus.enqueue(PlayerDied{player, 1200}); // handlers run in dispatchQueued()
bus.dispatchQueued();
```

### Anında ve kuyruklu iletim

- `publish()` dönmeden önce tüm handler'ları abone olma sırasına göre çağırır.
- `enqueue()` bir kopya saklar; `dispatchQueued()` saklanan tüm event'leri kuyruğa eklendikleri sırayla
  iletir. `dispatchQueued()` sırasında handler'ların kuyruğa eklediği event'ler bir sonraki çağrıyı
  bekler. Kuyruğa eklenen event'ler trivially copyable olmalıdır.

Event'leri karenin belirli bir noktasında işlemek için kuyruklu iletimi kullanın (pencereler böyle
yapar); gönderenin tepkinin gerçekleşmiş olmasına ihtiyaç duyduğu durumlarda ise anında iletimi kullanın.

### Subscription'lar

`subscribe()` bir `Subscription` döndürür. `Subscription` yaşadığı sürece handler kayıtlı kalır;
aboneliği bitirmek için onu yok edin veya `reset()` çağırın. Subscription'ları, handler'ını kaydettikleri
nesnenin üyesi olarak saklayın; böylece o nesneden daha uzun yaşayamazlar.

Handler'lar çalışırken publish, enqueue, subscribe ve unsubscribe yapabilir. Bir dispatch sırasında
abone olan handler, bir sonraki publish'ten itibaren event alır; bir dispatch sırasında aboneliği biten
handler bir daha çağrılmaz.

### Kurallar

- Bus'ı tek bir thread'den (normalde main thread) kullanın.
- Handler'ların yakaladığı veriler 56 byte ile sınırlıdır; daha büyük durum için pointer yakalayın.
- Tüm subscription'ları bus'tan önce yok edin.
- Her event tipi bir kez görüldükten sonra publish bellek ayırmaz.

## Zaman

`monotonicSeconds()` yüksek çözünürlüklü, monoton bir zaman damgası döndürür. `Stopwatch` aralıkları
ölçer.

`FrameClock` zaman damgalarını kare başına zamanlamaya dönüştürür. Uzun duraklamalar (bir breakpoint,
pencere sürükleme) oyun sıçramasın diye sınırlandırılır:

```cpp
illusion::FrameClock clock(0.25); // at most 250 ms per frame
const illusion::FrameTime& frame = clock.tick(illusion::monotonicSeconds());
// frame.deltaSeconds, frame.totalSeconds, frame.frameIndex
```

`FixedTimestep` simülasyonu kare hızından bağımsız, sabit bir hızda çalıştırır:

```cpp
illusion::FixedTimestep physics(1.0 / 60.0);
const illusion::u32 steps = physics.advance(frame.deltaSeconds);
for (illusion::u32 i = 0; i < steps; ++i)
{
    simulate(physics.stepSeconds());
}
renderInterpolated(physics.alpha());
```

Kare başına en fazla `maxStepsPerFrame` adım (varsayılan 8) çalışır; simülasyon daha fazla geride kalırsa
sonraki tüm kareleri yavaşlatmak yerine fazla zaman atılır.

Her iki sınıf da saati kendisi okumak yerine zamanı parametre olarak alır; böylece test'ler ve
tekrar oynatmalar (replay) deterministiktir.
