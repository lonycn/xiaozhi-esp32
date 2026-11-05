import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class OledFaceTest(unittest.TestCase):
    def test_conversation_hints_animation_lifecycle_and_pixel_bounds(self):
        source = r'''
#include "main/display/oled_face.h"
#include <cassert>
#include <set>
using namespace oled_face;
int main() {
    assert(FromText("今天不开心") == Mood::Concerned);
    assert(FromText("别担心，我会帮你的") == Mood::Concerned);
    assert(FromText("谢谢，太棒了！") == Mood::Happy);
    assert(FromText("笑死，我正在用300%音量大笑！") == Mood::Happy);
    assert(FromText("给你一个微笑") == Mood::Happy);
    assert(FromText("让我想想，分析一下") == Mood::Thinking);
    assert(FromText("你是说那个吗？") == Mood::Curious);
    assert(FromText("哇，没想到！") == Mood::Surprised);
    assert(FromText("嘿嘿，开个玩笑") == Mood::Winking);
    assert(FromText("晚安，睡个好觉") == Mood::Sleepy);
    assert(FromText("现在是十二点") == Mood::Calm);
    assert(FromEmotion("sad") == Mood::Concerned);
    assert(FromEmotion("confused") == Mood::Curious);
    assert(FromEmotion("unknown") == Mood::Calm);

    Animator face;
    face.SetMode(Mode::Idle, 0);
    std::set<Pixels> frames;
    for (uint32_t t = 0; t < 24000; t += 80) {
        const auto p = face.At(t);
        assert(p.gaze >= -1 && p.gaze <= 1);
        assert(p.lift >= -1 && p.lift <= 1);
        assert(p.tilt >= -1 && p.tilt <= 1);
        frames.insert(Render(p));
    }
    assert(frames.size() >= 8);
    face.SetMode(Mode::Listening, 24000);
    assert(face.At(24160).mood == Mood::Curious);
    face.SetMode(Mode::Thinking, 25000);
    assert(face.At(25000).mood == Mood::Thinking);
    assert(face.At(26600).gaze != face.At(25000).gaze);
    face.SetEmotion("sad", 26700);
    face.SetMode(Mode::Speaking, 27000);
    face.SetReply("我会陪着你", 27000);
    assert(face.At(27000).mood == Mood::Concerned); // retain server hint for plain text
    face.SetEmotion("happy", 27001); // provider default must not overwrite concern
    assert(face.At(27001).mood == Mood::Concerned);
    face.SetReply("谢谢，太棒了！", 28000);
    assert(face.At(28000).mood == Mood::Happy);
    assert(Render(face.At(28000)) != Render(face.At(28320)));
    face.SetReply("嘿嘿", 29000);
    assert(face.At(29160).mood == Mood::Winking);
    assert(face.At(29640).mood == Mood::Calm);
    face.SetMode(Mode::Listening, 30000);
    assert(face.mood() == Mood::Calm); // no prior reply emotion leaks into next turn
    assert(face.At(30000).mouth_open == 0);

    // The face stays inside its own 32 px tile, preserving the clock and subtitles.
    std::set<Pixels> moods;
    for (int m = 0; m < 8; ++m) {
        Pose p;
        p.mood = static_cast<Mood>(m);
        moods.insert(Render(p));
        for (int gaze = -1; gaze <= 1; ++gaze)
            for (int lift = -1; lift <= 1; ++lift)
                for (int tilt = -1; tilt <= 1; ++tilt)
                    for (int opening = 0; opening <= 2; ++opening) {
                        p.gaze = gaze; p.lift = lift; p.tilt = tilt; p.mouth_open = opening;
                        const auto pixels = Render(p);
                        for (int y = 0; y < 32; ++y)
                            for (int x = 0; x < 32; ++x) {
                                const bool bit = pixels[y * 4 + x / 8] & (0x80 >> (x % 8));
                                if (x == 0 || x >= 30 || y == 0 || y >= 30)
                                    assert(!bit);
                            }
                    }
    }
    assert(moods.size() == 8);
    face.SetMode(Mode::Idle, UINT32_MAX - 100);
    const auto before_wrap = face.At(UINT32_MAX - 100 + 80);
    const auto after_wrap = face.At(59);
    assert(before_wrap.mood == after_wrap.mood);
    assert(before_wrap.gaze == after_wrap.gaze);
}
'''
        compiler = shutil.which("c++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix="esp32-micro-face-") as directory:
            cpp = pathlib.Path(directory) / "test.cc"
            binary = pathlib.Path(directory) / "test"
            cpp.write_text(source)
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
