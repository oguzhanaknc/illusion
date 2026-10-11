# Rendering

Render modülü GPU cihazını oluşturur ve görüntüleri pencerelere sunar (present). Vulkan 1.3 (macOS'ta
MoltenVK) üzerinde [NVRHI](https://github.com/NVIDIA-RTX/NVRHI) ile kuruludur; oyun kodu GPU işlerini
NVRHI command list'leri ile kaydeder.

Header'lar: `#include <illusion/render/RenderDevice.hpp>`, `<illusion/render/Swapchain.hpp>`

!!! note "Erken aşama"
    Bu milestone cihazı, swapchain'leri, shader build hattını ve tanılama amaçlı bir üçgeni sağlar.
    Mesh'ler, Material'ler, PBR aydınlatma ve gölgeler renderer milestone'unda gelecek (yol haritasına
    bakın).

## Gereksinimler

- Vulkan 1.3 destekli bir GPU sürücüsü (dynamic rendering, synchronization2, timeline semaphore).
  Güncel tüm NVIDIA, AMD ve Intel sürücüleri ile Mesa (CI'da kullanılan lavapipe yazılım renderer'ı
  dahil) bu koşulu karşılar.
- Derlemek veya çalıştırmak için Vulkan SDK **gerekmez**. Validation layer'larını elde etmek için
  kurabilirsiniz; motor, mevcut olduklarında Debug build'lerinde bunları otomatik olarak açar.

## Cihazı oluşturma

```cpp
illusion::RenderDeviceDesc desc;
desc.applicationName = "My Game";
auto device = illusion::RenderDevice::create(desc);
if (!device)
{
    return 1; // no suitable GPU; the reason is in the log
}
ILL_LOG_INFO("Game", "rendering on {}", device->gpu().name);
nvrhi::IDevice* gpu = device->device();
```

- Vulkan loader çalışma zamanında açılır; hiçbir şey Vulkan SDK'ya link edilmez.
- Birden fazla GPU uygun olduğunda `gpuPreference` harici (discrete, varsayılan) veya tümleşik
  (integrated) GPU'yu seçer.
- `headless = true` test'ler, araçlar ve sunucular için present desteği olmayan bir cihaz oluşturur.
  Aksi halde native bir `Platform` var olmalıdır; çünkü hangi surface extension'larının gerektiğine
  pencere sistemi karar verir.
- Aynı anda yalnızca bir `RenderDevice` var olabilir. Ondan önce tüm NVRHI nesnelerini ve swapchain'leri
  yok edin.

### Validation

`validation` (`ValidationMode::Default`: motor assertion'larla derlendiğinde, yani Debug ve CI
build'lerinde açık), kuruluysa Vulkan validation layer'ını ve NVRHI'nin kendi validation layer'ını açar. Mesajlar log'a yazılır; `validationErrorCount()` hataları
sayar. GPU test'leri ve örnek uygulama bu değer sıfır değilse başarısız olur.

## Bir pencereye present etme

```cpp
auto swapchain = illusion::Swapchain::create(*device, *window);
nvrhi::CommandListHandle commands = device->device()->createCommandList();

while (!window->shouldClose())
{
    platform->pollEvents();
    if (swapchain->beginFrame()) // false while the window is minimized
    {
        commands->open();
        commands->clearTextureFloat(swapchain->currentBackBuffer(), nvrhi::AllSubresources,
                                    nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
        // ... draw into swapchain->currentFramebuffer() ...
        commands->close();
        device->device()->executeCommandList(commands);
        swapchain->present();
    }
}
device->waitIdle();
```

- `beginFrame()`, `framesInFlight` (varsayılan 2) kare yuvasından biri boşalana kadar bekler, ardından
  bir back buffer alır. Pencere boyutu değiştiyse önce swapchain'i yeniden oluşturur.
- Ekran destekliyorsa back buffer'lar sRGB formatı kullanır; böylece shader'lar doğrusal (linear) renk
  yazar.
- `vsync` (varsayılan açık) FIFO present modunu seçer; vsync kapalıyken mailbox veya immediate mod
  kullanılır. `setVsync()` bunu bir sonraki karede değiştirir.

## Koordinat kuralları

Renderer, Direct3D tarzı clip space'i izler: **y yukarıyı gösterir** ve derinlik 0'dan 1'e gider. NVRHI
bunu sağlamak için Vulkan viewport'unu çevirir; bu yüzden `glm::perspective()` ile üretilen projeksiyon
matrisleri y çevirmesi olmadan çalışır. Texture koordinatlarının başlangıç noktası sol üst köşedir.

## Shader'lar

Shader'lar [Slang](https://shader-slang.org/) (HLSL uyumlu bir dil) ile yazılır ve build sırasında
SPIR-V'ye derlenir. Derlenen bytecode programa gömülür; bu yüzden dağıtılacak veya yüklenecek shader
dosyası yoktur.

```cmake
illusion_add_shaders(my_game
    NAMESPACE my_game::shaders
    HEADER my_game/Shaders.hpp
    SHADERS
        meshVertex shaders/Mesh.slang vertexMain vertex
        meshFragment shaders/Mesh.slang fragmentMain fragment)
```

Her satır bir C++ fonksiyon adını, kaynak dosyayı, entry point'i ve stage'i belirtir. Üretilen header
`std::span<const std::byte> meshVertex() noexcept;` gibi bildirimler içerir; bu byte'ları
`nvrhi::IDevice::createShader()`'a verin. Shader uyarıları hata sayılır ve build, `#include` edilen
dosyaları takip eder.

Slang derleyicisi otomatik olarak indirilir (sabitlenmiş sürüm). Başka bir `slangc` kullanmak için
`ILLUSION_SLANGC_EXECUTABLE` değişkenini ayarlayın.

## Tanılama

`DebugTriangle`, hiçbir buffer kullanmadan renkli bir üçgen çizer. `illusion_hello_triangle` örneği,
shader derlemesinden present'e kadar tüm yolu doğrulamak için bunu kullanır:

```bash
./build/linux-clang/samples/Debug/illusion_hello_triangle --frames 300
```
