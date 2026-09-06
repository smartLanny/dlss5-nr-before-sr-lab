"""Anchor/transaction tests; synthetic excerpts do NOT prove a full core build.
The optional real-source test is mandatory in the Windows workflow.
"""
import importlib.util
import os
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('post_patch',ROOT/'scripts/patch-post-compat.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
FIXTURE='''#include "MinHook.h"
struct FeatureRecord {
    int create_flags = 0;
};
void RememberFeature() {
    if (feature_id == 1) { record.generation = next(); }
    GetInt(params, "DLSS.Feature.Create.Flags", &record.create_flags);
}
struct NrApi { int unused; };
void Poll() {
    PollNgxLifecycleModules(false);
}
#include "nr_settings_ui.inl"
#include "nr_codec_descriptors.inl"
bool CreateSlot() {
    if (!CreateTexture(device, width, height, L"NRChain.SRIntermediate",
                           &slot.source)) { return false; }
}
bool RebindSlotExternal() {
    if (post_sr) {
        ID3D12Resource_AddRef(external);
        ReleaseCom(slot.result);
        RebindExposureSource(device, exposure, slot);
        slot.last_submission_queue = nullptr;
        return true;
    }
}
bool EnsureMotionProxy() {
    if ((motion_desc.Format != DXGI_FORMAT_R16G16_FLOAT &&
         motion_desc.Format != DXGI_FORMAT_R32G32_FLOAT &&
         motion_desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT) ||
        (motion_desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0) {
        return false;
    }
    if (new_source) {
        srv.Format = motion_desc.Format;
    }
}
void DispatchCodec(int x, float scale = 1.0f) {
    use(g_config.mode,g_config.post_transfer_strength);
    g_config_generation.load();
}
bool ResolveNrGuideBinding() {
    if (high) {
        if (mode == 2) { resolve_output(); }
    }
    if (mode == 2 && (s.motion_source_width != width ||
                       s.motion_source_height != height))
        s.resample_motion = true;
    x = mode == 2 && guide_width != 0;
    y = mode == 2 && guide_height != 0;
}
struct PostSrSubstitution {
    unsigned int height = 0;
};
PostSrSubstitution PrepareAfterSr() {
    if (!g_config.enabled || g_config.mode != 1 || feature.feature_id != 1 ||
        command_list == nullptr || sr_params == nullptr) return substitution;
    ID3D12Resource *output = nullptr;
    NrGuideBinding guides = {};
    if (!ResolveNrGuideBinding(sr_params, feature, 1, width, height,
         guide_width, guide_height, output)) { return substitution; }
    NrContext *context = FindOrCreateContext(
        sr_handle, feature.generation, 1, width, height,
        post_sr_color_mode);
    ArmDeltaStatsCaptureLocked(*slot, *context, color_contract);
    substitution.height = height;
    return substitution;
}
bool FinishAfterSr() {
    const unsigned int height = substitution.height;
    Transition(command_list, slot.source, slot.source_state,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    DispatchCodec(command_list, slot, g_codec.encode, width, height,
                  color_contract, (feature.create_flags & 1) != 0,
                  0, 0, 0, 0, slot.codec_color_mode);
    if (!BindNrParameters(context,slot,guides,feature,
        width, height, color_contract, first)) { return false; }
    Log("model",
            width, height, substitution.guide_width, substitution.guide_height,
            other);
    g_last_nr_width.store(width, relaxed);
    g_last_nr_height.store(height, relaxed);
    g_last_nr_linear_scale.store(1.0f, relaxed);
    DispatchCodec(command_list, slot, g_codec.combine,
                  width, height, color_contract, false,0,0,0,
                  substitution.output_base_y, slot.codec_color_mode);
    return true;
}
const char kCodecShader[] = R"HLSL(
[numthreads(8, 8, 1)]
void EncodeCS(uint3 tid : SV_DispatchThreadID) {
    float4 source = OriginalColor.Load(int3(sourceCoord, 0));
}
void CombineCS(uint3 tid : SV_DispatchThreadID) {
    float3 storedProxy = ProxyColor.Load(int3(tid.xy, 0)).rgb;
    float3 storedNeural = NeuralColor.Load(int3(tid.xy, 0)).rgb;
    bool finiteInputs = all(storedOriginal.rgb == storedOriginal.rgb) && other;
}
void WriteCombined(uint2 coord) {
    StageCapture.Store4(a,
        asuint(ProxyColor.Load(int3(coord, 0))));
    StageCapture.Store4(b,
        asuint(NeuralColor.Load(int3(coord, 0))));
}
)HLSL";
NgxResult ProcessEvaluate() {
    if (feature.feature_id == 13) {
        passthrough_count();
        return original(command_list, handle, params, callback);
    }
    if (!IsUpscalingCarrierFeature(feature.feature_id)) return original();
    if (ShouldGuardFeature1ForRayReconstruction()) return original();
}
'''
class SourcePatch(unittest.TestCase):
    def test_minimal_composition(self):
        out=m.patch_core(FIXTURE)
        self.assertIn('ProcessRrPost(original,command_list,handle,params,callback,feature)',out)
        self.assertIn('ShouldGuardFeature1ForRayReconstruction()',out)
        self.assertIn('if (feature_id == 1 || feature_id == 13)',out)
        self.assertIn('feature.rr_depth_type != 1',out)
        self.assertIn('frame_config.mode = effective_mode',out)
        self.assertIn('g_config_generation.load()',out)
        self.assertNotIn('frame_config_generation',out)
        self.assertIn('work_width, work_height, color_contract, first, slot.motion_proxy',out)
        self.assertIn('guides.jitter.x = guides.jitter.y = 0.0f',out)
        self.assertEqual(out.count('float4 LoadPostArea'),1)
        self.assertIn('SampleProxyStoredV2',out)
        self.assertIn('NrCanReadMotion(device,motion_desc)',out)
    def test_duplicate_or_drift_refused(self):
        out=m.patch_core(FIXTURE)
        with self.assertRaises(ValueError):m.patch_core(out)
        for old in ('    if (feature_id == 1) {','    srv.Format = motion_desc.Format;',
                    '    const unsigned int height = substitution.height;'):
            with self.assertRaises(ValueError):m.patch_core(FIXTURE.replace(old,'// changed'))
    def test_descriptor_rebuild_keeps_typed_views(self):
        src=('srv(0,slot.source,slot.source_format);\n'
             'srv(3,slot.motion_source,slot.motion_source?ID3D12Resource_GetDesc(slot.motion_source).Format:DXGI_FORMAT_R16G16_FLOAT);')
        result=m.patch_descriptors(src)
        self.assertIn('slot.source_srv_format',result)
        self.assertIn('NrMotionReadFormat',result)
        with self.assertRaises(ValueError):m.patch_descriptors(result)
    def test_ui_is_real_control_not_global_mode_write(self):
        ui=(ROOT/'src/interop/nr_post_ui.inl').read_text(encoding='utf-8')
        self.assertIn('nr_post_settings::Request',ui)
        self.assertIn('100,75,67,50',ui)
        self.assertNotIn('g_config.mode =',ui)
        self.assertIn('No recent RR-post attempt',ui)
    def test_settings_do_not_enable_before_reading_ini(self):
        s=(ROOT/'src/interop/nr_post_settings.inl').read_text()
        self.assertIn('std::atomic<bool> ready{false}',s)
        self.assertIn('observed_revision==revision.load',s)
        self.assertIn('nr_f8::g_config_hold',s)
    @unittest.skipUnless(os.environ.get('POST_COMPAT_CORE_SOURCE'),'requires actual complete core; minimal fixture is not full integration')
    def test_actual_core(self):
        source=Path(os.environ['POST_COMPAT_CORE_SOURCE']).read_text(encoding='utf-8')
        out=m.patch_core(source)
        for fn in ('Substitution PrepareBeforeSr(','bool DispatchPostSrNativeSnapshot('):
            a,b=m.block(source,fn);x,y=m.block(out,fn)
            self.assertEqual(source[a:b],out[x:y])
        self.assertEqual(out.count('const unsigned int work_width = substitution.work_width;'),1)
        self.assertIn('int effective_mode = 0',out)
if __name__=='__main__':unittest.main()
