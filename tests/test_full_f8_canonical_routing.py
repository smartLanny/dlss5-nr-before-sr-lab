"""Check that the real canonical call chain executes every full-Core F8 mode.

This checks production routing only, not Windows/NGX/game execution. Expected
negative modes are extracted from the validator's actual CLI declaration.
"""
import ast
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FullF8CanonicalRouting(unittest.TestCase):
    def test_all_validator_negative_modes_are_mandatory_after_the_core_build(self):
        script = (ROOT / 'scripts/build-unified-candidate.ps1').read_text(encoding='utf-8-sig')
        validator = ast.parse((ROOT / 'scripts/validate-feedback-timing-full.py').read_text())
        declarations = [node for node in ast.walk(validator)
                        if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)
                        and node.func.attr == 'add_argument' and node.args
                        and isinstance(node.args[0], ast.Constant) and node.args[0].value == '--negative-control']
        self.assertEqual(len(declarations), 1)
        expected = next(ast.literal_eval(key.value) for key in declarations[0].keywords if key.arg == 'choices')
        loop = re.search(r'foreach \(\$Control in @\(([^\n]+)\)\) \{\n(.*?)\n\}', script, re.S)
        self.assertIsNotNone(loop)
        modes = re.findall(r"'([^']+)'", loop.group(1))
        self.assertEqual(len(modes), len(set(modes)))
        self.assertEqual(set(modes), set(expected))
        command = "(Join-Path $PSScriptRoot 'validate-feedback-timing-full.py')"
        calls = [line for line in script.splitlines() if command in line]
        self.assertEqual(len(calls), 2)
        for call in calls:
            self.assertIn('--source-root $Core --source-manifest $F8Manifest', call)
            self.assertNotIn('--compile-only', call)
        self.assertNotIn('--negative-control', calls[0])
        self.assertIn('--negative-control $Control', calls[1])
        self.assertIn('if ($LASTEXITCODE) { throw', loop.group(2))
        self.assertLess(script.index("'build-perf12-candidate.ps1'"), script.index(command))
        self.assertLess(script.index(command), script.index('full_core_f8_writer=$true'))
        self.assertIn('full_core_f8_negative_receipts=$F8NegativeReceipts', script)

    def test_streamline_windows_fixture_uses_the_pinned_compiler(self):
        script = (ROOT / 'scripts/build-multi-api-candidate.ps1').read_text(encoding='utf-8-sig')
        self.assertLess(script.index('$env:CXX = $Cxx'), script.index("'tests/test_streamline_factory_lock.py'"))
        self.assertIn('Windows Streamline adapter/source contract failed.', script)
        self.assertIn("'tests/test_projection_memory_runtime.py'", script)
        self.assertIn('Windows projection, Streamline ABI/module pin or DXGI runtime contract failed.', script)
        fixture = (ROOT / 'tests/test_streamline_factory_lock.py').read_text()
        self.assertIn("os.environ.get('CXX', 'g++')", fixture)
        self.assertIn("if os.name == 'nt':", fixture)
        self.assertIn("flags += ['-static']", fixture)


if __name__ == '__main__':
    unittest.main()
