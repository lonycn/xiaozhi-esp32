import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class OledIdleSceneTest(unittest.TestCase):
    def test_clock_bounds_blinking_colon_and_expression_cycle(self):
        source = r'''
#include "main/display/oled_idle_scene.h"
#include <cassert>
int main() {
    for (int hour = 0; hour < 24; ++hour) {
        for (int minute = 0; minute < 60; ++minute) {
            const auto on = oled_idle::Clock(hour, minute, true);
            const auto off = oled_idle::Clock(hour, minute, false);
            int changed = 0;
            for (int y = 0; y < 32; ++y) {
                for (int x = 0; x < 128; ++x) {
                    const int index = y * 16 + x / 8;
                    const auto mask = 0x80 >> (x % 8);
                    if (x < 36 || x >= 94 || y < 7 || y >= 25)
                        assert(!(on[index] & mask));
                    if ((on[index] ^ off[index]) & mask) {
                        assert(x >= 64 && x < 66);
                        assert((y >= 12 && y < 14) || (y >= 19 && y < 21));
                        ++changed;
                    }
                }
            }
            assert(changed == 8);
        }
    }
    assert(oled_idle::Clock(-1, 0, true) == oled_idle::Clock(24, 60, true));
    assert(oled_idle::ExpressionAt(0) == oled_idle::Expression::Happy);
    assert(oled_idle::ExpressionAt(3000) == oled_idle::Expression::Blink);
    assert(oled_idle::ExpressionAt(3200) == oled_idle::Expression::Happy);
    assert(oled_idle::ExpressionAt(3400) == oled_idle::Expression::Blink);
    assert(oled_idle::ExpressionAt(3600) == oled_idle::Expression::Happy);
    oled_idle::Pixels connected{};
    oled_idle::Pixels offline{};
    oled_idle::Wifi(connected, true);
    oled_idle::Wifi(offline, false);
    assert(connected != offline);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 128; ++x)
            if (x < 113 || x > 125 || y < 9 || y > 21)
                assert(!(offline[y * 16 + x / 8] & (0x80 >> (x % 8))));

}
'''
        compiler = shutil.which("c++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix="esp32-idle-display-") as directory:
            cpp = pathlib.Path(directory) / "test.cc"
            binary = pathlib.Path(directory) / "test"
            cpp.write_text(source)
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
