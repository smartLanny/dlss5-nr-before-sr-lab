"""Compile actual snapshot and setter functions; GPU/NGX/game are not exercised."""
from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = '024f9cc32778c1056db364d76d087ee257da6502'


class SnapshotCopyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shutil.which(os.environ.get('CXX', 'g++'))
        if not cls.compiler:
            raise unittest.SkipTest('C++ compiler unavailable')
        spec = importlib.util.spec_from_file_location('snapshot_extract', ROOT / 'scripts/validate-command-state-lockperf.py')
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        cls.extract = staticmethod(helper.extract)
        cls.temp = tempfile.TemporaryDirectory(prefix='nr-snapshot-copy-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        cls.source = (ROOT / 'src/nr_command_state.inl').read_text(encoding='utf-8-sig')
        cls.current = cls.build('candidate', cls.source)

    @classmethod
    def build(cls, name, source):
        folder = cls.directory / name
        folder.mkdir()
        text = re.search(r'enum class CommandRootKind[^;]+;', source).group(0) + '\n'
        for kind in ('CommandDynamicState', 'CommandRootArgument', 'NativeCommandState'):
            text += cls.extract(source, f'struct {kind} {{').rstrip() + ';\n'
        text += cls.extract(source, 'template <typename T> void CommandStateAssign(')
        for name in ('ReleaseNativeCommandState', 'InvalidateCommandRootArgument',
                     'CommandStateSetRoot', 'CommandStateSetHeaps', 'CommandStateSetValue', 'CommandStateSetConstants'):
            text += cls.extract(source, 'void ' + name + '(')
        if 'void CopyCommandRootArguments(' in source:
            text += cls.extract(source, 'void CopyCommandRootArguments(')
        text += cls.extract(source, 'bool CopyNativeCommandState(')
        (folder / 'production_snapshot.inl').write_text(text, encoding='utf-8')
        binary = folder / ('snapshot.exe' if os.name == 'nt' else 'snapshot')
        # Match the canonical Core translation unit's warning policy. Existing
        # ReleaseNativeCommandState intentionally clears CommandDynamicState and
        # GCC diagnoses that pre-existing memset as -Wclass-memaccess.
        flags = ['-std=c++20', '-O2', '-Wall', '-Wextra', '-Wpedantic']
        if os.environ.get('NR_SNAPSHOT_SANITIZE') == '1':
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        result = subprocess.run([cls.compiler, *flags, '-I' + str(folder),
                                 str(ROOT / 'tests/command_state_snapshot_copy.cpp'), '-o', str(binary)],
                                capture_output=True, text=True, timeout=90)
        if result.returncode:
            raise AssertionError('C++ compile failed:\n' + result.stdout + result.stderr)
        return binary

    def run_case(self, binary, *args, expected=0, diagnostic=None):
        result = subprocess.run([str(binary), *args], capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        if diagnostic:
            self.assertIn(diagnostic, result.stdout + result.stderr)
        if result.stdout:
            print(result.stdout.strip())
        return result

    def test_production_capture_restore(self):
        self.run_case(self.current)

    def test_inactive_payload_budget(self):
        self.run_case(self.current, '--budget', diagnostic='inactive payload bytes avoided=32768')

    def test_negative_controls(self):
        mutations = (
            ('tag', 'to.kind = from.kind;', '(void)from.kind;', 'FAIL binding tag'),
            ('value', 'to.value = from.value;', '(void)from.value;', 'FAIL binding scalar'),
            ('mask', 'to.constant_mask = from.constant_mask;', '(void)from.constant_mask;', 'FAIL constant mask'),
            ('constants', 'memcpy(to.constants, from.constants, sizeof(from.constants));',
             '(void)from.constants;', 'FAIL valid constant payload'),
            ('short-constants', 'memcpy(to.constants, from.constants, 16 * sizeof(from.constants[0]));',
             '(void)from.constants;', 'FAIL valid constant payload'),
        )
        for name, before, after, diagnostic in mutations:
            with self.subTest(name=name):
                self.assertEqual(self.source.count(before), 1)
                binary = self.build('negative-' + name, self.source.replace(before, after))
                self.run_case(binary, expected=1, diagnostic=diagnostic)

    def test_blanket_copy_negative_control(self):
        # Replace the actual production call with the exact previous copy
        # statement. This needs no Git history and exercises its original path.
        call = 'CopyCommandRootArguments(destination.arguments, source.arguments);'
        self.assertEqual(self.source.count(call), 1)
        baseline_source = self.source.replace(call,
            'memcpy(destination.arguments, source.arguments, sizeof(source.arguments));')
        baseline = self.build('blanket-copy', baseline_source)
        self.run_case(baseline)
        self.run_case(baseline, '--budget', expected=3, diagnostic='inactive constant words untouched=0/8192')
        if os.environ.get('NR_SNAPSHOT_BENCH') == '1':
            print('BASELINE exact pre-change memcpy statement (' + BASE + ')')
            self.run_case(baseline, '--bench')
            print('CANDIDATE production CopyNativeCommandState')
            self.run_case(self.current, '--bench')


if __name__ == '__main__':
    unittest.main()
