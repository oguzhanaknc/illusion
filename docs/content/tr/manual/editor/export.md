# Oyunu Export Etmek

Export, bir projeyi Editor olmadan çalışan bir oyuna çevirir: projenin [oyun modülleri](../modules.md) içine
bağlanmış tek bir executable ve cook edilip paketlenmiş Asset'leri taşıyan bir `data/` dizini (ADR 0055).
Bunu `export.build` yapar; Editor'ün menüsünden, bir yapay zekâ ajanından veya `illusion_ctl` ile çalıştırılır.
Aynı komut [headless Editor](automation.md) içinde de çalışır; build makineleri bu şekilde export eder.

## Ön koşullar

- Bir C++ derleyicisi ve CMake; [Proje Modülleri](project-modules.md) ile aynıları. Bir projenin ilk export'u
  configure ve link eder, bu dakikalar sürer; sonrakiler yalnızca değişeni yeniden build eder.
- Export ettiğiniz configuration için [player](../runtime.md#oyuncu-player) içeren bir SDK. Motorun Build ağacı bunu
  sağlar (`<build>/sdk/<Config>/illusion-sdk.json` içinde `"player": true`); kurulu bir motor da sağlar.
  [Player SDK](../modules.md#player-sdk) bölümüne bakın.
- `assets:/` altında bir başlangıç sahnesi olan bir proje (`project.illusion`).

Export, onu build eden makinenin platformunu ve mimarisini hedefler; cross-compilation yoktur.

## Export ayarları

`settings:/export.json` (tür `illusion.ExportSettings`) oyunu tanımlar. `project.settings_get` ve
`project.settings_set` ile düzenlediğiniz bir [proje ayarıdır](../runtime.md#proje-ayarlar) ve export edilen
oyunun parçası olmaz.

| Alan | Anlamı |
| --- | --- |
| `productName` | Oyunun ve dizininin adı. Boşsa projenin adı kullanılır. Executable, bu adın küçük harfli halidir ve diğer karakterler alt çizgiye çevrilir; bu yüzden bir harfle başlamalıdır. |
| `organization` | Yayıncı; oyunun kullanıcı verisi dizininin üst dizini. Boşsa ürün adı kullanılır. |
| `identifier` | `com.example.game` gibi ters DNS tanımlayıcısı, en az iki parça. |
| `copyright` | Oyunun telif satırı. |
| `icon` | En az 256 piksellik kare bir PNG Asset'i. |
| `exclude` | `assets:/` altında gönderilmeyen dosyalar. |

Oyunun sürümü projenin `version` değeridir. Bir sonraki bölüm, her platformun `identifier`, `copyright` ve
`icon` ile ne yaptığını anlatır.

### Icon, identifier ve copyright

Icon, `assets:/` altında en az 256 piksellik kare bir PNG'dir; yoksa oyun motorun icon'unu alır. Export, bir
platformun ihtiyaç duyduğu tüm boyutları bundan `illusion::icon` ile üretir.

| Platform | Icon | `identifier` | `copyright` |
| --- | --- | --- | --- |
| Windows | 16 ile 256 piksel arasındaki tüm boyutları içeren bir `.ico`; executable'a gömülür ve installer tarafından kullanılır. | Kullanılmaz. | Executable'ın sürüm kaynağındaki `LegalCopyright`; ürün adı, sürüm ve organization (`CompanyName`) yanında durur. |
| macOS | Bundle'ın `Contents/Resources` dizininde bir `.icns`. | `Info.plist` içindeki bundle identifier. Boşsa `com.<organization>.<name>`; organization ve ürün adından üretilir. | `NSHumanReadableCopyright`. |
| Linux | Executable'ın yanında `<executable>.png` (512 piksel). | Kullanılmaz. | Kullanılmaz. |

Telif satırı, Windows installer'ının gösterdiği lisans metninin ikinci satırı da olur. Export, bir şey yazmadan önce
ayarları denetler: PNG olmayan, kare olmayan veya 256 pikselden küçük bir icon, ters DNS olmayan bir `identifier`
ve birden fazla satırlı bir `copyright` komutu başarısız kılar.

### Exclude desenleri

Bir desen, `assets:/` altına göre bir yoldur ve `/` ayırıcısı kullanır. `*` tek bir dizin içinde herhangi bir
karakter dizisiyle eşleşir, `**` dizinler dahil her diziyle eşleşir, `**/` ise hiç dizinle de eşleşir:

```json
{"exclude": ["raw/**", "*.psd", "**/notes.txt"]}
```

Hariç tutulan dosya ne paketlenir ne de cook edilir. Ona ihtiyaç duyan bir Asset (örneğin hariç tutulan bir
Texture'ı adlandıran bir Material) export'un cook adımını başarısız kılar; başlangıç sahnesini hariç tutmak da
öyle. Rapor, dışarıda bırakılanları `skipped` içinde listeler.

## Export penceresi

**File > Export…** (komut paletinde de) Export penceresini açar. Aynı `settings:/export.json` dosyasını düzenler ve
aynı `export.build` komutunu çağırır; pencerenin yaptığını bir ajan da yapabilir.

- **Settings**: Product name, Organization, Identifier, Copyright, Icon (bir Asset yolu) ve Excluded files (satır
  başına bir desen); bkz. [Export ayarları](#export-ayarlar). İpuçları boş bir alanın ne anlama geldiğini gösterir,
  örneğin Product name için projenin adı. Version salt okunurdur: `project.illusion` içinde ayarlanır.
  `project.settings_set` hatası ait olduğu alanın altında görünür.
- **Target**: Platform, editörün çalıştığı platformdur; gönderdiğiniz her platformda export edin. Package, bu
  platformun türlerini sunar: Folder only, Windows'ta Folder and ZIP archive ile Folder and installer (NSIS),
  macOS'ta Folder and disk image (DMG), Linux'ta Folder and .tar.gz archive. Output folder ve Configuration,
  `output` ve `configuration` parametreleridir; Clean build `clean` demektir.
- **Signing**: Windows'ta Sign command; macOS'ta Codesign identity ve Notary profile. Boş bir alan kendi ortam
  değişkenini kullanır, ipucu onu adlandırır. Linux "Linux exports are not signed." gösterir.
- **Export** komutu başlatır. Çalışırken pencere aşamayı (örneğin "Cooking assets") bir ilerleme çubuğu ve
  **Cancel** ile gösterir; alanlar devre dışıdır.
- **Result** Exported'ı ya da başarısız olan aşamayı, süreyi, Game folder'ı, Package file'ı, Size'ı, exclude
  desenlerinin dışarıda bıraktığı dosyaları ve Signed ile Notarized rozetlerini gösterir. Diagnostics hata ve
  uyarıları listeler; kaynak dosyası olana tıklamak onu Asset Browser'da seçer. Tool output build araçlarının son
  satırlarını tutar, **Show in folder** oyunun klasörünü dosya yöneticisinde açar.

`export.build` tek mesajda bildirdiği sorunlar (product name, identifier, organization, exclude desenleri) alanın
yanında değil, sonuçta görünür.

## Export çalıştırmak

Bir ajan komutu çağırır; `output` yolu mutlak ya da projeye göredir:

```json
{"call": "export.build", "params": {"output": "C:/Games/out", "configuration": "Release", "clean": true}}
```

Kabuktan `illusion_ctl`, [Otomasyon](automation.md) sayfasında anlatıldığı gibi komutu projenin Editor'üne
çağırır:

```bash
illusion_ctl --project ~/games/tetris call export.build '{"output": "/tmp/tetris-out", "configuration": "Release"}'
```

| Param | Anlamı |
| --- | --- |
| `output` | Oyunu alan dizin. Varsayılan, projedeki `.illusion/export/<platform>/out`. Projenin `assets`, `settings` veya `modules` dizinlerinin içinde olmamalıdır. |
| `configuration` | Build'de kullanılacak SDK configuration'ı. Varsayılan `Release`, yoksa Editor'ün kendisininki. Modüller onun bayraklarıyla derlenir. |
| `clean` | Export'un Build ağacını siler ve yeniden configure eder. |
| `package` | `none` (varsayılan), `archive` veya `installer`; bkz. [Paketler](#paketler). |
| `signCommand` | Windows: bir dosyayı imzalayan komut satırı; bkz. [İmzalama](#imzalama). |
| `codesignIdentity` | macOS: `codesign` kimliği. |
| `notaryProfile` | macOS: DMG'yi notarize eden `notarytool` keychain profili. |
| `linuxRuntime` | Steam Runtime denetimi için ayrılmıştır; boş bırakın. |

`export.build` [asenkrondur](automation.md): aşamasını ilerleme olarak bildirir ve export bitince döner. Aynı
anda yalnızca bir modül build'i veya export çalışır; ikincisi yeniden denenebilir bir `Busy` ile başarısız olur.

## Aşamalar

1. **validate**: proje, ayarları, modülleri ve SDK denetlenir; her sorun tek bir hatada listelenir ve hiçbir şey
   yazılmaz.
2. **generate**: oyunun build'ini `.illusion/export/<platform>/` altına yazar (`CMakeLists.txt`, dosya listeleri).
3. **cook**: `assets:/` altındaki her Asset'i yeni bir cache'e import eder; eski bir cache gönderilemez.
4. **pack**: `assets.illpak`, `settings.illpak` ve `cache.illpak` dosyalarını yazar.
5. **configure** ve **build**: `cmake`, executable'ı modüllerden ve SDK'nın player'ından configure edip build eder,
   ardından ürettiğini imzalar (bkz. [İmzalama](#imzalama)).
6. **install**: bir geçici dizine kurar ve ancak tamamlanınca `<output>/<Product>` konumuna taşır (macOS'ta
   `<output>/<Product>.app`).
7. **package**: `package` verilmişse CPack paketi yazar; installer imzalanır ve macOS'ta notarize edilir.

## Çıktı

```text
<output>/
  Showcase/
    showcase.exe          the game (no extension on Linux and macOS)
    data/
      assets.illpak       the project's assets, minus excluded files and glTF sources
      settings.illpak     the project settings, without export.json
      cache.illpak        the cooked assets
```

Linux'ta icon ve desktop dosyası executable'ın yanında durur (bkz. [Linux](#linux)); macOS'ta oyun bir bundle'dır
(bkz. [macOS bundle](#macos-bundle)).

glTF kaynakları (`.gltf`, `.glb` ve adlandırdıkları buffer'lar) cache onların yerini aldığı için çıkarılır; görüntü
kaynakları `assets.illpak` içinde kalır. Executable Editor kodu içermez.

## Paketler

`package`, dağıtım için `output` içinde oyunun dizininin yanına tek bir dosya yazar. Dosyanın adı
`<Product>-<version>-<platform><suffix>` biçimindedir; sürüm projeninkidir ve harf, rakam, `.`, `-`, `+` veya `_`
olmayan karakterler `_` olarak yazılır.

| `package` | Windows | macOS | Linux |
| --- | --- | --- | --- |
| `none` | Yalnızca oyunun dizini. | Yalnızca bundle. | Yalnızca oyunun dizini. |
| `archive` | `Showcase` dizinini içeren `Showcase-1.0.0-windows.zip`. | `Showcase.app` içeren `Showcase-1.0.0-macos.zip`. | `Showcase` dizinini içeren `Showcase-1.0.0-linux.tar.gz`. |
| `installer` | `Showcase-1.0.0-windows-setup.exe`, bir NSIS installer'ı. | Bundle'ı ve `/Applications` bağlantısını içeren `Showcase-1.0.0-macos.dmg`. | `archive` ile aynı `.tar.gz`. |

Windows installer'ı `Program Files\<Product>` altına kurar; bir Başlat menüsü kısayolu, bir uninstaller ve oyunun
icon'unu ekler, eski bir kurulum varsa önce onu kaldırır. Sonucun `package` alanı dosyanın tam yoludur; başarısız
bir export hiçbir dosya bırakmaz.

Paketleri, SDK'nın CMake'inden CPack üretir; `cpack` onun yanında bulunmalıdır. Installer'lar bir araç daha ister;
eksik bir araç, onu adlandıran bir hatayla doğrulamayı başarısız kılar:

- Windows: [NSIS](https://nsis.sourceforge.io)'ten `makensis`; `PATH` üzerinde ya da `Program Files\NSIS` içinde;
- macOS: `hdiutil`; her macOS'ta vardır.

## İmzalama

İmzalama isteğe bağlıdır ve oyunun ne yaptığını asla değiştirmez. Param'larda verilmeyen bir ayar, Editor
sürecinin bir ortam değişkeninden gelir; böylece build makinesi sırlarını komutların ve projelerin dışında tutar.
Param'lardaki boş bir string, o ayarı bu export için kapatır. Sırlar hiçbir zaman proje dosyalarına yazılmaz ve
çıktı, `signCommand`'ın çalıştırdığı programın yalnızca adını gösterir; parola içerebilecek argümanlarını asla
göstermez. Başka bir platformun ayarı hatadır: macOS ve Linux'ta `signCommand`, Windows ve Linux'ta
`codesignIdentity` ile `notaryProfile`.

İmzalama install'dan önce, build'in ürettiği dosyalar üzerinde yapılır. Oyunun dizini ve her paket bu dosyalardan
kopyalanır; hepsi aynı imzalı baytları taşır ve raporun `signedBinaries` alanı true olur.

### Windows

`signCommand` (varsayılan: `ILLUSION_SIGN_COMMAND` ortam değişkeni), bir program ve `{file}` yer tutucusundan oluşan
bir komut satırıdır. Çift tırnak, boşluk içeren bir parçayı gruplar; ters bölü için kaçış gerekmez. Export onu
executable üzerinde ve `package: "installer"` için kurulum programı üzerinde çalıştırır:

```json
{"call": "export.build", "params": {"output": "C:/Games/out", "package": "installer", "signCommand": "signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /a {file}"}}
```

Program `PATH` üzerinde olmalıdır. Program ve `{file}` içermeyen bir komut doğrulamayı başarısız kılar.

### macOS

- `codesignIdentity` (varsayılan: `ILLUSION_CODESIGN_IDENTITY`), `codesign`'ın kullandığı kimliktir; örneğin
  `Developer ID Application: Example Ltd (TEAMID)`. Export önce `Contents/Frameworks` içindeki her kütüphaneyi, sonra
  bundle'ı hardened runtime ve zaman damgasıyla imzalar. Dağıttığınız bir bundle için Developer ID kimliği gerekir.
- `-` kimliği ad hoc imzalar: sertifika ve zaman damgası yoktur; bundle'ın açıldığını yerelde denemek için uygundur.
  Bu durumda üretilen entitlements, bundle'a konan Vulkan loader'ın yüklenebilmesi için library validation'ı kapatır;
  gerçek bir kimlik entitlement istemez.
- `installer` paketinde, kimlik `-` değilse DMG de imzalanır.
- `notaryProfile` (varsayılan: `ILLUSION_NOTARY_PROFILE`), bir `notarytool` keychain profilini adlandırır. Export
  DMG'yi `xcrun notarytool submit --wait` ile gönderir, ardından bileti `xcrun stapler staple` ile ekler ve raporun
  `notarized` alanı true olur. Bir Developer ID kimliği (boş veya `-` olmayan), `package: "installer"` ve `xcrun`
  gerekir.

Profili export eden makinede bir kez oluşturun; parola, Apple ID'nizin uygulamaya özel parolasıdır ve keychain içinde
kalır:

```bash
xcrun notarytool store-credentials "illusion-notary" --apple-id you@example.com --team-id TEAMID
```

`illusion-notary` profil adını `notaryProfile` olarak verirsiniz. Başarısız bir imzalama veya notarization, aracın son
satırlarını hatada aktarır ve export paket bırakmadan başarısız olur.

Bu ayarların hiçbiri yoksa export imzasızdır: `signedBinaries` ve `notarized` false olur.

## macOS bundle

macOS'ta oyun `<Product>.app` olur; executable ürünün adını taşır:

```text
Showcase.app/
  Contents/
    Info.plist                   bundle identifier, name, version, copyright, icon, minimum macOS
    MacOS/Showcase               the game
    Resources/
      data/*.illpak              the archives
      *.icns                     the icon
      vulkan/icd.d/MoltenVK_icd.json
    Frameworks/
      libvulkan.1.dylib          the Vulkan loader
      libMoltenVK.dylib          MoltenVK
```

Bundle kendi Vulkan loader'ını ve MoltenVK'yı taşır; bu yüzden Vulkan SDK olmayan bir Mac'te çalışır. Export bunları
build eden makineden kopyalar: Vulkan SDK (`VULKAN_SDK` ayarlı) veya Homebrew
(`brew install vulkan-loader molten-vk`). İkisi de bulunamazsa CMake uyarı verir ve bundle, oyuncunun Mac'inde bir
Vulkan loader'a ihtiyaç duyar.

## Linux

Oyunun dizini, executable ve `data/` dışında `<executable>.png` (icon, 512 piksel) ve `<executable>.desktop` dosyasını
da içerir; `.desktop`, ürün adını, `Exec` olarak executable'ı, icon'u ve `Game` kategorisini taşıyan bir desktop
girdisidir. Oyunu menülerde listelemek için üçünü bir masaüstünün uygulama dizinlerine kopyalayın. `.tar.gz` paketi
bunları da içerir.

## Rapor

Sonuç; `succeeded`, ulaşılan `stage`, `error`, `platform`, `configuration`, `directory` (oyunun dizini), `files`
(gönderilen her dosya ve boyutu, `output`'a göre), `totalBytes`, `skipped`, `output` (araç çıktısının son satırları)
ile `errors` ve `warnings` sayılarını taşır. `package` paket dosyasının tam yoludur (paket yoksa
boş), `signedBinaries` executable'ın veya bundle'ın imzalandığını, `notarized` ise DMG'nin notarize edilip staple
edildiğini gösterir.

## Export başarısız olursa

Bir doğrulama sorunu, her nedeni adlandıran bir mesajla komutu başarısız kılar. Başarısız bir cook, derleme veya
link komutu başarısız kılmaz: komut `succeeded: false`, başarısız olan `stage` ve dosya, satır ve mesaj içeren
`diagnostics` ile döner; tıpkı `module.build` gibi. Geçici dizin silinir, bu yüzden çıktıda hiçbir zaman yarım bir
oyun kalmaz. Önce `diagnostics`, ham araç satırları için ise `output` alanını okuyun.

## Henüz olmayanlar

Boş olmayan bir `linuxRuntime` "not supported yet" hatasıyla reddedilir: executable'ın Steam Runtime'a karşı denetimi
Steam çalışmasıyla (M8-6) gelir. O zamana kadar neler yapabileceğinizi [yayın rehberi](../../guides/release.md)
anlatır.

## Sınırlar

- Hariç tutulmadıkça `assets:/` altındaki her şey gönderilir; export, sahnelerin eriştiklerini taramaz.
- Adı, sürümü veya başlangıç sahnesini değiştirmek yeniden export etmek demektir.
- İlk export CMake'i configure eder ve dakikalar sürer.
- Dosyaları dışarıda bırakmanın tek yolu exclude desenleridir; hariç tutulan bir dosyaya bağlı Asset cook'u
  başarısız kılar.
- Image kaynakları, cook edilmiş hallerinin yanında `assets.illpak` içinde kalır; yalnızca glTF kaynakları atılır.
- Gerçek sertifikayla imzalama ve notarization CI'da test edilmez, çünkü CI'da sertifika yoktur: imzalı bir
  paketi her yayından önce elle denetleyin (`codesign --verify`, `spctl`, temiz bir makinede başlatma).
- Package aşaması başarısız olursa bitmiş oyun dizini kalır.
