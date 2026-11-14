# Proje Modülleri

Bir projenin oyun kodu, kaynakları `modules/` içinde duran [oyun modüllerinden](../modules.md) oluşur. Editor
bunları oluşturur, CMake ve kendisinin derlendiği derleyiciyle derler, derleyicinin hatalarını gösterir ve
sonucu yeniden başlatmadan [Play moduna](play-mode.md) yükler (ADR 0043). Bir yapay zekâ ajanı da aynısını
[Editor commands](../../reference/editor-commands.md) üzerinden yapar.

## Modül oluşturma

`module.create` bir şablondan modül yazar:

```json
{"method": "module.create", "params": {"name": "tetris"}}
```

```text
modules/
  CMakeLists.txt          finds the engine's module SDK and adds every module directory
  .clangd                 points code editors at the compile commands of the last module build
  tetris/
    CMakeLists.txt        illusion_add_module(tetris SHARED SOURCES tetris.cpp USES ...)
    tetris.cpp            ILL_MODULE(tetris, ...) with empty load, start and update functions
```

**Modules → New module...** onu editörün menüsünden çalıştırır: palet adı sorar.

Modül adı, bir harfle başlayan 1 ile 48 arası küçük harf, rakam ve alt çizgidir; dizinin, CMake hedefinin ve
kütüphanenin adıdır. Kaynakları modülün `CMakeLists.txt` dosyasına ekleyin. Dosyalar sıradan proje
dosyalarıdır: `module.create` geri alınabilir ve kaynak herhangi bir kod editöründe ya da dosya komutlarıyla
düzenlenir.

## Build

**Modules → Build Modules** (**Ctrl+B**) `module.build` komutunu çalıştırır:

1. İlk Build, editörün modül SDK'sıyla `.illusion/build/modules` içinde (git tarafından yok sayılır) CMake'i
   yapılandırır: derleyicisi, bayrakları ve yapılandırmasıyla; böylece modül editörün build anahtarını alır.
   Son yapılandırma aynı araç zinciriyle başarılı olduğu sürece sonraki Build'ler bu adımı atlar; başarısız bir
   yapılandırmadan sonra ya da başka bir editör Build'inin SDK'sıyla bir sonraki Build temiz bir önbellekle
   yeniden yapılandırır. `"reconfigure": true` onu her zaman çalıştırır.
   Windows'ta MSVC ve clang-cl, Visual Studio geliştirici ortamına ihtiyaç duyar. Böyle bir ortamın dışında
   (Başlat menüsünden ya da Explorer'dan) başlatılan editör, SDK'da `environmentScript` olarak kayıtlı olan
   derleyicinin `vcvars64.bat` dosyasını bir kez çalıştırır ve kurduğu ortamı CMake'e verir.
2. `cmake --build` modülleri (ya da yalnızca `"modules"` içindekileri) derler ve ilerlemeyi bildirir.
3. Başarı durumunda henüz yüklenmemiş her modül yüklenir, yüklü olanlar hot reload ile yenilenir:
   Component'leri değerlerini korur ve çalışan bir play oturumu çalışmaya devam eder. `"load": false` yalnızca
   derler.

`module.build` asenkrondur: ajanlar `rpc.progress` bildirimleri alır ve onu `rpc.cancel` ile iptal edebilir.
Başarısız bir derleme komut hatası değildir; sonuç `"succeeded": false` der ve diagnostikleri listeler:

```json
{
  "succeeded": false,
  "configured": false,
  "errors": 1,
  "warnings": 0,
  "diagnostics": [
    {"file": "modules:/tetris/tetris.cpp", "line": 42, "column": 9, "severity": "error",
     "code": "C2065", "message": "'boardd': undeclared identifier"}
  ],
  "libraries": [],
  "loaded": [],
  "reloaded": [],
  "modules": [],
  "output": "..."
}
```

GCC, Clang, MSVC, clang-cl, MSVC linker ve CMake diagnostikleri tanınır. Proje içindeki dosyalar sanal yollarla
(`modules:/...`), diğerleri aracın yazdığı gibi verilir. Her diagnostik ayrıca dosyası ve satırıyla Console'a
`Build` kategorisinde yazılır; `output` ise ayrıştırıcının tanımadığı her şey için CMake çıktısının son
satırlarını tutar.

`module.reload` editör dışında derlenen kütüphaneler için bir modülü adıyla ya da kütüphane dosyası değişen
her modülü hot reload ile yeniler (**Ctrl+R**).

Yeni kod yansıtılmış bir Component'in alanlarını değiştirdiğinde, edit ve play World'lerindeki Component'ler
kaybolmak yerine alan adına göre taşınır ([Component layout taşıma](../modules.md#component-layout-tasma)).
`module.reload` ve `module.build` sonuçlarının `modules` listesi her modül için `migrated` türlerini
`components`, `addedFields`, `removedFields` ve `resetFields` ile verir; aboneler bir `module.migrated`
event'i alır:

```json
{"module": "tetris", "types": ["tetris.Score"], "components": 1}
```

## Kod editörü kurulumu

`module.build`, `compile_commands.json` dosyasını (her kaynak, include yolları ve tanımlarıyla)
`.illusion/build/modules/` altına yazar; `module.create` ise clangd'yi ona yönlendiren `modules/.clangd`
dosyasını ekler. clangd tabanlı kod editörleri (Zed, clangd eklentili VS Code, CLion) modüller bir kez
derlendikten sonra motor header'larını tamamlar ve aralarında gezinir; editörde proje klasörünü (ya da
`modules/`) açın:

```yaml
CompileFlags:
  CompilationDatabase: ../.illusion/build/modules
```

VS Code'un Microsoft C/C++ eklentisiyle veritabanını bunun yerine projenin `.vscode/settings.json` dosyasında
ayarlayın:

```json
{
  "C_Cpp.default.compileCommands": "${workspaceFolder}/.illusion/build/modules/compile_commands.json"
}
```

## Kaynakları açma

`editor.open_source` bir dosyayı kod editörünüzde bir satır ve sütunda açar: [tercihlerin](preferences.md)
**External Tools** komut satırı (`codeEditor`); `{file}`, `{line}` ve `{column}` değiştirilir, komut `{file}`
içermiyorsa dosya sona eklenir. Boşluk içeren bir program yolunu tırnak içine alın. Komut ayarlı değilse dosya
sistemin varsayılan uygulamasıyla (`explorer`, `open` ya da `xdg-open`) açılır. Editör programı bağımsız
başlatır; böylece editör kapandığında program açık kalır.

```json
{"method": "editor.open_source", "params": {"path": "modules:/tetris/tetris.cpp", "line": 42, "column": 9}}
```

| Editör | `codeEditor` |
| --- | --- |
| Visual Studio Code | `code -g {file}:{line}:{column}` |
| Sublime Text | `subl {file}:{line}:{column}` |
| CLion | `clion --line {line} --column {column} {file}` |

Console'daki bir kaynak bağlantısına (örneğin bir build diagnostiğinin dosyasına) tıklamak, bir proje dosyasıysa
onu aynı şekilde açar; diğer kaynak bağlantıları panoya kopyalanır. Windows'ta programın `.exe` dosyasının tam
yolunu verin (örneğin `Code.exe`), çünkü `code.cmd` gibi batch dosyaları doğrudan başlatılmaz.

## Modül SDK'sı

Motorun Build'i `<build>/sdk` içine bir CMake paketi yazar: `illusionConfig.cmake`, `illusion::core` ile API
hedeflerini (`illusion::physics_api`, `audio_api`, `input_api`, `render_api`, `runtime_api`) içe aktarır ve
`illusion_add_module()` ile `illusion_project_modules()` fonksiyonlarını tanımlar.
`<build>/sdk/<Config>/illusion-sdk.json` araç zincirini tanımlar (sürüm 2 `player` ekler, bkz. [Player SDK](../modules.md#player-sdk)); editör kendi Build'ininkini kullanır.
Profiling Build'leri modülleri statik bağladığı için SDK yazmaz.

Editör derleyiciyi ortamı üzerinden bulur. Windows'ta onu bir geliştirici komut isteminden
(`tools\windows\vsdev.cmd`) başlatın; aksi hâlde yapılandırma adımı başarısız olur ve CMake'in hatasını bir
diagnostik olarak bildirir.

!!! warning "Ajanlar kod çalıştırabilir"
    `module.build`, `module.reload` ve `module.load` editör sürecinin içinde C++ derler ve yükler. Bunlar
    `build` sınıfı komutlardır: `ask` izin modu önce bir kişiye sorar.
