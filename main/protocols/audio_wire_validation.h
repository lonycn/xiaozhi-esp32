#pragma once
#include <cstddef>
#include <cstdint>
namespace audio_wire {
constexpr size_t kMaxOpusBytes = 8192;
inline uint16_t U16(const uint8_t* p) { return (uint16_t(p[0]) << 8) | p[1]; }
inline uint32_t U32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
struct View { const uint8_t* data; size_t size; uint32_t timestamp; };
inline bool Parse(const void* raw, size_t length, int version, View& out) {
    if (!raw) return false;
    auto p = static_cast<const uint8_t*>(raw); size_t header = 0, size = length; uint32_t ts = 0;
    if (version == 2) {
        header = 16;
        if (length < header || U16(p) != 2 || U16(p + 2) != 0 || U32(p + 4) != 0) return false;
        size = U32(p + 12); ts = U32(p + 8);
    } else if (version == 3) {
        header = 4;
        if (length < header || p[0] != 0 || p[1] != 0) return false;
        size = U16(p + 2);
    } else if (version != 1) return false;
    if (!size || size > kMaxOpusBytes || length - header != size) return false;
    out = {p + header, size, ts}; return true;
}
inline bool Parameters(int rate, int duration) {
    return (rate == 8000 || rate == 12000 || rate == 16000 || rate == 24000 || rate == 48000) &&
           (duration == 5 || duration == 10 || duration == 20 || duration == 40 || duration == 60 || duration == 80 || duration == 100 || duration == 120);
}
}
