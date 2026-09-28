"""Actual pinned-source tests for the colour4 edge extension, not a full build.

--generated checks an actual completed compatibility export. It must not be a
hand-assembled preview; explicitly wrong paths fail rather than silently skip.
"""
from __future__ import annotations
import argparse
import ast
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]
REPO=ROOT
GENERATED=None

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
m1=load('edge_t_m1',ROOT/'scripts/patch-colour-lab.py')
m3=load('edge_t_m3',ROOT/'scripts/patch-colour-restore.py')
stage=load('edge_t_stage',ROOT/'scripts/patch-colour-stage.py')
edge=load('edge_t_edge',ROOT/'scripts/patch-colour-edge.py')
shader=(ROOT/'src/nr_colour_edge.hlsli').read_text(encoding='utf-8')
def body(s,k): return s[slice(*m3.definition(s,k))]
def blob(path):
    # Compare the repository's canonical text blob; CRLF checkout conversion
    # is not a shader edit. A changed character still fails the frozen pin.
    return subprocess.check_output(['git','hash-object',str(path)],cwd=REPO,text=True).strip()

def staged_source():
    raw=(REPO/'src/nr_before_sr.cpp').read_text(encoding='utf-8')
    s=m3.patch_runtime(m1.patch_runtime(raw),*[(ROOT/'src'/p).read_text(encoding='utf-8') for p in
        ('nr_colour_restore.hlsli','nr_colour_prepare.hlsli','nr_scaled_proxy_prepare.hlsli')])
    return stage.patch_runtime(s,(ROOT/'src/nr_colour_stage_reference.hlsli').read_text(encoding='utf-8'),m3)

class EdgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.old=staged_source();cls.new=edge.patch_runtime(cls.old,shader,m3)
    def test_actual_base_identity(self):
        # Beta 8 moves native temporal guide validation to the recorded
        # lifecycle token; Beta 9 adds a cold Feature18 commit-pressure guard.
        # The codec shader and colour/reconstruction math
        # are unchanged; the semantic checks below still guard this baseline.
        # Guide-domain follow-up to Reconstruction1: reviewed 32bfacf3 -> df3b6dfb.
        # Only invalid guide guards, immutable capture metadata and bypass naming
        # changed. The full kCodecShader string and GPU/resource contracts match.
        # docs/interop/GUIDE_DOMAIN_AUDIT_20260925.md records the scoped evidence.
        # Keep this exact source pin AND the generated semantic assertions below.
        for p,sha in [('src/nr_before_sr.cpp','3aa963eb4275f423c6c44e1785fcaeca44c5d137'),
                      ('scripts/patch-colour-stage.py','7236bb5adeb9e6991254ac0e1735121b750bb009'),
                      ('src/nr_colour_stage_reference.hlsli','49f529e973ba3e22dfaa0981f43a942652050eb6')]:
            self.assertEqual(blob(REPO/p),sha,'Baseline changed; review before rebasing: '+p)
    def test_paired_sampling_and_old_fallback(self):
        s=body(self.new,'void CombineV2CS(')
        self.assertIn('ColourEdgeSamplePair(center,',s)
        self.assertIn('proxy = SampleProxyLinearV2(center);',s)
        self.assertIn('edgePairValid && ModelNeighborhoodFiniteV2(center)',s)
        self.assertLess(s.index('ColourStageReference('),s.index('ColourEdgeSamplePair('))
        raw=body(self.new,'ColourStagePixel ColourStageReference(')
        self.assertIn('ColourEdgeSamplePair(center, prepared, ColourStageNeutral())',raw)
        self.assertIn('valid && pairValid',raw)
        self.assertIn('ModelNeighborhoodFiniteV2(center)',raw)
        self.assertIn('TransferStrength, FrameConfidence',raw)
    def test_resource_model_and_neutral_invariants(self):
        for k in ('void EncodeCS(','void EncodeV2CS(','bool SameHistoryConfig(',
                  'struct CodecConstants {','cbuffer Constants : register(b0) {',
                  'bool BindNrParameters(','float4 LoadFinalNeuralStoredV2(',
                  'float3 SampleProxyLinearV2(','float3 SampleNeuralLinearV2(',
                  'ColourLabRgbResult ColourStageRestore(','float3 ColourStageLoadNeural(',
                  'void CombineCS(','void WriteCombined('):
            self.assertEqual(body(self.old,k),body(self.new,k),k)
        for token in ('CreateCommittedResource','ResourceBarrier','ExecuteCommandLists','CreateFeature','EvaluateFeature'):
            self.assertEqual(self.old.count(token),self.new.count(token),token)
        self.assertIn('kCodecRootConstantCount = 60;',self.new)
    def test_output_only_configuration_and_receipt(self):
        self.assertIn('colour_edge_recovery',body(self.new,'bool SameConfig('))
        self.assertNotIn('colour_edge_recovery',body(self.new,'bool SameHistoryConfig('))
        self.assertEqual(self.new.count('L"ColourEdgeRecovery"'),2)
        d=body(self.new,'void DispatchCodec(')
        self.assertIn('v2_combine, edge_scaled',d)
        self.assertIn('recorded_edge_recovery = edge_flags != 0u',d)
        self.assertIn('restore_encoding != nr_colour_restore::Encoding::Unknown',d)
        self.assertIn('source_flags | edge_flags |',d)
    def test_actual_snapshot_dispatch_variant(self):
        d=body(self.old,'void DispatchCodec(').replace('g_config','frame_config')
        pos=d.index('{')+1;d=d[:pos]+'\n    ChainConfig frame_config = g_config;'+d[pos:]
        # Whole-source transform is rerun with only the actual dispatch changed.
        s=self.old.replace(body(self.old,'void DispatchCodec('),d,1)
        d=body(edge.patch_runtime(s,shader,m3),'void DispatchCodec(')
        self.assertIn('frame_config.colour_edge_recovery',d)
        self.assertNotIn('g_config.colour_edge_recovery',d)
    def test_capture_records_real_paired_samples(self):
        w=body(self.new,'void WriteCombinedV2(')
        self.assertLess(w.index('if (!CapturePixelAddress'),w.index('ColourEdgeSamplePair('))
        self.assertIn('fullSource), false)',w)
        self.assertIn('asuint(float4(edgeCaptureP, 1.0))',w)
        self.assertIn('asuint(float4(edgeCaptureN, 1.0))',w)
        self.assertIn('asuint(result)',w)
        self.assertIn('RecordPostStoreExpected(coord,result.rgb)',w)
        self.assertNotIn('ColourStageNeutral()',w)
    def test_positive_pair_and_no_new_resources(self):
        for token in ('Texture2D','RWTexture','register(','Interlocked','History'):
            self.assertNotIn(token,shader)
        self.assertIn('p.proxy += proxy * weight;',shader)
        self.assertIn('p.neural += neural * weight;',shader)
        self.assertIn('ColourLabGuideWeight(reference, proxy)',shader)
        self.assertNotIn('ColourLabGuideWeight(reference, neural)',shader)
        self.assertIn('Width == ModelWidth && Height == ModelHeight',shader)
    def test_drift_and_atomic_transform(self):
        with self.assertRaises(ValueError):edge.patch_runtime(self.new,shader,m3)
        a='    float3 proxy = SampleProxyLinearV2(center);\n    float3 neural = SampleNeuralLinearV2(center);'
        with self.assertRaises(ValueError):edge.patch_runtime(self.old.replace(a,''),shader,m3)
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);(root/'core/src').mkdir(parents=True)
            path=root/'core/src/nr_before_sr.cpp';path.write_text(self.old,encoding='utf-8')
            (root/'core/src/nr_colour_lab_ui.inl').write_text('wrong UI anchor')
            inputs={path:(ROOT/path).read_bytes() for path in edge.INPUTS}
            with self.assertRaises(ValueError):edge.apply(root,inputs,m3)
            self.assertEqual(path.read_text(encoding='utf-8'),self.old)
    def test_ui_feedback_and_build_contract(self):
        # Apply the inherited stage UI transform to the checked-out source.
        # The recipe test separately compiles the final panel with UI substitutes.
        ui=stage.patch_ui((ROOT/'src/nr_colour_lab_ui.inl').read_text(encoding='utf-8'),m3)
        ui=edge.patch_ui(ui,m3,(ROOT/'src/nr_quality_ui.inl').read_text(encoding='utf-8'))
        self.assertIn('next.colour_edge_recovery = edge;',ui)
        self.assertIn('sample.policy.recorded_edge_recovery',ui)
        self.assertIn('F8',ui)
        self.assertNotIn('ApplyConfig(',ui)
        self.assertNotIn('WritePrivateProfile',ui)
        self.assertIn('\\0',ui)
        for p in ('scripts/patch-colour-edge.py','scripts/prepare-colour4-edge.py'):
            ast.parse((ROOT/p).read_text(encoding='utf-8'))
        gen=(ROOT/'scripts/prepare-colour4-edge.py').read_text(encoding='utf-8')
        self.assertLess(gen.index("check('test_colour_stage.py'"),gen.index('edge_patch.apply('))
        self.assertLess(gen.index('edge_patch.apply('),gen.index("check('test_colour_edge.py'"))
        self.assertIn("manifest['files']=base.file_hashes(stage)",gen)
    def test_explicit_generated_export(self):
        if GENERATED is None:self.skipTest('full compatibility-generated export absent; real raw-source tests ran')
        text=(GENERATED/'src/nr_before_sr.cpp').read_text(encoding='utf-8')
        self.assertIn(shader,text)
        self.assertIn(edge.VERSION,text)
        self.assertIn('frame_config.colour_edge_recovery',body(text,'void DispatchCodec('))
        self.assertIn('ColourEdgeSamplePair(center, prepared, ColourStageNeutral())',text)
        self.assertNotIn('colour_edge_recovery',body(text,'bool SameHistoryConfig('))
        for p in edge.INPUTS[1:]:self.assertEqual((GENERATED/p).read_bytes(),subprocess.check_output(['git','show','HEAD:'+p],cwd=ROOT))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--repo',type=Path,default=ROOT)
    p.add_argument('--generated',type=Path);a,rest=p.parse_known_args();REPO=a.repo;GENERATED=a.generated
    unittest.main(argv=['test_colour_edge.py']+rest)
