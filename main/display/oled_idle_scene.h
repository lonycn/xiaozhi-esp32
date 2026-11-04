#pragma once

#include <array>
#include <cstdint>

namespace oled_idle {
constexpr int kWidth = 128;
constexpr int kHeight = 32;
using Pixels = std::array<uint8_t, kWidth * kHeight / 8>;

enum class Expression { Happy, Wink, Silly };

inline Expression ExpressionAt(uint32_t elapsed_ms) {
    const auto phase = (elapsed_ms / 200) % 60;
    if (phase >= 18 && phase < 20)
        return Expression::Wink;
    if (phase >= 36 && phase < 40)
        return Expression::Silly;
    return Expression::Happy;
}

inline int FaceOffsetAt(uint32_t elapsed_ms) {
    const auto phase = (elapsed_ms / 200) % 60;
    return phase >= 36 && phase < 38 ? -1 : 0;
}

inline void Rect(Pixels& pixels, int x, int y, int width, int height) {
    for (int row = y; row < y + height; ++row) {
        for (int col = x; col < x + width; ++col) {
            if (col >= 0 && col < kWidth && row >= 0 && row < kHeight) {
                pixels[row * kWidth / 8 + col / 8] |= uint8_t(0x80 >> (col % 8));
            }
        }
    }
}

inline void Digit(Pixels& pixels, int x, int y, int value) {
    constexpr uint8_t segments[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
    const uint8_t mask = value >= 0 && value <= 9 ? segments[value] : 0x40;
    if (mask & 0x01)
        Rect(pixels, x + 2, y, 6, 2);
    if (mask & 0x02)
        Rect(pixels, x + 8, y + 2, 2, 6);
    if (mask & 0x04)
        Rect(pixels, x + 8, y + 10, 2, 6);
    if (mask & 0x08)
        Rect(pixels, x + 2, y + 16, 6, 2);
    if (mask & 0x10)
        Rect(pixels, x, y + 10, 2, 6);
    if (mask & 0x20)
        Rect(pixels, x, y + 2, 2, 6);
    if (mask & 0x40)
        Rect(pixels, x + 2, y + 8, 6, 2);
}

inline Pixels Clock(int hour, int minute, bool colon_visible) {
    Pixels pixels{};
    const bool valid = hour >= 0 && hour < 24 && minute >= 0 && minute < 60;
    Digit(pixels, 66, 13, valid ? hour / 10 : -1);
    Digit(pixels, 78, 13, valid ? hour % 10 : -1);
    Digit(pixels, 102, 13, valid ? minute / 10 : -1);
    Digit(pixels, 114, 13, valid ? minute % 10 : -1);
    if (colon_visible) {
        Rect(pixels, 94, 18, 2, 2);
        Rect(pixels, 94, 25, 2, 2);
    }
    return pixels;
}
}  // namespace oled_idle
