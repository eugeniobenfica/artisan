# Notes for coding agents

Artisan is a minimal, fast, cross-platform 2D game engine (C++20): an engine library
with no Qt (`engine/`), an SDL3 player (`player/`), and a Qt 6 Widgets editor
(`editor/`) that looks like bemotion (`libs/bestyle`, shared with it). Read
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) before changing code.

## Rules

- Layers point down: `core` ← `nodes` ← `script` ← `gfx` ← `runtime` in the engine;
  the editor's `app` ← `ui`. The engine never includes Qt or SDL; the player and the
  editor bring the window.
- The `.artisan` format, the Lua API and node types change only compatibly: new
  optional fields with defaults, never renames. New script abilities and nodes are
  documented in `docs/SCRIPTING.md` and tested in the same commit.
- `libs/bestyle` is a git subtree of the bestyle repository, shared with bemotion:
  commit changes there on their own (no other files), then `tools/bestyle.sh push`;
  `tools/bestyle.sh pull` brings in the other apps' changes.
- Colors in the editor come from theme tokens (`theme::kText`, `kAccent`…) read at
  paint time, labels through `setTone` — themes change while the app runs. Interface
  text is English, short, sentence case, through `tr()`.
- UTF-8 paths: `platform::fromUtf8` / `utf8` / `toQt` / `fromQt` (never
  `path.string()` on Windows).
- Vulkan goes through volk (`VK_NO_PROTOTYPES`); Qt code without Vulkan support must
  still build (`QT_CONFIG(vulkan)`).
- Match the surrounding code (`.clang-format`); change only what the task needs.

## Build, run, test (Windows, MSYS2 UCRT64)

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build && ctest --test-dir build
```

- The editor needs `C:\msys64\ucrt64\bin` on `PATH` (Qt DLLs): `run.bat` does it. The
  player is static and needs nothing.
- **Look at results.** The player: `build/bin/artisan-player <project> --frames 120`
  (prints its log). The editor: run with `ARTISAN_APPDATA=<scratch folder>` (never the
  user's settings) and `ARTISAN_CAPTURE=<folder>`: it screenshots every screen and quits.
  Export: `build/bin/artisan --export windows <project> <out>`.
- If linking fails with "Permission denied", an `artisan.exe` is running — maybe the
  user's. Never kill it unless you started it.
- Commits: short imperative subject, body with what and why, `Co-Authored-By:` trailer
  for AI-assisted work; docs and tests in the same commit.

## Pitfalls already met

- nlohmann: `for (auto& [k, v] : j.value("x", json::object()).items())` iterates a dead
  temporary — keep the value in a local first.
- `QListWidget` makes its first row current when it gets the keyboard: select from
  `itemSelectionChanged`, not `currentItemChanged`.
- A `QVulkanWindow` inside the editor gets the keys itself: window shortcuts are
  `Qt::ApplicationShortcut`, and widgets that need Delete / F accept `ShortcutOverride`.
- Files in the OneDrive folder can be locked for a moment while syncing: writes retry
  once (`platform::writeFile`).
