# Başlarken

Bu sayfa illusion'ı kaynak koddan derlemeyi, test'leri ve benchmark'ları çalıştırmayı anlatır.

## Gereksinimler

| Araç | Sürüm | Notlar |
| --- | --- | --- |
| CMake | 3.28+ | Tüm build'ler preset'lerle yapılır. |
| Ninja | 1.11+ | Visual Studio ile birlikte gelir; diğer sistemlerde `pip install ninja`. |
| C++ derleyicisi | aşağıya bakın | `std::format` destekli C++20. |
| Python | 3.10+ | Format ve dokümantasyon kontrolleri. |
| Git | güncel bir sürüm | |

Derleyiciler:

- **Windows:** "Desktop development with C++" iş yükü kurulu Visual Studio 2022 17.10+ veya Visual
  Studio 2026. Aynı kurulumdaki clang-cl de çalışır.
- **macOS:** Xcode 15+ (Apple Clang). Minimum deployment target macOS 13.3'tür.
- **Linux:** GCC 13+ veya Clang 18+ (Ubuntu 24.04 ikisini de içerir).

Üçüncü taraf kütüphaneler configure aşamasında otomatik olarak indirilir ve SHA-256 hash'leri ile
doğrulanır. Elle kurulum gerekmez.

## Derleme ve test

Depoyu klonlayın ve platformunuza uygun preset'i seçin:

```bash
git clone https://github.com/oguzhanaknc/illusion.git
cd illusion
cmake --preset linux-clang
cmake --build --preset linux-clang-debug
ctest --preset linux-clang-debug
```

Mevcut configure preset'leri:

| Preset | Platform |
| --- | --- |
| `windows-msvc`, `windows-clang` | Windows (Visual Studio developer shell içinden çalıştırın) |
| `linux-gcc`, `linux-clang` | Linux |
| `macos-clang` | macOS |
| `linux-clang-asan`, `linux-clang-tsan` | Sanitizer'lı Linux |
| `*-profile` | Tracy profiling açık |
| `ci-*` | CI'ın kullandığı yapılandırmanın aynısı (uyarılar hata sayılır, assert'ler açık) |

Her configure preset'inin `<preset>-debug`, `<preset>-release` ve `<preset>-relwithdebinfo` adlı build
ve test preset'leri vardır.

Windows'ta `tools\windows\vsdev.cmd` yardımcı betiği herhangi bir komutu Visual Studio developer
ortamında çalıştırır; böylece önce bir developer shell açmanız gerekmez:

```bat
tools\windows\vsdev.cmd cmake --preset windows-msvc
tools\windows\vsdev.cmd cmake --build --preset windows-msvc-debug
```

## Build klasörünü senkronize edilen klasörlerin dışında tutun

Build klasörleri varsayılan olarak `build/<preset>` altına oluşturulur. Depo OneDrive, Dropbox veya
iCloud tarafından senkronize edilen bir klasördeyse build klasörünü başka bir yere koyun: senkronizasyon
istemcileri build sırasında dosyaları kilitler ve gereksiz bant genişliği harcar. `CMakePresets.json`
dosyasının yanına bir `CMakeUserPresets.json` oluşturun (Git tarafından yok sayılır):

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "dev",
      "inherits": "windows-msvc",
      "binaryDir": "C:/dev/illusion-build/${presetName}"
    }
  ]
}
```

Ardından `cmake --preset dev` ve `cmake --build C:/dev/illusion-build/dev --config Debug` ile derleyin.

## Build seçenekleri

| Seçenek | Varsayılan | Açıklama |
| --- | --- | --- |
| `ILLUSION_BUILD_TESTS` | `ON` | Unit test'leri derler. |
| `ILLUSION_BUILD_BENCHMARKS` | `ON` | Benchmark'ları derler (optimize yapılandırmalarda CTest test'i olarak kaydedilir). |
| `ILLUSION_WARNINGS_AS_ERRORS` | `OFF` | Uyarıları hata olarak ele alır (CI preset'lerinde `ON`). |
| `ILLUSION_ENABLE_ASSERTS` | `OFF` | `ILL_ASSERT`'ü tüm yapılandırmalarda açık tutar; Debug'da her zaman açıktır. |
| `ILLUSION_PROFILING` | `OFF` | Motoru Tracy için enstrümante eder. |
| `ILLUSION_SANITIZER` | boş | `address` (ASan + UBSan) veya `thread` (TSan); yalnızca GCC/Clang. |

## Benchmark'lar

Benchmark'lar kritik yolları ölçer ve bir işlem performans bütçesini aştığında başarısız olur. Release
ve RelWithDebInfo build'lerinde `ctest`'in bir parçası olarak çalışırlar. İsterseniz isim filtresiyle
doğrudan da çalıştırabilirsiniz:

```bash
./build/linux-clang/benchmarks/Release/illusion_benchmarks ecs/
```

`ILLUSION_BENCH_TOLERANCE` ortam değişkeni tüm bütçeleri çarpar (paylaşımlı runner'lar gürültülü olduğu
için CI `3.0` kullanır).

## Sonraki adımlar

Oyun verisinin nasıl düzenlendiğini öğrenmek için [ECS kılavuzunu](manual/ecs.md), ardından
[mimariye genel bakış](architecture/overview.md) sayfasını okuyun.
