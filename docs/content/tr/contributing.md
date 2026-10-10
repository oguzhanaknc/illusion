# Katkıda Bulunma

illusion, insanlar ve AI agent'lar tarafından aynı kurallarla geliştirilir. Agent'lar için yetkili ve
ayrıntılı kurallar [`AGENTS.md`](https://github.com/oguzhanaknc/illusion/blob/main/AGENTS.md) dosyasındadır;
bu sayfa onları özetler.

## İş akışı

1. Değişikliği bir branch üzerinde yapın.
2. Test ekleyin veya güncelleyin. Her özellik ve her hata düzeltmesi test'lerle birlikte gelir.
3. Dokümantasyonu aynı değişiklik içinde **hem** İngilizce **hem** Türkçe güncelleyin.
4. Aşağıdaki kontrolleri çalıştırın.
5. Commit'lemeden önce diff'i gözden geçirin. Commit mesajları
   [Conventional Commits](https://www.conventionalcommits.org/) biçimini izler, örneğin
   `feat(ecs): add queries`.

## Kontroller

```bash
python tools/check_format.py
python tools/check_docs_parity.py
cmake --preset ci-linux-clang
cmake --build --preset ci-linux-clang-debug
ctest --preset ci-linux-clang-debug
```

CI aynı kontrolleri Windows, macOS ve Linux'ta çalıştırır; ayrıca sanitizer build'lerini, dokümantasyon
build'ini ve API referansını da kontrol eder.

## Kod stili

- C++20. Format, clang-format 22 ile zorunlu tutulur (`python tools/check_format.py --fix`).
- Tipler ve enum'lar `PascalCase`; fonksiyonlar ve değişkenler `camelCase`; üyeler `m_camelCase`;
  sabitler `kPascalCase`; makrolar `ILL_UPPER_CASE`.
- Dosyalar ana tiplerinin adını alır: `JobSystem.hpp`, `JobSystem.cpp`.
- Public header'lar `include/illusion/<module>/` altında yer alır; uygulama ayrıntıları `src/` içine
  veya bir `detail` namespace'ine gider.
- Her public bildirimin bir Doxygen yorumu (`///`) vardır.
- Kare başına çalışan kodda exception, RTTI tabanlı mantık veya heap ayırma yoktur.

## Dokümantasyon

İngilizce ve Türkçe sayfalar senkron kalmalıdır; CI, iki dilin de aynı sayfalara, aynı başlık yapısına
ve birebir aynı kod bloklarına sahip olduğunu kontrol eder.

Türkçe dokümantasyonda teknik terimler İngilizce bırakılır; örneğin Entity, Component, System,
Archetype, Chunk, Query, Job, Shader, Render Pass, Mesh, Material, Texture, Asset ve Sink. Bunları
İngilizce metindekiyle birebir aynı yazın.

## Architecture Decision Record'lar

Geri alınması zor veya birden fazla modülü etkileyen her kararı kaydedin (bağımlılıklar, veri
formatları, thread modeli, public API kuralları):

1. `docs/adr/0000-template.md` dosyasını sıradaki boş numarayla `docs/adr/NNNN-short-title.md` olarak
   kopyalayın.
2. Bağlamı, kararı ve sonuçları doldurun. Durum "Proposed" olarak başlar.
3. ADR'yi `docs/content/en/architecture/decisions.md` dosyasına ve Türkçe karşılığına ekleyin.
4. Değişiklik birleştiğinde durumu "Accepted" yapın. Yerini başka bir karara bırakan ADR'ler silinmez,
   o şekilde işaretlenir.

## Bağımlılıklar

Üçüncü taraf kütüphaneler `cmake/Dependencies.cmake` içinde kesin bir sürüme sabitlenir, SHA-256 hash'i
ile doğrulanır ve lisanslarıyla birlikte `THIRD_PARTY_NOTICES.md` dosyasında listelenir. Yalnızca ticari
ve kapalı kaynak dağıtımla uyumlu lisanslara sahip bağımlılıklar ekleyin.
