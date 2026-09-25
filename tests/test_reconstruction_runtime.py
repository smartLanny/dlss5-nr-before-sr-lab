"""Run the actual final source transformation and check integration invariants.
These checks alone are not shader compilation or GPU execution.
"""
from pathlib import Path
import argparse,importlib.util,json,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
m1=load('recon_m1',ROOT/'scripts/patch-colour-lab.py')
m3=load('recon_m3',ROOT/'scripts/patch-colour-restore.py')
stage=load('recon_stage',ROOT/'scripts/patch-colour-stage.py')
edge=load('recon_edge',ROOT/'scripts/patch-colour-edge.py')
patch=load('recon_final',ROOT/'scripts/patch-reconstruction.py')
inputs={p:(ROOT/p).read_bytes() for p in patch.INPUTS}
GENERATED=None

def body(s,k):return s[slice(*m3.definition(s,k))]
def before():
    raw=(ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8')
    s=m3.patch_runtime(m1.patch_runtime(raw),*[(ROOT/'src'/p).read_text(encoding='utf-8') for p in ('nr_colour_restore.hlsli','nr_colour_prepare.hlsli','nr_scaled_proxy_prepare.hlsli')])
    s=stage.patch_runtime(s,(ROOT/'src/nr_colour_stage_reference.hlsli').read_text(),m3)
    return edge.patch_runtime(s,(ROOT/'src/nr_colour_edge.hlsli').read_text(),m3)

class ReconstructionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.old=before();cls.new=patch.patch_runtime(cls.old,inputs,m3)
    def test_existing_models_encoders_history_and_abi_unchanged(self):
        for k in ('void EncodeCS(','void EncodeV2CS(','bool SameHistoryConfig(',
                  'struct CodecConstants {','cbuffer Constants : register(b0) {',
                  'bool BindNrParameters(','float4 LoadFinalNeuralStoredV2(',
                  'ColourLabRgbResult ColourLabRestoreRgb(', 'ColourStagePixel ColourStageReference('):
            self.assertEqual(body(self.old,k),body(self.new,k),k)
        for token in ('ResourceBarrier(', 'CreateCommittedResource(', 'EvaluateFeature(', 'CreateFeature('):
            self.assertEqual(self.old.count(token),self.new.count(token),token)
        self.assertIn('kCodecRootConstantCount = 60;',self.new)
    def test_same_frame_paired_sampler_reaches_default_v2(self):
        combine=body(self.new,'void CombineV2CS(')
        self.assertIn('if (NrReconScaled())',combine)
        self.assertIn('NrReconSamplePair(center,',combine)
        self.assertIn('proxy,neural,edgePairValid);',combine)
        self.assertIn('edgePairValid && ModelNeighborhoodFiniteV2(center)',combine)
        self.assertIn('proxy = SampleProxyLinearV2(center);',combine)
        self.assertIn('ColourLabPreparedReference(storedOriginal.rgb, source)',combine)
    def test_guard_after_chroma_and_before_storage(self):
        for name,store in [('void CombineCS(','float3 sourceDomainResult'),('void CombineV2CS(','float3 storedResult')]:
            s=body(self.new,name)
            self.assertLess(s.index('result=NrReconPreserveColour('),s.index(store))
            self.assertGreater(s.index('result=NrReconPreserveColour('),s.index('ColourLabRestoreRgb('))
            self.assertIn('NrReconChromaRequested() && !lightStylePreview',s)
    def test_normalized_saved_settings_not_history(self):
        self.assertIn('int reconstruction_mode = 0;',self.new)
        self.assertIn('bool near_black_chroma_guard = false;',self.new)
        self.assertIn('reconstruction_mode',body(self.new,'bool SameConfig('))
        self.assertNotIn('reconstruction_mode',body(self.new,'bool SameHistoryConfig('))
        for key in ('ReconstructionMode','NearBlackChromaGuard'):self.assertEqual(self.new.count('L"'+key+'"'),2)
    def test_config_snapshot_and_constants_before_capture(self):
        s=body(self.old,'void DispatchCodec(').replace('g_config','frame_config')
        n=s.index('{')+1;s=s[:n]+'\n    ChainConfig frame_config = g_config;'+s[n:]
        text=self.old.replace(body(self.old,'void DispatchCodec('),s,1)
        d=body(patch.patch_runtime(text,inputs,m3),'void DispatchCodec(')
        self.assertIn('frame_config.reconstruction_mode',d)
        self.assertNotIn('g_config.reconstruction_mode',d)
        self.assertLess(d.index('constants.region_flags |= reconstruction.flags;'),d.index('nr_replay::recorder.Composition('))
        self.assertGreater(d.index('constants.region_flags |= reconstruction.flags;'),d.index('constants.region_flags=32u;'))
    def test_capture_keeps_real_neural_and_same_sampler(self):
        s=body(self.new,'void WriteCombinedV2(uint2 coord, uint2 destinationCoord, float4 result,')
        self.assertLess(s.index('if (!CapturePixelAddress'),s.index('if(paired)'))
        self.assertNotIn('NrReconSamplePair(',s)
        self.assertIn('capturedProxy',s)
        self.assertIn('capturedNeural',s)
        combine=body(self.new,'void CombineV2CS(')
        self.assertEqual(combine.count('capturedProxy, capturedNeural, NrReconScaled()'),8)
        self.assertLess(combine.index('const float3 capturedProxy'),combine.index('ApplyGenericStoredV1'))
        self.assertNotIn('NrReconSamplePair(',body(self.new,'ColourStagePixel ColourStageReference('))
    def test_repeat_or_drift_rejected_before_publish(self):
        with self.assertRaises(ValueError):patch.patch_runtime(self.new,inputs,m3)
        with self.assertRaises(ValueError):patch.patch_runtime(self.old.replace('    float3 sourceDomainResult =\n','    float3 RENAMED =\n'),inputs,m3)
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'core/src').mkdir(parents=True)
            f=root/'core/src/nr_before_sr.cpp';f.write_text(self.old)
            (root/'core/src/nr_color_policy.inl').write_text('invalid original policy')
            with self.assertRaises(ValueError):patch.apply(root,inputs,m3)
            self.assertEqual(f.read_text(),self.old)
    def test_no_new_textures_or_original_sampler_changes(self):
        shader=(ROOT/'src/nr_reconstruction_sampling.hlsli').read_text()
        for forbidden in ('Texture2D<','RWTexture','register(','Interlocked','CreateFeature'):
            self.assertNotIn(forbidden,shader)
        for name in ('float3 SampleProxyLinearV2(','float3 SampleNeuralLinearV2('):
            self.assertEqual(body(self.old,name),body(self.new,name))
        self.assertIn('LoadFinalNeuralStoredV2(p)',shader)
        self.assertIn('if(spatial>0.0)',shader)
        self.assertIn('Width!=ModelWidth||Height!=ModelHeight',shader)
    def test_real_ui_does_not_overwrite_other_settings(self):
        s=(ROOT/'src/nr_reconstruction_ui.inl').read_text()
        import re
        edits=set(re.findall(r'next\.(\w+)\s*=(?!=)',s)) | set(re.findall(r'&next\.(\w+)',s))
        self.assertEqual(edits,{'reconstruction_mode','near_black_chroma_guard'})
        self.assertIn('去除暗噪',s)
        self.assertIn('ReconstructionUiModeName(choice)',s)
        self.assertIn('if(choice==2)ImGui::SameLine();',s)
        for label in ('A##reconstruction', 'B##reconstruction', 'A+B', 'NrCombo('):self.assertNotIn(label,s)
        self.assertIn('sample.config_generation==generation',s)
        for bad in ('ApplyConfig(', 'WritePrivateProfile','CreateFeature(','EvaluateFeature('):self.assertNotIn(bad,s)
        self.assertIn('F8',s)
    def test_complete_generated_source_when_provided(self):
        if GENERATED is None:
            self.skipTest('complete source generation not supplied; source transformations still tested')
        s=(GENERATED/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
        self.assertIn(inputs['src/nr_reconstruction_math.hlsli'].decode(),s)
        self.assertIn(inputs['src/nr_reconstruction_sampling.hlsli'].decode(),s)
        self.assertIn('frame_config.reconstruction_mode',body(s,'void DispatchCodec('))
        self.assertIn('constants.region_flags |= reconstruction.flags;',s)
        pages=(GENERATED/'src/nr_unified_pages.inl').read_text()
        self.assertIn('DrawReconstructionControls(next,generation,pending,changed,discrete);',pages)
        f=(GENERATED/'src/nr_feedback_picture.inl').read_text()
        self.assertIn('requested.reconstructionMode',f)
        a=(GENERATED/'src/nr_feedback_capture.inl').read_text()
        self.assertIn('reconstruction.recorded',a)
        self.assertIn('not GPU/pixel proof',f)
        for p in patch.INPUTS[1:]:self.assertEqual((GENERATED/p).read_bytes(),inputs[p])

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--generated',type=Path);p.add_argument('--repo',type=Path)
    a,rest=p.parse_known_args();GENERATED=a.generated
    unittest.main(argv=['reconstruction']+rest)
