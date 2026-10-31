# Play Modu

Play modu oyunu editörün içinde çalıştırır. **Play**, düzenlenen sahneyi ayrı bir play World'e kopyalar ve
üzerinde bir [GameSession](../runtime.md) çalıştırır: oyun modülleri, girdi, fizik ve ses. **Stop** play
World'ü atar; oyun ne yapmış olursa olsun düzenlenen sahne Play'den önceki hâlinin aynısıdır (ADR 0042). Her
parçası bir [Editor commands](../../reference/editor-commands.md) komutudur; böylece bir yapay zekâ ajanı oyunu
aynı API ile başlatır, adım adım ilerletir ve inceler.

## Başlatmak ve durdurmak

| Tuş | Komut | Ne yapar |
| --- | --- | --- |
| **F5** | `play.start` | Sahneyi kopyalar ve oyunu başlatır. |
| **Shift+F5** | `"mode": "simulate"` ile `play.start` | Yalnızca fizik: gövdeler düşer ve çarpışır, oyun kodu ve girdi çalışmaz. |
| **F6** | `play.pause` | Duraklatır; duraklatılmış oyun yalnızca adımlarla ilerler. |
| **F7** | `play.step` | Sabit fizik adımlarıyla ilerletir (varsayılan bir). |
| | `play.stop` | Play World'ü yok eder ve düzenlemeye döner. |
| | `play.status` | Bir oturumun çalışıp çalışmadığını söyler: `stopped`, `playing` ya da `paused`; adımları ve oyun zamanıyla. |

Kopya, sahne kaydedilmeden bellekte yapılır ve Entity kimliklerini korur. Oyun proje ayarlarını
(`settings:/physics.json`, `settings:/input.json`, ...) başlarken okur; bunları `project.settings_set` ile
düzenleyin. `play.set_time_scale` oyunu yavaşlatır (0.5) ya da dondurur (0); sonraki oturumlarda da geçerli
kalır ve `play.step`'i değiştirmez.

Tekrarlanabilir çalıştırmalar için `play.start`'a `"fixedStep": true` verin: her kare tam olarak bir fizik
adımı ilerler ve oyunun oluşturduğu Entity'ler kimliklerini `seed`'den alır; böylece aynı sahne, girdi ve seed
her zaman aynı World'ü verir.

```json
{"method": "play.start", "params": {"mode": "play", "fixedStep": true, "seed": 7}}
```

Araç çubuğunun Play denetimleri oturumu izler: oturum çalışırken **Play**, **Stop** olur (F5 ve Shift+F5 de
durdurur), **Pause** duraklatılmışken basılı kalır ve sonraki tıklamada sürdürür; hiçbir şey oynamazken
**Pause** ve **Step** kapalıdır. Durum çubuğu **Playing** ya da **Paused** der ve paneller temanın play
rengini alır (`playModeTint` tercihi); böylece çalışan oyun düzenlenen sahneyle karıştırılmaz. Bu sırada
yapılan değişiklikler Stop ile kaybolur.

Kopyadan önce `play.start`, kaydedilmemiş bir sahnenin [autosave](projects.md) kopyasını yazar (autosave
açıksa); böylece editörü çökerten oyun kodu hiçbir şey kaybettirmez.

## Game görünümü

Bir oturum çalışırken her Viewport play World'ü gösterir: Game görünümü sahnenin ana Camera'sından, Scene
görünümleri kendi editör kameralarından. `viewport.pick`, `viewport.sample`, `viewport.screenshot` ve diğer
Viewport komutları oyunun gösterdiğini okur ve bildirdikleri Entity'ler play World'ündür (oynarken
`"world": "edit"` verilen bir Viewport komutu başarısız olur).

Klavyeyi ve fareyi oyuna vermek için Game görünümüne tıklayın: imleç gizlenir ve sınırsız hareket eder, tuşlar
ve düğmeler oyunun girdi eylemlerine ulaşır, editör kısayolları bekler. **Shift+F1** bunları geri alır (oyun
basılı her tuşun bırakıldığını görür); Pause ve Stop da alır. Ajanlar, pencere girdiyi yakalamış olsun ya da
olmasın, girdiyi `input.inject` ile gönderir.

**Maximize on Play** açıkken (Game görünümünün araç çubuğundaki düğme ya da `maximizeOnPlay` tercihi) Game
görünümü oynarken pencereyi doldurur; diğer paneller Stop ile geri gelir.

## Oynarken düzenlemek

Bir oturum çalışırken Entity, Component, transform, hiyerarşi ve pano komutları play World üzerinde çalışır.
Değişiklikleri hemen uygulanır (değişen bir `RigidBody` ya da `Collider` gövdeyi yeniden kurar, yeni bir
`AudioSource` klibi sesi yeniden başlatır) ama geri alınamaz ve Stop ile kaybolur. Bunun yerine düzenlenen
sahneyi geri alınabilir bir adım olarak değiştirmek için parametrelere `"world": "edit"` ekleyin:

```json
{"method": "entity.rename", "params": {"entity": "/Light", "name": "Sun", "world": "edit"}}
```

Yalnızca düzenlenen sahnede anlamlı olan komutlar (`scene.save`, `history.undo`, Asset ve Material komutları)
her zaman onun üzerinde çalışır. Seçim Entity kimliklerini tutar; bu yüzden iki World'de de aynı Entity'leri
gösterir.

Oynarken bulunan değerleri saklamak için Stop'tan önce bunları `play.keep_changes` ile düzenlenen sahneye
kopyalayın. Adı verilen Entity'lerin kaydedilen Component'lerini (ya da yalnızca listelenenleri) tek bir geri
alma adımı olarak kopyalar; oyunun oluşturduğu Entity'ler bir uyarıyla atlanır.

```json
{"method": "play.keep_changes", "params": {"entities": ["/Player"], "components": ["illusion.Transform"]}}
```

## Bir oyunu ajandan test etmek

Bir ajan çalışan oyunu üç komutla yönetir:

| Komut | Ne yapar |
| --- | --- |
| `input.inject` | Tuşlara ve fare düğmelerine oyuncu gibi basar ya da bırakır veya imleci taşır. Oyun bunu bir sonraki güncellemesinde görür. |
| `input.read` | Oyunun girdi eylemlerinin durumunu döndürür: basılı, basıldı, bırakıldı ve değer. |
| `play.run_until` | Bir Entity var olana, yok olana ya da bir Component değeri verilen değere eşit olana veya `maxSteps` dolana kadar oyunu ilerletir. |

```json
{"method": "input.inject", "params": {"keys": [{"key": "Space", "down": true}]}}
{"method": "play.step", "params": {"steps": 1}}
{"method": "play.run_until", "params": {"maxSteps": 600, "entity": "Board", "component": "game.Score", "field": "/lines", "equals": 1}}
```

## Girdi eylemleri

Girdi eylemleri Asset'i (bir `.input` dosyası, bkz. [Girdi eylemleri](../actions.md)) oyunun eylemlerini ve
varsayılan bağlamalarını listeler. `input.*` Asset komutları onu geri alınabilir dosya değişiklikleri olarak
düzenler:

| Komut | Ne yapar |
| --- | --- |
| `input.actions` | Eylemleri türleri ve bağlamalarıyla listeler. |
| `input.action_define` | Bir eylem ekler (dosyayı oluşturarak) ya da türünü ve bağlamalarını değiştirir. |
| `input.action_remove` | Bir eylemi kaldırır. |
| `input.action_rename` | Bir eylemi yeniden adlandırır. |
| `input.bind`, `input.unbind` | Bir bağlama ekler ya da kaldırır. |

Projeyi Asset'e yönlendirmek için `illusion.InputSettings` (`actions`) üzerinde `project.settings_set`
kullanın.

## Oyun modülleri

`module.load` bir oyun modülünü paylaşımlı kütüphanesinden yükler ve çalışan bir oturumda başlatır,
`module.unload` onu durdurup kaldırır, `module.list` yüklü modülleri listeler. Yüklenemeyen bir modül 1302
koduyla ve yükleyicinin nedeniyle başarısız olur (bkz. [Modüller](../modules.md)). [Proje
Modülleri](project-modules.md) projenin kendi modüllerini derler ve yükler.
