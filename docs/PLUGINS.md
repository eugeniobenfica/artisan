# Plugins

A plugin adds ready-made objects to the editor's library and commands to its **Plugins**
menu. It is a folder with two files:

```
my-plugin/
  plugin.json   who it is and what it may do
  main.lua      what it adds
```

Settings › Plugins lists them: each one can be turned off, its folder shown, its log read.
**Install Plugin...** copies a plugin's folder (pick its `plugin.json`) into the app data
folder's `plugins/`; **Reload** loads every plugin again after you edit one. The ones that
come with Artisan live in its `assets/plugins/` (Level Tools): read it as an
example.

## plugin.json

```json
{
  "id": "my-plugin",
  "name": "My Plugin",
  "version": "1.0.0",
  "author": "You",
  "description": "What it adds, in a sentence.",
  "icon": "star",
  "permissions": ["edit"]
}
```

`icon` (optional) is its picture on the Plugins page, by name (the list is below).
`permissions` lists what the plugin may do beyond reading the scene. There is one:
`edit`, to change it (`scene.add`, `scene.set`, `scene.remove`). The Plugins page shows
them on the plugin's card.

## main.lua

It runs once when the plugin loads, in a Lua 5.4 state of its own: the `math`, `string`,
`table` and `utf8` libraries, no `io`, `os` or `require`. It describes what it adds:

```lua
plugin.object {
    name = "Coin", category = "Arcade", icon = "star",
    tip = "A golden coin",
    make = function(o)            -- o.name is set; fill the rest
        o.shape = { kind = "circle", size = { 30, 30 }, color = "#FFD34D", glow = 16 }
    end,
}

plugin.command {
    name = "Scatter Coins", icon = "star",
    run = function()
        local x, y = scene.camera()
        scene.add { name = "Coin", position = { x, y }, shape = { kind = "circle" } }
    end,
}
```

An object is written as a project file writes it: `name`, `position`, `rotation`, `scale`,
`layer`, `visible`, `shape` (`kind`: rectangle, rounded_rectangle, circle, capsule; `size`,
`color` as `"#RRGGBB"`, `corner_radius`, `outline`, `outline_color`, `glow`, `lit`), `sprite`,
`light` (`color`, `radius`, `intensity`), `script` (a file in the project) and `graph` (nodes
and links, as in `project.artisan`).

Icons by name: `star`, `circle`, `rectangle`, `person`, `bulb`, `cube`, `image`, `zigzag`,
`ripple`, `marker`, `magnet`, `align_left`, `fit`, `plugin`, `code`, `wave`, `bounce`.

### While a command runs

| Function | |
| --- | --- |
| `scene.objects()` | every object, as tables (with `id`) |
| `scene.get(id)` | one object, or nil |
| `scene.add(object)` | adds it (at the camera when it has no `position`), returns its id — `edit` |
| `scene.set(id, fields)` | changes the fields given, keeps the rest — `edit` |
| `scene.remove(id)` | `edit` |
| `scene.selected()` / `scene.select(id)` | the selected object's id (nil: the scene) |
| `scene.camera()` | where the game's camera looks: x, y |
| `scene.name()` | the scene's name |
| `log(...)` | a line in the plugin's log (Settings › Plugins) |

A command is one undo step, and nothing is kept if it fails: its error goes to the log and the
Console.
