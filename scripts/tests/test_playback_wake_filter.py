import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class PlaybackWakeFilterTest(unittest.TestCase):
    def test_spoken_name_filters_only_the_primary_wake_command(self):
        source = r'''
#include "main/audio/playback_wake_filter.h"
#include <cassert>
#include <thread>
int main() {
    PlaybackWakeFilter filter;
    assert(!filter.Suppresses(0, 1000));
    filter.Configure(1, "大冷总管");
    assert(!filter.Suppresses(1, 1000));
    filter.SetPlaybackText("我是大冷总管，00后机车女孩。", 1000);
    assert(filter.Suppresses(1, 1000));
    assert(!filter.Suppresses(2, 1000));  // 你好总管
    assert(!filter.Suppresses(3, 1000));  // 总管总管
    assert(!filter.Suppresses(4, 1000));  // 停止口令
    assert(!filter.Suppresses(5, 1000));  // 退出口令
    filter.SetPlaybackText("今天是星期三。", 1000);
    assert(filter.Suppresses(1, 1599));
    assert(!filter.Suppresses(1, 1600));
    filter.SetPlaybackText("", 1500);
    assert(!filter.Suppresses(1, 1600));
    filter.SetPlaybackText("大冷总管", 1000);
    std::thread recognition([&] {
        for (int i = 0; i < 1000; ++i) (void)filter.Suppresses(1, 1000);
    });
    filter.Clear();
    recognition.join();
    assert(!filter.Suppresses(1, 1000));
    filter.Configure(7, "");
    filter.SetPlaybackText("任意文本", 1000);
    assert(!filter.Suppresses(7, 1000));
}
'''
        compiler = shutil.which("c++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix="esp32-playback-wake-") as directory:
            cpp = pathlib.Path(directory) / "test.cc"
            binary = pathlib.Path(directory) / "test"
            cpp.write_text(source)
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
                            "-I", str(ROOT), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
