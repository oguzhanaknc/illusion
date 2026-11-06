# Profiler ve Diagnostics

**Profiler** paneli (Window → Profiler veya `profiling` yerleşimi) editörün kare süresinin nereye gittiğini, her
viewport'un Render Pass'lerinin GPU'da ne kadar sürdüğünü ve editörün bildiği sorunları gösterir. Her parçası
aynı zamanda bir [Editör komutu](../../reference/editor-commands.md)'dur; böylece bir yapay zekâ ajanı aynı
sayıları okur ve aynı sorunlar üzerinde çalışır.

Profiler editörün kendisini ölçer. Bir oyunun veya motorun ayrıntılı zaman çizelgesi için `ILLUSION_PROFILING`
seçeneğiyle Build alın ve Tracy'yi bağlayın (bkz. [Profiling](../profiling.md)).

## CPU

CPU sekmesi son 240 karenin süresini bir grafik olarak çizer; üstünde ortalama, en uzun kare ve saniyedeki kare
sayısı yer alır. Alttaki tablo karenin *yerlerini* ve her birinin ne kadar sürdüğünü listeler:

| Yer | Neyi ölçer |
| --- | --- |
| `events` | Pencere ve girdi olayları (odakta olmayan bir editör burada girdi bekler). |
| `wait` | İstek bekleyen bir `--headless` editör (pencere olayları yoktur). |
| `requests` | Bağlı ajanların ve araçların istekleri (`illusion_ctl`, MCP). |
| `ui` | Editörün arayüzünü oluşturmak; çizilen her panel bunun altında yer alır, örneğin `panel.outliner`. |
| `render` | Viewport'ları render etmek ve pencereyi göstermek. |
| `play` | Play modunda çalışan oyun. |
| `editor` | Karenin sonunda editörün kendi işi: Asset değişiklikleri, otomatik kayıt, prefab yenilemeleri. |

Tablo her yer için en yeni karedeki süreyi, kare başına ortalamayı, tek bir karedeki en yüksek süreyi ve en yeni
karede kaç kez çalıştığını gösterir. Bir anı daha yakından incelemek için kaydı duraklatın: kaydedilen kareler
siz devam ettirene veya temizleyene kadar kalır.

Aynı veri `profiler.get`'ten gelir; `profiler.set` duraklatır, devam ettirir ve temizler:

```json
{"frames": 60}
```

```json
{"paused": true}
```

Profiler sabit bir kare halkasına kaydeder; bu yüzden kayıt hiç bellek ayırmaz ve Scope başına bir
mikrosaniyenin çok altında maliyeti vardır. Motor kodu aynı kaydediciyi, `illusion/core/FrameProfiler.hpp`
içindeki `FrameProfiler`'ı kullanabilir:

```cpp
FrameProfiler profiler;
profiler.beginFrame();
{
    FrameProfiler::Scope scope(profiler, "update");
    update();
}
profiler.endFrame();
```

## GPU

GPU sekmesi her viewport'u Render Pass'lerinin GPU süresiyle listeler: gölgeler, depth prepass, ambient
occlusion, shading ve post-processing. Süreler, GPU'nun bitirdiği en yeni karenin timestamp sorgularından gelir;
bu yüzden onları okumak hiçbir zaman GPU'yu beklemez. Gizli bir viewport veya timestamp'i olmayan bir GPU
*Not measured yet* gösterir. `render.stats` aynı süreleri `gpu` nesnesinde döndürür.

## Diagnostics

Diagnostics sekmesi editörün bulduğu sorunları, önce hatalar olmak üzere listeler:

| Kod | Önem | Anlamı |
| --- | --- | --- |
| `render.validation` | error | Renderer Vulkan validation hatalarına yol açtı (hedef sıfırdır). |
| `log.errors` | error | Log en son temizlendiğinden beri log'daki hata mesajları. |
| `asset.missing` | error | Sahnelerin, Material'ların veya ayarların başvurduğu bir Asset mevcut değil. |
| `render.lights_dropped` | warning | Bir viewport'ta renderer'ın çizdiğinden daha fazla etkin ışık var. |
| `log.warnings` | warning | Log'daki uyarı mesajları. |
| `prefab.unresolved` | warning | Prefab'ı yüklenemeyen bir prefab instance'ı; kaydını korur. |

Bir sorunun yerine gitmek için ona tıklayın: log mesajları Console'u açar, eksik bir Asset Asset Browser'da
klasörünü açar, çözülemeyen bir instance da seçilir. Bir şeyi değiştirdikten sonra **Refresh**'e basın.
`diagnostics.get` aynı listeyi döndürür; her sorun `code`, `subject` (bir viewport, bir Asset yolu veya bir
Entity id'si) ve `count` ile gelir.
