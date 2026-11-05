"""The same extractor used by the Windows WARP build; fixtures are not a GPU test."""
import importlib.util
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('post_gpu_extract', ROOT / 'scripts/extract-post-gpu.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

SOURCE = '''constexpr UINT kCodecSrvCount = 5;
constexpr UINT kCodecRootConstantCount = 31;
constexpr unsigned int kDiagnosticStatsUintCount = 12;
const char kCodecShader[] = R"HLSL(
float4 LoadPostArea() { return 0; }
const bool scaled = ModelWidth != Width;
)HLSL";
struct CodecConstants {
    unsigned int width;
};
unsigned int AlignWorkExtent(unsigned int value) { /* } */ return value; }
void SelectAlignedWorkExtent(unsigned int value) { const char* s="}"; // }
(void)s; }
'''
DESCRIPTORS = 'void WriteCodecDescriptorSet() { use(source_srv_format, NrMotionReadFormat); }'
MOTION = 'DXGI_FORMAT NrMotionReadFormat(DXGI_FORMAT f) { return f; }'
NAMES = ['DESCRIPTOR_CONSTANTS','SHADER','CONSTANTS','MOTION_READ_FORMAT','ALIGN_WORK','DESCRIPTOR_WRITER']
TEMPLATE = '\n'.join('// @PRODUCTION_' + s + '@' for s in NAMES)

class Extraction(unittest.TestCase):
    def test_actual_types_and_stats_count(self):
        result = m.extract(SOURCE, DESCRIPTORS, MOTION, TEMPLATE)
        self.assertIn('constexpr UINT kCodecSrvCount = 5;', result)
        self.assertIn('constexpr unsigned int kDiagnosticStatsUintCount = 12;', result)
        self.assertEqual(result.count(DESCRIPTORS), 1)
        self.assertIn('const char* s="}"; // }\n(void)s; }', result)
        self.assertNotIn('@PRODUCTION_', result)

    def test_crlf(self):
        self.assertEqual(m.extract(SOURCE, DESCRIPTORS, MOTION, TEMPLATE),
                         m.extract(SOURCE.replace('\n','\r\n'), DESCRIPTORS, MOTION, TEMPLATE))

    def test_old_malformed_descriptor_shape_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'descriptor constants'):
            m.extract(SOURCE.replace('constexpr UINT', 'constexpr unsigned int'), DESCRIPTORS, MOTION, TEMPLATE)

    def test_missing_stats_and_duplicates_rejected(self):
        for wrong in (SOURCE.replace('constexpr unsigned int kDiagnosticStatsUintCount = 12;', ''),
                      SOURCE + '\nconstexpr unsigned int kDiagnosticStatsUintCount = 12;'):
            with self.assertRaisesRegex(ValueError, 'stats UAV'):
                m.extract(wrong, DESCRIPTORS, MOTION, TEMPLATE)

    def test_harness_markers_are_exact(self):
        for wrong in (TEMPLATE + '\n// @PRODUCTION_SHADER@', TEMPLATE + '\n// @PRODUCTION_NEW@', TEMPLATE.replace('// @PRODUCTION_SHADER@','')):
            with self.assertRaises(ValueError): m.extract(SOURCE, DESCRIPTORS, MOTION, wrong)

    def test_requires_staged_typed_source(self):
        for src, writer in ((SOURCE.replace('float4 LoadPostArea', 'float4 OldArea'), DESCRIPTORS),
                            (SOURCE, DESCRIPTORS.replace('source_srv_format', 'source_format'))):
            with self.assertRaises(ValueError): m.extract(src, writer, MOTION, TEMPLATE)

    def test_changed_functions_do_not_silently_truncate(self):
        with self.assertRaises(ValueError): m.function('int f() { R"tag(})tag"; }', 'int f(')
        with self.assertRaises(ValueError): m.function('int f() {', 'int f(')
        with self.assertRaises(ValueError): m.function('int f() {} int f() {}', 'int f(')

    def test_actual_cli_freezes_runtime_helper_and_hashes_both_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'src/interop').mkdir(parents=True)
            (root/'src/nr_before_sr.cpp').write_text(SOURCE)
            (root/'src/nr_codec_descriptors.inl').write_text(DESCRIPTORS)
            (root/'src/interop/nr_motion_access_d3d12.inl').write_text(MOTION)
            template = root/'template.cpp.in'
            template.write_text(TEMPLATE)
            output = root/'generated/host.cpp'
            with patch.object(sys, 'argv', ['extract-post-gpu.py', '--source-root', str(root),
                    '--template', str(template), '--output', str(output)]):
                m.main()
            helper = output.parent/'ci_warp_runtime.h'
            self.assertEqual(helper.read_bytes(), (ROOT/'tests/ci_warp_runtime.h').read_bytes())
            receipt = json.loads((output.parent/'ci-warp-runtime-input.json').read_text())
            self.assertEqual(receipt['helper_sha256'], hashlib.sha256(helper.read_bytes()).hexdigest())
            self.assertEqual(receipt['generated_harness_sha256'], hashlib.sha256(output.read_bytes()).hexdigest())

    def test_cli_cannot_overwrite_helper_or_its_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('ci_warp_runtime.h', 'ci-warp-runtime-input.json'):
                with self.subTest(name=name):
                    output = root/name
                    output.write_text('preserve existing input')
                    with patch.object(sys, 'argv', ['extract-post-gpu.py', '--source-root', str(root),
                            '--template', str(root/'template'), '--output', str(output)]):
                        with self.assertRaisesRegex(ValueError, 'frozen helper or input receipt'):
                            m.main()
                    self.assertEqual(output.read_text(), 'preserve existing input')

if __name__ == '__main__': unittest.main()
