"""Source-shape checks for the generic WARP helper extraction; not a PS/GPU run."""
import importlib.util
from pathlib import Path
import re
import unittest
ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('general_gpu_extractor', ROOT/'scripts/extract-post-gpu.py')
m = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(m)

class GeneralHarness(unittest.TestCase):
    def test_actual_powershell_pattern_extracts_complete_pure_helpers(self):
        script = (ROOT/'scripts/validate-codec-gpu.ps1').read_text(encoding='utf-8-sig')
        declaration = "$pattern = '(?ms)^DXGI_FORMAT ' + [regex]::Escape($name) + '\\([^{}]*\\)\\s*\\{.*?^\\}'"
        self.assertIn(declaration, script)
        for name, filename in [('NrMotionReadFormat','nr_motion_access_d3d12.inl'),
                               ('NrExposureReadFormat','nr_exposure_access_d3d12.inl')]:
            text = (ROOT/'src/interop'/filename).read_text(encoding='utf-8')
            pattern = r'(?ms)^DXGI_FORMAT ' + re.escape(name) + r'\([^{}]*\)\s*\{.*?^\}'
            for candidate in (text, text.replace('\n','\r\n')):
                definitions = re.findall(pattern, candidate)
                self.assertEqual(len(definitions), 1)
                self.assertEqual(definitions[0], m.function(candidate, 'DXGI_FORMAT '+name+'('))
            self.assertEqual(len(re.findall(pattern,text+text)),2)
        self.assertIn('$definitions.Count -ne 1',script)
        self.assertIn("$writer.Contains($name + '(')",script)
        self.assertIn('$helperDefinitions + $statsCount.Value',script)

    def test_source_inputs_use_explicit_utf8(self):
        script = (ROOT/'scripts/validate-codec-gpu.ps1').read_text(encoding='utf-8-sig')
        readers = [line for line in script.splitlines() if 'Get-Content ' in line]
        self.assertEqual(len(readers),5)
        self.assertTrue(all('-Encoding UTF8' in line for line in readers))
        self.assertNotIn('C:\\Users\\', script)

if __name__ == '__main__': unittest.main()
