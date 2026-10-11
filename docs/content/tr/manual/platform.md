# Pencereler ve Input

Platform modülü pencere açar, klavye ve fareyi okur. GLFW üzerine kuruludur, ancak GLFW hiçbir public
header'da görünmez.

Header'lar: `#include <illusion/platform/Platform.hpp>`, `<illusion/platform/Window.hpp>`,
`<illusion/platform/Events.hpp>`

## Ana döngü

```cpp
illusion::EventBus events;
auto platform = illusion::Platform::create();
if (!platform)
{
    return 1; // no display available; the reason is in the log
}

illusion::WindowDesc desc;
desc.title = "My Game";
auto window = illusion::Window::create(*platform, desc, &events);

illusion::FrameClock clock;
while (!window->shouldClose())
{
    platform->pollEvents();   // new input frame + OS events
    events.dispatchQueued();  // deliver window events
    const illusion::FrameTime& frame = clock.tick(illusion::monotonicSeconds());
    update(frame.deltaSeconds, window->input());
    render();
}
```

Kurallar:

- Tam olarak bir `Platform` oluşturun; onu ve tüm pencereleri main thread üzerinde kullanın (macOS bunu
  zorunlu kılar).
- Pencereleri `Platform`'dan önce, `EventBus`'ı ise onu kullanan pencerelerden sonra yok edin.
- `Platform::create()` ve `Window::create()` başarısız olduğunda `nullptr` döndürür ve nedenini log'a
  yazar.

### Headless mod

`Platform::create(PlatformDesc{PlatformBackend::Headless})` ekran olmadan çalışır. Pencereler simüle
edilir: boyutlar, başlıklar ve imleç modları normal davranır, ancak işletim sisteminden event gelmez.
Test'ler, dedicated server'lar ve komut satırı araçları bunu kullanır.

## Pencere özellikleri

| Fonksiyon | Açıklama |
| --- | --- |
| `size()` | Pencere koordinatlarında istemci alanı boyutu. |
| `framebufferSize()` | Piksel cinsinden boyut; yüksek DPI ekranlarda farklıdır ve pencere küçültülmüşken (0, 0) olur. |
| `contentScale()` | Pencere koordinatı başına piksel (örneğin Retina ekranlarda 2.0). |
| `setTitle()`, `setSize()` | Başlığı (UTF-8) ve istemci alanı boyutunu değiştirir. |
| `setCursorMode()` | `Normal`, `Hidden` veya `Captured` (kamera kontrolü için kilitli, sınırsız hareket). |
| `isFocused()`, `isMinimized()` | Pencere durumu. |

## Klavye ve fare

`window->input()` geçerli karenin `InputState`'ini döndürür:

```cpp
const illusion::InputState& input = window->input();
if (input.wasPressed(illusion::Key::Space))
{
    jump();
}
if (input.isDown(illusion::Key::W))
{
    moveForward(frame.deltaSeconds);
}
if (input.isDown(illusion::MouseButton::Right))
{
    rotateCamera(input.mouseDelta());
}
zoom(input.scrollDelta().y);
```

- `isDown` bir tuş basılı tutulduğu sürece true'dur; `wasPressed` ve `wasReleased` yalnızca tuşun
  değiştiği karede true'dur. Tek bir kare içindeki hızlı bir dokunuş her iki kenarı da bildirir.
- Tuşlar, ABD düzenine göre adlandırılmış fiziksel konumlardır (`Key::W` her düzende `E`'nin solundaki
  tuştur). Yazılan karakterler için `text()` kullanın: klavye düzenlerini ve giriş yöntemlerini (IME)
  doğru işler.
- `keyName()` ve `keyFromName()` tuşları yapılandırma dosyaları için kararlı isimlere dönüştürür.
- Bir pencere odağı kaybettiğinde basılı tüm tuşlar ve düğmeler bırakılır ve karşılık gelen bırakma
  event'leri gönderilir; böylece tuşlar asla takılı kalmaz.

## Pencere event'leri

Pencereler event'lerini `Window::create()`'e verilen `EventBus`'a kuyruklar. Event'ler
`EventBus::dispatchQueued()` ile iletilir:

| Event | Ne zaman gönderilir |
| --- | --- |
| `WindowCloseRequestedEvent` | Kullanıcı pencereyi kapatmak istediğinde. |
| `WindowResizedEvent` | İstemci alanının boyutu değiştiğinde. |
| `FramebufferResizedEvent` | Framebuffer boyutu değiştiğinde; swapchain'i yeniden oluşturun. |
| `WindowFocusEvent` | Odak kazanıldığında veya kaybedildiğinde. |
| `KeyEvent` | Bir tuş basıldığında, tekrarlandığında veya bırakıldığında. |
| `MouseButtonEvent`, `MouseMovedEvent`, `MouseScrolledEvent` | Fare input'u. |
| `TextInputEvent` | Bir karakter yazıldığında. |

```cpp
illusion::Subscription onResize = events.subscribe<illusion::FramebufferResizedEvent>(
    [&](const illusion::FramebufferResizedEvent& e) { renderer.resize(e.size); });
```

Event'ler pencerelerini bir `WindowId` ile belirtir; bunu `window->id()` ile karşılaştırın. Id'ler asla
yeniden kullanılmaz; bu yüzden penceresi yok edildikten sonra iletilen bir event zararsızdır.

`InputState`'i sorgulamak oynanış (gameplay) için uygundur; event'ler ise her tuş basışını sırasıyla
görmesi gereken UI ve araçlar için uygundur.
