#include "artisan/runtime/Input.h"

#include <array>

namespace artisan {

namespace {

constexpr std::array<std::string_view, static_cast<size_t>(Key::Count)> kNames = {
    "",  "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x",
    "y", "z", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "left", "right", "up", "down", "space", "enter", "escape",
    "shift", "ctrl", "alt", "tab", "backspace", "mouse_left", "mouse_right", "mouse_middle"};

} // namespace

Key keyFromName(std::string_view name) {
    for (size_t i = 1; i < kNames.size(); ++i)
        if (kNames[i] == name) return static_cast<Key>(i);
    if (name == "return") return Key::Enter;
    if (name == "esc") return Key::Escape;
    return Key::Unknown;
}

std::string_view keyName(Key key) { return kNames[static_cast<size_t>(key)]; }

} // namespace artisan
