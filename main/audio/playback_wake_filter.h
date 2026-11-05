#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Configured once before recognition starts. Only the suppression state crosses
// between the application task and the recognition task.
class PlaybackWakeFilter {
public:
    void Configure(int primary_command_id, const std::string& display_name) {
        Configure({{primary_command_id, display_name}});
    }

    void Configure(const std::vector<std::pair<int, std::string>>& phrases) {
        phrases_.clear();
        for (const auto& [id, text] : phrases) {
            phrases_.push_back(std::make_unique<Phrase>(id, text));
        }
    }

    void SetPlaybackText(std::string_view text, uint64_t now_ms) {
        for (const auto& phrase : phrases_) {
            if (!phrase->text.empty() && text.find(phrase->text) != std::string_view::npos) {
                phrase->suppressed_until_ms.store(std::numeric_limits<uint64_t>::max());
            } else if (phrase->suppressed_until_ms.load() == std::numeric_limits<uint64_t>::max()) {
                // MultiNet may recognize the last speaker samples after playback drains.
                phrase->suppressed_until_ms.store(now_ms + 600);
            }
        }
    }

    void Clear() {
        for (const auto& phrase : phrases_)
            phrase->suppressed_until_ms.store(0);
    }

    bool Suppresses(int command_id, uint64_t now_ms) const {
        for (const auto& phrase : phrases_) {
            if (command_id > 0 && command_id == phrase->id) {
                return now_ms < phrase->suppressed_until_ms.load();
            }
        }
        return false;
    }

private:
    struct Phrase {
        Phrase(int command_id, const std::string& display_text)
            : id(command_id), text(display_text) {}
        int id;
        std::string text;
        std::atomic<uint64_t> suppressed_until_ms{0};
    };
    std::vector<std::unique_ptr<Phrase>> phrases_;
};
