"""Expose the production network CPU gate through the canonical portable runner."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NetworkSelection(unittest.TestCase):
    def test_network_admission_create_callers_and_retirement(self):
        compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
        if not compiler:
            self.skipTest("requires a GNU-compatible C++20 host compiler; no Windows/NGX execution is implied")
        with tempfile.TemporaryDirectory(prefix="nr-network-portable-") as temporary:
            result = subprocess.run([
                sys.executable, str(ROOT / "scripts/validate-nr-network.py"),
                "--compiler", compiler, "--output", str(Path(temporary) / "receipts"),
            ], cwd=ROOT, capture_output=True, text=True, timeout=180)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
