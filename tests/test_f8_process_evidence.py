"""Real child-process/file evidence tests; no compiler, GPU, NGX or game."""
import ast
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
from types import SimpleNamespace
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('f8_process_evidence', ROOT / 'scripts/f8_process_evidence.py')
PROCESS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROCESS)


class ProcessEvidence(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def run_child(self, code, label='child', **kwargs):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return PROCESS.run_logged([sys.executable, '-c', code], self.root, label, **kwargs)

    def receipt(self, label='child'):
        return json.loads((self.root / f'{label}.process.json').read_text())

    def test_real_success_retains_exact_bytes_and_identity(self):
        result = self.run_child("import os;os.write(1,b'out\\x00\\xff');os.write(2,b'err\\r\\n')", check=True)
        self.assertEqual(result.returncode, 0)
        self.assertEqual((self.root / 'child.stdout.log').read_bytes(), b'out\x00\xff')
        self.assertEqual((self.root / 'child.stderr.log').read_bytes(), b'err\r\n')
        r = self.receipt()
        self.assertEqual(r['returncode'], 0)
        self.assertEqual(r['state'], 'completed')
        self.assertEqual(r['stdout_bytes'], 5)
        self.assertEqual(r['stdout_sha256'], hashlib.sha256(b'out\x00\xff').hexdigest())
        self.assertEqual(r['command'][0], sys.executable)
        self.assertEqual(r['executable_sha256'], PROCESS.sha256(Path(sys.executable)))
        self.assertNotIn('passed', r)

    def test_failure_is_retained_before_check_raises(self):
        with self.assertRaises(subprocess.CalledProcessError) as failure:
            self.run_child("import sys;print('FAIL: exact assertion',file=sys.stderr);sys.exit(7)", check=True)
        self.assertEqual(failure.exception.returncode, 7)
        self.assertEqual(self.receipt()['returncode'], 7)
        self.assertIn(b'FAIL: exact assertion', (self.root / 'child.stderr.log').read_bytes())

    def test_expected_negative_keeps_exit_and_text_for_original_oracle(self):
        result = self.run_child("import sys;print('FAIL: mutation',file=sys.stderr);sys.exit(1)", capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn('FAIL: mutation', result.stderr)
        self.assertEqual(self.receipt()['returncode'], 1)

    def test_capture_bytes_and_text_newlines_match_subprocess_contract(self):
        raw = self.run_child("import os;os.write(1,b'a\\r\\nb\\rc')", label='raw', capture_output=True)
        text = self.run_child("import os;os.write(1,b'a\\r\\nb\\rc')", label='text', capture_output=True, text=True)
        self.assertEqual(raw.stdout, b'a\r\nb\rc')
        self.assertEqual(text.stdout, 'a\nb\nc')

    def test_launch_error_never_has_exit_zero(self):
        with self.assertRaises(OSError):
            PROCESS.run_logged([str(self.root / 'missing.exe')], self.root, 'child', check=True)
        r = self.receipt()
        self.assertEqual(r['state'], 'launch-error')
        self.assertIsNone(r['returncode'])
        self.assertEqual(r['stdout_bytes'], 0)

    def test_duplicate_label_preserves_first_evidence_and_does_not_launch(self):
        self.run_child("print('first')")
        before = {p.name: p.read_bytes() for p in self.root.iterdir()}
        with patch.object(PROCESS.subprocess, 'run', side_effect=AssertionError('must not launch')):
            with self.assertRaises(FileExistsError):
                self.run_child("print('second')")
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.root.iterdir()})

    def test_partial_collision_never_truncates_existing_evidence(self):
        p = self.root / 'child.stderr.log';p.write_bytes(b'prior failure')
        with patch.object(PROCESS.subprocess, 'run', side_effect=AssertionError('must not launch')):
            with self.assertRaises(FileExistsError):
                self.run_child("print('new')")
        self.assertEqual(p.read_bytes(), b'prior failure')

    def test_large_raw_logs_have_bounded_tails_and_complete_hash(self):
        self.run_child("import os;os.write(1,b'X'*200000);os.write(2,b'Y'*100000)")
        for name, size, byte in (('stdout', 200000, b'X'), ('stderr', 100000, b'Y')):
            self.assertEqual((self.root / f'child.{name}.log').stat().st_size, size)
            self.assertEqual((self.root / f'child.{name}-tail.txt').read_bytes(), byte * PROCESS.TAIL_BYTES)
            self.assertEqual(self.receipt()[f'{name}_sha256'], hashlib.sha256(byte * size).hexdigest())

    def test_input_hashes_bind_current_generated_source(self):
        source = self.root / 'host.cpp';source.write_bytes(b'first input')
        self.run_child('pass', inputs={'generated_harness': source})
        source.write_bytes(b'later input')
        self.assertEqual(self.receipt()['inputs_sha256']['generated_harness'], hashlib.sha256(b'first input').hexdigest())

    def test_missing_input_stops_before_process(self):
        with patch.object(PROCESS.subprocess, 'run', side_effect=AssertionError('must not launch')):
            with self.assertRaises(FileNotFoundError):
                self.run_child('pass', inputs={'missing': self.root / 'missing.cpp'})
        self.assertFalse((self.root / 'child.process.json').exists())

    def test_invalid_labels_cannot_escape_attempt(self):
        for label in ('../outside', 'slash/name', '', 'a b'):
            with self.subTest(label=label), self.assertRaises(ValueError):
                self.run_child('pass', label=label)
        self.assertEqual(list(self.root.iterdir()), [])

    def test_interruption_is_not_completion_or_pass(self):
        with patch.object(PROCESS.subprocess, 'run', side_effect=KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt):
                self.run_child('pass')
        self.assertEqual(self.receipt()['state'], 'interrupted')
        self.assertIsNone(self.receipt()['returncode'])

    def test_console_encoding_error_cannot_hide_exit_or_receipt(self):
        with patch('builtins.print', side_effect=UnicodeError('console unavailable')):
            result = self.run_child("import os;os.write(2,b'\\xff')")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(self.receipt()['state'], 'completed')
        self.assertEqual((self.root / 'child.stderr.log').read_bytes(), b'\xff')

    def test_actual_driver_routes_all_four_child_stages_to_evidence(self):
        tree = ast.parse((ROOT / 'scripts/validate-f8-controller.py').read_text())
        calls = [node for node in ast.walk(tree) if isinstance(node, ast.Call)
                 and isinstance(node.func, ast.Name) and node.func.id == 'run_logged']
        self.assertEqual(len(calls), 4)
        observed = []
        def record(command, directory, label, **kwargs):
            observed.append((command, directory, label, kwargs))
        for gpu in (False, True):
            observed.clear()
            ns = dict(run_logged=record, args=SimpleNamespace(gpu=gpu, compiler='compiler', source=None),
                      out=self.root, root=ROOT, Path=Path, __file__=str(ROOT / 'scripts/validate-f8-controller.py'),
                      cpp=self.root / 'controller.cpp', exe=self.root / 'controller.exe',
                      name='control', bad_cpp=self.root / 'negative.cpp', bad_exe=self.root / 'negative.exe',
                      negative_out=self.root / 'private-negative')
            for call in calls:
                eval(compile(ast.Expression(call), '<actual-driver-call>', 'eval'), ns)
            stages = {item[2]: item for item in observed if item[2] != 'execute'}
            self.assertTrue(stages['compile'][3]['check'])
            self.assertTrue(stages['negative-control-compile'][3]['check'])
            self.assertEqual('-ld3d12' in stages['compile'][0], gpu)
            executions = [item for item in observed if item[2] == 'execute']
            positive = next(item for item in executions if item[1] == self.root)
            negative = next(item for item in executions if item[1] != self.root)
            self.assertTrue(positive[3]['check'])
            self.assertEqual(positive[3]['inputs']['core'], ROOT / 'src/nr_before_sr.cpp')
            self.assertEqual(negative[0][1], str(ns['negative_out']))
            self.assertTrue(negative[3]['capture_output'] and negative[3]['text'])
            self.assertNotIn('check', negative[3])


if __name__ == '__main__':
    unittest.main(verbosity=2)
