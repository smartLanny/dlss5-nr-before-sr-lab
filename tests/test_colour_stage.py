"""Actual colour4 integration gates; --repo or --generated is REQUIRED.

No small fixture can masquerade as a complete Core. Source transformations,
CPU math, Windows compilation, GPU and in-game validation are separate gates.
"""
from __future__ import annotations
import argparse
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
REPO=ROOT # Discovery also checks real source rather than returning a green skip.
GENERATED=None

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

class StageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.m1=load('stage_m1',ROOT/'scripts/patch-colour-lab.py')
        cls.m2=load('stage_m2',ROOT/'scripts/patch-colour-restore.py')
        cls.m4=load('stage_m4',ROOT/'scripts/patch-colour-stage.py')
        cls.shader=(ROOT/'src/nr_colour_stage_reference.hlsli').read_text(encoding='utf-8')
        cls.old=None
        if REPO:
            raw=(REPO/'src/nr_before_sr.cpp').read_text(encoding='utf-8')
            cls.old=cls.m2.patch_runtime(cls.m1.patch_runtime(raw),
                (ROOT/'src/nr_colour_restore.hlsli').read_text(encoding='utf-8'),
                (ROOT/'src/nr_colour_prepare.hlsli').read_text(encoding='utf-8'),
                (ROOT/'src/nr_scaled_proxy_prepare.hlsli').read_text(encoding='utf-8'))
            cls.text=cls.m4.patch_runtime(cls.old,cls.shader,cls.m2)
        else:
            cls.text=(GENERATED/'src/nr_before_sr.cpp').read_text(encoding='utf-8')

    def body(self,text,sig):return text[slice(*self.m2.definition(text,sig))]

    def test_raw_reference_precedes_aesthetic_control_flow(self):
        for sig in ('void CombineCS(', 'void CombineV2CS('):
            s=self.body(self.text,sig)
            self.assertLess(s.index('ColourStageReference('),s.index('ApplyGenericStoredV1('))
            self.assertLess(s.index('ColourStageReference('),s.index('FaceMaskV1('))
            self.assertLess(s.index('ColourStageReference('),s.index('FaceOriginalLuma==1'))
            self.assertLess(s.index('ColourStageReference('),s.index('RegionPreviewV1('))
            self.assertIn('if (ColourLabRgbEnabled() && ColourLabRawEditRequested())',s)
            self.assertIn('reference.invalid',s)
            # Removing the entire entry block recovers the old compositor.
            if self.old:
                begin=s.index('    // Colour4: read the actual model result')
                branch=s.index('    if (ColourLabRgbEnabled() && ColourLabRawEditRequested())',begin)
                left,right=self.m2.definition(s[branch:],'if (ColourLabRgbEnabled() && ColourLabRawEditRequested())')
                without=s[:begin]+s[branch+right+1:]
                self.assertEqual(without,self.body(self.old,sig))

    def test_sampler_uses_actual_expression_and_capture_is_real(self):
        n=self.body(self.text,'float3 SampleNeuralLinearV2(')
        expected=n.replace('SampleNeuralLinearV2(','ColourStageSampleNeural(',1).replace('LoadNeuralLinearV2(','ColourStageLoadNeural(')
        self.assertEqual(self.body(self.text,'float3 ColourStageSampleNeural('),expected)
        for sig in ('void WriteCombined(', 'void WriteCombinedV2('):
            self.assertNotIn('ColourStage',self.body(self.text,sig))
        self.assertIn('ModelNeighborhoodFiniteV2(center)',self.shader)
        self.assertIn('LoadFinalNeuralStoredV2(int2(pixel))',self.shader)
        self.assertIn('original.a',self.shader)

    def test_default_gpu_and_history_contracts_unchanged(self):
        if self.old:
            for sig in ('void EncodeCS(', 'void EncodeV2CS(', 'float4 LoadFinalNeuralStoredV2(',
                        'struct CodecConstants {', 'cbuffer Constants : register(b0) {',
                        'bool SameHistoryConfig(', 'void WriteCombined(', 'void WriteCombinedV2('):
                self.assertEqual(self.body(self.old,sig),self.body(self.text,sig))
        self.assertNotIn('colour_neutral_probe',self.body(self.text,'bool SameHistoryConfig('))
        self.assertIn('colour_neutral_probe',self.body(self.text,'bool SameConfig('))
        self.assertEqual(self.text.count('L"ColourNeutralProbe"'),0)
        self.assertIn('bool colour_neutral_probe = false;',self.text)
        self.assertIn('next.colour_neutral_probe = g_config.colour_neutral_probe;',self.body(self.text,'void ReloadConfig('))
        self.assertNotIn('colour_neutral_probe',self.body(self.text,'bool SaveConfig('))
        self.assertIn('persisted_next.colour_neutral_probe=g_config.colour_neutral_probe;',self.body(self.text,'bool ApplyConfig('))
        self.assertIn('const bool preview_only=(next.colour_neutral_probe!=g_config.colour_neutral_probe ||',self.body(self.text,'bool ApplyConfig('))
        self.assertIn('kCodecRootConstantCount = 60;',self.text)
        self.assertNotIn('RWTexture',self.shader)
        self.assertNotIn('Texture2D',self.shader)

    def test_actual_dispatch_and_f8_recording(self):
        s=self.body(self.text,'void DispatchCodec(')
        cfg='frame_config' if 'ChainConfig frame_config = g_config;' in s else 'g_config'
        self.assertIn(f'{cfg}.colour_neutral_probe, any_combine, restore_encoding)',s)
        self.assertIn('slot.colour_restore_encoding.For(slot.last_used_serial)',s)
        self.assertIn('colour_stage_flags | nr_colour_restore::Flags',s)
        self.assertIn('recorded_neutral_probe = colour_stage_flags != 0u',s)
        if GENERATED:self.assertNotIn('g_config.colour_neutral_probe',s)
        if REPO:
            stage=Path(tempfile.mkdtemp(prefix='colour4-test-'))
            self.addCleanup(__import__('shutil').rmtree,stage)
            (stage/'core/src').mkdir(parents=True)
            (stage/'core/src/nr_before_sr.cpp').write_text(self.old,encoding='utf-8')
            for p in ('src/nr_color_policy.inl','src/nr_feedback_picture.inl','src/nr_colour_lab_ui.inl'):
                s=(REPO/p).read_text(encoding='utf-8')
                if p.endswith('nr_color_policy.inl'):s=self.m2.patch_policy(self.m1.patch_policy(s))
                if p.endswith('nr_feedback_picture.inl'):s=self.m2.patch_feedback(s)
                (stage/'core'/p).write_text(s,encoding='utf-8')
            inputs={p:(ROOT/p).read_bytes() for p in self.m4.INPUTS}
            self.m4.apply(stage,inputs,self.m2)
            policy=(stage/'core/src/nr_color_policy.inl').read_text()
            self.assertIn('requested_neutral_probe',policy)
            f8=(stage/'core/src/nr_feedback_picture.inl').read_text()
            self.assertIn('config.colour_neutral_probe',f8)
            self.assertNotIn('g_config.colour_neutral_probe',f8)

    def test_ui_transactions_captures_and_bypass(self):
        ui=(ROOT/'src/nr_colour_lab_ui.inl').read_text(encoding='utf-8')
        ui=self.m4.patch_ui(ui,self.m2)
        self.assertIn('next.colour_neutral_probe = neutralProbe;',ui)
        self.assertIn('next.colour_neutral_probe = false;',ui)
        self.assertIn('sample.policy.recorded_neutral_probe',ui)
        self.assertIn('RequestStageCapture()',ui)
        self.assertNotIn('ApplyConfig(',ui)
        self.assertNotIn('WritePrivateProfile',ui)
        self.assertIn('F8',ui)
        self.assertIn('BUG',ui)

    def test_drift_and_partial_publish_fail_closed(self):
        if not self.old:
            # The real generated target is already patched and cannot be patched again.
            with self.assertRaises(ValueError):self.m4.patch_runtime(self.text,self.shader,self.m2)
            return
        for anchor in ('float3 SampleNeuralLinearV2(', '    float4 storedOriginal = OriginalColor.Load(int3(sourceCoord, 0));'):
            with self.assertRaises(ValueError):
                self.m4.patch_runtime(self.old.replace(anchor,anchor+anchor,1),self.shader,self.m2)
        with tempfile.TemporaryDirectory() as temp:
            stage=Path(temp);(stage/'core/src').mkdir(parents=True)
            for p in ('nr_before_sr.cpp','nr_color_policy.inl','nr_feedback_picture.inl','nr_colour_lab_ui.inl'):
                (stage/'core/src'/p).write_text(self.old if p=='nr_before_sr.cpp' else 'missing anchor',encoding='utf-8')
            with self.assertRaises(ValueError):
                self.m4.apply(stage,{p:(ROOT/p).read_bytes() for p in self.m4.INPUTS},self.m2)
            self.assertEqual((stage/'core/src/nr_before_sr.cpp').read_text(),self.old)

    def test_generator_declaration_regression(self):
        code=(ROOT/'scripts/patch-colour-restore.py').read_text(encoding='utf-8')
        compile(code,'patch-colour-restore.py','exec')
        for name in ('patch_feedback','apply'):
            needle='def '+name+'('
            self.assertEqual(code.count(needle),1)
            bad=code.replace(needle,'def '+name+'def '+name+'(',1)
            with self.assertRaises(SyntaxError):compile(bad,'negative-colour3.py','exec')

    def test_exact_candidate_identity(self):
        for name in ('kAddonVersion','kBuildIdentity'):
            self.assertIn(f'constexpr const char *{name} = "{self.m4.VERSION}";',self.text)

if __name__=='__main__':
    p=argparse.ArgumentParser();choice=p.add_mutually_exclusive_group(required=True)
    choice.add_argument('--repo',type=Path);choice.add_argument('--generated',type=Path)
    args,rest=p.parse_known_args();REPO=args.repo;GENERATED=args.generated
    unittest.main(argv=['test_colour_stage.py']+rest)
