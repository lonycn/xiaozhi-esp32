#pragma once
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

// v1 audio has no per-packet identity. Only an accepted TTS start opens the gate;
// tagged servers additionally prevent a canceled turn from reopening it.
class PlaybackFence {
public:
    struct Identity { std::string turn; int64_t control; };
    Identity identity() const { std::lock_guard<std::mutex> lock(mutex_); return {turn_, control_}; }
    uint32_t epoch() const { std::lock_guard<std::mutex> lock(mutex_); return epoch_; }
    void Invalidate() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!turn_.empty()) { canceled_.push_back(turn_); if (canceled_.size() > 32) canceled_.pop_front(); }
        ++epoch_; active_ = false; expecting_ = false; turn_.clear();
    }
    void NewConnection() { std::lock_guard<std::mutex> lock(mutex_); ++epoch_; active_ = false; expecting_ = false; control_ = -1; turn_.clear(); canceled_.clear(); }
    void ExpectResponse() { std::lock_guard<std::mutex> lock(mutex_); expecting_ = true; }
    bool Start(uint32_t epoch, const std::string& turn, int64_t control = -1) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (epoch != epoch_ || !expecting_ || (control >= 0 && control <= control_)) return false;
        for (const auto& old : canceled_) if (!turn.empty() && turn == old) return false;
        turn_ = turn; control_ = control; active_ = true; return true;
    }
    bool Current(uint32_t epoch, const std::string& turn = {}, int64_t control = -1) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return epoch == epoch_ && active_ && (turn.empty() || turn == turn_) && (control < 0 || control == control_);
    }
    bool Stop(uint32_t epoch, const std::string& turn, int64_t control = -1) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (epoch != epoch_ || !active_ || (!turn.empty() && turn != turn_) || (control >= 0 && control != control_)) return false;
        active_ = false; expecting_ = false; return true;
    }
private:
    mutable std::mutex mutex_;
    uint32_t epoch_ = 0;
    bool active_ = false, expecting_ = false;
    int64_t control_ = -1;
    std::string turn_;
    std::deque<std::string> canceled_;
};
