# illusion

illusion; Windows, macOS ve Linux için C++20 ile yazılmış, performans odaklı bir 3D oyun motorudur.
Steam ve benzeri mağazalarda ticari oyunlar yayımlamak için tasarlanmıştır.

!!! warning "Erken geliştirme aşaması"
    illusion temel katman aşamasındadır. Core kütüphanesi (logging, bellek, Job System, ECS)
    tamamlanmış ve test edilmiştir; pencereler, klavye ve fare input'u, sanal dosya sistemi ve Vulkan 1.3
    render cihazı da hazırdır. Gamepad'ler, tam renderer, ses, fizik, editör ve export hattı
    planlanmıştır; bkz. [yol haritası](https://github.com/oguzhanaknc/illusion/blob/main/ROADMAP.md).

## Tasarım hedefleri

- **Veri odaklı.** Entity ve Component'ler, structure-of-arrays depolamalı, Archetype tabanlı bir ECS
  içinde tutulur; böylece System'ler sıkı paketlenmiş bellek üzerinden akarak çalışır.
- **Varsayılan olarak paralel.** Bellek ayırmayan bir kuyruğa sahip Job System işleri tüm çekirdeklerde
  çalıştırır; ECS Query'leri paralel olarak dolaşabilir.
- **Kare başına bellek ayırma yok.** Kararlı durumdaki kareler genel heap yerine arena ve pool kullanır.
- **Ölçülebilir.** Kritik yolların CI'da çalışan, performans bütçeli benchmark'ları vardır ve motor
  Tracy profiler için enstrümante edilmiştir.
- **Test edilmiş.** Her özellik, üç platformda da çalışan unit test'ler ve sanitizer build'leri ile
  birlikte gelir.
- **Araçlar ve AI agent'lar tarafından kontrol edilebilir.** Editör, agent'ların insan müdahalesi
  olmadan oyun geliştirebilmesi için bir komut API'si sunacaktır.

## Platformlar

| Platform | Derleyiciler | Grafik (planlanan) |
| --- | --- | --- |
| Windows 10/11 x64 | MSVC 19.40+, clang-cl 18+ | Vulkan |
| macOS 13.3+ | Apple Clang 15+ | MoltenVK üzerinden Vulkan |
| Linux (Ubuntu 24.04+, Steam Runtime "sniper") | GCC 13+, Clang 18+ | Vulkan |

## Sonraki adımlar

- [Başlarken](getting-started.md): motoru derleyin ve test'leri çalıştırın.
- [Kılavuz](manual/ecs.md): temel sistemleri öğrenin.
- [Mimari](architecture/overview.md): motorun nasıl kurgulandığı ve nedenleri.
- [Katkıda Bulunma](contributing.md): iş akışı, kod stili ve dokümantasyon kuralları.
