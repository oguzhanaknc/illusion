# Ajanla Tetris

Bu eğitimde Claude Code, editörün MCP köprüsü üzerinden eksiksiz bir Tetris oyunu kurar: ajanın yazıp
derlediği bir oyun modülü, Material'lar, bir sahne, input eylemleri ve test etmek için oynadığı bir oyun. Her
şeyin editör penceresinde gerçekleştiğini izlersiniz ve hayır deme hakkınız her zaman sizdedir. Oyun, ajan
kabul sözleşmesinin (`tests/acceptance/tetris/SPEC.md`) oyunudur; CI bunu senaryolu bir oturuma karşı kontrol
eder.

illusion'ın bir Release Build'ine (`build/bin/Release` içinde `illusion_editor`, `illusion_mcp` ve
`illusion_ctl`), editörün modül SDK'sının kullanabileceği bir C++ derleyicisine (editörün derlendiği
derleyici) ve Claude Code'a ihtiyacınız var. Köprüyü daha önce kullanmadıysanız önce
[İlk Ajan Oturumunuz](first-agent-session.md) eğitimini yapın.

## 1. Projeyi oluşturun

Boş bir proje oluşturun; `project.create`, Claude Code'un okuduğu `.mcp.json` dosyasını da yazar:

```bash
mkdir -p ~/games
illusion_ctl --launch build/bin/Release/illusion_editor --editor-arg --workspace --editor-arg ~/games \
    call project.create '{"directory": "tetris", "name": "Tetris", "template": "empty"}'
cp tests/acceptance/tetris/SPEC.md ~/games/tetris/SPEC.md
```

## 2. Editör penceresini açın

Editörü bir soketle kendiniz başlatın. `.mcp.json` içindeki köprü `auto` modunda çalışır ve headless bir editör
başlatmak yerine bu editöre bağlanır; böylece ajanın her değişikliği sizin pencerenizde görünür:

```bash
build/bin/Release/illusion_editor --project ~/games/tetris --rpc-listen
```

**Window → Layouts → Agent** seçin: AI Activity paneli ajanın her çağrısını argümanları ve ne kadar
sürdüğüyle listeler. Ajanın neler yapabileceğine panelin izin modu karar verir: pencerenin varsayılanı olan
**Ask** modunda ajan sahneyi serbestçe düzenler, ama modül kaynağını yazmak ya da derlemek gibi dosya, Build ve
yıkıcı komutlar siz izin verene kadar bekler. Bir komut türüne oturumun geri kalanı için izin verirseniz bir
daha sorulmaz. Bkz. [İzin modları](../manual/editor/ai-agents.md#izin-modlar).

## 3. Sözleşmeyi ajana verin

Claude Code'u projede başlatın ve `illusion` sunucusunu onaylayın:

```bash
cd ~/games/tetris
claude
```

Sonra şunu isteyin:

> Read illusion://guide, then build the game described in SPEC.md in this project with the editor tools.
> Write the module, build it with module_build until it compiles without errors, create the materials, the
> scene and the input actions, save the scene, and test the rules in Play mode with fixed steps before you
> report back.

## 4. Ajanın yaptıkları

Sıra oturumdan oturuma değişir, ama başarılı bir oturum şu adımlardan geçer:

| Adım | Araçlar |
| --- | --- |
| Kılavuzu, sözleşmeyi ve gereken araçları okur | `illusion://guide`, `editor_describe` |
| Modülü oluşturur ve kaynağını yazar | `module_create`, `project_file_write` |
| Derler, tanılamaları okur ve düzeltir | `module_build` |
| Modülün reflect ettiği Component'leri kontrol eder | `component_schema` |
| Sekiz Material'ı oluşturur | `material_create` |
| Sahneyi kurar: kamera, güneş, gökyüzü, duvarlar ve zemin, Board, PieceQueue ve Score | `editor_batch`, `entity_create`, `component_add` |
| Beş eylemi tanımlar ve projeyi bunlara yönlendirir | `input_action_define`, `project_settings_set` |
| Sahneyi kaydeder | `scene_save` |
| Oynar: Play modunu başlatır, tuşlara basar, adım ilerletir ve parçaları geri okur | `play_start`, `input_inject`, `play_step`, `component_get` |

Başarısız bir derleme bir araç hatası değildir: `module_build` tanılamalarla birlikte `"succeeded": false`
döndürür; ajan kaynağı düzenleyip yeniden derler. Ajan oynarken pencerenizin Game görünümü parçaların
düştüğünü gösterir.

## 5. Sonucu kendiniz kontrol edin

- **Play**'e basın, sonra ok tuşlarını ve Space'i (ya da bir gamepad'in D-pad, South ve North düğmelerini)
  kullanın. **Stop** sahneyi tam olarak eski hâline getirir.
- `modules/tetris/tetris.cpp` dosyasını kod editörünüzde açın (`editor_open_source` onu bir satırda açar) ve
  ajanın yazdıklarını okuyun.
- Ajanın son adımını `history_undo` ve `{"scope": "mine"}` ile ya da pencerenizde **Edit → Undo** ile geri
  alın: ajanın değişiklikleri sıradan geri alma adımlarıdır.
- Oyunu SPEC.md'deki kurallarla karşılaştırın: doğma konumu, SRS kaydırmaları, seviyeye göre yerçekimi, satır
  puanları ve oyunun bitişi.

## 6. Bir oturumu kaydedin

Bir oturumun her MCP mesajını bir hata raporu için ya da iki oturumu karşılaştırmak için saklamak üzere
`.mcp.json` içinde köprünün `args` listesine `--record session.jsonl` ekleyin. Diğer köprü seçeneklerini
[yapay zekâ ajanları kılavuzu](../manual/editor/ai-agents.md) listeler.

## Sonraki adımlar

- [Elle Tetris](tetris-by-hand.md), aynı oyunu editör penceresinde adım adım kurar.
- [Otomasyon kılavuzu](../manual/editor/automation.md), senaryolu kabul transcript'inin aynı komutları bir
  ajan olmadan nasıl çalıştırdığını gösterir.
