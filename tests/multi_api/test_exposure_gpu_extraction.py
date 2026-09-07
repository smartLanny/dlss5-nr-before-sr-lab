"""The GPU generator includes the real pure exposure view resolver, not a duplicate."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('exposure_gpu_extract',ROOT/'scripts/extract-post-gpu.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
SOURCE='''constexpr UINT kCodecSrvCount = 5;
constexpr UINT kCodecRootConstantCount = 31;
constexpr unsigned int kDiagnosticStatsUintCount = 28;
const char kCodecShader[] = R"HLSL(
float4 LoadPostArea() { return 0; }
const bool scaled = ModelWidth != Width;
)HLSL";
struct CodecConstants {
    unsigned width;
};
unsigned int AlignWorkExtent(unsigned value) { return value; }
void SelectAlignedWorkExtent(unsigned value) { use(value); }
'''
WRITER='void Write() { use(source_srv_format,NrMotionReadFormat,NrExposureReadFormat); }'
MOTION='DXGI_FORMAT NrMotionReadFormat(DXGI_FORMAT f) { return f; }'
TEMPLATE='\n'.join('// @PRODUCTION_'+s+'@' for s in ['DESCRIPTOR_CONSTANTS','SHADER','CONSTANTS','MOTION_READ_FORMAT','ALIGN_WORK','DESCRIPTOR_WRITER'])
EXPOSURE=(ROOT/'src/interop/nr_exposure_access_d3d12.inl').read_text()
class ExposureExtraction(unittest.TestCase):
    def test_requires_real_helper_when_writer_uses_it(self):
        with self.assertRaisesRegex(ValueError,'Missing actual exposure'):
            m.extract(SOURCE,WRITER,MOTION,TEMPLATE)
    def test_injects_exact_function_before_the_writer(self):
        out=m.extract(SOURCE,WRITER,MOTION,TEMPLATE,EXPOSURE)
        fn=m.function(EXPOSURE,'DXGI_FORMAT NrExposureReadFormat(')
        self.assertEqual(out.count(fn),1)
        self.assertLess(out.index(fn),out.index(WRITER))
        self.assertNotIn('NrResolveExposureAccess(',out) # C-interface device helper is not needed in the C++ pixel harness.
    def test_historical_writer_still_extracts_without_exposure_header(self):
        out=m.extract(SOURCE,WRITER.replace(',NrExposureReadFormat',''),MOTION,TEMPLATE)
        self.assertNotIn('NrExposureReadFormat',out)
    def test_actual_cli_and_no_overwriting_input(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); src=root/'src'; (src/'interop').mkdir(parents=True)
            paths={'nr_before_sr.cpp':SOURCE,'nr_codec_descriptors.inl':WRITER,
                   'interop/nr_motion_access_d3d12.inl':MOTION,'interop/nr_exposure_access_d3d12.inl':EXPOSURE}
            for name,text in paths.items(): (src/name).write_text(text)
            template=root/'template.cpp';template.write_text(TEMPLATE)
            output=root/'out.cpp'
            cmd=[sys.executable,str(ROOT/'scripts/extract-post-gpu.py'),'--source-root',str(root),'--template',str(template),'--output']
            subprocess.run(cmd+[str(output)],check=True,capture_output=True)
            self.assertEqual(output.read_text(),m.extract(SOURCE,WRITER,MOTION,TEMPLATE,EXPOSURE))
            rejected=subprocess.run(cmd+[str(src/'interop/nr_exposure_access_d3d12.inl')],capture_output=True)
            self.assertNotEqual(rejected.returncode,0)
            self.assertEqual((src/'interop/nr_exposure_access_d3d12.inl').read_text(),EXPOSURE)
if __name__=='__main__': unittest.main()
