# Architecture

## Parts

```
editor (Qt)  ──runs / exports──▶  artisan-player (SDL3)
   │                                   │
   └───────────── artisan_engine ──────┘
                  core ◀ nodes ◀ script ◀ gfx ◀ runtime
```

- **`artisan_engine`** (static library, no Qt, no window): everything a game needs to
  run. Layers point down only: `core` (math, scene, project format) ← `nodes` (graphs,
  library, compiler) ← `script` (Lua) ← `gfx` (Vulkan) ← `runtime` (input, drawing a
  scene, the game loop).
- **`artisan-player`**: the SDL3 window, input and file access around `Game`. On
  Windows it is linked statically: an exported game is one executable and its data.
  On Android it is `libmain.so`, loaded by SDL's activity.
- **`artisan`** (the editor): `app` (platform, settings, project files, export) ←
  `ui` (screens, panels). The viewport draws with the engine's own `Renderer2D`
  inside a `QVulkanWindow`, so the editor shows exactly what the game draws.
- **`libs/bmstyle`**: bemotion's look (theme tokens, style, icons, controls). Kept
  identical to bemotion's so both apps look the same and fixes travel both ways.

## The project format

A project is a folder: `project.artisan` (JSON), `scripts/`, `assets/`. Paths inside are
relative, with forward slashes. The format changes only compatibly: new optional fields
with defaults, never renames (`kFormatVersion` is raised only when an older Artisan
must refuse the file). Reading repairs what it can (duplicate ids) instead of refusing.

## Rendering

Everything is an instanced quad (`QuadInstance`, 96 bytes: position, size, rotation,
shape, colors, glow, lighting, UVs). `Renderer2D` sorts by layer (then the order given),
uploads the instances to a per-frame host-visible buffer and draws runs of the same
texture with one `vkCmdDraw` each. The fragment shader evaluates the shape's signed
distance (anti-aliased with `fwidth`), its outline and glow, samples the sprite, and
shades lit objects with the ambient color plus up to 32 point lights from a per-frame
uniform buffer. Colors are premultiplied. The editor's grid is a shape too (`Grid`).

`Renderer2D` records into a render pass it does not own: the player's swapchain
(`VulkanContext`) or the editor's `QVulkanWindow`.

## Behavior

`LuaRuntime` owns one `lua_State`. Each behavior (a script file or a compiled graph)
is a chunk loaded with its own `_ENV` table (reading through to the shared API), and an
object proxy (`self`, a userdata) that reads and writes the entity by id. Lua is compiled
as C++ so errors unwind with exceptions.

Node graphs never run on their own: `compileGraph` turns events into `start` / `update`
functions, follows execution wires into statements and data wires into expressions.

## The editor

`EditorContext` is the open project shared by the panels: the selection, `checkpoint()`
before a change (a snapshot of the scene for undo; changes under the same key close in
time merge into one step), `changed()` after it. Panels listen to `sceneChanged` and
`selectionChanged`.

Export (`app/Exporter`) copies the platform's player and the game's data; `artisan
--export <platform> <project> <out>` does it without a window.

## Recipes

- **A new component** (say, a collider): its struct and field in `core/Scene.h`, its JSON
  in `core/Project.cpp`, how it draws in `runtime/SceneDraw.cpp` (if it does), its section
  in `editor/ui/ObjectsPanel.cpp`, its Lua fields in `script/LuaRuntime.cpp`, a test.
- **A new node**: one entry in `nodes/NodeLibrary.cpp`; the test compiles and runs every
  node, so it is covered at once; add it to `docs/SCRIPTING.md`.
- **A new script function**: `LuaApi` in `script/LuaRuntime.cpp`, `docs/SCRIPTING.md`, a test.
