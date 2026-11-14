# Oyunu yayınlamak

Bu rehber, bir projeyi Editor'den bir mağazaya yükleyebileceğiniz dosyalara götürür. Her adım
[`export.build`](../manual/editor/export.md) komutunu kullanır; her ayarın ayrıntıları için o sayfaya bakın.

Yayınladığınız platformda export edin: export, onu build eden makinenin platformunu ve mimarisini hedefler; bu
yüzden Windows build'i Windows'tan, macOS build'i macOS'tan gelir.

## Kontrol listesi

1. Export ayarlarını doldurun.
2. Sürümü belirleyin.
3. Paketli bir Release export'u alın.
4. İmzalayın (Windows) veya imzalayıp notarize edin (macOS).
5. Sonucu denetleyin.
6. Yükleyin.

## 1. Export ayarları

`settings:/export.json` dosyasını `project.settings_set` ile düzenleyin:

```json
{"call": "project.settings_set", "params": {"type": "illusion.ExportSettings", "value": {"productName": "Showcase", "organization": "Example Ltd", "identifier": "com.example.showcase", "copyright": "Copyright 2026 Example Ltd", "icon": "assets:/icons/showcase.png"}}}
```

- `productName` oyunun dizinini, penceresini ve paketini adlandırır; `organization` yayıncıdır ve kullanıcı verisi
  dizininin üst dizinidir.
- `icon`, en az 256 piksellik kare bir PNG'dir. Yoksa oyun motorun icon'unu gösterir.
- `identifier`, macOS bundle identifier'ıdır. Bir kez seçin: macOS'a uygulamanın adını söyler ve farklı bir
  identifier farklı bir uygulamadır.
- `copyright`, Windows dosya özelliklerine ve macOS bundle'ına yazılır.

## 2. Sürüm

Oyunun sürümü, `project.illusion` içindeki projenin `version` değeridir. Paketi adlandırır
(`Showcase-1.0.0-windows-setup.exe`) ve Windows sürüm kaynağına ve macOS bundle'ına yazılır; bu yüzden export'tan
önce değiştirin, sonra değil.

## 3. Release export'u alın

```json
{"call": "export.build", "params": {"output": "C:/Games/out", "configuration": "Release", "package": "installer", "clean": true}}
```

- `configuration: "Release"`, oyunu ve modüllerini optimizasyonla build eder. O configuration için player içeren
  bir SDK gerekir.
- `clean: true`, export'un Build ağacını siler; böylece eski bir build'den hiçbir şey sürüme girmez.
- `package: "installer"`, Windows'ta bir NSIS `setup.exe` (`makensis` gerekir) ve macOS'ta bir DMG (`hdiutil`
  gerekir) yazar. `archive` bir ZIP yazar; Linux'ta `.tar.gz`, orada `installer` aynı dosyayı üretir.

Bir projenin ilk export'u configure ve link eder; bu dakikalar sürer.

## 4. İmzalayın

İmzasız bir oyun açılır, ancak Windows SmartScreen onun hakkında uyarır ve macOS Gatekeeper onu engeller. Sırları
Editor sürecine ortamı üzerinden verin; böylece hiçbir komutta ve proje dosyasında bulunmazlar.

### Windows

`signtool` gibi bir imzalama aracı kurun, sertifikayı ona erişilebilir kılın ve dosyanın geleceği yere `{file}`
koyarak komut satırını ayarlayın:

```bash
set ILLUSION_SIGN_COMMAND=signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /a {file}
illusion_ctl --project C:/games/showcase call export.build "{\"package\": \"installer\"}"
```

Export, executable'ı install'dan önce imzalar; böylece oyunun dizini, ZIP ve installer aynı imzalı dosyayı taşır;
ardından installer'ı imzalar.

### macOS

1. Anahtarlığınıza bir Developer ID Application sertifikası kurun.
2. Bir notarization profilini bir kez kaydedin (parola, Apple ID'nizin uygulamaya özel parolasıdır):

   ```bash
   xcrun notarytool store-credentials "illusion-notary" --apple-id you@example.com --team-id TEAMID
   ```

3. Kimlik ve profille export edin:

   ```bash
   export ILLUSION_CODESIGN_IDENTITY="Developer ID Application: Example Ltd (TEAMID)"
   export ILLUSION_NOTARY_PROFILE="illusion-notary"
   illusion_ctl --project ~/games/showcase call export.build '{"package": "installer"}'
   ```

Export, bundle içindeki kütüphaneleri, ardından bundle'ı hardened runtime ile imzalar; DMG'yi imzalar; Apple'a
gönderip kararı bekler; bileti staple eder. Süre Apple'a bağlıdır, çoğu zaman birkaç dakikadır. Notarization
`package: "installer"` ister.

Bundle'ı sertifikasız kendi Mac'inizde denemek için `-` kimliğini (ad hoc) kullanın. Böyle bir bundle dağıtım için
değildir.

## 5. Sonucu denetleyin

`export.build` sonucunda `succeeded: true`, `errors: 0`, `package` içinde tam yol, `signedBinaries` ve (macOS'ta bir
profille) `notarized` true olmalıdır. Sonra dosyaları bir oyuncu gibi denetleyin:

- Paketi, ne motorun ne de SDK'nın bulunduğu bir makinede kurun veya açın, oyunu çalıştırın ve başlangıç sahnesini
  oynayın.
- Windows: executable için (ve kurulum programı için) `signtool verify /pa /v showcase.exe`; sürüm ve telif için
  dosya özelliklerinin Ayrıntılar sekmesi.
- macOS: imzayı doğrulayın, Gatekeeper'a uygulamayı sorun ve DMG üzerindeki bileti denetleyin:

  ```bash
  codesign --verify --deep --strict --verbose=2 Showcase.app
  spctl -a -vv Showcase.app
  xcrun stapler validate Showcase-1.0.0-macos.dmg
  ```

  Notarize edilmiş bir uygulama için `spctl`, `source=Notarized Developer ID` ile `accepted` bildirir.

## 6. Yükleyin

`package` içinde adı geçen paket dosyasını yükleyin: kurulum programı, DMG veya arşiv. Export'tan sonra hiçbir
dosyayı değiştirmeyin; değişen bir dosya imzasını kaybeder.

### Steam

Bugün doğru olan: oyunun dizini (Windows ve Linux'ta `<output>/<Product>`, macOS'ta `<Product>.app`) eksiksiz bir
oyundur ve ihtiyacı olan her şeyi içerir; bu yüzden onu Steam'in kendi araçlarıyla bir depot olarak
yükleyebilirsiniz. Steamworks modülü (achievement'lar, cloud kayıtları, Steam Input) ve bir Linux export'unun Steam
Runtime "sniper"a karşı denetimi M8-6'da planlıdır ve henüz yoktur: `linuxRuntime` reddedilir ve bir export, onu link
eden Linux makinesi kadar taşınabilirdir. O zamana kadar bir Linux build'ini desteklediğiniz en eski dağıtımda deneyin.
