# Oyunu yayınlamak

Bu rehber, bitmiş bir projeyi bir mağazaya yükleyebileceğiniz dosyalara götürür. Her adım
[`export.build`](../manual/editor/export.md) komutunu kullanır; her ayarın ayrıntıları için o sayfaya bakın.
Projenin doğrulamadığı şeyler [Bilinen sınırlar](#bilinen-snrlar) bölümünde listelenir: bir adıma
güvenmeden önce onu okuyun.

Yayınladığınız platformda export edin: export, onu build eden makinenin platformunu ve mimarisini hedefler; bu
yüzden Windows build'i Windows'tan, macOS build'i macOS'tan ve Linux build'i Linux'tan gelir.

## Kontrol listesi

1. Export ayarlarını doldurun ve sürümü belirleyin.
2. Paketli bir Release export'u alın.
3. İmzalayın (Windows) veya imzalayıp notarize edin (macOS); Linux'ta build aldığınız dağıtımı denetleyin.
4. Steam için: SDK'yı verin, `store` ayarını yapın ve yeniden export edin.
5. Sonucu bir oyuncu gibi denetleyin.
6. Yükleyin.

## 1. Export ayarlarını hazırlayın

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
- `exclude`, dosyaları oyunun dışında bırakır; örneğin kaynak dosyalar için `raw/**`. Dışarıda bırakılan bir
  dosyaya ihtiyaç duyan bir Asset export'un cook adımını bozar; bu yüzden bir export deneyin.

Oyunun sürümü, `project.illusion` içindeki projenin `version` değeridir. Paketi adlandırır
(`Showcase-1.0.0-windows-setup.exe`) ve Windows sürüm kaynağına ve macOS bundle'ına yazılır; bu yüzden export'tan
önce değiştirin, sonra değil.

## 2. Release export'u alın

```json
{"call": "export.build", "params": {"output": "C:/Games/out", "configuration": "Release", "package": "installer", "clean": true}}
```

- `configuration: "Release"`, oyunu ve modüllerini optimizasyonla build eder. O configuration için player içeren
  bir SDK gerekir.
- `clean: true`, export'un Build ağacını siler; böylece eski bir build'den hiçbir şey sürüme girmez.
- `package: "installer"`, Windows'ta bir NSIS `setup.exe` (`makensis` gerekir) ve macOS'ta bir DMG (`hdiutil`
  gerekir) yazar. `archive` bir ZIP yazar; Linux'ta `.tar.gz`, orada `installer` aynı dosyayı üretir.

Bir projenin ilk export'u configure ve link eder; bu dakikalar sürer. Aynı export Editor'den de çalışır:
**File > Export…** [Export penceresini](../manual/editor/export.md#export-penceresi) açar.

## 3. Windows'ta imzalama

İmzasız bir oyun açılır, ancak Windows SmartScreen onun hakkında uyarır. `signtool` gibi bir imzalama aracı
kurun, sertifikayı ona erişilebilir kılın ve export'a dosyanın geleceği yere `{file}` koyduğunuz bir komut satırı
verin. Komut satırını Editor'ün ortamı tutar; böylece hiçbir komutta ve proje dosyasında bulunmaz:

```bash
set ILLUSION_SIGN_COMMAND=signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /a {file}
illusion_ctl --project C:/games/showcase call export.build "{\"package\": \"installer\"}"
```

`export.build` komutunun `signCommand` param'ı aynı işi tek bir export için yapar. Program `PATH` üzerinde olmalı;
çift tırnak boşluklu bir parçayı gruplar.

Export iki şeyi şu sırayla imzalar:

1. Oyunun executable'ı: build edildikten hemen sonra, install'dan önce. Oyunun dizini, ZIP ve installer'ın içeriği
   o dosyadan kopyalanır; bu yüzden hepsi aynı imzalı baytları taşır.
2. `package` değeri `installer` ise kurulum programı.

Başka hiçbir dosyayı imzalamaz: executable'ın yanında dağıttığınız bir DLL, yanında geldiği imzayı taşır ya da hiç
taşımaz. Çıktı yalnızca programın adını gösterir, argümanlarını asla göstermez; çünkü parola içerebilirler.

## 4. macOS'ta imzalama ve notarization

1. Anahtarlığınıza bir Developer ID Application sertifikası kurun. Kimlik, onun adıdır; örneğin
   `Developer ID Application: Example Ltd (TEAMID)`.
2. Export eden makinede bir notarization profilini bir kez kaydedin (parola, Apple ID'nizin uygulamaya özel
   parolasıdır ve anahtarlıkta kalır):

   ```bash
   xcrun notarytool store-credentials "illusion-notary" --apple-id you@example.com --team-id TEAMID
   ```

3. Kimlik ve profille export edin:

   ```bash
   export ILLUSION_CODESIGN_IDENTITY="Developer ID Application: Example Ltd (TEAMID)"
   export ILLUSION_NOTARY_PROFILE="illusion-notary"
   illusion_ctl --project ~/games/showcase call export.build '{"package": "installer"}'
   ```

Export'un otomatik yaptıkları: `Contents/Frameworks` içindeki kütüphaneleri, ardından bundle'ı hardened runtime ve
zaman damgasıyla imzalar; DMG'yi imzalar; DMG'yi `notarytool submit --wait` ile gönderir; bileti staple eder.
Bekleme süresi Apple'a bağlıdır, çoğu zaman birkaç dakikadır. Notarization bir Developer ID kimliği ve
`package: "installer"` ister. Sonucun `signedBinaries` ve `notarized` alanları ne olduğunu söyler.

Elle denetlemeniz gerekenler, bitmiş DMG üzerinde ve tercihen oyunu hiç build etmemiş bir Mac'te:

```bash
codesign --verify --deep --strict --verbose=2 Showcase.app
spctl -a -vv Showcase.app
xcrun stapler validate Showcase-1.0.0-macos.dmg
```

Notarize edilmiş bir uygulama için `spctl`, `source=Notarized Developer ID` ile `accepted` bildirir. Sonra
uygulamayı bağlanmış DMG'den açın ve başlangıç sahnesini oynayın.

`-` kimliği ad hoc imzalar; bundle'ı kendi Mac'inizde denemek için uygundur. Böyle bir bundle dağıtım için
değildir.

## 5. Linux ve Steam Runtime

Bir Linux export'u; executable, `data/`, icon ve bir `.desktop` dosyası içeren bir dizindir (ve bir `.tar.gz`);
hiçbir şey imzalanmaz. Executable, onu build eden makinenin sistem kütüphanelerine link edilir; o dağıtımda ve
daha yenilerinde çalışır, daha eskilerinde çalışmaz.

Steam, Linux oyunlarını Steam Runtime "sniper" container'ında çalıştırır. Export, executable'ı ona karşı henüz
denetlemez: `linuxRuntime` "not supported yet" ile reddedilir ve proje sniper container'larında bir export'u ne
build etmiş ne de çalıştırmıştır. Bu olana kadar desteklediğiniz en eski dağıtımda build alın ve Steam build'ini
yayınlamadan önce bir Linux makinesinde Steam istemcisi üzerinden deneyin.

## 6. Steam

### SDK'yı verin

Steamworks SDK Valve'indir, tescillidir ve illusion'ın ya da bir projenin parçası değildir. Valve'den indirin ve
Editor'ün `ILLUSION_STEAMWORKS_SDK` ortam değişkenini onun dizinine, yani `public/steam/steam_api.h` dosyasının
bulunduğu dizine ayarlayın:

```bash
export ILLUSION_STEAMWORKS_SDK=~/sdk/steamworks
```

Export başlamadan önce başlığı ve platformun redistributable kütüphanesini (`steam_api64.dll`,
`libsteam_api.so`, `libsteam_api.dylib`) denetler; biri eksikse değişkenin adını veren bir mesajla başarısız olur.
Motorun release paketi SDK'yı içermez: oyunun build'i `illusion::steam`'i sizin SDK'nıza karşı derler.

### Store'u ve app id'yi ayarlayın

App id'niz, uygulamayı oluşturduğunuz Steamworks'ten gelir. Onu `settings:/export.json` içinde ayarlayın; Export
penceresi store'u henüz göstermez:

```json
{"call": "project.settings_set", "params": {"type": "illusion.ExportSettings", "value": {"productName": "Showcase", "store": {"kind": "steam", "steamAppId": 480}}}}
```

`project.settings_set` değerin tamamını değiştirir; bu yüzden 1. adımdaki diğer alanları da gönderin. 480 numaralı
id Valve'in test uygulamasıdır ve build'i denemek için uygundur.

Bir geliştirme configuration'ında (`Release` ve `MinSizeRel` dışındaki her şey) yapılan export ayrıca
executable'ın yanına `steam_appid.txt` yazar; böylece siz test ederken oyun Steam dışında da başlar. Release
export'u onu yazmaz ve hiçbir pakete girmez. Onu bir depot'a eklemeyin: Steam üzerinden başlatılan bir oyun ona
ihtiyaç duymaz.

### Depot'lar

Bir depot, bir platformun dosyalarını tutar. Her platformda export edin, sonra tablonun adını verdiğini export'un
yazdığı gibi yükleyin; çıktı düzeni [Çıktı](../manual/editor/export.md#ckt) bölümünde anlatılır.

| Platform | Depot içeriği | Başlatma seçeneği |
| --- | --- | --- |
| Windows | `<output>/<Product>/`: executable, `data/` ve `steam_api64.dll`. | Executable, `showcase.exe`. |
| macOS | `<output>/<Product>.app`; `libsteam_api.dylib` `Contents/MacOS` içindedir. | `.app`. |
| Linux | `<output>/<Product>/`: executable, `data/`, `libsteam_api.so`, icon ve `.desktop` dosyası. | Executable, `showcase`. |

Kurulum programını ya da DMG'yi değil, dizini yükleyin: Steam dosyaları kendisi kurar ve installer ya da disk
imajı bir şey eklemez. Export'tan sonra hiçbir dosyayı değiştirmeyin; değişen bir dosya imzasını kaybeder.
Depot'ları Valve'in kendi araçlarıyla oluşturun ve yükleyin (Steamworks partner sitesi ve `steamcmd`).

### Cloud kayıtları ve achievement'lar

Bir oyun modülü store'a `StoreService` ile ulaşır. Aynı çağrılar Play modunda, testlerde ve Steam'de çalışır;
store yokken null implementasyonu achievement'ları ve stat'ları bellekte, cloud dosyalarını `user:/cloud` içinde
tutar. İsimler 1 ile 64 arasında ASCII harf, rakam, `_`, `-` ve `.` karakterlerinden oluşur ve bir achievement'ın
adı, Steamworks'te ona verdiğiniz API adıdır. Showcase modülünün kodu şudur:

```cpp
// Count this run in a cloud file.
std::array<std::byte, sizeof(u32)> save{};
const Expected<u64, StoreError> read = store.readCloudFile("showcase.save", save);
if (read && *read == save.size())
{
    std::memcpy(&runtime.runs, save.data(), save.size());
}
++runtime.runs;

// Later: an achievement, a stat, and the cloud file again.
static_cast<void>(store.unlockAchievement("SHOWCASE_STARTED"));
static_cast<void>(store.setAchievementProgress("SHOWCASE_WALKER", {frame / 60, 4}));
static_cast<void>(store.setStatInt("showcase_frames", static_cast<i32>(stats.frames)));
static_cast<void>(store.storeStats());
std::memcpy(save.data(), &runtime.runs, save.size());
static_cast<void>(store.writeCloudFile("showcase.save", save));
```

Achievement'ları, stat'ları ve Steam Cloud kotasını yüklemeden önce Steamworks'te tanımlayın; oyun bunları
oluşturamaz. Oyunlar `overlayActive()` değerini her karede sorgular ve Steam overlay'i onları kapatırken genellikle
duraklar. [Scripting API](../reference/scripting-api.md) sayfasına ve
`samples/showcase/modules/showcase/showcase.cpp` dosyasına bakın.

## 7. Yayınlamadan önce

- [ ] Sürüm, `project.illusion` içinde istediğiniz sürümdür ve export'un paket adı onu gösterir.
- [ ] `configuration: "Release"` ve `clean: true` ile export ettiniz; `succeeded` true ve `errors` 0.
- [ ] Windows: `signedBinaries` true ve `signtool verify /pa /v` executable ve kurulum programı için geçer.
- [ ] macOS: `signedBinaries` ve `notarized` true; `codesign`, `spctl` ve `stapler` 4. adımdaki gibi geçer.
- [ ] Paketi ne motorun ne de bir SDK'nın bulunduğu bir makinede kurdunuz veya açtınız ve başlangıç sahnesini
      oynadınız.
- [ ] Steam: build bir Release export'undan geldi, `steam_appid.txt` içermez ve onu Steam istemcisi üzerinden,
      oturum açıkken başlatıp achievement'ların, stat'ların ve cloud kaydının çalıştığını gördünüz.
- [ ] Linux: desteklediğiniz en eski dağıtımda denediniz.
- [ ] Export'un dosyalarını değiştirmeden yüklediniz.

## Bilinen sınırlar

Projenin kendi testleri bunları denetleyemez; elle yapın.

- **Gerçek sertifikalarla imzalama ve notarization'ın CI kapsamı yoktur.** CI, Windows hook'unu bir kayıt komutuyla,
  macOS'u ad hoc kimlikle sınar. Gerçek bir sertifika, gerçek bir notarization ve temiz bir makinede Gatekeeper
  denetimi, her release'ten önce elle yapılır.
- **Steam backend'i hiç build edilmedi ve test edilmedi.** `SteamStoreService` ve `steam` paket bileşeni,
  geliştirme makinesinde ve CI'da bulunmayan Steamworks SDK'sını ister. Yalnızca null store build edilir ve test
  edilir. İlk Steam build'inizde hatalar bulmayı bekleyin; achievement'ları, stat'ları ve cloud kayıtlarını Steam
  istemcisinde test edin.
- **Steam Input glyph'leri açık bir iştir.** Steam'de `glyphPath()` hiçbir şey döndürmez: kendi tuş
  görsellerinizi çizin.
- **Steam üzerinden başlatılma zorunluluğu uygulanmaz.** `SteamAPI_RestartAppIfNecessary()` çağrılmaz; bu yüzden
  Steam dışında başlatılan bir Steam oyunu null store ile çalışır.
- **Sniper container denetimi açık bir iştir.** `linuxRuntime` reddedilir ve hiçbir CI job'ı Steam Runtime
  container'larında export etmez ya da çalıştırmaz.
- **Proje tarafından denetlenmeyenler:** macOS bundle'ının içindeki Steam kütüphanesinin export'un imzalamasına
  dahil olduğu ve Steam'in yukarıdaki tablodaki depot düzenlerini kabul ettiği. İkisini de ilk yüklemenizde
  doğrulayın.
- Export penceresi `store` ayarını henüz göstermez.
- Başarısız bir Package aşaması, bitmiş oyun dizinini bırakır.
