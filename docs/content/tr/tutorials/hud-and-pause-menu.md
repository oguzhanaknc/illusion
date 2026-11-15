# HUD ve duraklatma menüsü

Bu eğitim bir oyuna köşede bir puan göstergesi, menüyü açan tuşu (ya da düğmeyi) gösteren bir ipucu ve Resume,
bir ses kaydırıcısı ve Quit içeren bir duraklatma menüsü kazandırır. UI'ı editörde bir kez elle, bir kez de bir
ajanın göndereceği komutlarla kurarsınız; ayrıca puanı sayan ve menüyü yöneten küçük bir C++ oyun modülü
yazarsınız. Eğitim, HUD'ı ve duraklatma menüsü bunun tam sürümü olan `samples/techdemo` örneğini izler.

Bir illusion Release build'ine ve editörün derlendiği C++ derleyicisine ihtiyacınız var. Burada kullanılan her
Component'i [Oyun UI'ı kılavuzu](../manual/ui.md) açıklar.

## 1. Ne yapacaksınız

- Sağ üst köşede, kontur ile çizilen **Score** metnini ve sol üstte bir ipucunu (**Esc** Menu) içeren bir
  **HUD** canvas'ı. Tuş etiketi bir `UiGlyph`'tir: oyuncunun en son kullandığı cihaz için `ui.cancel`'ın ilk
  bağlamasını gösterir ve yeniden atamayı izler.
- Başlangıçta gizli olan ve **modal** olan bir **Pause Menu** canvas'ı: görünürken oyunun diğer input
  action'ları basılmamış okunur. Paneli bir `Shown` geçişiyle kayarak gelir. İçinde bir **Resume** düğmesi,
  etiketiyle bir **Volume** kaydırıcısı ve bir **Quit** düğmesi vardır; fare, klavye ve gamepad ile çalışır.
- String table anahtarlarıyla bulunan İngilizce ve Türkçe metinler ve puanı sayan, menüyü Escape (`ui.cancel`)
  ile açan, ana ses düzeyini ayarlayan ve oyunu bitiren bir modül.

## 2. Projeyi, bir fontu ve metinleri oluşturun

Editörü başlatın ve **3D Starter** şablonundan `hud` klasöründe **Hud** adlı bir proje oluşturun (şablonda bir
kamera vardır, böylece Game view UI'ın arkasında bir sahne gösterir):

```bash
build/bin/Release/illusion_editor --workspace ~/games
```

Her şablon `assets/fonts/Roboto-Regular.ttf` dosyasını getirir, bu yüzden `UiText` hemen çalışır; `assets/`
içindeki başka her `.ttf` veya `.otf` dosyası da bir font Asset'idir. Varsayılan karakter aralıkları Basic
Latin, Latin-1 ve Latin Extended-A'yı kapsar; bu, İngilizce ve Türkçe için yeterlidir.

Oyuncuların okuduğu metinler string table'lara aittir. Kod editörünüzde `assets/locales/en.json` dosyasını
oluşturun; İngilizce dosya tabandır:

```json
{
  "format": "illusion.locale",
  "version": 1,
  "locale": "en",
  "displayName": "English",
  "plural": "en",
  "number": {"decimal": ".", "group": ","},
  "strings": {
    "hud.score": "Score {points}",
    "hud.menuHint": "Menu",
    "hud.menu.title": "Paused",
    "hud.menu.resume": "Resume",
    "hud.menu.volume": "Volume {percent}%",
    "hud.menu.quit": "Quit"
  }
}
```

ve aynı anahtarlarla `assets/locales/tr.json` dosyasını:

```json
{
  "format": "illusion.locale",
  "version": 1,
  "locale": "tr",
  "displayName": "Türkçe",
  "plural": "tr",
  "number": {"decimal": ",", "group": "."},
  "fallback": "en",
  "strings": {
    "hud.score": "Puan {points}",
    "hud.menuHint": "Menü",
    "hud.menu.title": "Duraklatıldı",
    "hud.menu.resume": "Devam et",
    "hud.menu.volume": "Ses %{percent}",
    "hud.menu.quit": "Çık"
  }
}
```

`{points}` ve `{percent}` modülün doldurduğu argümanlardır; etkin dilin sayı biçimleri ve çoğul kuralları
bunlara uygulanır (bkz. [Yerelleştirme](../manual/localization.md)). Oyun başlarken `assets:/locales` içindeki
her dosyayı okur.

## 3. HUD

Hiçbir şey seçmeden **Entity > Game UI > Canvas** seçin. **F2** ile adını **HUD** yapın. Menü `gameui.create`
komutunu çalıştırır; her öğe tek bir undo adımıdır.

**Puan.** **HUD**'ı seçin ve **Entity > Game UI > Text** seçin: metin seçili canvas'ın içine, sol üstünden 24
canvas birimi uzağa gelir ve projenin ilk fontunu kullanır. Adını **Score** yapın, sonra
[Inspector](../manual/editor/scene-editing.md)'da:

1. `UiElement` yerleşiminin **anchors** düğmesini açın ve sağ üst ön ayarını seçin; öğe dikdörtgenini korur ve
   anchor'larını köşeye taşır.
2. Yerleşimi, metin 296 x 64 birim olacak ve üstten ve sağ kenardan 24 birim uzakta duracak şekilde ayarlayın:
   offset min (-320, 24) ve offset max (-24, 88).
3. `UiText` içinde **Text**'i silin, **Key**'i `hud.score`, **Size**'ı 48, **Align**'ı `End` yapın, **Wrap**'i
   kapatın ve **Outline Width**'i 2 yapın.

Öğeyi ve tutamaçlarını, UI'ı oyunun alacağı boyutta çizen **Game view**'da da sürükleyebilirsiniz; anchor'lar
işaretçi olarak çizilir. Sürüklemeleri 8 birime yapıştırmak için Ctrl'ye basılı tutun. Bkz.
[Game view'da düzenleme](../manual/ui.md#editing-in-the-game-view).

**İpucu.** **HUD**'ın içinde bir **Stack** oluşturun ve adını **Hint** yapın: sol üste bağlayın (offset min
(24, 24), offset max (600, 72)) ve stack'in **Axis**'ini `Horizontal`, **Spacing**'ini 12 ve **Cross Align**'ını
`Center` yapın. İçinde iki **Text** öğesi oluşturun. Birincinin adını **Menu Glyph** yapın: Size'ını (0, 44)
yapın (genişlik metni izler), **Text**'ini `Esc` yapın ve **Action**'ı `ui.cancel` olan **UI glyph**
Component'ini ekleyin. İkincinin adını **Menu Hint** yapın ve **Key**'ini `hud.menuHint`, boyutunu aynı yapın.
Stack'in içindeki öğeleri stack yerleştirir, bu yüzden anchor'ları yoktur.

Glyph `Esc` gösterir; en son bir gamepad kullanıldıysa gamepad'in East düğmesinin etiketini gösterir. Bağlaması
olmayan bir action için yazdığınız `Esc` metni kalır. Glyph'ler etikettir, resim değildir.

!!! note "Ajanla"
    Bir ajan aynı HUD'ı `gameui.create`, `gameui.set_anchors`, `gameui.place`, `component.set` ve
    `entity.create` ile oluşturur (Game UI, Components ve Entities kategorilerindeki
    [Editör komutları](../reference/editor-commands.md)). Bunları ayrı çağrılar olarak ya da bir
    `editor.batch` içinde gönderin:

    ```json
    {"call": "gameui.create", "params": {"kind": "canvas", "name": "HUD"}}
    {"call": "gameui.create", "params": {"kind": "text", "parent": "/HUD", "name": "Score"}}
    {"call": "gameui.set_anchors", "params": {"entity": "/HUD/Score", "preset": "top_right"}}
    {"call": "gameui.place", "params": {"entity": "/HUD/Score", "min": [1600, 24], "max": [1896, 88]}}
    {"call": "component.set", "params": {"entity": "/HUD/Score", "type": "illusion.UiText", "value": {"key": "hud.score", "size": 48, "align": "End", "wrap": false, "outlineWidth": 2.0, "outlineColor": [0.01, 0.012, 0.015, 1.0]}}}
    {"call": "entity.create", "params": {"name": "Hint", "parent": "/HUD", "components": {"illusion.UiElement": {"placement": {"anchorMin": [0, 0], "anchorMax": [0, 0], "offsetMin": [24, 24], "offsetMax": [600, 72]}}, "illusion.UiStack": {"axis": "Horizontal", "spacing": 12, "crossAlign": "Center"}}}}
    {"call": "entity.create", "params": {"name": "Menu Glyph", "parent": "/HUD/Hint", "components": {"illusion.UiElement": {"size": [0, 44]}, "illusion.UiText": {"font": "assets:/fonts/Roboto-Regular.ttf", "text": "Esc", "size": 32, "wrap": false, "outlineWidth": 2.0}, "illusion.UiGlyph": {"action": "ui.cancel"}}}}
    {"call": "entity.create", "params": {"name": "Menu Hint", "parent": "/HUD/Hint", "components": {"illusion.UiElement": {"size": [0, 44]}, "illusion.UiText": {"font": "assets:/fonts/Roboto-Regular.ttf", "key": "hud.menuHint", "size": 32, "wrap": false, "outlineWidth": 2.0}}}}
    ```

    `gameui.layout` her öğenin dikdörtgenini canvas birimi ve piksel olarak döndürür; böylece ajan sonucu
    resim olmadan denetleyebilir (`{"call": "gameui.layout", "params": {"entity": "/HUD"}}`);
    `"viewport": "game"` ile `viewport.screenshot` bir resim alır.

## 4. Duraklatma menüsü

İkinci bir canvas oluşturun ve adını **Pause Menu** yapın. Inspector'da `UiCanvas`'ın **Order**'ını 10 yapın
(HUD'ın üstüne çizilir) ve **Modal**'ı açın. Sonra aşağıdaki gibi kurun; listede belirtilen yerde her öğe
bir üsttekinin içine girer.

1. **Dim**: canvas'ın içinde bir **Panel**; canvas'ı dolduracak şekilde gerdirme ön ayarıyla bağlanmış, dört
   offset'i de 0 ve `UiImage`'ının rengi koyu, yarı saydam bir renk.
2. **Panel**: canvas'ın içinde bir **Stack**, ortaya bağlanmış, offset min (-320, -300) ve offset max
   (320, 300). Ona bir `UiImage` verin (koyu bir renk) ve stack'in **Spacing**'ini 24, **Padding**'ini
   (48, 40, 48, 40) ve **Cross Align**'ını `Center` yapın. Tetikleyicisi `Shown`, **Duration**'ı 0.25,
   **Offset**'i (0, 48), **Scale**'i (0.96, 0.96) ve **Opacity**'si 0 olan **UI transition** Component'ini
   ekleyin: menü göründüğünde panel kaymış, daha küçük ve saydam başlar ve dinlenme durumuna doğru yumuşar.
3. **Heading**: **Panel**'in içinde bir **Text**, boyut (0, 80), `UiText` anahtarı `hud.menu.title`, boyut 60,
   ortalı.
4. **Resume**: **Panel**'in içinde bir **Button**, boyut (440, 72), etiketin anahtarı `hud.menu.resume`,
   ortalı. Bir düğmenin etiketi, alt öğesi **Label**'dır.
5. **Volume Label**: bir **Text**, boyut (440, 40), anahtar `hud.menu.volume`, ortalı.
6. **Volume**: **Panel**'in içinde bir **Slider**, boyut (440, 24), **Value** 1, **Min** 0, **Max** 1 ve **Step**
   0.05. İlk alt öğesi **Fill** değere kadar gerilir, ikinci alt öğesi **Handle** onun üzerinde durur.
7. **Quit**: bir **Button**, boyut (440, 72), etiketin anahtarı `hud.menu.quit`.

Son olarak **Pause Menu**'yü seçin ve **enabled** onay kutusunu kapatın (`entity.set_enabled`): menü başlangıçta
gizlidir. Sahneyi **Ctrl+S** ile kaydedin. Panelin içindeki öğelerin kendi yerleşimi yoktur: stack onları
`Center` olan `Cross Align` ile alt alta yerleştirir.

Çalışırken menüye bakmak için canvas'ı bir an için yeniden açın: Game view onu her öğe dinlenme durumundayken
çizer, çünkü bir geçişi yalnızca çalışan bir oyun ilerletir.

!!! note "Ajanla"
    Menü, font ve anahtarlarla aynı çağrı kümesidir; `gameui.create` düğmeyi ve kaydırıcıyı alt öğeleriyle
    birlikte oluşturur, `component.set` Component'lerini değiştirir:

    ```json
    {"call": "gameui.create", "params": {"kind": "canvas", "name": "Pause Menu"}}
    {"call": "component.set", "params": {"entity": "/Pause Menu", "type": "illusion.UiCanvas", "value": {"order": 10, "modal": true}}}
    {"call": "entity.create", "params": {"name": "Dim", "parent": "/Pause Menu", "components": {"illusion.UiElement": {"placement": {"anchorMin": [0, 0], "anchorMax": [1, 1], "offsetMin": [0, 0], "offsetMax": [0, 0]}}, "illusion.UiImage": {"tint": [0.0, 0.01, 0.02, 0.55]}}}}
    {"call": "entity.create", "params": {"name": "Panel", "parent": "/Pause Menu", "components": {"illusion.UiElement": {"placement": {"anchorMin": [0.5, 0.5], "anchorMax": [0.5, 0.5], "offsetMin": [-320, -300], "offsetMax": [320, 300]}}, "illusion.UiImage": {"tint": [0.03, 0.045, 0.04, 0.92]}, "illusion.UiStack": {"spacing": 24, "padding": {"left": 48, "top": 40, "right": 48, "bottom": 40}, "crossAlign": "Center"}, "illusion.UiTransition": {"trigger": "Shown", "duration": 0.25, "ease": "EaseOut", "offset": [0, 48], "scale": [0.96, 0.96], "opacity": 0.0}}}}
    {"call": "entity.create", "params": {"name": "Heading", "parent": "/Pause Menu/Panel", "components": {"illusion.UiElement": {"size": [0, 80]}, "illusion.UiText": {"font": "assets:/fonts/Roboto-Regular.ttf", "key": "hud.menu.title", "size": 60, "align": "Center", "wrap": false}}}}
    {"call": "gameui.create", "params": {"kind": "button", "parent": "/Pause Menu/Panel", "name": "Resume"}}
    {"call": "component.set", "params": {"entity": "/Pause Menu/Panel/Resume", "type": "illusion.UiElement", "value": {"size": [440, 72]}}}
    {"call": "component.set", "params": {"entity": "/Pause Menu/Panel/Resume/Label", "type": "illusion.UiText", "value": {"key": "hud.menu.resume", "size": 36, "wrap": false}}}
    {"call": "entity.create", "params": {"name": "Volume Label", "parent": "/Pause Menu/Panel", "components": {"illusion.UiElement": {"size": [440, 40]}, "illusion.UiText": {"font": "assets:/fonts/Roboto-Regular.ttf", "key": "hud.menu.volume", "size": 30, "align": "Center", "wrap": false}}}}
    {"call": "gameui.create", "params": {"kind": "slider", "parent": "/Pause Menu/Panel", "name": "Volume"}}
    {"call": "component.set", "params": {"entity": "/Pause Menu/Panel/Volume", "type": "illusion.UiElement", "value": {"size": [440, 24]}}}
    {"call": "component.set", "params": {"entity": "/Pause Menu/Panel/Volume", "type": "illusion.UiSlider", "value": {"value": 1.0, "min": 0.0, "max": 1.0, "step": 0.05}}}
    {"call": "gameui.create", "params": {"kind": "button", "parent": "/Pause Menu/Panel", "name": "Quit"}}
    {"call": "component.set", "params": {"entity": "/Pause Menu/Panel/Quit", "type": "illusion.UiElement", "value": {"size": [440, 72]}}}
    {"call": "component.set", "params": {"entity": "/Pause Menu/Panel/Quit/Label", "type": "illusion.UiText", "value": {"key": "hud.menu.quit", "size": 36, "wrap": false}}}
    {"call": "entity.set_enabled", "params": {"entity": "/Pause Menu", "enabled": false}}
    {"call": "scene.save", "params": {}}
    ```

    `tests/acceptance/game-ui/menu.jsonl`, bir menü kuran, oynayan ve `gameui.click` ile bir düğmeye tıklayan
    bir ajanın tam transkriptidir.

## 5. Oyun modülü

Komut paletinden **New module**'ü `hud` adıyla çalıştırın. Modül UI'ı `UiService` ile okur, ana ses düzeyini
`AudioService` ile ayarlar ve oyunu `SceneService` ile bitirir; bu yüzden `ui`, `audio` ve `runtime` kullanır:

```cmake
illusion_add_module(hud SHARED SOURCES hud.cpp USES audio runtime ui)
```

`modules/hud/hud.cpp` dosyasını açın. Modülün durumu, yönettiği öğeleri tutan tek bir runtime Component'tir.
Sahneye kaydedilmez ve hot reload'dan sağ çıkar:

```cpp
#include <illusion/audio/AudioService.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/runtime/SceneService.hpp>
#include <illusion/ui/UiService.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace hud
{

using namespace illusion;

struct State
{
    Entity score{};
    Entity menu{};
    Entity resume{};
    Entity volume{};
    Entity volumeLabel{};
    Entity quit{};
    f64 seconds = 0.0;
    u32 points = 0;
    bool scoreShown = false;
};

} // namespace hud
```

`start` öğeleri adlarıyla bir kez bulur ve saklar. Bir ad hiyerarşide dolaşmaya mal olur, bir Entity ise hiçbir
şeye; bu yüzden modül `find`'ı `update` içinde çağırmaz. Gizli bir canvas'ın öğeleri de bulunur:

```cpp
namespace
{

using namespace illusion;
using hud::State;

constexpr f64 kPointSeconds = 1.0;
constexpr u32 kPointsPerStep = 10;

bool loadHud(ModuleContext&)
{
    reflectRuntime<State>("hud.State", {.displayName = "HUD state"});
    return true;
}

void startHud(ModuleContext& context)
{
    UiService* ui = context.service<UiService>();
    if (ui == nullptr || !context.hasWorld() || context.worldRole() != WorldRole::Simulation)
    {
        return;
    }
    State state;
    state.score = ui->find({}, "Score");
    state.menu = ui->find({}, "Pause Menu");
    state.resume = ui->find(state.menu, "Resume");
    state.volume = ui->find(state.menu, "Volume");
    state.volumeLabel = ui->find(state.menu, "Volume Label");
    state.quit = ui->find(state.menu, "Quit");
    context.world().create(Name{"HUD State"}, state);
}
```

Metinler argümanlı string table anahtarlarıdır; modül dile bağlı bir string'i hiç kendisi kurmaz. Menü geçerli
ana ses düzeyiyle gösterilir ve odak Resume'a taşınır; böylece bir gamepad ya da klavye hemen kullanabilir:

```cpp
void showScore(UiService& ui, const State& state)
{
    const std::array points{UiTextArgument::number("points", static_cast<f64>(state.points))};
    static_cast<void>(ui.setTextKey(state.score, "hud.score", points));
}

void showVolume(UiService& ui, const State& state, f32 volume)
{
    const std::array percent{UiTextArgument::number("percent", std::round(static_cast<f64>(volume) * 100.0))};
    static_cast<void>(ui.setTextKey(state.volumeLabel, "hud.menu.volume", percent));
}

void showMenu(UiService& ui, const AudioService* audio, const State& state, bool show)
{
    static_cast<void>(ui.setVisible(state.menu, show));
    if (show)
    {
        const f32 volume = audio != nullptr ? audio->masterVolume() : 1.0f;
        static_cast<void>(ui.setSliderValue(state.volume, volume));
        showVolume(ui, state, volume);
        static_cast<void>(ui.setFocus(state.resume));
    }
}
```

`update`, son UI güncellemesinin event'lerini okur: `Cancelled` `ui.cancel`'dır (Escape ya da gamepad'in East
düğmesi), `Clicked` bir düğme, `ValueChanged` kaydırıcıdır. Puan menü kapalıyken artar; bu, sizin oyununuzun
yerini tutar:

```cpp
void updateHud(ModuleContext& context, f64 deltaSeconds)
{
    UiService* ui = context.service<UiService>();
    if (ui == nullptr)
    {
        return;
    }
    World& world = context.world();
    Entity self{};
    world.query<const State>().each([&self](Entity entity, const State&) { self = entity; });
    if (!world.isAlive(self))
    {
        return;
    }
    // A copy: the UI changes the structure of the World, which would invalidate a reference.
    State state = world.get<State>(self);
    AudioService* audio = context.service<AudioService>();
    SceneService* scenes = context.service<SceneService>();

    std::array<UiEvent, 16> events{};
    const u32 count = std::min<u32>(ui->pollEvents(events), static_cast<u32>(events.size()));
    for (const UiEvent& event : std::span(events).first(count))
    {
        if (event.type == UiEventType::Cancelled)
        {
            showMenu(*ui, audio, state, !ui->isVisible(state.menu));
        }
        else if (event.type == UiEventType::Clicked && event.entity == state.resume)
        {
            showMenu(*ui, audio, state, false);
        }
        else if (event.type == UiEventType::Clicked && event.entity == state.quit && scenes != nullptr)
        {
            scenes->requestExit();
        }
        else if (event.type == UiEventType::ValueChanged && event.entity == state.volume)
        {
            if (audio != nullptr)
            {
                audio->setMasterVolume(event.value);
            }
            showVolume(*ui, state, event.value);
        }
    }

    const bool open = ui->isVisible(state.menu);
    // A menu shown in this update is laid out at its end; the focus can go to Resume only afterwards.
    if (open && ui->focused().isNull())
    {
        static_cast<void>(ui->setFocus(state.resume));
    }
    bool changed = !state.scoreShown;
    if (!open)
    {
        state.seconds += std::max(deltaSeconds, 0.0);
        while (state.seconds >= kPointSeconds)
        {
            state.seconds -= kPointSeconds;
            state.points += kPointsPerStep;
            changed = true;
        }
    }
    if (changed)
    {
        showScore(*ui, state);
        state.scoreShown = true;
    }
    world.get<State>(self) = state;
}

constexpr ModuleApi kHud =
    makeModuleApi("hud", "1.0.0", {.load = &loadHud, .start = &startHud, .update = &updateHud});

} // namespace

ILL_MODULE(hud, kHud)
```

Menü görünürken canvas'ı modaldır: `InputService` ile `move` ya da `jump` okuyan bir modül bunları basılmamış
görür; böylece oyun kendi denetimi olmadan durur. Modül UI'ı yalnızca `UiService` ile değiştirir, bu yüzden
düzen `world.markChanged()` olmadan izler (bkz. [UI'ı script'lemek](../manual/ui.md#scripting-the-ui)).

**Ctrl+B**'ye (**Modules → Build Modules**) basın. Build başarılı olunca modül yüklenir.

## 6. Deneyin

**Play**'e basın:

- Puan sağ üst köşede artar ve ipucu **Esc Menu** gösterir.
- **Escape**'e basın: menü kayarak gelir, Resume odaktadır ve puan durur. Odağı ok tuşları, W ve S ya da D-pad
  ile taşıyın, bir düğmeyi Enter, Space ya da South düğmesiyle etkinleştirin. Düğmelere tıklayın ve kaydırıcıyı
  fareyle sürükleyin.
- Bir gamepad'e dokunun: ipucu onun `ui.cancel` bağlamasının etiketine döner.
- Dili değiştirin: `Clicked` dalında `ui->setLanguage("tr")` çağıran bir düğme ekleyin (tech demo'da vardır);
  anahtarı olan her metin izler, ses etiketi `{percent}` argümanını korur.
- **Quit** oyunu bitirir: player kapanır ve editör Play modundan çıkar.

Bir ajan aynısını `play.start`, `gameui.focus` ve `/Pause Menu/Resume` üzerinde `gameui.click` ile dener ve
tıklamanın döndürdüğü `events`'i okur. Oyun çalışırken modülü değiştirip **Ctrl+B**'ye basın: durumunu koruyarak
hot-reload edilir.

## 7. Sınırlar

- Metin soldan sağa, kod noktası başına bir glyph ile çizilir; shaping ve sağdan sola metin yoktur: Arapça,
  İbranice ve Hint yazı sistemleri çalışmaz, CJK font atlas'ı için fazla büyük karakter aralıkları gerektirir.
- Oyuncu bir text field'da metin seçemez ve panoyu kullanamaz.
- Glyph'ler etikettir (`Esc`, `A`, `Cross`), tuş resmi değildir.
- UI ekranı kaplar; dünyadaki bir karakterin üstünde etiket mümkün değildir ve Scene view UI çizmez.

## Export edin {#export-it}

Oyun, export edildiğinde editör olmadan çalışır: **File > Export…** ya da `export.build` komutu, oyunun
dizinini çalıştırılabilir dosya ve paketlenmiş verisiyle (fontlar ve locale dosyaları dahil) yazar.
[Export kılavuzu](../manual/editor/export.md) ayarları, [Oyunu Yayınlamak](../guides/release.md) ise bir
mağazaya giden yolu anlatır.

## Sonraki adımlar

- Stack ve grid'lerle düzen, text field'lar, scroll view'lar ve geçişler için [Oyun UI'ı kılavuzunu](../manual/ui.md)
  okuyun.
- `samples/techdemo` modülü bir dil düğmesi ve kare süresi göstergesi ekler; `tests/acceptance/tetris` bir puan
  HUD'ı ve bir oyun sonu menüsü gösterir.
- [UI nasıl kurulur](../architecture/game-ui.md) katmanları ve bütçeleri açıklar.
