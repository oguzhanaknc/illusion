# Outliner ve Inspector

Outliner ve Inspector, `illusion_editor` içinde bir sahneyi kurmak ve düzenlemek için kullanılan iki ana paneldir. Her iki paneldeki her eylem bir [Editor command](../../reference/editor-commands.md) çalıştırır; böylece elle yapılan düzenlemeler ile bir yapay zekâ ajanının yaptığı düzenlemeler aynı undo geçmişini, doğrulama kurallarını ve transaction'ları paylaşır.

## Outliner

Outliner, açık sahnedeki her Entity'yi hiyerarşi sırasına (`HierarchyIndex`) göre bir ağaç olarak listeler. Devre dışı Entity'ler (ve devre dışı Entity'lerin `Inactive` etiketini taşıyan alt Entity'leri) soluk eğik yazı ile çizilir.

| Denetim | Yaptığı iş |
| --- | --- |
| Arama çubuğu | Siz yazarken ağacı süzer. Düz metin Entity adlarını bulanık eşleştirir; belirteçler Component, durum veya kaynağa göre süzer. |
| **+** düğmesi | Boş bir Entity (`entity.create`), 3B temel şekil (`entity.create_primitive`), ışık (`entity.create_light`) veya kamera (`entity.create_camera`) oluşturmak için bir menü açar. |
| Genişletme oku | Bir Entity'nin alt öğelerini daraltır veya genişletir; **Sol** ve **Sağ** ok tuşları daraltır, genişletir veya alt öğelere geçer. |
| Göz ikonu | Yalnızca editörde geçerli görünürlüğü değiştirir (`hidden` ile `view.set_entities`). Gizli Entity'ler sahne dosyasında değişmeden kalır. |
| Kilit ikonu | Yalnızca editörde geçerli kilitlemeyi değiştirir (`locked` ile `view.set_entities`). Kilitli Entity'lerin Transform ve Component alanları Inspector'da salt okunurdur. |

### Arama süzgeçleri

İstediğiniz sayıda belirteci bir ad sorgusuyla birleştirin:

| Belirteç | Eşleştiği Entity'ler |
| --- | --- |
| `has:Component` veya `t:Type` | kararlı adı veya görünen adı belirteci içeren bir Component'e sahip olanlar (örneğin `has:Light` veya `t:RigidBody`). |
| `disabled:true` / `disabled:false` | devre dışı (`Disabled` veya `Inactive`) ya da etkin olanlar. |
| `prefab:true` | bir prefab örneğine ait olanlar. |

Bir süzgeç etkinken, eşleşen Entity'ler sağda tam hiyerarşi yollarıyla düz bir liste olarak gösterilir.

### Seçim, yeniden adlandırma ve sürükle-bırak

- **Tıklama** bir Entity seçer (`selection.set`); **Ctrl+tıklama** seçimdeki durumunu değiştirir (`selection.add` / `selection.remove`); **Shift+tıklama** çapa satırdan itibaren aralığı seçer; **Yukarı** ve **Aşağı** görünür satırlar arasında gezinir.
- Birincil seçili Entity'yi satır içinde yeniden adlandırmak için **F2** tuşuna basın (veya bağlam menüsünden **Rename** seçin) (`entity.rename`).
- Bir veya daha fazla Entity'yi başka bir satırın üzerine **sürükleyerek** ebeveynini değiştirin (`hierarchy.set_parent`) ya da kardeşler arasında yeniden sıralamak için bir satırın üstündeki veya altındaki ince boşluğa bırakın (`hierarchy.reorder`). Dünya Transform'u yerine yerel Transform'u korumak için bırakırken **Alt** tuşunu basılı tutun (`keepWorld: false`). Sahne köküne taşımak için panelin altındaki boş alana sürükleyin.
- Bir Entity'yi atamak için Outliner'dan Inspector'daki herhangi bir `Entity` referans alanının üzerine **sürükleyin**.

```json
{"method": "hierarchy.set_parent", "params": {"entities": ["/Crate"], "parent": "/Room", "keepWorld": true}}
{"method": "view.set_entities", "params": {"entities": ["/Room"], "hidden": false, "locked": true}}
```

## Inspector

Inspector geçerli seçimi gösterir ve düzenler. Başka bir yerde seçimi değiştirirken Inspector'ı mevcut Entity'lerine sabitlemek için panelin sağ üst köşesindeki **iğne** düğmesine tıklayın.

### Entity başlığı

Inspector'ın en üstünde:

- **Etkin** onay kutusu incelenen her Entity üzerinde `Disabled` etiketini değiştirir (`entity.set_enabled`).
- **Ad** alanı birincil Entity'yi yeniden adlandırır (`entity.rename`) veya kaç Entity'nin incelendiğini gösterir.
- **Kilit** düğmesi incelenen Entity'ler üzerinde `view.set_entities` (`locked`) durumunu değiştirir.
- **ID** düğmesi birincil Entity'nin 16-hex `EntityGuid` değerini panoya kopyalar.

### Transform kartı

İncelenen her Entity'de bir `Transform` olduğunda ilk sırada Transform kartı görünür:

- Başlıktaki parçalı seçici ile **Local** ve **World** uzayı arasında geçiş yapın (`"space": "local"` veya `"world"` ile `transform.set`).
- **Position**, **Rotation** (Euler derece cinsinden) ve **Scale** renkli X/Y/Z sürükleme alanlarını kullanır. Inspector, Euler açılarınızı Entity başına önbelleğe alır; böylece `180°` sapma (yaw) veya çok turlu açılar quaternion'dan geri ayrıştırıldığında ters dönmez.
- Yalnızca o özelliği sıfırlamak için herhangi bir satır etiketine (**Position**, **Rotation** veya **Scale**) sağ tıklayın ya da tüm Transform'u sıfırlamak için kartın `⋮` menüsünü açın (`transform.reset`).

```json
{"method": "transform.set", "params": {"entities": ["$selection"], "space": "local", "position": [0, 2, 0], "euler": [0, 180, 0]}}
```

### Component kartları ve alan widget'ları

İncelenen Entity'ler üzerindeki her reflection'lı Component; ikonu, görünen adı ve bir `⋮` menüsü (**Reset**, **Copy Component**, **Paste Component Values**, **Remove Component** ve `docsAnchor` tanımlıysa **Documentation**) ile daraltılabilir bir kart olarak çizilir. Geçici çalışma zamanı Component'leri (`PhysicsBody`, `CharacterBody`, `RenderMesh`, `RenderSky`) salt okunur gösterilir.

Alan widget'ları Component'in reflection bilgisinden ve `FieldMeta`'dan otomatik olarak seçilir:

| Alan / Üstveri | Widget |
| --- | --- |
| Sayılar (`f32`, `f64`, tamsayılar) | Birim sonekli sürükleme alanı (`m`, `°`, `kg`, `N`, `s`, `Hz`, `dB`, `lm`, `lx`, `cd`). `displayUnit = Unit::Degrees` ile radyan olarak saklanan açılar derece cinsinden düzenlenir ve otomatik dönüştürülür. |
| `FieldHint::Slider` / `LogSlider` | `[min, max]` veya `[softMin, softMax]` aralığında doğrusal ya da logaritmik kaydırıcı. |
| `Vec2`, `Vec3`, `Vec4`, `Quat` | Çok eksenli sürükleme alanları (`Quat`, Euler derece olarak düzenlenir). |
| `FieldHint::Color` / `HdrColor` | Renk örneği ve seçici; `Color` sRGB `#rrggbb` veya doğrusal RGB yazar, `HdrColor` ayrı bir yoğunluk sürükleme alanı ekler. |
| `Enum` ve `Flags` | Numaralandırmalar için açılır liste; bit maskesi bayrakları için aç/kapa çipleri. |
| `FieldHint::Layer` | Çarpışma katmanı seçicisi (`0..31`). |
| `Entity` | Hedefin adını gösteren Entity düğmesi (tıklamak onu seçer), aranabilir Entity seçici açılır penceresi, Entity'yi bir Scene görünümünde seçen bir damlalık (bkz. [Scene Viewport'u](viewport.md#bir-alan-icin-entity-secmek)), temizleme düğmesi ve Outliner satırları için bırakma hedefi. |
| `Asset` (`AssetRef`) | Sanal yol alanı (`assets:/...` veya `builtin:/...`), `FieldMeta::assetKind` ile süzülen aranabilir Asset seçici, temizleme düğmesi ve Asset'ler için bırakma hedefi. |
| `Optional<T>`, `Array<T>`, `FixedArray<T>`, `Struct` | `Optional` için onay kutusu, `Array` için eleman sayısı ve `+` / `-` düğmeleri, struct'lar için girintili iç içe satırlar. |

Herhangi bir sayısal alanı veya vektör alanını sürüklerken:

- **10×** daha hızlı değiştirmek için **Shift**, **0.1×** daha yavaş değiştirmek için **Alt** tuşunu basılı tutun.
- Sonsuz sürükleme (**infinite drag**) için imleç yakalanır (`CursorMode::Captured`); böylece ekran kenarına takılmadan sürüklemeye devam edebilirsiniz.
- Bir sürüklemenin ardışık kareleri aynı `mergeKey` değerini paylaşır ve fare düğmesini bıraktığınızda **tek bir undo adımında** birleşir.
- Yalnızca o alanı sıfırlamak için herhangi bir özellik etiketine sağ tıklayıp **Reset** seçin (`path` ile `component.reset`).

### Özel çiziciler ve satır içi doğrulama

Birkaç motor Component'i, ilgisiz alanların kalabalık etmemesi için kartını etkin moda göre uyarlar:

- **Collider** (`illusion.Collider`) yalnızca seçilen `shape` değerine ait boyutları gösterir (`Box` için `halfExtents`, `Sphere` için `radius`, `Capsule` ve `Cylinder` için `radius` ve `halfHeight`, `ConvexHull` ve `Mesh` için `mesh` ve `meshIndex`).
- **Joint** (`illusion.Joint`) seçilen `JointType` için geçerli olmayan çapa, eksen, sınır ve motor alanlarını gizler; `minLimit` / `maxLimit` alanlarını yalnızca `limited` işaretliyken gösterir.
- **AudioSource** (`illusion.AudioSource`) `spatial` kapalıyken 3B mesafe, azalma, doppler ve koni alanlarını gizler.

Bir Component'te yapılandırma sorunu olduğunda, kartının üst kısmında tek tıklamalı düzeltme düğmesi içeren bir uyarı şeridi görünür:

- `requiredComponents` bağımlılığı eksik olan bir Component, eksik Component'i seçimin tamamına eklemek için **Add** düğmesini sunar.
- `Dynamic` bir `RigidBody`'ye sahip Entity üzerindeki `shape = Mesh` olan bir `Collider`, üçgen Mesh'lerin yalnızca statik/kinematik olduğunu bildirir ve **Use ConvexHull** seçeneğini sunar.

### Çoklu Entity düzenleme

İki veya daha fazla Entity seçildiğinde:

- Inspector, bu Entity'lerin Component'lerinin **kesişimini** gösterir.
- Değeri seçim genelinde farklılık gösteren alanlar `—` gösterir. Bir alanı (veya bir vektörün tek bir eksenini) düzenlemek, seçili her Entity'de yalnızca o alanı veya ekseni tek bir `editor.batch` transaction'ı ve tek bir undo adımında günceller.

### Component ekleme, kopyalama ve yapıştırma

Her reflection'lı Component'i görünen adına, kararlı adına veya kategorisine göre aramak için Inspector'ın altındaki **Add Component** düğmesine tıklayın. **Enter** tuşuna basmak veya bir öğeye tıklamak, Component'i — `Transform` ya da `Collider` gibi eksik `requiredComponents` bağımlılıklarıyla birlikte — tek bir undo adımında ekler.

Bir Entity'den **Copy Component** (`component.copy`) yapmak ve herhangi bir seçime **Paste Component Values** (`component.paste`) uygulamak için kartın `⋮` menüsünü kullanın; kopyalanan Component türünü seçime eklemek veya güncellemek için Inspector'ın altındaki **Paste Component** düğmesine de tıklayabilirsiniz.

```json
{"method": "component.set_field", "params": {"entity": "/Crate", "type": "illusion.Collider", "path": "shape", "value": "ConvexHull"}}
{"method": "component.reset", "params": {"entity": "/Crate", "type": "illusion.Collider", "path": "halfExtents/x"}}
```
