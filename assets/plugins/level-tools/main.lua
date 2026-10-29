-- Level Tools: commands in the Plugins menu. They change the scene through `scene` (the "edit"
-- permission in plugin.json allows it); each command is one undo step.

plugin.command {
    name = "Scatter Coins", icon = "star",
    tip = "Eight coins in a ring around the camera",
    run = function()
        local cx, cy = scene.camera()
        for i = 1, 8 do
            local a = (i - 1) / 8 * math.pi * 2
            scene.add {
                name = "Coin " .. i,
                position = { cx + math.cos(a) * 220, cy + math.sin(a) * 220 },
                shape = { kind = "circle", size = { 30, 30 }, color = "#FFD34D", glow = 16 },
            }
        end
        log("Added 8 coins")
    end,
}

plugin.command {
    name = "Line Up Objects", icon = "align_left",
    tip = "Every object in a row, left to right, in the hierarchy's order",
    run = function()
        local cx, cy = scene.camera()
        local objects = scene.objects()
        local x = cx - (#objects - 1) * 60
        for _, o in ipairs(objects) do
            scene.set(o.id, { position = { x, cy } })
            x = x + 120
        end
    end,
}

plugin.command {
    name = "Snap All to Grid", icon = "magnet",
    tip = "Every object's position rounded to 32 pixels",
    run = function()
        for _, o in ipairs(scene.objects()) do
            local p = o.position
            scene.set(o.id, { position = { math.floor(p[1] / 32 + 0.5) * 32, math.floor(p[2] / 32 + 0.5) * 32 } })
        end
    end,
}

plugin.command {
    name = "Center the Selected Object", icon = "fit",
    tip = "Moves the selected object to where the camera looks",
    run = function()
        local id = scene.selected()
        if not id then return log("Nothing is selected") end
        local cx, cy = scene.camera()
        scene.set(id, { position = { cx, cy } })
    end,
}
