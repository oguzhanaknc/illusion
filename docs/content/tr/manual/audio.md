# Ses

illusion sesi [miniaudio](https://github.com/mackron/miniaudio) ile çalar: mesafe zayıflaması, panlama ve
doppler etkisiyle 3D ses, müzik streaming ve bus'lı bir mikser. Sesler, `AudioSource` taşıyan sıradan
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
fire.clip = "game:/audio/fire.ogg";
fire.loop = true;
world.create(fireTransform, fire);

// Background music: not spatial, streamed, on the music bus.
illusion::AudioSource music;
music.clip = "game:/audio/theme.ogg";
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
   başlar.
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

## Kaynaklar

| `AudioSource` alanı | Anlamı |
| --- | --- |
| `clip` | Clip'in sanal yolu |
| `bus` | Mikser bus'ı (varsayılan `Sfx`) |
| `volume`, `pitch`, `pan` | Doğrusal kazanç; çalma hızı ([1/16, 16] aralığına sıkıştırılır); [-1, 1] stereo denge |
| `loop` | Sonda durmak yerine baştan başla |
| `playOnStart` | `AudioWorld` Entity'yi ilk gördüğünde başla |
| `spatial` | `Transform`'da 3D ses (varsayılan); `false` sesi olduğu gibi çalar (müzik, arayüz) |
| `stream` | Clip'i `Stream` modunda yükle |
| `attenuation`, `minDistance`, `maxDistance`, `rolloff` | Mesafe zayıflaması (aşağıda) |
| `dopplerFactor` | Doppler etkisinin gücü; 0 kapatır |
| `priority` | Tüm voice'lar doluyken voice'u kimin koruyacağı |

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
  şey, clip'ler yüklendikten sonra bellek ayırmaz.
