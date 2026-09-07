"""Compile actual RR production includes against portable API test doubles.

This is a control-flow/F8 regression, not a Windows add-on build or GPU test.
Run: python -m unittest discover -s tests/multi_api -p test_rr_post_diagnostics.py -v
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class RrPostDiagnosticsTest(unittest.TestCase):
    def test_production_boundaries_and_f8(self):
        # CXX is an executable name/path, without embedded flags.
        requested = os.environ.get("CXX")
        compiler = shutil.which(requested) if requested else (
            shutil.which("clang++") or shutil.which("g++")
        )
        if not compiler:
            self.skipTest("clang++/g++ unavailable; portable RR test was not run")
        with tempfile.TemporaryDirectory(prefix="nr-rr-diagnostic-") as directory:
            exe = Path(directory) / ("rr-test.exe" if os.name == "nt" else "rr-test")
            source = ROOT / "tests/multi_api/rr_post_diagnostics_harness.cpp"
            result = subprocess.run(
                [compiler, "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
                 str(source), "-o", str(exe)],
                capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run(
                [str(exe)], capture_output=True, text=True, timeout=10,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("PASS 50", result.stdout)


if __name__ == "__main__":
    unittest.main()
