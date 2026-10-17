# Job System

Job System, küçük iş birimlerini (**Job**) sabit sayıda worker thread'den oluşan bir havuzda çalıştırır.

Header: `#include <illusion/core/JobSystem.hpp>`

## Job System oluşturma

```cpp
illusion::JobSystem jobs;                                // hardware threads - 1 workers
illusion::JobSystem small(illusion::JobSystemDesc{2, 1024}); // 2 workers, 1024 queue slots
```

Motor başlangıçta bir Job System oluşturur; normalde kendi örneğinizi oluşturmak yerine onu kullanırsınız.

## Job gönderme ve bekleme

```cpp
illusion::JobCounter counter;
for (int i = 0; i < 64; ++i)
{
    jobs.submit([i] { processTile(i); }, &counter);
}
jobs.wait(counter); // runs queued Jobs on this thread while waiting
```

Bir `JobCounter` bitmemiş Job'ları sayar. `wait()` boşta beklemez: bekleyen thread, sayaç sıfıra inene
kadar kuyruktaki Job'ları çalıştırır. Bu yüzden Job'lar kilitlenmeye (deadlock) yol açmadan başka Job'lar
gönderip onları bekleyebilir.

## Paralel döngüler

`parallelFor()` bir aralığı parçalara (batch) böler ve tüm parçalar bittiğinde döner:

```cpp
std::vector<float> values(100000, 1.0f);
jobs.parallelFor(static_cast<illusion::u32>(values.size()), 4096,
                 [&values](illusion::u32 begin, illusion::u32 end) {
                     for (illusion::u32 i = begin; i < end; ++i)
                     {
                         values[i] *= 2.0f;
                     }
                 });
```

Her Job'a en az birkaç mikrosaniyelik iş düşecek bir batch boyutu seçin.

## Kurallar

- **Job'lar bellek ayırmaz.** Bir Job, lock-free bir kuyruğun 64 byte'lık yuvasında doğrudan saklanır.
  Yakalanan (capture) veriler 48 byte ile sınırlıdır; daha büyük bir yapı için pointer yakalayın.
- **Job'lar exception fırlatmamalıdır.** Bir Job'dan kaçan exception süreci sonlandırır.
- **Job'lar** `JobSystem::wait()` dışında hiçbir şeyde **bloklanmamalıdır**. Bir bekleme boyunca mutex
  tutmayın ve Job içinde bloklayan I/O yapmayın; bunun için ayrı bir thread kullanın.
- Kuyruk doluysa `submit()` Job'ı hemen çağıran thread üzerinde çalıştırır. `trySubmit()` ise `false`
  döndürür; bir Job'ın çağıran thread üzerinde asla çalışmaması gereken yerlerde onu kullanın (physics modülü
  Jolt'un Job'ları için bunu yapar).
- Bir `JobCounter`, onunla gönderilen tüm Job'lardan daha uzun yaşamalıdır.

`JobSystem::currentWorkerIndex()` çağıran worker'ın indeksini (diğer thread'lerde -1) döndürür; bu,
thread başına geçici bellek için kullanışlıdır.
