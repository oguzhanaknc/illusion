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
| `setCursorShape()` | Ok, metin, yeniden boyutlandırma (tümü, NS, EW, NESW, NWSE), el, izin yok veya artı imleci. |
| `setCursorPosition()` | İmleci taşır (pencere koordinatları). |
| `position()`, `setPosition()` | İstemci alanının ekrandaki konumu. |
| `maximize()`, `restore()`, `iconify()` | Pencere durumunu değiştirir; `WindowDesc::maximized` pencereyi büyütülmüş açar. |
| `isFocused()`, `isMinimized()`, `isMaximized()` | Pencere durumu. |
| `setIcon()` | Pencere simgesini farklı boyutlardaki RGBA görüntülerden ayarlar. |
| `setImeRect()` | Giriş yöntemine (IME) metnin nereye yazıldığını bildirir; aday listesi oranın yanında açılır (Windows). |
| `setClipboardText()`, `clipboardText()` | Sistem panosu (UTF-8). |

Bazı sistemler her şeye izin vermez: Wayland'de pencereler konumlarını bilmez (`position()` nullopt,
`setPosition()` false döndürür) ve imleç yalnızca yakalanmışken taşınabilir; macOS ve Wayland simgeyi
uygulama paketinden veya desktop dosyasından alır (`setIcon()` false döndürür). Sistemde olmayan imleç
şekilleri ok olarak gösterilir.

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
| `FilesDroppedEvent` | Pencereye dosya veya klasör bırakıldığında. |
| `ContentScaleChangedEvent` | Piksel ölçeği değiştiğinde, örneğin farklı DPI'lı bir monitörde. |
| `WindowMovedEvent` | Pencere taşındığında (Wayland'de değil). |
| `CursorEnteredEvent` | İmleç istemci alanına girdiğinde veya çıktığında. |
| `WindowRefreshEvent` | İçeriğin yeniden çizilmesi gerektiğinde, örneğin yeniden boyutlandırma sırasında. |

```cpp
illusion::Subscription onResize = events.subscribe<illusion::FramebufferResizedEvent>(
    [&](const illusion::FramebufferResizedEvent& e) { renderer.resize(e.size); });
```

Event'ler pencerelerini bir `WindowId` ile belirtir; bunu `window->id()` ile karşılaştırın. Id'ler asla
yeniden kullanılmaz; bu yüzden penceresi yok edildikten sonra iletilen bir event zararsızdır.

`InputState`'i sorgulamak oynanış (gameplay) için uygundur; event'ler ise her tuş basışını sırasıyla
görmesi gereken UI ve araçlar için uygundur.

### Bırakılan dosyalar

Bir bırakma işlemi tek bir `FilesDroppedEvent` kuyruklar; yolları (UTF-8) `droppedPath()` ile okunur ve
bir sonraki `pollEvents()` veya `waitEvents()` çağrısına kadar geçerli kalır:

```cpp
illusion::Subscription onDrop = events.subscribe<illusion::FilesDroppedEvent>(
    [&](const illusion::FilesDroppedEvent& e) {
        for (illusion::u32 i = 0; i < e.count; ++i)
        {
            importAsset(window->droppedPath(e.first + i));
        }
    });
```

`Window::dropFiles()`, input kaydını oynatma ve otomasyon için bir bırakmayı aynı şekilde iletir.

### Yüksek DPI ekranlar

`contentScale()`, pencere koordinatı başına piksel sayısıdır. `framebufferSize()` boyutunda render edin,
UI'ı pencere koordinatlarında yerleştirin ve `ContentScaleChangedEvent` geldiğinde fontları yeniden
oluşturun.

## Monitörler

`platform->monitors()`, bağlı ekranları birincil olan önce gelecek şekilde adı, konumu, boyutu, çalışma
alanı (görev çubukları ve dock'lar hariç) ve içerik ölçeğiyle listeler. Headless backend simüle edilmiş
tek bir monitör bildirir.

## Event döngüsünü uyandırma

Yalnızca input geldiğinde yeniden çizen araçlar `waitEvents()` içinde uyur. `wake()`, beklemeyi herhangi
bir thread'den sonlandırır:

```cpp
// main thread: sleep until input arrives or a background task finishes
platform->waitEvents(1.0);

// worker thread, when a result is ready:
platform->wake();
```

## Tuş adları

Tuş kodları fiziksel konumları adlandırır. `keyDisplayName()`, bir tuşun etkin klavye düzeninde yazdığı
karakteri döndürür; böylece kısayollar kullanıcının klavyesinde basılı olduğu gibi gösterilebilir:

```cpp
const std::optional<std::string> character = platform->keyDisplayName(illusion::Key::Y);
// "y" on a US layout, "z" on a German one; nullopt for keys like Space or Shift
```

Karakter yazmayan tuşlar nullopt döndürür; onların yerine çevrilmiş etiketlerini gösterin
(`glyphLabelKey()`, bkz. [Yerelleştirme](localization.md)).
