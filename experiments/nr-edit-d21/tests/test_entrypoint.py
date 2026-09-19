"""Regression for the real upgrade-module/launcher import interface.
This does not execute Git mutation, product staging, Windows compilation or GPU work.
"""
from pathlib import Path
import importlib.util
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]

class EntrypointTest(unittest.TestCase):
    def test_upgrade_exports_exact_source_refs(self):
        path = ROOT / "tools" / "upgrade_v4.py"
        spec = importlib.util.spec_from_file_location("d21_upgrade_entrypoint_check", path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.assertEqual(module.BASE, "04eda2e4601ac6c57a2be85149d9c817d8dbf8a0")
        self.assertEqual(module.D21, "cbfb33919696a2840ed5154c91abaea56228b661")
        self.assertTrue(callable(module.upgrade))

    def test_launcher_help_reaches_argument_parser(self):
        # A fresh interpreter catches missing exports that py_compile misses.
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools" / "prepare_d21.py"), "--help"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, encoding="utf-8", errors="replace", timeout=20,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--core", result.stdout)
        self.assertIn("--prepare-only", result.stdout)
        self.assertNotIn("Traceback", result.stderr)

if __name__ == "__main__":
    unittest.main()
