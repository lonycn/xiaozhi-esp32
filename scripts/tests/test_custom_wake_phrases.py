import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class CustomWakePhrasesTest(unittest.TestCase):
    def test_fallback_labels_validation_and_legacy_deduplication(self):
        source = r'''
#include "main/audio/wake_words/custom_wake_phrases.h"
#include <cassert>
int main() {
    using namespace custom_wake_phrases;
    std::vector<Phrase> phrases;
    assert(Parse("ni hao zong guan", "你好总管",
                 "zong guan zong guan;ni hao xiao yun;xiao yun xiao yun",
                 "总管总管;你好小云;小云小云", phrases));
    assert(phrases.size() == 4);
    assert(phrases[0].command == "ni hao zong guan" && phrases[0].text == "你好总管");
    assert(phrases[1].command == "zong guan zong guan" && phrases[1].text == "总管总管");
    assert(phrases[2].command == "ni hao xiao yun" && phrases[2].text == "你好小云");
    assert(phrases[3].command == "xiao yun xiao yun" && phrases[3].text == "小云小云");
    for (const auto& phrase : phrases) assert(phrase.command != "da leng zong guan");
    assert(!Parse("primary", "主词", "one;two", "只有一个", phrases));
    assert(!Parse("primary", "主词", "one;two", "一;", phrases));
    assert(!Parse("primary", "主词", "one;one", "一;一", phrases));
    assert(!Parse("primary", "主词", "primary", "重复", phrases));
    assert(!Parse("", "主词", "", "", phrases));
    assert(phrases.size() == 4);  // invalid input never installs a partial set
    assert(Parse(" primary ", "主词", "one; ;primary; one ; two", "", phrases));
    assert(phrases.size() == 3 && phrases[1].text == "主词");
    assert(phrases[2].command == "two");
}
'''
        compiler = shutil.which("c++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "test.cc"
            binary = Path(directory) / "test"
            cpp.write_text(source)
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
