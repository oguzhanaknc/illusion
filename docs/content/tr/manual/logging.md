# Logging

Header: `#include <illusion/core/Log.hpp>`

## Mesaj yazma

Logging makrolarını bir kategori ve bir `std::format` metniyle kullanın:

```cpp
ILL_LOG_INFO("Render", "created {} buffers ({:.1f} MiB)", count, megabytes);
ILL_LOG_WARN("Assets", "texture '{}' is not a power of two", path);
ILL_LOG_ERROR("Audio", "device lost");
```

| Seviye | Makro | Kullanım |
| --- | --- | --- |
| Trace | `ILL_LOG_TRACE` | Çok ayrıntılı tanılama (Release build'lerinden çıkarılır). |
| Debug | `ILL_LOG_DEBUG` | Geliştirme tanılaması (Release build'lerinden çıkarılır). |
| Info | `ILL_LOG_INFO` | Normal çalışma. |
| Warn | `ILL_LOG_WARN` | Motorun toparlandığı beklenmedik durumlar. |
| Error | `ILL_LOG_ERROR` | Başarısız işlemler. |
| Fatal | `ILL_LOG_FATAL` | Kurtarılamaz hatalar. |

Format metinleri derleme zamanında kontrol edilir. Mesajlar thread-local bir buffer'a formatlanır; bu
yüzden logging'in kendisi bellek ayırmaz. 2048 karakterden uzun mesajlar kesilir ve `...` ile biter.

## Filtreleme

```cpp
illusion::Logger::instance().setLevel(illusion::LogLevel::Warn);
```

## Sink'ler

Bir Sink, filtreden geçen her mesajı alır. Varsayılan Logger konsola yazar. `LogSink`'ten türeterek kendi
Sink'inizi ekleyin:

```cpp
class FileSink final : public illusion::LogSink
{
public:
    explicit FileSink(const char* path) : m_file(std::fopen(path, "w")) {}
    ~FileSink() override { std::fclose(m_file); }

    void write(const illusion::LogMessage& message) override
    {
        std::fprintf(m_file, "[%s] %.*s\n", illusion::logLevelName(message.level).data(),
                     static_cast<int>(message.text.size()), message.text.data());
    }
    void flush() override { std::fflush(m_file); }

private:
    std::FILE* m_file;
};

illusion::Logger::instance().addSink(std::make_shared<FileSink>("game.log"));
```

`MemorySink` en son mesajları bellekte tutar; test'ler ve editör konsolu bunu kullanır.

## Thread'ler

Logger thread-safe'tir. Sink'ler, Logger'ın kilidi tutulurken her seferinde bir mesajla çağrılır; bu
yüzden bir Sink'in kendi kilidine ihtiyacı yoktur, ancak hızlı olmalı ve kendisi log yazmamalıdır.

## Assertion'lar

Header: `#include <illusion/core/Assert.hpp>`

| Makro | Davranış |
| --- | --- |
| `ILL_ASSERT(cond, ...)` | Debug'da (ve `ILLUSION_ENABLE_ASSERTS` ile her yerde) kontrol edilir. Koşulun yan etkisi olmamalıdır. |
| `ILL_VERIFY(cond, ...)` | `ILL_ASSERT` gibidir, ancak koşul her zaman değerlendirilir. |
| `ILL_CHECK(cond, ...)` | Kurtarılamaz çalışma zamanı hataları için her zaman kontrol edilir. |
| `ILL_UNREACHABLE()` | Asla çalışmaması gereken kodu işaretler. |

İsteğe bağlı bir format metni ve argümanlar hatayı açıklar: `ILL_ASSERT(index < size, "index {}", index)`.
Başarısız bir kontrol Fatal bir mesaj yazar ve süreci sonlandırır. Test'ler `setAssertHandler()` ile
bunun yerine exception fırlatan bir handler kurar.
