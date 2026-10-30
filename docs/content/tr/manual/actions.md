# Input Action'ları

Oyun kodu "Space'e veya A düğmesine basıldı mı?" yerine "oyuncu zıpladı mı?" diye sormalıdır.
`InputActions`, cihaz input'larını adlandırılmış action'lara eşler, oyuncuların bunları yeniden
atamasına izin verir, atamaları kaydeder ve UI'ya hangi düğme sembolünü göstereceğini söyler.

Header: `#include <illusion/input/InputActions.hpp>`

## Action tanımlama

```cpp
illusion::InputActions actions;
const illusion::ActionId jump = actions.addAction("Jump", illusion::ActionType::Button);
const illusion::ActionId move = actions.addAction("Move", illusion::ActionType::Vector2);
const illusion::ActionId throttle = actions.addAction("Throttle", illusion::ActionType::Axis);

actions.addBinding(jump, illusion::InputBinding::key(illusion::Key::Space));
actions.addBinding(jump, illusion::InputBinding::gamepadButton(illusion::GamepadButton::South));
actions.addBinding(move, illusion::InputBinding::keyVector(illusion::Key::W, illusion::Key::S,
                                                           illusion::Key::A, illusion::Key::D));
actions.addBinding(move, illusion::InputBinding::gamepadStick(illusion::GamepadStick::Left));
actions.addBinding(throttle, illusion::InputBinding::gamepadAxis(illusion::GamepadAxis::RightTrigger));
actions.addBinding(throttle, illusion::InputBinding::key(illusion::Key::S, -1.0f));
```

| Tip | Değer | Tipik kullanım |
| --- | --- | --- |
| `Button` | basılma ve bırakılma kenarları olan açık/kapalı | Zıplama, Ateş, Etkileşim |
| `Axis` | [-1, 1] aralığında tek değer | Gaz, Yakınlaştırma |
| `Vector2` | uzunluğu en fazla 1 olan 2D vektör | Hareket, Bakış |

Bir binding üzerindeki `scale` onu ters çevirir veya zayıflatır: `key(Key::S, -1.0f)` bir ekseni
negatife iter, `gamepadAxis(GamepadAxis::LeftY, -1.0f)` ise çubuk aşağı çekildiğinde bir button
action'ını tetikler.

## Action'ları her karede okuma

```cpp
std::vector<const illusion::GamepadState*> pads;
for (illusion::Gamepad* pad : gamepads->gamepads())
{
    pads.push_back(&pad->state());
}
actions.update(&window->input(), pads);

if (actions.wasPressed(jump))
{
    player.jump();
}
player.move(actions.vector(move));
```

Birden fazla binding etkin olduğunda en güçlü değer kazanır. Bir kare içinde başlayıp biten bir
dokunuş da `wasPressed` ve `wasReleased` bildirir. Yerel çok oyunculu oyunlarda her oyuncuya kendi
`InputActions` nesnesini verin ve yalnızca o oyuncunun gamepad'ini geçirin.

## Yeniden atama

`BindingCapture`, oyuncunun basacağı bir sonraki input'u bekler. Yakalama başladığında basılı olan
input'lar (menüyü açan düğme gibi) bırakılana kadar yok sayılır.

```cpp
illusion::BindingCapture capture(illusion::BindingCapture::Devices::Gamepad, illusion::ActionType::Button);
// each frame while "Press a button" is shown:
if (auto binding = capture.poll(&window->input(), pads))
{
    for (illusion::ActionId other : actions.findConflicts(*binding, jump))
    {
        actions.removeBinding(other, *binding); // or ask the player
    }
    actions.addBinding(jump, *binding);
}
```

## Atamaları kaydetme

`serialize()` okunabilir metin yazar, `deserialize()` ise onu geri okur. Action'lar kodda tanımlanır;
dosya yalnızca binding'lerini saklar:

```text
# illusion input bindings v1
Jump = Key.Space, Gamepad.South
Move = Keys.W.S.A.D, Stick.Left
Throttle = Axis.RightTrigger, Key.S*-1
```

Bilinmeyen action'lar ve hatalı binding'ler atlanır ve bildirilir; böylece eski veya elle düzenlenmiş
bir dosya oyunun başlamasını asla engellemez. Metni [dosya sistemi](filesystem.md) ile saklayın; örneğin
`user:/input.cfg` içinde.

## Input action Asset'leri

Bir oyunun action'ları da veridir: bir input action Asset'i (bir `.input` dosyası) her action'ı türü ve
varsayılan binding'leriyle tanımlar; böylece editör ve tasarımcılar onları koda dokunmadan değiştirir.
Binding metinleri `serialize()` ile aynıdır.

```json
{
  "format": "illusion.input",
  "version": 1,
  "actions": [
    {"name": "Jump", "type": "Button", "bindings": ["Key.Space", "Gamepad.South"]},
    {"name": "Move", "type": "Vector2", "bindings": ["Keys.W.S.A.D", "Stick.Left"]},
    {"name": "Throttle", "type": "Axis", "bindings": ["Axis.RightTrigger", "Key.S*-1"]}
  ]
}
```

```cpp
#include <illusion/input/InputActionsAsset.hpp>

illusion::InputActions actions;
if (auto loaded = illusion::loadInputActions(files, "assets:/game.input", actions); !loaded)
{
    ILL_LOG_ERROR("Game", "{}", loaded.error()); // "assets:/game.input: '/actions/1/type' must be ..."
}
const std::string text = illusion::writeInputActions(actions); // the same actions give the same text
```

`readInputActions()` bütün action'ları birden değiştirir, herhangi bir hatada ise hiçbirini: geçersiz bir
ad, iki kez kullanılan bir ad, bilinmeyen bir tür ya da binding, yanlış bir format veya daha yeni bir sürüm,
hatalı değerin JSON pointer'ıyla başarısız olur. Action adları `InputActions::isValidActionName()` kuralına
uymalıdır: boş değil, başında ve sonunda boşluk yok, `=`, `,`, `#` ya da satır sonu içermiyor. Projenin
`InputSettings::actions` ayarı, bir [oyun oturumunun](runtime.md) başladığı Asset'i belirtir; oyuncunun
kaydedilmiş yeniden atamaları (`serialize()`) bunun üzerine uygulanır.

Editörler kümeyi üç fonksiyonla daha değiştirir:

```cpp
actions.renameAction(*actions.findAction("Throttle"), "Accelerate"); // false if taken or invalid
actions.removeAction(*actions.findAction("Jump")); // later ids move down: look them up again
actions.clear();
```

## Glyph'ler

`glyphId()` bir binding için ikon kimliği, `glyphLabel()` ise kısa bir etiket döndürür; oyuncunun en son
kullandığı cihazın kontrolcü ailesini izler:

| Binding | Xbox | PlayStation | Nintendo | Klavye |
| --- | --- | --- | --- | --- |
| `Gamepad.South` | `xbox_a` / A | `ps_cross` / Cross | `switch_b` / B | |
| `Axis.LeftTrigger` | `xbox_lt` / LT | `ps_l2` / L2 | `switch_zl` / ZL | |
| `Key.Space` | | | | `key_space` / Space |

```cpp
const illusion::GamepadFamily family = actions.lastDevice() == illusion::InputDevice::Gamepad
                                           ? illusion::gamepadFamily(actions.lastGamepadType())
                                           : illusion::GamepadFamily::Generic;
const std::string icon = illusion::glyphId(binding, family);
```

Glyph kimliklerini kendi UI görsellerinize eşleyin; motor sürümleri arasında asla değişmezler.

`glyphLabel(binding, family)` sabit İngilizce etiketler döndürür. Oyuncuların okuduğu etiketler için,
etiketi bir [Localization](localization.md) nesnesinin etkin dilinde biçimlendiren çevrilebilir
overload'u kullanın; `glyphLabelKey()` string tablosu anahtarını verir:

```cpp
illusion::text::Localization localization;
localization.addTable(illusion::input::strings::kTable); // #include <illusion/input/InputStrings.hpp>
const std::string label = illusion::glyphLabel(binding, family, localization);
```
