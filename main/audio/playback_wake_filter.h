#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

// Configured once before recognition starts. Only the suppression state crosses
// between the application task and the recognition task.
class PlaybackWakeFilter {
public:
    void Configure(int primary_command_id, const std::string& display_name) {
        primary_command_id_ = primary_command_id;
        display_name_ = display_name;
        Clear();
    }

    void SetPlaybackText(std::string_view text, uint64_t now_ms) {
        if (!display_name_.empty() && text.find(display_name_) != std::string_view::npos) {
            suppressed_until_ms_.store(std::numeric_limits<uint64_t>::max());
        } else if (suppressed_until_ms_.load() == std::numeric_limits<uint64_t>::max()) {
            // MultiNet may recognize the last speaker samples after playback drains.
            suppressed_until_ms_.store(now_ms + 600);
        }
    }

    void Clear() { suppressed_until_ms_.store(0); }

    bool Suppresses(int command_id, uint64_t now_ms) const {
        return command_id > 0 && command_id == primary_command_id_ &&
               now_ms < suppressed_until_ms_.load();
    }

private:
    int primary_command_id_ = 0;
    std::string display_name_;
    std::atomic<uint64_t> suppressed_until_ms_{0};
};
