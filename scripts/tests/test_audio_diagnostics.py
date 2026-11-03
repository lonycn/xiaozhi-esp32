"""Compile the allocation-free level accumulator against signed PCM boundaries."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class AudioDiagnosticsTest(unittest.TestCase):
    def test_pcm_boundaries_and_accumulation(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        audio = Path(__file__).resolve().parents[2] / 'main' / 'audio'
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'test.cc'
            source.write_text('''
#include "audio_diagnostics.h"
#include <cassert>
int main() {
    audio_diagnostics::Levels levels;
    assert(levels.Rms() == 0);
    const int16_t pcm[] = {INT16_MIN, INT16_MAX, 0, -1};
    levels.Add(pcm, 4);
    assert(levels.samples == 4 && levels.peak == 32768);
    assert(levels.zeros == 1 && levels.clipped == 2);
    assert(levels.squares == 2147418114ULL);
    assert(levels.Rms() > 23169 && levels.Rms() < 23171);
    for (int i = 0; i < 16000; ++i) levels.Add(pcm, 4);
    assert(levels.squares == 2147418114ULL * 16001);
}
''')
            binary = Path(directory) / 'test'
            subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(audio), str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
