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

Dosya formatları çoğu zaman başka dosyalara göreli olarak başvurur (`../textures/a.png`). Böyle bir
referansı başvuran dosyanın diziniyle birleştirin, ardından `.` ve `..` bileşenlerini çözen ama yolun
mount'undan çıkmasına asla izin vermeyen `normalizePath()`'i çağırın:

```cpp
const auto path = illusion::VirtualFileSystem::normalizePath("game:/models/../textures/./a.png");
// path == "game:/textures/a.png"; "game:/../secret" gives std::nullopt
```

UTF-8 metinden gelen gerçek yollar (yapılandırma, macOS ve Linux'ta komut satırları) `utf8Path()` ile
oluşturulmalıdır. `std::filesystem::path`'i dar (narrow) bir string'den oluşturmak Windows'ta sistem
kod sayfasını kullanır ve ASCII dışı adları bozar:

```cpp
vfs.mount("mods", illusion::utf8Path(modDirectoryFromConfig));
```

## Okuma ve yazma

| Fonksiyon | Açıklama |
| --- | --- |
| `readFile()`, `readText()` | Bir dosyanın tamamını byte vektörüne veya string'e okur. |
| `writeFile()`, `writeText()` | Bir dosyanın tamamını yazar; yalnızca `ReadWrite` mount'larda. |
| `exists()` | Bir dosya veya dizinin var olup olmadığını kontrol eder. |
| `list()` | Bir dizini isme göre sıralı listeler. |
| `listRecursive()` | Bir dizinin altındaki her şeyi listeler; isimler `sub/a.png` gibi göreli yollardır. |
| `stat()` | Dosyayı dizinden ayırır; boyutunu ve değişiklik damgasını verir (`FileStat`). |
| `createDirectory()` | Bir dizini ve eksik üst dizinlerini oluşturur. |
| `remove()` | Bir dosyayı ya da bir dizini siler (içeriğiyle birlikte yalnızca `recursive` true ise). |
| `rename()` | Bir dosyayı veya dizini taşır ya da yeniden adlandırır; yazılabilir mount'lar arasında da. |
| `copy()` | Bir dosyayı veya dizin ağacını kopyalar; kaynak salt okunur ya da bir arşiv olabilir. |
| `resolve()` | Gerçek yolu verir; örneğin üçüncü taraf bir kütüphaneye geçirmek için. |

Her işlem exception fırlatmak yerine bir `FileError` döndürür. `fileErrorName()` bunu log'lar için metne
dönüştürür. Dosya yönetimi fonksiyonları hiçbir şeyin üzerine yazmaz: var olan bir hedef
`FileError::AlreadyExists`, `recursive` olmadan içi dolu bir dizini silmek `FileError::NotEmpty` verir; bir
mount'un kökü silinemez, taşınamaz ve değiştirilemez (`FileError::InvalidPath`). `FileStat::modified` yalnızca
bir dosyanın değişip değişmediğini anlamak içindir; takvim zamanı değildir.

```cpp
vfs.createDirectory("user:/saves/slot2");
vfs.copy("game:/defaults/profile.json", "user:/saves/slot2/profile.json");
illusion::FileStat stat;
if (vfs.stat("user:/saves/slot1", stat) == illusion::FileError::None && stat.isDirectory)
{
    vfs.remove("user:/saves/slot1", true);
}
```

Yazma işlemleri atomiktir: veri önce geçici bir dosyaya yazılır, ardından bu dosya hedefin yerine geçer;
böylece bir çökme veya elektrik kesintisi asla yarım yazılmış bir kayıt dosyası bırakmaz. Üst dizinler
otomatik olarak oluşturulur.

## Arşivler

Bir arşiv (`.illpak`), tüm bir dizin ağacını tek bir dosyada tutar. Onu `mountArchive()` ile mount edin
ve mount edilmiş bir dizin gibi okuyun:

Header: `<illusion/platform/Archive.hpp>`

```cpp
illusion::VirtualFileSystem files;
files.mountArchive("game", illusion::executableDirectory() / "data.illpak");

std::string config;
files.readText("game:/config/defaults.json", config); // same path as with a directory mount
```

- Arşiv mount'ları salt okunurdur: yazma işlemleri `FileError::ReadOnly` döndürür; dosyaların kendi
  yolları olmadığı için `resolve()` de `FileError::Unsupported` döndürür.
- Her dosyanın içerik hash'i okunurken denetlenir; hasarlı veri `FileError::IoError` verir.
- Arşiv belleğe eşlenir (memory-mapped); bu yüzden okumalar hızlıdır ve birden fazla thread'den
  güvenlidir.
- Arşivleri `illusion_pack` aracıyla ya da koddan `writeArchive()` ve `packDirectory()` ile oluşturun:

```bash
illusion_pack data/ build/data.illpak
```

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
