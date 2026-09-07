"""Actual carrier declaration replacement; pins remain checked by the generator."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('ngx_abi_patch', ROOT / 'scripts/patch-ngx-parameter-abi.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class NativeParameterAbiPatch(unittest.TestCase):
    def test_exact_declaration_only(self):
        source = 'before\n' + m.ORIGINAL + '\nafter\n'
        result = m.patch(source)
        self.assertEqual(result, 'before\n#include "nr_ngx_parameter_abi.h"\nafter\n')
        for changed in (source + m.ORIGINAL, source.replace('Reset()', 'Clear()'), result):
            with self.assertRaises(ValueError):
                m.patch(changed)

    def test_staged_header_is_the_compiled_header(self):
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            core = stage / 'core/src/interop'
            carrier = stage / 'carrier/src'
            core.mkdir(parents=True)
            carrier.mkdir(parents=True)
            reviewed = (ROOT / 'src/interop' / m.HEADER).read_bytes()
            (core / m.HEADER).write_bytes(reviewed)
            (carrier / 'dlss5-bridge.cpp').write_text(m.ORIGINAL, encoding='utf-8')
            m.apply(stage)
            self.assertEqual((carrier / m.HEADER).read_bytes(), reviewed)
            self.assertEqual((carrier / 'dlss5-bridge.cpp').read_text(encoding='utf-8'),
                             '#include "nr_ngx_parameter_abi.h"')

    def test_missing_header_does_not_mutate_carrier(self):
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            target = stage / 'carrier/src/dlss5-bridge.cpp'
            target.parent.mkdir(parents=True)
            target.write_text(m.ORIGINAL, encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'header absent'):
                m.apply(stage)
            self.assertEqual(target.read_text(encoding='utf-8'), m.ORIGINAL)

    @unittest.skipUnless(os.environ.get('PINNED_CARRIER_ROOT'), 'set pinned carrier checkout')
    def test_real_pinned_source(self):
        source = Path(os.environ['PINNED_CARRIER_ROOT']) / 'src/dlss5-bridge.cpp'
        result = m.patch(source.read_text(encoding='utf-8'))
        self.assertEqual(result.count('#include "nr_ngx_parameter_abi.h"'), 1)
        self.assertNotIn('struct NVSDK_NGX_Parameter\n{', result)

if __name__ == '__main__':
    unittest.main()
