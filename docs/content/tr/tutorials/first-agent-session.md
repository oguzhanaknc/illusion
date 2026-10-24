# İlk Ajan Oturumunuz

Bu eğitimde Claude Code, editörün MCP köprüsü üzerinden yeni bir projede küçük bir sahne kurar. illusion'ın
bir Release Build'ine (`build/bin/Release` içinde `illusion_editor` ve `illusion_mcp`) ve Claude Code'a
ihtiyacınız var. Burada kullanılan her şeyi [yapay zekâ ajanları kılavuzu](../manual/editor/ai-agents.md)
açıklar.

## 1. Bir proje oluşturun

Projeyi editörün kendisiyle oluşturun; `project.create`, Claude Code'un okuduğu `.mcp.json` dosyasını da
yazar:

```bash
mkdir -p ~/games
illusion_ctl --launch build/bin/Release/illusion_editor --editor-arg --workspace --editor-arg ~/games \
    call project.create '{"directory": "tetris", "name": "Tetris", "template": "3d-starter"}'
cat ~/games/tetris/.mcp.json
```

## 2. Claude Code'u projede başlatın

```bash
cd ~/games/tetris
claude
```

Claude Code, `.mcp.json` içindeki `illusion` sunucusunu kullanıp kullanmayacağını sorar; onaylayın. `/mcp`
sunucuyu ve araçlarını (`entity_create`, `scene_tree`, ...) listeler. Köprü, proje için arka planda penceresiz
bir editör başlattı.

## 3. Bir şey isteyin

Şuna benzer bir istem deneyin:

> Read illusion://guide. Then build a Tetris board: an entity "Board" with ten columns and twenty rows of
> cubes named "Cell x,y" under it, one metre apart, starting at the origin. Save the scene as
> assets:/scenes/board.scene and check the result with scene_tree.

Ajan komutları keşfeder, tahtayı `editor_batch` ile (bütün hücreler için tek bir geri alma adımı) oluşturur,
kaydeder ve kontrol eder. Her araç çağrısı, Claude Code'da argümanlarını ve sonucunu gösterir.

## 4. Ne olduğuna bakın

Ajandan `illusion_call` aracını `{"method": "editor.activity"}` ile çağırmasını isteyin ya da editör
`--rpc-listen` ile başlatıldıysa aynı editöre karşı kendiniz çalıştırın:

```bash
illusion_ctl --project ~/games/tetris call editor.activity '{"limit": 20}'
```

Her değişikliğin kaynağı `rpc:illusion_mcp`'dir. Ajandan son adımını geri almasını (`history_undo`,
`{"scope": "mine"}` ile) ve sahneyi yeniden kontrol etmesini isteyin.

## 5. Zaten açık olan editörü kullanın

Editörü bir soketle kendiniz başlattığınızda köprü, başka bir editör başlatmak yerine ona bağlanır:

```bash
illusion_editor --headless --project ~/games/tetris --rpc-listen
```

1. adımdaki `.mcp.json` köprüyü `auto` modunda çalıştırır; bu mod o editörü
`.illusion/run/editor-endpoint.json` üzerinden bulur.

## Sonraki adımlar

- `.mcp.json` dosyasının `args` listesine `--tools core` ekleyerek ajanın bağlamını küçük tutun.
- Her mesajı görmek için bir oturumu `--record session.jsonl` ile kaydedin.
- [Otomasyon kılavuzu](../manual/editor/automation.md) aynı komutları betiklerden kullanmayı gösterir.
