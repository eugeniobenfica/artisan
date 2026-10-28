# Artisan

A minimal, fast, cross-platform 2D game engine — free and open source.

Artisan is an editor and a runtime. You make a game in the editor (objects on the
left, the scene on the right, behavior in Lua or in node graphs), press **Play** to
run it in its own window, and **Export** it for Windows, Linux, macOS or Android.

## The idea

- **Minimal.** A small core that does a few things very well. A scene is a flat list
  of objects; an object is a transform plus the few things a 2D game needs (a shape,
  a sprite, a light) and its behavior. No hierarchy of managers to learn.
- **Easy.** The editor looks and works like [bemotion](https://github.com/eugeniobenfica/bemotion)
  (it shares its look, `libs/bmstyle`): calm, not a cockpit. A new project already
  plays.
- **Extremely fast.** One instanced draw call per texture, whatever the number of
  objects; the GPU does the shapes per pixel; the player is one static executable.
- **Innovative 2D graphics.** Shapes are signed distance fields drawn per pixel:
  sharp at any zoom, with outlines and glows for free. Up to 32 dynamic lights shade
  lit objects per pixel, and the background is "air" that lights tint without
  darkening — atmosphere with no setup.
- **Everywhere.** Vulkan on Windows, Linux and Android; MoltenVK on macOS.

## What is in it

| | |
| --- | --- |
| **Editor** | Start screen with your projects (previews, search), workspace with the Objects panel and inspector on the left and the viewport on the right (Scene, Nodes, Script and Console tabs), Settings (themes: Dark, Light, Midnight or your own JSON), Play and Export in the top right corner, undo / redo |
| **Rendering** | Vulkan (through volk), instanced quads, SDF rectangles / rounded rectangles / circles / capsules with outline and glow, sprites (PNG, JPG, BMP, TGA; pixel-art sampling), per-pixel 2D lights, the editor's grid drawn by the same shader |
| **Scripting** | Lua 5.4, one sandboxed environment per object: `start(self)`, `update(self, dt)`, `self.x`, `input.down("left")`, `camera.x`... ([docs/SCRIPTING.md](docs/SCRIPTING.md)) |
| **Nodes** | Visual scripting inspired by Unreal's Blueprints and GameMaker's events: execution wires and typed data wires, 38 nodes (events, flow, actions, input, math, logic, variables). Graphs compile to Lua — one runtime, and every graph can be read as code |
| **Export** | Windows / Linux (a folder: the game's executable + `game/`), macOS (an `.app`), Android (a Gradle project with the game inside) |

## Getting started (Windows)

Artisan builds with MSYS2's UCRT64 toolchain, the same as bemotion. In an MSYS2 UCRT64
shell, once:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-svg mingw-w64-ucrt-x86_64-vulkan-headers \
  mingw-w64-ucrt-x86_64-vulkan-loader mingw-w64-ucrt-x86_64-shaderc
```

Then double-click `run.bat` (it configures, builds what changed and opens the editor),
or by hand:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build && ctest --test-dir build
build/bin/artisan.exe
```

SDL3, volk, Lua, nlohmann/json and stb are fetched by CMake at pinned versions.

**Linux** (Ubuntu 24.04): `sudo apt install cmake ninja-build qt6-base-dev libvulkan-dev glslc libx11-dev libxext-dev libwayland-dev libxkbcommon-dev`,
then the same `cmake` commands. **macOS**: `brew install cmake ninja qt vulkan-headers vulkan-loader molten-vk shaderc`.

### The player and export without the editor

```bash
build/bin/artisan-player path/to/project        # run a game
build/bin/artisan --export windows path/to/project out/   # export from the command line
cmake -S . -B build-player -DARTISAN_PLAYER_ONLY=ON       # the engine and player only (no Qt)
```

Export needs the player of the target platform: the one next to the editor for this
computer, `templates/<platform>/artisan-player` for the others (CI builds them for every
platform). Android export makes a Gradle project: build it with `gradlew assembleRelease`
(Android SDK and NDK 28).

## Layout of the code

| Folder | What |
| --- | --- |
| `engine/artisan/core` | Math, scene and entities, the `.artisan` project format (no Qt, no GPU) |
| `engine/artisan/nodes` | Node graphs, the node library, the graph → Lua compiler |
| `engine/artisan/script` | The Lua runtime and the API scripts see |
| `engine/artisan/gfx` | `Renderer2D` (Vulkan), `VulkanContext` (swapchain), shaders |
| `engine/artisan/runtime` | Input, drawing a scene, the game loop |
| `player` | `artisan-player`: SDL3 window and input around the engine |
| `editor` | `artisan`: the Qt editor (`app`: files, settings, export; `ui`: screens and panels) |
| `libs/bmstyle` | bemotion's look (theme, style, icons, controls), shared |
| `platforms/android` | The Android Gradle template |
| `tests` | Engine tests (`ctest`) |

[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) explains how the parts fit.

## Roadmap

Next: text, audio, physics and collisions, tilemaps, animation (sprite sheets), scenes
switching, a bloom / post-processing pass, gamepads and touch, an asset browser,
translations (pt-BR first), installers and signed packages.

## License

MIT — see [LICENSE](LICENSE). Inter is under the SIL Open Font License
(`assets/fonts/Inter-OFL.txt`); SDL's Android glue under the zlib license.
