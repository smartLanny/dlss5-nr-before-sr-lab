"""Executable production F8 memory/retirement integration, not a source scan."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class MemoryF8Combined(unittest.TestCase):
    def test_actual_writers_collector_and_exact_controls(self):
        if os.name == 'nt':
            self.skipTest('Windows canonical gate executes the complete Core F8 TU')
        compiler = os.environ.get('CXX') or shutil.which('g++') or shutil.which('clang++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        with tempfile.TemporaryDirectory(prefix='nr-memory-f8-') as directory:
            result = subprocess.run([sys.executable, str(ROOT / 'scripts/validate-memory-f8-combined.py'),
                '--compiler', compiler, '--output', str(Path(directory) / 'evidence')],
                cwd=ROOT, capture_output=True, text=True, timeout=480)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout, end='')


if __name__ == '__main__':
    unittest.main(verbosity=2)
