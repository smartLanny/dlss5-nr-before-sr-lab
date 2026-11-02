"""Actual Streamline adapter and FG source selection; Win32/MinHook are doubles."""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StreamlineFactoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shutil.which(os.environ.get('CXX', 'g++'))
        if not cls.compiler:
            raise unittest.SkipTest('C++ compiler unavailable')
        cls.temp = tempfile.TemporaryDirectory(prefix='nr-streamline-factory-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.folder = Path(cls.temp.name)
        cls.source = (ROOT / 'src/nr_fg_streamline.inl').read_text(encoding='utf-8-sig')
        cls.consumer = (ROOT / 'src/nr_source_frame_runtime.inl').read_text(encoding='utf-8-sig')
        cls.binary = cls.build('candidate', cls.source, cls.consumer)

    @classmethod
    def build(cls, name, source, consumer):
        folder = cls.folder / name
        folder.mkdir()
        (folder / 'production_streamline.inl').write_text(source, encoding='utf-8')
        begin = consumer.index('if(inherited.KnownApplication())source=inherited.source;')
        end = consumer.index('inputs.source=source;consume=fg_consumer;', begin)
        selection = consumer[begin:end]
        if 'inputs.fg_frame' not in selection or len(selection) > 600:
            raise AssertionError('FG production selection boundary changed; review the extraction')
        (folder / 'production_fg_selection.inl').write_text(selection, encoding='utf-8')
        binary = folder / ('factory.exe' if os.name == 'nt' else 'factory')
        flags = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-pthread']
        if os.name == 'nt':
            # Match the canonical LLVM-MinGW native tests. The fixture must run
            # without assuming that toolchain runtime DLLs are on the PATH.
            flags += ['-static']
        if os.environ.get('NR_STREAMLINE_SANITIZE') == '1':
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        result = subprocess.run(
            [cls.compiler, *flags, '-I' + str(folder),
             '-DNR_STREAMLINE_IMPLEMENTATION="production_streamline.inl"',
             str(ROOT / 'tests/streamline_factory_lock.cpp'), '-o', str(binary)],
            capture_output=True, text=True, timeout=90)
        if result.returncode:
            raise AssertionError('C++ compile failed:\n' + result.stdout + result.stderr)
        return binary

    def run_case(self, binary, mode, expected=0, diagnostic=None):
        result = subprocess.run([str(binary), mode], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        if diagnostic:
            self.assertIn(diagnostic, result.stdout + result.stderr)
        if result.stdout:
            print(result.stdout.strip())

    def test_production_lock_and_lifetime_contracts(self):
        for mode in ('--reentrant', '--loader-cycle', '--contracts', '--pin-target',
                     '--pin-observer', '--unloaded', '--same-target-race',
                     '--different-target-race', '--foreign-hook', '--retry-owned-hook',
                     '--foreign-marker', '--retry-owned-marker'):
            with self.subTest(mode=mode):
                self.run_case(self.binary, mode)

    def test_previous_blocking_callback_negative_control(self):
        # Restore the exact old production callback statements from 53f06b51.
        # Both fixtures reach the blocking acquire before any new state is used.
        call = '        ObserveMarkerFunction(function);'
        self.assertEqual(self.source.count(call), 1)
        previous = self.source.replace(call, '''        AcquireSRWLockExclusive(&install_lock);
        (void)Install(function,reinterpret_cast<void*>(&HookMarker),reinterpret_cast<void**>(&marker));
        ReleaseSRWLockExclusive(&install_lock);''')
        binary = self.build('blocking-callback', previous, self.consumer)
        self.run_case(binary, '--reentrant', 1, 'FAIL recursive install-lock wait')
        self.run_case(binary, '--loader-cycle', 1, 'FAIL factory/loader install-lock cycle')

    def test_negative_controls(self):
        mutations = (
            ('target-pin', '--contracts',
             'GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,\n'
             '        reinterpret_cast<LPCWSTR>(original),&owner_pin)',
             'GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,\n'
             '        reinterpret_cast<LPCWSTR>(original),&owner_pin)',
             'FAIL detour and actual plugin target are pinned before deferral'),
            ('observer-pin', '--contracts',
             'GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,\n'
             '        reinterpret_cast<LPCWSTR>(&HookMarker),&observer_pin)',
             'GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,\n'
             '        reinterpret_cast<LPCWSTR>(&HookMarker),&observer_pin)',
             'FAIL detour and actual plugin target are pinned before deferral'),
            ('ready-skips-marker', '--foreign-hook',
             '&&\n       (!marker_target.load(std::memory_order_acquire)||Ready(reinterpret_cast<void**>(&marker)))',
             '', 'FAIL ready exports and an existing backoff never swallow a single deferred marker query'),
            ('late-poll', '--foreign-hook',
             'next_poll.store(0,std::memory_order_relaxed);', '(void)0;',
             'FAIL ready exports and an existing backoff never swallow a single deferred marker query'),
            ('lost-registration', '--contracts',
             '(void)Install(reinterpret_cast<void*>(target),reinterpret_cast<void*>(&HookMarker),reinterpret_cast<void**>(&marker));',
             '(void)target;', 'FAIL one factory registration is installed by the next available Poll'),
            ('ambiguous-race', '--different-target-race',
             'else if(expected!=original)marker_owner_ambiguous.store(true,std::memory_order_release);',
             '(void)expected;', 'FAIL different concurrent targets retire only the ambiguous PCL evidence'),
            ('stale-tls', '--contracts',
             '!sl_wire::enabled.load(std::memory_order_acquire)||\n'
             '       sl_wire::marker_owner_ambiguous.load(std::memory_order_acquire)',
             '!sl_wire::enabled.load(std::memory_order_acquire)',
             'FAIL ambiguous callable invalidates old TLS at the real FG consumer'),
            ('missing-end', '--contracts',
             'if(result==0&&(value==5||value==11||value==20))sl_present_source={};',
             '(void)result;', 'FAIL pre-install native pointer expires PresentEnd while F6 is off'),
            ('provider-switch', '--foreign-hook',
             'if(!provider&&GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN,L"sl.interposer.dll",&candidate))provider=candidate;',
             'if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN,L"sl.interposer.dll",&candidate))provider=candidate;',
             'FAIL marker retry retains the pinned provider identity instead of re-resolving its filename'),
        )
        for name, mode, before, after, diagnostic in mutations:
            with self.subTest(name=name):
                self.assertEqual(self.source.count(before), 1)
                binary = self.build(name, self.source.replace(before, after), self.consumer)
                self.run_case(binary, mode, 1, diagnostic)

    def test_consumer_rejects_ambiguous_tls_negative_control(self):
        current = 'else if(const auto present=CurrentStreamlinePresentSource())source=present;'
        self.assertEqual(self.consumer.count(current), 1)
        previous = self.consumer.replace(current, 'else if(sl_present_source)source=sl_present_source;')
        binary = self.build('consumer-raw-tls', self.source, previous)
        self.run_case(binary, '--contracts', 1, 'FAIL ambiguous callable invalidates old TLS at the real FG consumer')


if __name__ == '__main__':
    unittest.main()
