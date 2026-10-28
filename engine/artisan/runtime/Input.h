#pragma once

// Keyboard and mouse state as the game sees it. Platform layers (the SDL player) translate their
// events into Key values; scripts name keys by text ("left", "space", "a", "mouse_left").

#include "artisan/core/Math.h"

#include <bitset>
#include <string_view>

namespace artisan {

enum class Key : int {
    Unknown = 0,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Left, Right, Up, Down, Space, Enter, Escape, Shift, Ctrl, Alt, Tab, Backspace,
    MouseLeft, MouseRight, MouseMiddle,
    Count
};

Key keyFromName(std::string_view name);
std::string_view keyName(Key key);

class Input {
public:
    void set(Key k, bool down) {
        if (k != Key::Unknown) down_[static_cast<size_t>(k)] = down;
    }
    // Call once per frame, after the frame's update: "pressed" means since the last frame.
    void endFrame() { prev_ = down_; }
    void releaseAll() { down_.reset(); }

    bool down(Key k) const { return down_[static_cast<size_t>(k)]; }
    bool pressed(Key k) const { return down_[static_cast<size_t>(k)] && !prev_[static_cast<size_t>(k)]; }
    bool released(Key k) const { return !down_[static_cast<size_t>(k)] && prev_[static_cast<size_t>(k)]; }

    Vec2 mouse;      // in the window, pixels
    Vec2 mouseWorld; // in the scene (set by the game from the camera)

private:
    std::bitset<static_cast<size_t>(Key::Count)> down_, prev_;
};

} // namespace artisan
