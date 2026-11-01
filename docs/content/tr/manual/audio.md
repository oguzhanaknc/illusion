# Ses

illusion sesi [miniaudio](https://github.com/mackron/miniaudio) ile çalar: mesafe zayıflaması, panlama,
ses konileri ve doppler etkisiyle 3D ses, müzik streaming, bus'lı, filtreli ve reverb'lü bir mikser ve
arka planda yüklenen clip'ler. Sesler, `AudioSource` taşıyan sıradan
Entity'lerdir; `AudioListener` ise kamera Entity'sinde durur.

Header'lar: `<illusion/audio/AudioWorld.hpp>`, `<illusion/audio/AudioComponents.hpp>`

Kütüphane: `illusion::audio`

## Başlarken

```cpp
illusion::VirtualFileSystem files;
files.mount("game", gameDataDirectory);
illusion::World world;
illusion::AudioWorld audio(world, files);

// The listener: usually the camera Entity.
world.create(cameraTransform, illusion::AudioListener{});

// A crackling fire, positioned at its Transform.
illusion::AudioSource fire;
fire.clip = illusion::AssetRef("game:/audio/fire.ogg");
fire.loop = true;
world.create(fireTransform, fire);

// Background music: not spatial, streamed, on the music bus.
illusion::AudioSource music;
music.clip = illusion::AssetRef("game:/audio/theme.ogg");
music.bus = illusion::AudioBus::Music;
music.spatial = false;
music.stream = true;
music.loop = true;
world.create(music);

while (running)
{
    audio.update(frameSeconds); // starts new sources, moves 3D sounds, hands the mix to the audio thread
}
```

`AudioWorld::update()` her şeyi tek bir çağrıda yapar:

1. İlk kez görülen `AudioSource` Entity'leri clip'lerini yükler ve bir `AudioPlayback` Component'i alır
   (çalışma zamanı bağlantısı; kendiniz eklemeyin). `playOnStart` (varsayılan) açık olanlar çalmaya
   başlar ya da clip'leri arka planda yüklenince başlar (aşağıya bakın).
2. Yok edilen Entity'lerin ya da `AudioSource`'unu kaybeden Entity'lerin sesleri durur; biten sesler
   voice'larını serbest bırakır.
3. Ses seviyeleri, pitch'ler, bus'lar ve uzamsal seslerle listener'ın konum ve hızları ses thread'ine
   aktarılır. Doppler etkisi için hızlar, `Transform` hareketinin kare süresine bölümüdür.

Miksaj ses cihazının thread'inde çalışır; asla bellek ayırmaz ve oyun thread'ini beklemez. Yaptığınız
değişiklikler bir sonraki `update()`'te duyulur.

## Clip'ler

| Format | Uzantılar |
| --- | --- |
| WAV | `.wav` |
| FLAC | `.flac` |
| MP3 | `.mp3` |
| Ogg Vorbis | `.ogg` |

Clip'ler [sanal dosya sistemi](filesystem.md) üzerinden okunur; böylece hem açık dosyalardan hem de
paketlenmiş arşivlerden çalışırlar. Miksaj hızına dönüştürülürler; ikiden fazla kanal stereoya indirgenir
(downmix).

| `AudioClipMode` | Bellek | Kullanım |
| --- | --- | --- |
| `Decode` (varsayılan) | Çözülmüş PCM (örnek başına 4 bayt) | Efektler; istenen sayıda voice ucuza çalar |
| `Stream` (`AudioSource::stream`) | Sıkıştırılmış dosya; her voice çalarken çözer | Müzik, uzun ambiyans |

```cpp
// Preload during a loading screen, so the first play() does not read and decode the file.
const illusion::AudioClipHandle explosion = audio.loadClip("game:/audio/explosion.wav");

// Generated sound: interleaved samples in [-1, 1].
const illusion::AudioClipHandle beep = audio.createClip(samples, 1, 44100);

if (const auto info = audio.clipInfo(explosion))
{
    ILL_LOG_INFO("Game", "{} frames, {} channels, {} bytes", info->frames, info->channels, info->memoryBytes);
}
audio.unloadClip(explosion); // the handle is invalid at once; memory is freed when no voice plays it
```

`loadClip()`, yol daha önce yüklendiyse önbellekteki clip'i döndürür. Okunamayan veya çözülemeyen bir
clip bir uyarı yazar ve geçersiz bir handle verir; böyle bir clip'e sahip `AudioSource` sessiz kalır.

### Arka planda yükleme

`AudioWorld`'e bir [AssetStreamer](assets.md) verirseniz clip'ler oyunu durdurmadan yüklenir: dosya
streamer'ın I/O thread'inde okunur ve Job System üzerinde çözülür.

```cpp
illusion::JobSystem jobs;
illusion::AssetStreamer streamer(files, jobs);
illusion::AudioWorldDesc desc;
desc.streamer = &streamer; // AudioSource clips now load in the background
illusion::AudioWorld audio(world, files, desc);

const illusion::AudioClipHandle thunder = audio.requestClip("game:/audio/thunder.ogg");
while (running)
{
    streamer.update(); // publishes finished loads first
    audio.update(frameSeconds);
    if (audio.clipState(thunder) == illusion::AssetState::Ready)
    {
        audio.playOneShot(thunder);
    }
}
```

- `requestClip()` hemen bir handle döndürür; `clipState()`, sonraki bir `update()` biten yüklemeyi görene
  kadar `Loading`, sonra `Ready` olur (ya da log'a bir uyarıyla `Failed`). Streamer yoksa `loadClip()`
  gibi davranır.
- `AudioSource` clip'leri de bu yolla istenir. Çalması gereken bir kaynak clip'ini bekler
  (`AudioPlayback::pendingStart`) ve hazır olur olmaz başlar; `stop()` beklemeyi iptal eder.
- `playOneShot()` ve `clipInfo()` hazır bir clip ister. Yüklenmekte olan bir clip için `loadClip()`
  beklemeden handle'ını döndürür.
- Canlı yeniden yükleme: `AssetStreamer::reload()` streamer üzerinden yüklenmiş bir clip'in dosyasını
  bildirdiğinde (bkz. [Değişen dosyaları yeniden yükleme](assets.md#degisen-dosyalar-yeniden-yukleme)),
  clip arka planda yeniden yüklenir ve handle'ını korur. Yeni veri hazır olana kadar eski veri çalar; sonra
  onu çalan sesler durur, bunların arasındaki `AudioSource`'lar yeni veriyle yeniden başlar ve eski veri,
  mixer onu artık kullanmadığında serbest bırakılır. Başarısız bir yeniden yükleme eski veriyi korur.
  `stats().clipReloads` yeniden yüklemeleri sayar.

## Kaynaklar

| `AudioSource` alanı | Anlamı |
| --- | --- |
| `clip` | Clip; bir `AssetRef` (sanal yolu olarak kaydedilir) |
| `bus` | Mikser bus'ı (varsayılan `Sfx`) |
| `volume`, `pitch`, `pan` | Doğrusal kazanç; çalma hızı ([1/16, 16] aralığına sıkıştırılır); [-1, 1] stereo denge |
| `loop` | Sonda durmak yerine baştan başla |
| `playOnStart` | `AudioWorld` Entity'yi ilk gördüğünde başla |
| `spatial` | `Transform`'da 3D ses (varsayılan); `false` sesi olduğu gibi çalar (müzik, arayüz) |
| `stream` | Clip'i `Stream` modunda yükle |
| `attenuation`, `minDistance`, `maxDistance`, `rolloff` | Mesafe zayıflaması (aşağıda) |
| `dopplerFactor` | Doppler etkisinin gücü; 0 kapatır |
| `priority` | Tüm voice'lar doluyken voice'u kimin koruyacağı |
| `lowPassCutoff` | Sesi boğuklaştıran low-pass filtresinin Hz cinsinden kesim frekansı; 0 (varsayılan) kapalı ([Efektler](#efektler)) |
| `reverbSend` | Sesin reverb'e gönderilen payı, [0, 1] ([Efektler](#efektler)) |
| `coneInnerAngle`, `coneOuterAngle`, `coneOuterVolume` | Yönlü ses ([Ses konileri](#ses-konileri)) |

```cpp
audio.play(door);           // starts or restarts; a changed clip path is loaded now
audio.stop(door);
if (audio.isPlaying(door))  // as of the last update()
{
}
```

`Transform`'u olmayan kaynaklar orijinde çalar; bu yalnızca uzamsal iseler önemlidir.

## Tek seferlik sesler

Çarpma sesleri ve arayüz tıklamaları gibi Entity gerektirmeyen kısa sesler doğrudan çalınır:

```cpp
illusion::AudioPlayParams params;
params.spatial = true;
params.position = contact.point;
params.volume = 0.8f;
const illusion::AudioVoiceHandle voice = audio.playOneShot(explosion, params);
audio.stop(voice); // optional: one-shots stop by themselves at the end
```

## 3D ses

Listener, `AudioListener` ve `Transform` sahibi ilk Entity'dir: (kameralar gibi) Transform'un -z ekseni
boyunca bakar, yukarısı +y'dir. Hiç yoksa listener orijindedir ve -z yönüne bakar.

| `AudioAttenuation` | d mesafesindeki kazanç ([minDistance, maxDistance] aralığına sıkıştırılır) |
| --- | --- |
| `None` | 1 (ses yine de panlanır) |
| `Inverse` (varsayılan) | minDistance / (minDistance + rolloff × (d − minDistance)) |
| `Linear` | 1 − rolloff × (d − minDistance) / (maxDistance − minDistance) |
| `Exponential` | (d / minDistance)^−rolloff |

Sesler listener'a göre yönlerine göre panlanır; pitch'leri, kaynak ile listener'ın göreli hızı için
doppler etkisini izler (ses hızı: `AudioWorldDesc::speedOfSound`, 343,3 m/s). Bir kaynağın
`Transform`'unu tek karede çok uzağa taşımak çok hızlı bir hareket gibi görünür; ışınlanan sesler için
`dopplerFactor`'ı 0 yapın.

### Ses konileri

Konisi olan bir kaynak, bir hoparlör ya da bir insan sesi gibi önünde yüksek, arkasında kısık duyulur.
İleri ekseni `Transform`'unun -z eksenidir.

```cpp
illusion::AudioSource speaker;
speaker.clip = illusion::AssetRef("game:/audio/announcement.ogg");
speaker.coneInnerAngle = 1.5f;  // full volume within ~86 degrees around the speaker's forward axis
speaker.coneOuterAngle = 3.0f;  // quiet behind it
speaker.coneOuterVolume = 0.2f;
```

`coneInnerAngle` (radyan, açıklık açısının tamamı) içinde ses seviyesi değişmez; `coneOuterAngle`
ötesinde `coneOuterVolume` olur; arada kademeli olarak azalır. Varsayılan açılar (`kAudioFullCone`)
kaynağı her yöne eşit duyulur yapar.

## Mikser

Her ses, hepsi bir master ses seviyesinin altında olan beş bus'tan birinden çalar:

| `AudioBus` | Tipik içerik |
| --- | --- |
| `Sfx` | Ses efektleri |
| `Music` | Müzik |
| `Ambience` | Ambiyans döngüleri |
| `Dialogue` | Konuşmalar |
| `Ui` | Menü sesleri (oyun duraklatılmışken çalmaya devam etsin) |

```cpp
audio.setMasterVolume(settings.masterVolume);
audio.setBusVolume(illusion::AudioBus::Music, settings.musicVolume);
audio.setBusPaused(illusion::AudioBus::Sfx, true); // pause menu: effects freeze in place
```

## Efektler

```cpp
// A sound behind a wall: muffled, and a little of it in the room's reverb.
illusion::AudioSource& radio = world.get<illusion::AudioSource>(radioEntity);
radio.lowPassCutoff = 800.0f;
radio.reverbSend = 0.3f;

// Pause menu: muffle the game and keep the menu sounds clear.
audio.setBusFilter(illusion::AudioBus::Sfx, illusion::AudioBusFilter{600.0f, 0.0f});
audio.setBusFilter(illusion::AudioBus::Ambience, illusion::AudioBusFilter{600.0f, 0.0f});

// A radio voice: only the middle of the spectrum.
audio.setBusFilter(illusion::AudioBus::Dialogue, illusion::AudioBusFilter{3500.0f, 400.0f});

// A large hall.
illusion::AudioReverbSettings hall;
hall.roomSize = 0.85f;
hall.damping = 0.4f;
audio.setReverb(hall);
```

| Efekt | Nasıl ayarlanır | Notlar |
| --- | --- | --- |
| Ses başına low-pass | `AudioSource::lowPassCutoff`, `AudioPlayParams::lowPassCutoff` | Oktav başına 12 dB; her karede değişebilir |
| Bus filtreleri | `setBusFilter(bus, AudioBusFilter{lowPass, highPass})` | Bus'taki her şeye uygulanır; 0 filtreyi kapatır |
| Reverb | Seslerde `reverbSend`, oda için `setReverb()` | Tüm seslerin paylaştığı tek bir oda reverb'ü (Freeverb) |

Kesim frekansları [10 Hz, miksaj hızının %45'i] aralığına sıkıştırılır. Reverb her sesi bus'ının ses
seviyesiyle duyar; bu yüzden bir bus'ı kısmak ya da duraklatmak gönderdiğini de susturur, reverb'ün
kuyruğu ise çalmaya devam eder. Reverb yalnızca sesler onu beslerken ve kuyruğu sönene kadar çalışır;
en az 22050 Hz miksaj hızı gerektirir.

## Voice'lar ve çıkış

`AudioWorldDesc::maxVoices` (varsayılan 128) aynı anda kaç sesin çalabileceğini sınırlar. Tüm voice'lar
kullanımdayken yeni bir ses, önceliği kendisininkinden yüksek değilse en düşük öncelikli sesin (eşitler
arasında en eskisinin) voice'unu alır; aksi halde düşürülür. `stats()` aktif voice'ları, alınan
voice'ları, düşürülen sesleri ve clip belleğini sayar.

| `AudioOutput` | Kullanım |
| --- | --- |
| `Device` (varsayılan) | Varsayılan ses cihazı; cihaz yoksa `Null`'a düşer |
| `Null` | Ses kartı yok: bir thread miksi gerçek zamanlı tüketir (sunucular, CI) |
| `Offline` | Cihaz yok: miksajlanmış frame'leri çekmek için `render()` çağırırsınız (testler, offline render) |

```cpp
illusion::AudioWorldDesc desc;
desc.output = illusion::AudioOutput::Offline;
desc.sampleRate = 48000;
illusion::AudioWorld offline(world, files, desc);
std::vector<float> frames(2 * 480); // 10 ms of interleaved stereo
offline.update(0.0);
offline.render(frames);
```

## Sahneler ve thread'ler

- `reflectAudioComponents()`, sahne dosyaları için `AudioSource` (`illusion.AudioSource`) ve
  `AudioListener` (`illusion.AudioListener`) Component'lerini yansıtır; `AudioPlayback` çalışma zamanı
  durumudur ve kaydedilmez.
- `AudioWorld` thread-safe değildir: onu tek bir thread'den, World Query'lerinin dışında çağırın. World ve
  sanal dosya sistemi ondan uzun yaşamalıdır; onu yok etmek `AudioPlayback` Component'lerini kaldırır.
- Stream edilen bir clip'i başlatmak bir decoder açar (bir bellek ayırma); her karede çalışan diğer her
  şey, filtreler ve reverb dahil, clip'ler yüklendikten sonra bellek ayırmaz.
- `Inactive` Entity'lerin (bkz. [Disabled ve Inactive](ecs.md#disabled-ve-inactive)) kaynakları durur ve
  Inactive bir dinleyici yok sayılır. Entity yeniden etkin olduğunda `playOnStart` olan bir kaynak baştan
  başlar.
