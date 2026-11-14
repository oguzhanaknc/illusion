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

Oyunun sürümü projenin `version` değeridir. `identifier`, `copyright` ve `icon` denetlenir ve ayarlarda saklanır,
ancak executable bunları henüz kullanmaz; M8-4'ün paketleriyle devreye girerler.

### Exclude desenleri

Bir desen, `assets:/` altına göre bir yoldur ve `/` ayırıcısı kullanır. `*` tek bir dizin içinde herhangi bir
karakter dizisiyle eşleşir, `**` dizinler dahil her diziyle eşleşir, `**/` ise hiç dizinle de eşleşir:

```json
{"exclude": ["raw/**", "*.psd", "**/notes.txt"]}
```

Hariç tutulan dosya ne paketlenir ne de cook edilir. Ona ihtiyaç duyan bir Asset (örneğin hariç tutulan bir
Texture'ı adlandıran bir Material) export'un cook adımını başarısız kılar; başlangıç sahnesini hariç tutmak da
öyle. Rapor, dışarıda bırakılanları `skipped` içinde listeler.

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

`export.build` [asenkrondur](automation.md): aşamasını ilerleme olarak bildirir ve export bitince döner. Aynı
anda yalnızca bir modül build'i veya export çalışır; ikincisi yeniden denenebilir bir `Busy` ile başarısız olur.

## Aşamalar

1. **validate**: proje, ayarları, modülleri ve SDK denetlenir; her sorun tek bir hatada listelenir ve hiçbir şey
   yazılmaz.
2. **generate**: oyunun build'ini `.illusion/export/<platform>/` altına yazar (`CMakeLists.txt`, dosya listeleri).
3. **cook**: `assets:/` altındaki her Asset'i yeni bir cache'e import eder; eski bir cache gönderilemez.
4. **pack**: `assets.illpak`, `settings.illpak` ve `cache.illpak` dosyalarını yazar.
5. **configure** ve **build**: `cmake`, executable'ı modüllerden ve SDK'nın player'ından configure edip build eder.
6. **install**: bir geçici dizine kurar ve ancak tamamlanınca `<output>/<Product>` konumuna taşır.

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

glTF kaynakları (`.gltf`, `.glb` ve adlandırdıkları buffer'lar) cache onların yerini aldığı için çıkarılır; görüntü
kaynakları `assets.illpak` içinde kalır. Executable Editor kodu içermez.

## Rapor

Sonuç; `succeeded`, ulaşılan `stage`, `error`, `platform`, `configuration`, `directory` (oyunun dizini), `files`
(gönderilen her dosya ve boyutu, `output`'a göre), `totalBytes`, `skipped`, `output` (araç çıktısının son satırları)
ile `errors` ve `warnings` sayılarını taşır. Paketler ve imzalama gelene kadar `package` boştur,
`signedBinaries` ve `notarized` false olur.

## Export başarısız olursa

Bir doğrulama sorunu, her nedeni adlandıran bir mesajla komutu başarısız kılar. Başarısız bir cook, derleme veya
link komutu başarısız kılmaz: komut `succeeded: false`, başarısız olan `stage` ve dosya, satır ve mesaj içeren
`diagnostics` ile döner; tıpkı `module.build` gibi. Geçici dizin silinir, bu yüzden çıktıda hiçbir zaman yarım bir
oyun kalmaz. Önce `diagnostics`, ham araç satırları için ise `output` alanını okuyun.

## Henüz olmayanlar

Aşağıdaki parametreler M8-4'e kadar "not supported yet" hatasıyla reddedilir:

- paketler (`package`: `archive`, `installer`);
- imzalama ve notarization (`signCommand`, `codesignIdentity`, `notaryProfile`);
- Linux Steam Runtime denetimi (`linuxRuntime`);
- macOS `.app` paketi.

## Sınırlar

- Hariç tutulmadıkça `assets:/` altındaki her şey gönderilir; export, sahnelerin eriştiklerini taramaz.
- Adı, sürümü veya başlangıç sahnesini değiştirmek yeniden export etmek demektir.
- İlk export CMake'i configure eder ve dakikalar sürer.
- Dosyaları dışarıda bırakmanın tek yolu exclude desenleridir; hariç tutulan bir dosyaya bağlı Asset cook'u
  başarısız kılar.
