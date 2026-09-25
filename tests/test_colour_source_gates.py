"""Exercise the real opt-in source test methods without a Windows SDK.

Compile each production test method AST, including its original decorators,
with the CLI paths initially None, then set the path after class definition.
A probe path must be read when selected. This detects import-time skip wrappers
that remain skipped even after __unittest_skip__ is changed. It is a test of
source-validation routing, not a generated Core or GPU validation.
"""
from __future__ import annotations

import argparse
import ast
from pathlib import Path
import unittest
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]


class SourceReadProbe:
    def __truediv__(self, component):
        raise RuntimeError('selected-source-was-read:' + str(component))


def run_method(relative_path: str, class_name: str, method_name: str,
               path_name: str, selected: bool) -> unittest.TestResult:
    path = ROOT / relative_path
    tree = ast.parse(path.read_text(encoding='utf-8'), filename=str(path))
    owner = next(node for node in tree.body
                 if isinstance(node, ast.ClassDef) and node.name == class_name)
    method = next(node for node in owner.body
                  if isinstance(node, ast.FunctionDef) and node.name == method_name)
    # Preserve decorators: removing them would conceal the original defect.
    isolated = ast.Module(body=[ast.ClassDef(
        name='SelectedSourceTest',
        bases=[ast.Attribute(value=ast.Name(id='unittest', ctx=ast.Load()),
                             attr='TestCase', ctx=ast.Load())],
        keywords=[], body=[method], decorator_list=[])], type_ignores=[])
    def unexpected_transform(*args, **kwargs):
        raise RuntimeError('transform-ran-before-source-read')

    namespace = {'unittest': unittest, 'REPO': None, 'GENERATED': None,
                 'patch': SimpleNamespace(patch_runtime=unexpected_transform,
                                          patch_policy=unexpected_transform,
                                          patch_settings_ui=unexpected_transform,
                                          patch_ui=unexpected_transform)}
    exec(compile(ast.fix_missing_locations(isolated), str(path), 'exec'), namespace)
    namespace[path_name] = SourceReadProbe() if selected else None
    cls = namespace['SelectedSourceTest']
    # Reproduce the old CLI's attempted re-enable; the fixed methods have no
    # decorator wrapper and are unaffected by this compatibility simulation.
    getattr(cls, method_name).__unittest_skip__ = False
    result = unittest.TestResult()
    cls(method_name).run(result)
    return result


class SourceGateTests(unittest.TestCase):
    CASES = (
        ('tests/test_colour_lab.py', 'PatchTests', 'test_actual_sources', 'REPO'),
        ('tests/test_colour_restore.py', 'RestoreTests', 'test_actual_raw_source', 'REPO'),
        ('tests/test_colour_restore.py', 'RestoreTests', 'test_actual_generated_source', 'GENERATED'),
    )

    def test_no_path_is_an_explicit_skip(self):
        for case in self.CASES:
            with self.subTest(method=case[2]):
                result = run_method(*case, selected=False)
                self.assertEqual(result.testsRun, 1)
                self.assertEqual(len(result.skipped), 1)
                self.assertFalse(result.errors)
                self.assertFalse(result.failures)

    def test_selected_path_reaches_the_real_test_body(self):
        for case in self.CASES:
            with self.subTest(method=case[2]):
                result = run_method(*case, selected=True)
                self.assertEqual(result.testsRun, 1)
                self.assertFalse(result.skipped, 'a selected source test was silently skipped')
                self.assertFalse(result.failures)
                self.assertEqual(len(result.errors), 1)
                self.assertIn('selected-source-was-read:', result.errors[0][1])

    def test_generated_scaled_encoder_contract_is_consistent(self):
        text = (ROOT / 'tests/test_colour_restore.py').read_text(encoding='utf-8')
        tree = ast.parse(text)
        owner = next(n for n in tree.body
                     if isinstance(n, ast.ClassDef) and n.name == 'RestoreTests')
        method = next(n for n in owner.body
                      if isinstance(n, ast.FunctionDef) and n.name == 'test_actual_generated_source')
        assertions = []
        for node in ast.walk(method):
            if (isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)
                    and node.func.attr in ('assertIn', 'assertNotIn') and len(node.args) == 2):
                assertions.append((node.func.attr, ast.unparse(node.args[0]),
                                   ast.unparse(node.args[1])))
        target = "body(text, 'void EncodeV2CS(')"
        self.assertIn(('assertIn', "'ColourLabFilterPreparedProxyExactArea'", target), assertions)
        self.assertNotIn(('assertNotIn', "'ColourLab'", target), assertions)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=ROOT)
    args, rest = parser.parse_known_args()
    ROOT = args.source_root
    unittest.main(argv=['test_colour_source_gates.py'] + rest)
