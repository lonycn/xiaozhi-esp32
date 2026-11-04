#pragma once

#include <array>
#include <cstdint>

namespace oled_idle {
constexpr int kWidth = 128;
constexpr int kHeight = 32;
using Pixels = std::array<uint8_t, kWidth * kHeight / 8>;

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
    Digit(pixels, 36, 7, valid ? hour / 10 : -1);
    Digit(pixels, 48, 7, valid ? hour % 10 : -1);
    Digit(pixels, 72, 7, valid ? minute / 10 : -1);
    Digit(pixels, 84, 7, valid ? minute % 10 : -1);
    if (colon_visible) {
        Rect(pixels, 64, 12, 2, 2);
        Rect(pixels, 64, 19, 2, 2);
    }
    return pixels;
}

inline void Wifi(Pixels& pixels, bool connected) {
    // Single-pixel strokes keep the small icon lighter than the font glyph.
    Rect(pixels, 116, 10, 7, 1);
    Rect(pixels, 114, 11, 2, 1);
    Rect(pixels, 123, 11, 2, 1);
    Rect(pixels, 113, 12, 1, 1);
    Rect(pixels, 125, 12, 1, 1);
    Rect(pixels, 117, 14, 5, 1);
    Rect(pixels, 115, 15, 2, 1);
    Rect(pixels, 122, 15, 2, 1);
    Rect(pixels, 118, 18, 3, 1);
    Rect(pixels, 119, 21, 1, 1);
    if (!connected) {
        for (int offset = 0; offset < 13; ++offset)
            Rect(pixels, 113 + offset, 9 + offset, 1, 1);
    }
}
}  // namespace oled_idle
