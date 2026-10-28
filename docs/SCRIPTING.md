# Scripting

Every object can have a **Lua script** (a file in `scripts/`) and/or a **node graph**.
Both run the same way: the graph compiles to Lua that defines the same functions a
script does. Each object's behavior runs in an environment of its own, so names never
clash between objects, and two objects with the same script each keep their own state.

## Lifecycle

```lua
function start(self)        -- once, when the game starts
end

function update(self, dt)   -- every frame; dt is the frame's duration in seconds
end
```

## The object: `self`

| | |
| --- | --- |
| `self.x`, `self.y` | position (pixels, y down) |
| `self.rotation` | degrees, clockwise |
| `self.scale_x`, `self.scale_y` | scale (negative mirrors) |
| `self.visible` | shown or not |
| `self.layer` | drawing order: higher on top |
| `self.name`, `self.id` | its name, its id |
| `self:set_color(r, g, b, a)` | the shape's color or the sprite's tint (0 to 1) |
| `self:find(name)` | another object by name, or `nil` |
| `self:destroy()` | removes the object at the end of the frame |
| `vars` | a table of the object's own variables (the nodes' variables live there too) |

## The game

| | |
| --- | --- |
| `input.down(key)` | the key is held |
| `input.pressed(key)` / `input.released(key)` | since the last frame |
| `input.mouse_x`, `input.mouse_y` | the mouse in the scene |
| `time.dt`, `time.now` | the frame's duration, seconds since the start |
| `camera.x`, `camera.y`, `camera.zoom` | the view |
| `scene.find(name)` | an object by name |
| `log(...)`, `print(...)` | to the editor's Console (and the player's output) |

Keys: `"a"`…`"z"`, `"0"`…`"9"`, `"left"`, `"right"`, `"up"`, `"down"`, `"space"`,
`"enter"`, `"escape"`, `"shift"`, `"ctrl"`, `"alt"`, `"tab"`, `"backspace"`,
`"mouse_left"`, `"mouse_right"`, `"mouse_middle"`.

Scripts get Lua's `math`, `string`, `table`, `utf8` and `coroutine` libraries; not `io`,
`os`, `load`, `require`, `dofile`: a game's code cannot touch files or run programs. An
error stops that object's behavior (once, with a message in the Console); the rest of the
game goes on.

## Nodes

Execution wires (white arrows) say in what order things happen; data wires (colored by
type: green numbers, red booleans, pink text, grey anything) carry values. A chain starts
at an event.

| Category | Nodes |
| --- | --- |
| Events | On Start, On Update (gives `dt`), On Key Pressed, On Key Released, While Key Down |
| Flow | Branch, Sequence |
| Actions | Move By, Set Position, Rotate By, Set Rotation, Set Scale, Set Visible, Set Color, Camera Follow, Print |
| Object | Get Position, Get Rotation |
| Input | Is Key Down, Input Axis |
| Time | Delta Time, Time |
| Math | Add, Subtract, Multiply, Divide, Sine, Random |
| Logic | Greater, Less, Equal, And, Or, Not |
| Values | Number, Text |
| Variables | Set Variable, Get Variable |

In the Nodes tab: right-click or double-click empty space to add a node; drag from a pin
to wire it (drop on empty space to add a node already wired); Alt+click a pin to cut its
wires; click a value to type it; Delete removes the selected node; F frames all.
The ƒ button shows the Lua the graph becomes.

A new node is one entry in `engine/artisan/nodes/NodeLibrary.cpp`: its pins and the Lua
it emits. Values typed into pins are always emitted as literals of the pin's type, never
as code.
