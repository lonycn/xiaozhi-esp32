#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <string_view>

// A small, allocation-free face for the 32 px monochrome panel. All movement is
// in the features; the circular silhouette stays still beside the clock/text.
namespace oled_face {
constexpr int kSize = 32;
using Pixels = std::array<uint8_t, kSize * kSize / 8>;
enum class Mood { Calm, Happy, Thinking, Curious, Concerned, Surprised, Winking, Sleepy };
enum class Mode { Idle, Listening, Thinking, Speaking, Disabled };

inline const char* Name(Mood mood) {
    constexpr const char* names[] = {"calm",      "happy",     "thinking", "curious",
                                     "concerned", "surprised", "winking",  "sleepy"};
    return names[static_cast<unsigned>(mood)];
}

inline bool Contains(std::string_view text, std::initializer_list<std::string_view> words) {
    for (auto word : words)
        if (text.find(word) != std::string_view::npos)
            return true;
    return false;
}

// Sentence hints, not semantic sentiment analysis. Concern/negation precedes joy.
inline Mood FromText(std::string_view text) {
    text = text.substr(0, 768);
    if (Contains(text, {"😔", "😢", "抱歉", "对不起", "不开心", "难过", "遗憾", "别担心"}))
        return Mood::Concerned;
    if (Contains(text, {"😴", "晚安", "睡个好觉", "早点休息"}))
        return Mood::Sleepy;
    if (Contains(text, {"🤔", "让我想想", "我想想", "思考", "分析一下", "查一下", "查询一下"}))
        return Mood::Thinking;
    if (Contains(text, {"😮", "😲", "哇", "没想到", "居然"}))
        return Mood::Surprised;
    if (Contains(text, {"😉", "嘿嘿", "开个玩笑"}))
        return Mood::Winking;
    if (Contains(text, {"😊", "😄", "😂", "😆", "开心", "哈哈", "笑死", "大笑", "微笑", "笑一笑",
                        "谢谢", "恭喜", "太棒了", "真棒"}))
        return Mood::Happy;
    if (Contains(text, {"好奇", "为什么", "你是说", "怎么会", "？", "?"}))
        return Mood::Curious;
    return Mood::Calm;
}

inline Mood FromEmotion(std::string_view emotion) {
    if (Contains(emotion, {"sad", "crying", "angry", "embarrassed"}))
        return Mood::Concerned;
    if (emotion == "thinking")
        return Mood::Thinking;
    if (emotion == "confused" || emotion == "curious")
        return Mood::Curious;
    if (emotion == "surprised" || emotion == "shocked")
        return Mood::Surprised;
    if (emotion == "winking" || emotion == "silly" || emotion == "funny")
        return Mood::Winking;
    if (emotion == "sleepy")
        return Mood::Sleepy;
    if (emotion == "happy" || emotion == "laughing" || emotion == "loving")
        return Mood::Happy;
    return Mood::Calm;
}

struct Pose {
    Mood mood = Mood::Calm;
    int gaze = 0;
    int lift = 0;
    int tilt = 0;
    bool blink = false;
    int mouth_open = 0;
};

class Animator {
public:
    void SetMode(Mode mode, uint32_t now) {
        if (mode_ == mode)
            return;
        mode_ = mode;
        started_ = now;
        if (mode != Mode::Speaking)
            mood_ = Mood::Calm;
    }
    void SetReply(std::string_view text, uint32_t now) {
        const auto hint = FromText(text);
        if (hint != Mood::Calm || mood_ == Mood::Calm)
            mood_ = hint;
        reply_started_ = now;
    }
    void SetEmotion(std::string_view emotion, uint32_t now) {
        // An uninformative neutral/default happy event must not erase a more
        // specific sentence hint. Explicit non-neutral server emotions still work.
        const auto mood = FromEmotion(emotion);
        if (mood != Mood::Calm && !(mood == Mood::Happy && mood_ != Mood::Calm)) {
            mood_ = mood;
            reply_started_ = now;
        }
    }
    Mood mood() const { return mood_; }
    Pose At(uint32_t now) const {
        const auto elapsed = now - started_;  // unsigned subtraction survives tick wrap
        Pose p;
        p.blink = elapsed % 5900 >= 3000 && elapsed % 5900 < 3160;
        if (mode_ == Mode::Idle) {
            const auto t = elapsed % 24000;
            if (t >= 5000 && t < 8000) {
                p.gaze = t < 6400 ? -1 : 1;
                p.tilt = t < 6400 ? -1 : 1;
            } else if (t >= 11000 && t < 12200) {
                p.lift = (t - 11000) / 400 == 1 ? 1 : 0;
            } else if (t >= 15000 && t < 17400) {
                p.mood = Mood::Thinking;
                p.gaze = 1;
                p.lift = -1;
            } else if (t >= 20000 && t < 21200) {
                p.mood = Mood::Happy;
            } else if (t >= 22000 && t < 22400) {
                p.mood = Mood::Winking;
            }
        } else if (mode_ == Mode::Thinking) {
            p.mood = Mood::Thinking;
            p.gaze = (elapsed / 1600) % 2 ? 1 : -1;
            p.lift = -1;
        } else if (mode_ == Mode::Listening) {
            p.mood = Mood::Curious;
            p.tilt = elapsed % 8000 < 1000 ? 1 : 0;
        } else if (mode_ == Mode::Speaking) {
            p.mood = mood_;
            const auto t = now - reply_started_;
            // A small stylized speaking cycle, not phoneme/audio lip sync.
            constexpr int openings[] = {0, 1, 2, 1, 0, 1, 0, 0};
            p.mouth_open = openings[(t / 160) % 8];
            if (mood_ == Mood::Happy && t < 1200)
                p.lift = t / 400 == 1 ? 1 : 0;
            if (mood_ == Mood::Thinking || mood_ == Mood::Curious) {
                p.gaze = (t / 1600) % 2 ? 1 : 0;
                p.tilt = 1;
            }
            // Winks and surprise settle instead of looping a large gesture.
            if ((mood_ == Mood::Winking && t >= 640) || (mood_ == Mood::Surprised && t >= 1600))
                p.mood = Mood::Calm;
        }
        return p;
    }

private:
    Mode mode_ = Mode::Disabled;
    Mood mood_ = Mood::Calm;
    uint32_t started_ = 0;
    uint32_t reply_started_ = 0;
};

inline void Dot(Pixels& pixels, int x, int y) {
    if (x >= 0 && x < kSize && y >= 0 && y < kSize)
        pixels[y * 4 + x / 8] |= uint8_t(0x80 >> (x % 8));
}
inline void Rect(Pixels& pixels, int x, int y, int w, int h) {
    for (int row = y; row < y + h; ++row)
        for (int col = x; col < x + w; ++col)
            Dot(pixels, col, row);
}
inline void Eye(Pixels& pixels, int x, int y, int h) {
    Rect(pixels, x + 1, y, 2, h);
    if (h > 2)
        Rect(pixels, x, y + 1, 4, h - 2);
}
inline Pixels Render(const Pose& p) {
    Pixels pixels{};
    // Midpoint circle: a stable 29 px outline with a clear gap to the clock.
    int x = 14, y = 0, error = 1 - x;
    while (x >= y) {
        for (int sign_x : {-1, 1})
            for (int sign_y : {-1, 1}) {
                Dot(pixels, 15 + sign_x * x, 15 + sign_y * y);
                Dot(pixels, 15 + sign_x * y, 15 + sign_y * x);
            }
        ++y;
        if (error < 0)
            error += 2 * y + 1;
        else {
            --x;
            error += 2 * (y - x) + 1;
        }
    }
    const int dx = p.gaze, dy = p.lift;
    for (int side = 0; side < 2; ++side) {
        const int ex = 8 + 10 * side + dx;
        const int ey = 10 + dy + (side == 0 ? -p.tilt : p.tilt);
        if (p.blink || (p.mood == Mood::Winking && side == 1)) {
            Rect(pixels, ex, ey + 3, 4, 1);
        } else if (p.mood == Mood::Happy) {
            Rect(pixels, ex + 1, ey + 1, 2, 1);
            Dot(pixels, ex, ey + 2);
            Dot(pixels, ex + 3, ey + 2);
        } else if (p.mood == Mood::Sleepy) {
            Rect(pixels, ex, ey + 3, 4, 1);
            Dot(pixels, ex + 3, ey + 2);
        } else {
            Eye(pixels, ex, ey, p.mood == Mood::Surprised ? 7 : 5);
        }
        if (p.mood == Mood::Concerned) {
            Dot(pixels, ex + (side ? 0 : 3), ey - 3);
            Rect(pixels, ex + (side ? 1 : 0), ey - 2, 3, 1);
        } else if (p.mood == Mood::Curious && side == 1) {
            Rect(pixels, ex, ey - 3, 4, 1);
        }
    }
    const int mx = 15 + dx, my = 21 + dy;
    if (p.mouth_open > 0 || p.mood == Mood::Surprised) {
        const int h = p.mood == Mood::Surprised ? 3 : p.mouth_open + 1;
        Rect(pixels, mx - 1, my, 3, h);
    } else if (p.mood == Mood::Thinking) {
        Rect(pixels, mx - 1, my + 1, 4, 1);
        Dot(pixels, mx + 3, my);
    } else if (p.mood == Mood::Concerned) {
        Rect(pixels, mx - 2, my, 5, 1);
        Dot(pixels, mx - 3, my + 1);
        Dot(pixels, mx + 3, my + 1);
    } else if (p.mood == Mood::Sleepy) {
        Rect(pixels, mx - 1, my, 3, 1);
    } else {
        Rect(pixels, mx - 2, my + 1, 5, 1);
        Dot(pixels, mx - 3, my);
        Dot(pixels, mx + 3, my);
        if (p.mood == Mood::Happy)
            Rect(pixels, mx - 1, my + 2, 3, 1);
    }
    return pixels;
}
}  // namespace oled_face
