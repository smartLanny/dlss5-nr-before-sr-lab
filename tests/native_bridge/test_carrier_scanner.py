"""Run the actual generated scanner with deterministic loader/hook failures.

The scanner, promotion and RAII lease are extracted unchanged. Windows critical
sections are real; module enumeration, code addresses and hook outcomes are API
doubles. This is neither a game nor a real detour/GPU test.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tests.native_bridge.test_carrier_observer_failure import function


@unittest.skipUnless(os.environ.get('CARRIER_COMPLETION_SOURCE'), 'requires staged production source')
class CarrierScanner(unittest.TestCase):
    def test_scanner_failure_and_module_lifetime(self):
        source = (Path(os.environ['CARRIER_COMPLETION_SOURCE']) /
                  'carrier/src/dlss5-bridge.cpp').read_text(encoding='utf-8')
        fixture = Path(__file__).with_name('carrier_scanner.cpp').read_text(encoding='utf-8')
        lease = ''
        if 'class CarrierModuleLease {' in source:
            start = source.index('class CarrierModuleLease {')
            lease = source[start:source.index('\n};', start) + 3]
        extracted = lease + '\n' + function(source, 'static void PromoteObservedLayers()')
        extracted += '\n' + function(source, 'static int HookNewNgxModules()')
        fixture = fixture.replace('// ACTUAL_GENERATED_FUNCTIONS', extracted)
        with tempfile.TemporaryDirectory(prefix='carrier-scanner-') as tmp:
            cpp, exe = Path(tmp) / 'scanner.cpp', Path(tmp) / 'scanner.exe'
            cpp.write_text(fixture, encoding='utf-8')
            build = subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-O2',
                                    '-static', str(cpp), '-o', str(exe)],
                                   text=True, capture_output=True, timeout=90)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            failures = []
            for scenario in ('partial', 'failed', 'lease', 'reload', 'mismatch', 'forwarded', 'ordinary', 'rejected-reload'):
                result = subprocess.run([str(exe), scenario], text=True, capture_output=True, timeout=10)
                print(scenario + ': ' + (result.stdout + result.stderr).strip())
                if result.returncode:
                    failures.append(scenario + ': ' + result.stdout + result.stderr)
            self.assertFalse(failures, '\n'.join(failures))


if __name__ == '__main__':
    unittest.main(verbosity=2)
