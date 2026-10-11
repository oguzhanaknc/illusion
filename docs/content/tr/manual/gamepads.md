# Gamepad'ler

Input modülü; Xbox, PlayStation, Nintendo ve diğer standart kontrolcüleri SDL3'ün gamepad alt sistemi
üzerinden destekler. Hot-plug, titreşim (rumble), ışık çubuğu, hareket sensörleri, touchpad ve DualSense
adaptive trigger'ları dahildir.

Header'lar: `#include <illusion/input/GamepadSystem.hpp>`, `<illusion/input/Gamepad.hpp>`

## Kurulum

```cpp
illusion::EventBus events;
auto gamepads = illusion::GamepadSystem::create(&events);

while (running)
{
    platform->pollEvents();
    gamepads->update(window->isFocused()); // new input frame, connections, buttons, axes
    events.dispatchQueued();

    for (illusion::Gamepad* pad : gamepads->gamepads())
    {
        const illusion::GamepadState& state = pad->state();
        if (state.wasPressed(illusion::GamepadButton::South))
        {
            jump();
        }
        move(state.stick(illusion::GamepadStick::Left));
    }
}
```

- Tek bir `GamepadSystem` oluşturun ve onu main thread üzerinde kullanın.
- Zaten bağlı olan kontrolcüler ilk `update()` çağrısında bildirilir.
- Pencere odağını `update()`'e geçirin. Oyunun odağı yokken tüm gamepad'ler bırakılmış olarak okunur;
  böylece başka bir uygulamada kullanılan kontrolcü oyunu hareket ettirmez.
- Bir kontrolcünün bağlantısı kesildiğinde `Gamepad*` pointer'ları geçersiz olur. `GamepadId`'yi saklayın
  ve gamepad'i her karede `find()` ile bulun.

## Bağlantılar ve oyuncular

`GamepadConnectedEvent` ve `GamepadDisconnectedEvent` event bus'a kuyruklanır. Her gamepad en düşük boş
oyuncu yuvasını (`playerIndex()`, 0 tabanlı) alır; yuva, oyuncu ışıkları olan kontrolcülerde gösterilir
ve bağlantı kesildikten sonra yeniden kullanılır. `findByPlayer()` bir yuvanın gamepad'ini döndürür.

## Düğmeler, çubuklar ve tetikler

Düğmeler **konumlarına** göre adlandırılır: `South`, Xbox'ta A, PlayStation'da Cross, Nintendo
kontrolcülerinde B'dir. Doğru sembolü göstermek için [glyph'leri](actions.md#glyphler) kullanın.

| Değer | Aralık | Notlar |
| --- | --- | --- |
| `stick(GamepadStick::Left)` | uzunluk 0-1 | +x sağ, **+y yukarı**; dairesel ölü bölge (varsayılan 0.15) |
| `trigger(GamepadTrigger::Right)` | 0-1 | ölü bölge 0.05 |
| `axis(GamepadAxis::LeftX)` | -1-1 | ölü bölge uygulanmamış ham değer |

`isDown`, `wasPressed` ve `wasReleased` klavye tuşları gibi çalışır. Ölü bölgeleri her gamepad için
`state().setStickDeadZone()` ve `setTriggerDeadZone()` ile değiştirin.

## Cihaz özellikleri

```cpp
const illusion::GamepadCapabilities& caps = pad->capabilities();
pad->rumble(0.4f, 0.8f, 150);                 // low and high frequency motors, milliseconds
if (caps.triggerRumble)
{
    pad->rumbleTriggers(0.0f, 0.6f, 100);     // Xbox impulse triggers
}
if (caps.led)
{
    pad->setLed(255, 64, 0);                  // DualShock 4 / DualSense light bar
}
if (caps.adaptiveTriggers)
{
    pad->setTriggerEffect(illusion::GamepadTrigger::Right,
                          illusion::TriggerEffect::weapon(0.3f, 0.6f, 1.0f));
}
```

| Özellik | Desteklendiği yer | API |
| --- | --- | --- |
| Titreşim | Çoğu kontrolcü | `rumble()` |
| Tetik titreşimi | Xbox One / Series | `rumbleTriggers()` |
| Işık çubuğu | DualShock 4, DualSense | `setLed()` |
| Adaptive trigger'lar | DualSense | `TriggerEffect::resistance`, `weapon`, `vibration` veya `off` ile `setTriggerEffect()` |
| Jiroskop ve ivmeölçer | DualShock 4, DualSense, Switch Pro | `setMotionSensorsEnabled(true)`, ardından `state().gyroscope()` (rad/s) ve `accelerometer()` (m/s²) |
| Touchpad | DualShock 4, DualSense | `state().finger(0)` ve `finger(1)`, normalize edilmiş konumlar |

Kontrolcüde ilgili özellik yoksa fonksiyonlar `false` döndürür. Hareket sensörleri pili tükettikleri
için varsayılan olarak kapalıdır.

## Sanal gamepad'ler

`attachVirtualGamepad()`, gerçek donanım gibi davranan simüle edilmiş bir kontrolcü oluşturur: bağlanır,
ayarladığınız input'ları bildirir ve aldığı çıktıları (titreşim, ışık, efektler) kaydeder. Test'ler,
input kayıtları ve editörü kontrol eden AI agent'lar bunu kullanır.

```cpp
illusion::VirtualGamepadDesc desc;
desc.type = illusion::GamepadType::PS5;
auto virtualPad = gamepads->attachVirtualGamepad(desc);
virtualPad->setButton(illusion::GamepadButton::South, true);
virtualPad->setAxis(illusion::GamepadAxis::LeftY, 1.0f); // stick up
gamepads->update();
```
