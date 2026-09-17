"""Output/input integration guards for RGB recovery and paired HDR preparation.

Default tests use deliberately small wiring fixtures, NOT a generated Core.
--repo verifies actual raw pinned inputs; --generated verifies a real exported
Core after all compatibility adapters. Neither mode is a shader/game test.
"""
from __future__ import annotations
import argparse
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
REPO: Path | None = None
GENERATED: Path | None = None


def load(name: str, file: str):
    spec = importlib.util.spec_from_file_location(name, ROOT/'scripts'/file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


m1 = load('colour_m1_test', 'patch-colour-lab.py')
m2 = load('colour_m2_test', 'patch-colour-restore.py')
shader = (ROOT/'src/nr_colour_restore.hlsli').read_text(encoding='utf-8')
prepare_shader = (ROOT/'src/nr_colour_prepare.hlsli').read_text(encoding='utf-8')


def body(text: str, signature: str) -> str:
    return text[slice(*m2.definition(text, signature))]


def fixture(generated: bool = False) -> str:
    # Only independently inspected literal anchors are represented here.
    # Omitted source is intentionally not used for a C++/HLSL build claim.
    cfg = 'frame_config' if generated else 'g_config'
    dispatch = '''void DispatchCodec() {
    const bool v2_combine = pipeline == g_codec.combine_v2;
    const bool any_combine = pipeline == g_codec.combine || v2_combine;
    const auto probe_policy = NrProbePolicySnapshot();
    ApplyRequestedUnverifiedHdrColor(color_policy, g_config.allow_unverified_hdr_color,
        g_config.color_strength, ActiveTransferStrength(g_config), g_config.color_protection,
        v2_combine && (independent_chroma || low_grid_color)
            ? nr_color_amplification::ChromaConsumer::GainIndependent
            : nr_color_amplification::ChromaConsumer::GainCoupled);
    const float effective_color_strength = color_policy.effective_color_strength;
    const unsigned int base_codec_flags = source_flags |
        (capture_delta ? 8u : 0u);
    ID3D12GraphicsCommandList_Dispatch(
        command_list, (width + 7) / 8,
        ((pipeline==g_codec.restore_edit_horizontal ? model_height : height)+7)/8, 1);
}'''.replace('g_config', cfg)
    if generated:
        dispatch = dispatch.replace('void DispatchCodec() {',
            'void DispatchCodec() {\n    ChainConfig frame_config = g_config;')
    return '''#include "nr_colour_lab_policy.h"
struct ChainConfig { int mode; };
struct NrSlot { unsigned long long last_used_serial; };
void NormalizeConfig(ChainConfig &config) { }
bool SameConfig(const ChainConfig &a, const ChainConfig &b) {
    return a.mode == b.mode;
}
bool SameHistoryConfig(const ChainConfig &a, const ChainConfig &b) {
    return a.mode==b.mode;
}
void LoadConfig() {
    next.color_strength = ReadIniFloat(L"ColorStrength", 1.0f);
}
void SaveConfig() {
    record_write(WriteIniInt(L"ProxyCurve", static_cast<int>(config.proxy_curve)));
}
struct CodecConstants { unsigned passthrough; };
cbuffer Constants : register(b0) { uint Passthrough; };
float3 HdrSourceToModelLinear(float3 source) {
    if ((Passthrough & 256u) != 0u) return RationalPeakShoulder(display);
    return originalHdr;
}
float3 PqSourceToModelLinear(float3 source) {
    if ((Passthrough & 256u) != 0u) return RationalPeakShoulder(display);
    return originalPq;
}
float3 ApplySelectedProxyCurve(float3 sceneLinear) {
    if (ProxyCurve == 0u) { return exponential; }
    return hybrid;
}
void EncodeCS() { ProxyOut[tid.xy]=source; }
void EncodeV2CS() { ProxyOut[tid.xy]=source; }
[numthreads(8, 8, 1)]
void CombineCS() {
    float3 result = lumaOnly;
    if (ColorStrength > 0.000001) { result=legacy; }
    WriteCombined(tid.xy,destinationCoord,float4(result,storedOriginal.a));
}
void CombineV2CS() {
    float3 fullProxy = EncodeProxyLinearV2(source);
    float3 result = lumaOnly;
    bool candidateChromaApplied = false;
    if (ColorStrength > 0.000001) { result=legacy; }
    WriteCombinedV2(tid.xy,destinationCoord,float4(result,storedOriginal.a));
}
''' + dispatch


class RestoreTests(unittest.TestCase):
    def test_raw_and_generated_wiring(self):
        for generated in (False, True):
            original = fixture(generated)
            patched = m2.patch_runtime(original, shader, prepare_shader)
            self.assertEqual(patched.count('// NR_COLOUR_RESTORE_MATH_BEGIN'), 1)
            self.assertEqual(patched.count('ColourLabRgbResult restored ='), 2)
            self.assertEqual(patched.count('L"ColourRestoreMode"'), 2)
            self.assertEqual(patched.count('L"ColourInputCurve"'), 2)
            history=body(patched,'bool SameHistoryConfig(')
            self.assertIn('a.colour_input_curve == b.colour_input_curve',history)
            self.assertNotIn('colour_restore_mode',history)
            self.assertEqual(history.replace('a.colour_input_curve == b.colour_input_curve && ',''),body(original,'bool SameHistoryConfig('))
            for signature in ('void EncodeCS(', 'void EncodeV2CS(',
                              'struct CodecConstants {', 'cbuffer Constants : register(b0) {'):
                self.assertEqual(body(original, signature), body(patched, signature))
            dispatch = body(patched, 'void DispatchCodec(')
            cfg = 'frame_config' if generated else 'g_config'
            self.assertIn(f'{cfg}.colour_restore_mode, any_combine, restore_encoding)', dispatch)
            self.assertIn('slot.colour_restore_encoding.For(slot.last_used_serial)',dispatch)
            self.assertLess(dispatch.index('ID3D12GraphicsCommandList_Dispatch('),dispatch.index('slot.colour_restore_encoding ='))
            self.assertIn('pipeline == g_codec.lanczos_vertical',dispatch)
            self.assertIn('(colour_restore || (v2_combine &&', dispatch)
            if generated:
                self.assertNotIn('g_config.colour_restore_mode', dispatch)
            self.assertIn('else if (ColorStrength > 0.000001)', body(patched, 'void CombineCS('))
            self.assertIn('else if (ColorStrength > 0.000001)', body(patched, 'void CombineV2CS('))
            with self.assertRaises(ValueError):
                m2.patch_runtime(patched, shader, prepare_shader)

    def test_drift_fails_closed(self):
        for anchor in ('    float3 fullProxy = EncodeProxyLinearV2(source);',
                       '    const unsigned int base_codec_flags = source_flags |',
                       '    const auto probe_policy = NrProbePolicySnapshot();'):
            with self.assertRaises(ValueError):
                m2.patch_runtime(fixture().replace(anchor, anchor+'\n'+anchor), shader, prepare_shader)
            with self.assertRaises(ValueError):
                m2.patch_runtime(fixture().replace(anchor, ''), shader, prepare_shader)

    def test_brace_parser_comments_and_strings(self):
        text='void f() { /* } */ const char *s="\\\"}"; // }\n if (x) { y(); } }\nvoid g() {}'
        self.assertEqual(body(text,'void f('), text.split('\nvoid g')[0])
        with self.assertRaises(ValueError): m2.definition('void f() {', 'void f(')

    def test_all_stage_transforms_validate_before_writing(self):
        with tempfile.TemporaryDirectory() as temp:
            stage=Path(temp);(stage/'core/src').mkdir(parents=True)
            source=stage/'core/src/nr_before_sr.cpp';source.write_text(fixture())
            (stage/'core/src/nr_color_policy.inl').write_text('struct NrColorPolicy { };')
            (stage/'core/src/nr_feedback_picture.inl').write_text('missing anchor')
            inputs={'src/nr_colour_restore.hlsli':shader.encode(),
                    'src/nr_colour_prepare.hlsli':prepare_shader.encode(),
                    'src/nr_colour_restore_policy.h':b'policy'}
            with self.assertRaises(ValueError): m2.apply(stage,inputs)
            self.assertEqual(source.read_text(),fixture())
            self.assertFalse((stage/'core/src/nr_colour_restore_policy.h').exists())

    def test_ui_transaction_and_fallback_feedback(self):
        ui=(ROOT/'src/nr_colour_lab_ui.inl').read_text(encoding='utf-8')
        self.assertIn('next.colour_restore_mode = recovery;',ui)
        self.assertIn('next.colour_restore_mode = 0;',ui)
        self.assertIn('sample.policy.recorded_restore_mode',ui)
        self.assertIn('sample.status != NrColorPolicyStatus::Current',ui)
        self.assertIn('F8',ui)
        self.assertNotIn('ApplyConfig(',ui)
        self.assertNotIn('WritePrivateProfile',ui)
        text='    if (!valid) return; // A busy snapshot must not report constructor defaults as facts.'
        feedback=m2.patch_feedback(text)
        self.assertLess(feedback.index('if (!valid) return'),feedback.index('requested.colourRestoreMode'))
        self.assertIn('config.colour_restore_mode',feedback)
        self.assertNotIn('g_config',feedback)

    def test_input_curve_is_explicit_and_hdr_only(self):
        policy=(ROOT/'src/nr_colour_restore_policy.h').read_text()
        self.assertIn('kDefaultInputCurve = 0',policy)
        self.assertIn('source_flags & (1u | 16u | 32u)',policy)
        self.assertIn('Passthrough & (1u | 16u | 32u)',prepare_shader)
        patched=m2.patch_runtime(fixture(),shader,prepare_shader)
        for signature in ('float3 HdrSourceToModelLinear(', 'float3 PqSourceToModelLinear(', 'float3 ApplySelectedProxyCurve('):
            self.assertIn('ColourLabN2Requested()',body(patched,signature))

    def test_no_new_gpu_binding_or_temporal_state(self):
        self.assertNotIn('Texture2D',shader)
        self.assertNotIn('RWTexture',shader)
        self.assertNotIn('Interlocked',shader)
        self.assertNotIn('History',shader)
        self.assertIn('if (!ColourLabPostEncoding()) return EncodeProxyLinearV2(source);',shader)
        self.assertIn('PqSourceToModelLinear(storedOriginal)',shader)
        self.assertIn('HdrSourceToModelLinear(storedOriginal)',shader)

    @unittest.skipUnless(REPO, 'supply --repo to inspect real raw repository files')
    def test_actual_raw_source(self):
        original=(REPO/'src/nr_before_sr.cpp').read_text(encoding='utf-8')
        patched=m2.patch_runtime(m1.patch_runtime(original),shader,prepare_shader)
        self.assertEqual(patched.count('ColourLabRgbResult restored ='),2)
        for signature in ('void EncodeCS(', 'void EncodeV2CS(',
                          'struct CodecConstants {', 'cbuffer Constants : register(b0) {',
                          'float3 SampleProxyLinearV2(', 'float3 SampleNeuralLinearV2(',
                          'float4 LoadFinalNeuralStoredV2('):
            self.assertEqual(body(original,signature),body(patched,signature))
        policy=(REPO/'src/nr_color_policy.inl').read_text(encoding='utf-8')
        self.assertIn('recorded_restore_mode',m2.patch_policy(m1.patch_policy(policy)))
        feedback=(REPO/'src/nr_feedback_picture.inl').read_text(encoding='utf-8')
        self.assertIn('requested.colourRestoreMode',m2.patch_feedback(feedback))

    @unittest.skipUnless(GENERATED, 'supply --generated to inspect a real completed export')
    def test_actual_generated_source(self):
        text=(GENERATED/'src/nr_before_sr.cpp').read_text(encoding='utf-8')
        self.assertEqual(text.count('ColourLabRgbResult restored ='),2)
        self.assertIn(shader,text)
        for name in ('kAddonVersion','kBuildIdentity'):
            self.assertIn(f'constexpr const char *{name} = "beta0.5-dline21-colour2";',text)
        dispatch=body(text,'void DispatchCodec(')
        self.assertIn('ChainConfig frame_config = g_config;',dispatch)
        self.assertIn('frame_config.colour_restore_mode',dispatch)
        self.assertNotIn('g_config.colour_restore_mode',dispatch)
        self.assertNotIn('colour_restore_mode',body(text,'bool SameHistoryConfig('))
        self.assertIn('a.colour_input_curve == b.colour_input_curve',body(text,'bool SameHistoryConfig('))
        self.assertIn(prepare_shader,text)
        self.assertNotIn('ColourLab',body(text,'void EncodeCS('))
        self.assertNotIn('ColourLab',body(text,'void EncodeV2CS('))
        self.assertIn('kCodecRootConstantCount = 60;',text)
        self.assertIn('ColourLabRgbEnabled()',body(text,'void CombineCS('))
        self.assertIn('ColourLabRgbEnabled()',body(text,'void CombineV2CS('))
        self.assertIn('storedOriginal.a',body(text,'void CombineCS('))
        self.assertIn('storedOriginal.a',body(text,'void CombineV2CS('))
        self.assertTrue((GENERATED/'src/nr_colour_restore_policy.h').is_file())


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--repo',type=Path)
    parser.add_argument('--generated',type=Path)
    args,rest=parser.parse_known_args();REPO=args.repo;GENERATED=args.generated
    RestoreTests.test_actual_raw_source.__unittest_skip__=not bool(REPO)
    RestoreTests.test_actual_generated_source.__unittest_skip__=not bool(GENERATED)
    unittest.main(argv=['test_colour_restore.py']+rest)
