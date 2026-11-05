"""Portable execution of production memory observation with controlled services."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class MemoryObservation(unittest.TestCase):
    def test_context_walk_and_trend_mutations(self):
        compiler = os.environ.get('CXX') or shutil.which('g++') or shutil.which('clang++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        if os.name == 'nt':
            self.skipTest('Windows canonical gate runs validate-memory-observation.py with the pinned LLVM toolchain')
        with tempfile.TemporaryDirectory(prefix='nr-memory-observation-') as directory:
            result = subprocess.run([sys.executable, str(ROOT / 'scripts/validate-memory-observation.py'),
                '--compiler', compiler, '--output', str(Path(directory) / 'evidence')],
                cwd=ROOT, capture_output=True, text=True, timeout=300)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout, end='')


if __name__ == '__main__':
    unittest.main(verbosity=2)
