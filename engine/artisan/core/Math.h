#pragma once

// The few math types the engine uses: 2D vectors and linear RGBA colors. World units are pixels,
// y points down (like the screen and Vulkan's clip space), angles are in degrees at the API and
// radians inside.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace artisan {

constexpr float kPi = 3.14159265358979f;
constexpr float radians(float degrees) { return degrees * (kPi / 180.0f); }

struct Vec2 {
    float x = 0.0f, y = 0.0f;
    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator*(Vec2 o) const { return {x * o.x, y * o.y}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(Vec2 o) { return x += o.x, y += o.y, *this; }
    Vec2& operator-=(Vec2 o) { return x -= o.x, y -= o.y, *this; }
    constexpr bool operator==(const Vec2&) const = default;
    float length() const { return std::sqrt(x * x + y * y); }
    Vec2 rotated(float rad) const {
        const float c = std::cos(rad), s = std::sin(rad);
        return {x * c - y * s, x * s + y * c};
    }
};

struct Color {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    constexpr bool operator==(const Color&) const = default;
    static constexpr Color rgb(uint32_t hex, float alpha = 1.0f) {
        return {((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f, alpha};
    }
};

} // namespace artisan
