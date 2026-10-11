# Dosya Sistemi

Header: `#include <illusion/platform/FileSystem.hpp>`

## Sanal yollar

Motor ve oyun kodu asla gerçek yollar kullanmaz. `mount:/relative/path` biçiminde sanal yollar kullanır:

```cpp
illusion::VirtualFileSystem vfs;
vfs.mount("game", illusion::executableDirectory() / "data");
vfs.mount("user", illusion::userDataDirectory("MyStudio", "MyGame"), illusion::MountAccess::ReadWrite);

std::string config;
if (vfs.readText("game:/config/defaults.json", config) != illusion::FileError::None)
{
    // handle the missing file
}
vfs.writeText("user:/settings.json", settingsJson);
```

Böylece aynı kod editörde (ayrı dosyalar), export edilmiş oyunlarda (paketlenmiş veriler) ve kullanıcı
başına kayıt konumlarıyla çalışır; ayrıca bir yol asla kendi mount'unun dışına çıkamaz:

- Mount adları küçük harf, rakam, `_` ve `-` kullanır.
- Göreli kısım `/` ayırıcıları kullanır. Boş bileşenler, `.`, `..`, ters eğik çizgiler ve sürücü harfleri
  `FileError::InvalidPath` ile reddedilir.
- Yollar her platformda UTF-8'dir.

## Okuma ve yazma

| Fonksiyon | Açıklama |
| --- | --- |
| `readFile()`, `readText()` | Bir dosyanın tamamını byte vektörüne veya string'e okur. |
| `writeFile()`, `writeText()` | Bir dosyanın tamamını yazar; yalnızca `ReadWrite` mount'larda. |
| `exists()` | Bir dosya veya dizinin var olup olmadığını kontrol eder. |
| `list()` | Bir dizini isme göre sıralı listeler. |
| `resolve()` | Gerçek yolu verir; örneğin üçüncü taraf bir kütüphaneye geçirmek için. |

Her işlem exception fırlatmak yerine bir `FileError` döndürür. `fileErrorName()` bunu log'lar için metne
dönüştürür.

Yazma işlemleri atomiktir: veri önce geçici bir dosyaya yazılır, ardından bu dosya hedefin yerine geçer;
böylece bir çökme veya elektrik kesintisi asla yarım yazılmış bir kayıt dosyası bırakmaz. Üst dizinler
otomatik olarak oluşturulur.

## Platform dizinleri

| Fonksiyon | Windows | macOS | Linux |
| --- | --- | --- | --- |
| `userDataDirectory(org, app)` | `%APPDATA%\org\app` | `~/Library/Application Support/org/app` | `$XDG_DATA_HOME/org/app` veya `~/.local/share/org/app` |
| `executableDirectory()` | `.exe` dosyasının klasörü | Çalıştırılabilir dosyanın klasörü | Çalıştırılabilir dosyanın klasörü |

`userDataDirectory()` dizini oluşturmaz; onu mount etmek için dizinin var olması gerekir, bu yüzden önce
`std::filesystem::create_directories()` ile oluşturun.

## Thread'ler

Mount'ları kullanmadan önce tek bir thread üzerinde ayarlayın. Bundan sonra okuma ve yazma işlemleri
herhangi bir thread'de çalışabilir. Dosya işlemleri bloklayıcıdır; bu yüzden onları Job'lardan çağırmayın
(bkz. [Job System kuralları](jobs.md#kurallar)); ayrı bir I/O thread'inde çalıştırın. Asenkron asset
streaming bunu ileriki bir milestone'da sağlayacaktır.
