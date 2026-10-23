# Projeler

Başlık: `#include <illusion/editor/core/Project.hpp>`

Bir proje; bir oyunun içeriğini, ayarlarını ve modül kaynaklarını tutan bir dizindir. Editör aynı anda bir
proje açar ve dosyalarına yalnızca mount'lar üzerinden ulaşır.

## Yerleşim

```text
MyGame/
  project.illusion      the manifest
  assets/               content: scenes, materials, models, input, audio      assets:
  settings/             project settings                                       settings:
  modules/              game module sources (editor only)                      modules:
  .illusion/            per-machine state, git-ignored
    cache/              derived data                                           cache:
    journal/            crash journals of running and crashed sessions
    autosave/           rotating autosaves of dirty scenes
    trash/              contents of replaced and deleted files, for undo
  .gitignore
```

Bir projeyi açmak eksik standart dizinleri oluşturur.

## Manifest

`project.illusion` bir JSON dosyasıdır:

```json
{
  "format": "illusion.project",
  "name": "Tetris",
  "version": "0.1.0",
  "engineVersion": "0.7.0",
  "startupScene": "assets:/scenes/main.scene",
  "modules": ["tetris"]
}
```

`name` zorunludur; `version` bir semantik sürümdür (varsayılan `0.1.0`); `startupScene` projeyle birlikte
açılır. `parseProjectManifest()` ve `writeProjectManifest()` onu okur ve yazar.

## Mount'lar

| Mount | Gösterdiği yer | Erişim |
| --- | --- | --- |
| `assets:` | `assets/` | Okuma-yazma |
| `settings:` | `settings/` | Okuma-yazma |
| `modules:` | `modules/` | Okuma-yazma |
| `cache:` | `.illusion/cache/` | Okuma-yazma |
| `builtin:` | Yerleşik Asset'ler (dosyasız) | Salt okunur |
| `editor:` | Editörün yanındaki `editor-data/` | Salt okunur |

Dosya komutları (`project.file_write`, `file_move`, `file_delete`, `file_mkdir`) yalnızca yazılabilir
mount'larda çalışır ve geri alınabilir: değiştirilen ve silinen içerikler oturum bitene kadar
`.illusion/trash` içinde tutulur.

## Şablonlar

Yeni projeler `editor-data/templates/` içindeki bir şablonun kopyasıdır:

| Şablon | İçerik |
| --- | --- |
| `empty` | Boş bir başlangıç sahnesi |
| `3d-starter` | Bir kamera, bir güneş, prosedürel bir gökyüzü, bir zemin düzlemi ve bir küp |

```cpp
auto project = illusion::editor::Project::create("C:/Games/Tetris", "Tetris",
                                                 illusion::executableDirectory() / "editor-data/templates/empty");
if (!project)
{
    ILL_LOG_ERROR("Editor", "{}", project.error());
}
```

`Project::create()` boş olmayan bir dizini reddeder; manifest'e ismi ve motor sürümünü yazar. Ardından
`EditorContext::openProject()` projeyi mount eder, çökme journal'ını başlatır ve başlangıç sahnesini açar.

## Çöken bir oturumu kurtarmak

Editör bir projeyi açtığında `.illusion/journal/` içindeki diğer oturumların journal'ları, düzgün
kapanmadan biten oturumlardır. `EditorContext::leftoverJournals()` onları en yeniden başlayarak listeler;
`project.recover` en yenisini (veya adı verileni) başladığı sahnenin üzerine tek bir undo adımı olarak
yeniden oynatır. Journal ardından silinir.
