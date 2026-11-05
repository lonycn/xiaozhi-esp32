#pragma once

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace audio_diagnostics {
struct Levels {
    uint64_t samples = 0;
    uint64_t squares = 0;
    uint64_t zeros = 0;
    uint64_t clipped = 0;
    int peak = 0;

    void Add(const int16_t* data, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            const int32_t value = data[i];
            const int magnitude = value < 0 ? -value : value;
            if (magnitude > peak)
                peak = magnitude;
            squares += static_cast<int64_t>(value) * value;
            zeros += value == 0;
            clipped += value == INT16_MIN || value == INT16_MAX;
        }
        samples += size;
    }
    double Rms() const { return samples ? std::sqrt(static_cast<double>(squares) / samples) : 0; }
};
}  // namespace audio_diagnostics

#if CONFIG_XIAOZHI_AUDIO_DIAGNOSTICS
#include <esp_log.h>
#include <esp_timer.h>

namespace audio_diagnostics {
// Each instance belongs to one audio task; windows are discarded after logging.
struct Window {
    Levels levels;
    uint32_t success = 0;
    uint32_t failed = 0;
    uint32_t timeouts = 0;
    uint32_t results = 0;
    int64_t last_log = 0;

    void Record(const char* stage, const int16_t* data, size_t size, bool ok, unsigned flags = 0,
                bool timeout = false, unsigned result_count = 0) {
        if (ok) {
            ++success;
            levels.Add(data, size);
        } else {
            ++failed;
        }
        timeouts += timeout;
        results += result_count;
        const int64_t now = esp_timer_get_time();
        if (now - last_log < 1000000)
            return;
        ESP_LOGI("AudioDiagnostics",
                 "%s ok=%lu failed=%lu samples=%llu peak=%d rms=%.1f zero=%llu clipped=%llu "
                 "flags=0x%x timeouts=%lu results=%lu",
                 stage, static_cast<unsigned long>(success), static_cast<unsigned long>(failed),
                 static_cast<unsigned long long>(levels.samples), levels.peak, levels.Rms(),
                 static_cast<unsigned long long>(levels.zeros),
                 static_cast<unsigned long long>(levels.clipped), flags,
                 static_cast<unsigned long>(timeouts), static_cast<unsigned long>(results));
        *this = Window{};
        last_log = now;
    }
};
}  // namespace audio_diagnostics
#endif
