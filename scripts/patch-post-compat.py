#!/usr/bin/env python3
"""Actual SR/RR post-NR and scaled-work wiring on the inspected 0.4.5 core.

This transformation runs after the DX11 and format transformations. No source
checkout is overwritten; the parent staging transaction publishes only after
all exact anchors and manifest checks pass.
"""
from __future__ import annotations
import importlib.util
import re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sp=importlib.util.spec_from_file_location('format_parser',ROOT/'scripts/patch-format-access.py')
base=importlib.util.module_from_spec(sp);sp.loader.exec_module(base)
once,block,edit=base.once,base.block,base.edit_block

def replace_definition(text, signature, fn):
    return edit(text,signature,fn)

def patch_core(text: str) -> str:
    if '#include "interop/nr_post_compat.h"' not in text:
        text=once(text,'#include "MinHook.h"','#include "MinHook.h"\n#include "interop/nr_post_compat.h"')
    if '#include "interop/nr_post_settings.inl"' not in text:
        text=once(text,'struct NrApi {','#include "interop/nr_post_settings.inl"\n\nstruct NrApi {')
    if '    nr_post_settings::Poll();' not in text:
        text=once(text,'    PollNgxLifecycleModules(false);','    PollNgxLifecycleModules(false);\n    nr_post_settings::Poll();')
    text=once(text,'#include "nr_settings_ui.inl"','#include "interop/nr_post_ui.inl"\n#include "nr_settings_ui.inl"')
    # DLSSD depth interpretation is declared at Create, not inferred from R32.
    text=edit(text,'struct FeatureRecord {',lambda s:once(s,'    int create_flags = 0;',
        '    int create_flags = 0;\n    int rr_depth_type = -1; // 0 linear, 1 hardware'))
    text=edit(text,'void RememberFeature(',lambda s:once(s,
        '    GetInt(params, "DLSS.Feature.Create.Flags", &record.create_flags);',
        '    GetInt(params, "DLSS.Feature.Create.Flags", &record.create_flags);\n'
        '    if (feature_id == 13) {\n'
        '        unsigned int depth_type = 0;\n'
        '        if (GetUInt(params, "DLSS.Use.HW.Depth", &depth_type))\n'
        '            record.rr_depth_type = static_cast<int>(depth_type);\n'
        '        else GetInt(params, "DLSS.Use.HW.Depth", &record.rr_depth_type);\n'
        '    }'))
    # RR histories have real generations too, without relabeling their feature.
    text=edit(text,'void RememberFeature(',lambda s:once(s,'    if (feature_id == 1) {','    if (feature_id == 1 || feature_id == 13) {'))
    # Feature 13 now owns an NrContext as well. Preserve the original successful
    # native-release gate and fence-delayed retirement; failed Release retains it.
    for release in ('NgxResult HookRelease(', 'NgxResult HookCoreRelease(', 'NgxResult HookDlssdRelease('):
        text=edit(text,release,lambda s:once(s,
            '        if (feature.feature_id == 1) {',
            '        if (feature.feature_id == 1 || feature.feature_id == 13) {')
            .replace('// Only Feature 1 owns an NrContext. Auxiliary handle classification is',
                     '// Feature 1 and post-RR Feature 13 own NrContexts. Auxiliary classification is'))

    # Model texture allocations follow N, while the native output snapshot must
    # still fit O. It is copied to origin 0, separately from the game's atlas.
    def create(s):
        return once(s,'if (!CreateTexture(device, width, height, L"NRChain.SRIntermediate",',
            'if (external_desc.Width > UINT_MAX || !CreateTexture(device,\n'
            '                           static_cast<unsigned int>(external_desc.Width),\n'
            '                           external_desc.Height, L"NRChain.SRIntermediate",')
    text=edit(text,'bool CreateSlot(',create)

    def rebind(s):
        anchor='''        ID3D12Resource_AddRef(external);
        ReleaseCom(slot.result);'''
        replacement='''        // Rotating post targets may change physical O while N remains in one
        // model bucket. Rebuild only an already-retired slot's snapshot.
        const auto old_snapshot = ID3D12Resource_GetDesc(slot.source);
        ID3D12Resource *new_snapshot = nullptr;
        if (old_snapshot.Width != external_desc.Width || old_snapshot.Height != external_desc.Height) {
            if (external_desc.Width > UINT_MAX || !CreateTexture(device,
                    static_cast<unsigned int>(external_desc.Width),external_desc.Height,
                    L"NRChain.SRIntermediate",&new_snapshot)) return false;
        }
        if (new_snapshot) {
            ReleaseCom(slot.source); slot.source = new_snapshot;
            slot.source_state = D3D12_RESOURCE_STATE_COMMON;
            D3D12_SHADER_RESOURCE_VIEW_DESC source_view{};
            source_view.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            source_view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            source_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            source_view.Texture2D.MipLevels = 1;
            ID3D12Device_CreateShaderResourceView(device,slot.source,&source_view,cpu);
        }
        ID3D12Resource_AddRef(external);
        ReleaseCom(slot.result);'''
        s=once(s,anchor,replacement)
        # The snapshot's destination UAV changes when the snapshot is resized.
        return once(s,'''        RebindExposureSource(device, exposure, slot);
        slot.last_submission_queue = nullptr;''','''        if (new_snapshot && slot.snapshot_heap) WritePostSrSnapshotDescriptors(device,slot);
        RebindExposureSource(device, exposure, slot);
        slot.last_submission_queue = nullptr;''')
    text=edit(text,'bool RebindSlotExternal(',rebind)

    text=once(text,'#include "nr_codec_descriptors.inl"',
        '#include "interop/nr_motion_access_d3d12.inl"\n#include "nr_codec_descriptors.inl"')
    def motion_access(s):
        old='''    if ((motion_desc.Format != DXGI_FORMAT_R16G16_FLOAT &&
         motion_desc.Format != DXGI_FORMAT_R32G32_FLOAT &&
         motion_desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT) ||
        (motion_desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0) {'''
        s=once(s,old,'''    const DXGI_FORMAT motion_view = NrMotionReadFormat(motion_desc.Format);
    if (motion_view == DXGI_FORMAT_UNKNOWN ||
        (motion_desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0 ||
        ((slot.motion_source != motion || slot.motion_proxy == nullptr) &&
         !NrCanReadMotion(device,motion_desc))) {''')
        return once(s,'        srv.Format = motion_desc.Format;',
                       '        srv.Format = motion_view;')
    text=edit(text,'bool EnsureMotionProxy(',motion_access)

    # Dispatch sees effective post mode while the user's requested Mode2 remains
    # immutable. The configuration copy also keeps colour policy/stats coherent.
    def dispatch(s):
        start=s.index('{')
        signature=s[:start]; body=s[start:]
        closing=signature.rfind(')')
        signature=signature[:closing]+', int effective_mode = 0'+signature[closing:]
        body=re.sub(r'\bg_config\b','frame_config',body)
        body=body[:1]+'''
    ChainConfig frame_config = g_config;
    if (effective_mode != 0) frame_config.mode = effective_mode;
'''+body[1:]
        return signature+body
    text=edit(text,'void DispatchCodec(',dispatch)

    # Both front and post can use a smaller model raster. Never crop high-res
    # motion to N, and scale displacement magnitude independently of resampling.
    def guides(s):
        s=once(s,'        if (mode == 2) {','        if (mode == 2 || mode == 1) {')
        s=once(s,'    if (mode == 2 && (s.motion_source_width != width ||',
                   '    if ((mode == 2 || mode == 1) && (s.motion_source_width != width ||')
        s=s.replace('mode == 2 && guide_width != 0','(mode == 2 || mode == 1) && guide_width != 0')
        s=s.replace('mode == 2 && guide_height != 0','(mode == 2 || mode == 1) && guide_height != 0')
        return s
    text=edit(text,'bool ResolveNrGuideBinding(',guides)
    text=edit(text,'struct PostSrSubstitution {',lambda s:once(s,'    unsigned int height = 0;',
        '    unsigned int height = 0;\n    unsigned int work_width = 0, work_height = 0;'))

    def prepare(s):
        s=once(s,'!((g_config.mode == 1 || (compatibility_fallback && g_config.mode == 2)) && feature.feature_id == 1)',
            '!(nr_post_compat::IsPost(feature.feature_id, g_config.mode, nr_post_settings::Read().rr) || (compatibility_fallback && g_config.mode == 2 && feature.feature_id == 1))')
        s=once(s,'    ID3D12Resource *output = nullptr;',
            '    if (feature.feature_id == 13 && feature.rr_depth_type != 1 && feature.rr_depth_type != 0) {\n'
            '        static unsigned depth_declines = 0;\n'
            '        if (++depth_declines <= 3) Log("RR post depth type=%d has an unknown depth declaration; native RR retained. F8 TXT for BUG feedback.", feature.rr_depth_type);\n'
            '        SetBypassReason(BypassReason::RayReconstructionGuard);\n'
            '        return substitution;\n'
            '    }\n'
            '    ID3D12Resource *output = nullptr;')
        s=once(s,'    NrGuideBinding guides = {};','''    const auto requested_work = nr_post_compat::Work({width,height},nr_post_settings::Read().percent);
    unsigned int work_width = requested_work.work.w, work_height = requested_work.work.h;
    if (!work_width || !work_height) { ReleaseCom(device); return substitution; }
    if (requested_work.scaled)
        SelectAlignedWorkExtent(work_width,work_height,width,height,work_width,work_height);
    NrGuideBinding guides = {};''')
        s=once(s,'ResolveNrGuideBinding(sr_params, feature, 1, width, height,',
                 'ResolveNrGuideBinding(sr_params, feature, 1, work_width, work_height,')
        s=once(s,'''    NrContext *context = FindOrCreateContext(
        sr_handle, feature.generation, 1, width, height,''','''    // SR/RR colour is already reconstructed. Preserve the captured native
    // input jitter for diagnostics; the new colour grid has zero sample jitter.
    // Native depth remains on its declared R grid. Motion carries its explicit
    // native convention; phase-sensitive games still require runtime checks.
    guides.jitter.known = true;
    guides.jitter.x = guides.jitter.y = 0.0f;
    guides.jitter.source_x = guides.jitter.source_y = 0.0f;
    guides.jitter.valid_x = guides.jitter.valid_y = true;
    guides.jitter.source_width = width; guides.jitter.source_height = height;
    guides.jitter.scale_x = static_cast<float>(work_width)/width;
    guides.jitter.scale_y = static_cast<float>(work_height)/height;
    guides.jitter_offset_x = guides.jitter_offset_y = 0.0f;
    NrContext *context = FindOrCreateContext(
        sr_handle, feature.generation, 1, work_width, work_height,''')
        s=once(s,'    ArmDeltaStatsCaptureLocked(*slot, *context, color_contract);','''    if (guides.signature.resample_motion &&
        !EnsureMotionProxy(device,*slot,guides.motion,work_width,work_height)) {
        ReleaseCom(device); return substitution;
    }
    ArmDeltaStatsCaptureLocked(*slot, *context, color_contract);''')
        s=once(s,'    substitution.height = height;',
            '    substitution.height = height;\n    substitution.work_width = work_width;\n    substitution.work_height = work_height;')
        return s
    text=edit(text,'PostSrSubstitution PrepareAfterSr(',prepare)

    def finish(s):
        s=once(s,'    const unsigned int height = substitution.height;',
            '    const unsigned int height = substitution.height;\n'
            '    const unsigned int work_width = substitution.work_width;\n'
            '    const unsigned int work_height = substitution.work_height;')
        s=once(s,'    Transition(command_list, slot.source, slot.source_state,','''    if (substitution.guides.signature.resample_motion) {
        const auto &motion = substitution.guides.signature;
        Transition(command_list,slot.motion_proxy,slot.motion_proxy_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        DispatchMotionResample(command_list,slot,work_width,work_height,
            motion.motion_source_width,motion.motion_source_height,motion.motion_x,motion.motion_y);
        UavBarrier(command_list,slot.motion_proxy);
        Transition(command_list,slot.motion_proxy,slot.motion_proxy_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    Transition(command_list, slot.source, slot.source_state,''')
        s=once(s,'''    DispatchCodec(command_list, slot, g_codec.encode, width, height,
                  color_contract, (feature.create_flags & 1) != 0,
                  0, 0, 0, 0, slot.codec_color_mode);''','''    DispatchCodec(command_list, slot, g_codec.encode, work_width, work_height,
                  color_contract, (feature.create_flags & 1) != 0,
                  0, 0, 0, 0, slot.codec_color_mode, width, height,
                  work_width, work_height, 1.0f, 1);''')
        s=once(s,'width, height, color_contract, first)) {',
            'work_width, work_height, color_contract, first, slot.motion_proxy)) {')
        s=once(s,'            width, height, substitution.guide_width, substitution.guide_height,',
            '            work_width, work_height, substitution.guide_width, substitution.guide_height,')
        s=once(s,'g_last_nr_width.store(width,','g_last_nr_width.store(work_width,')
        s=once(s,'g_last_nr_height.store(height,','g_last_nr_height.store(work_height,')
        s=once(s,'g_last_nr_linear_scale.store(1.0f,',
            'g_last_nr_linear_scale.store(static_cast<float>(std::sqrt(\n'
            '        double(work_width)*work_height/(double(width)*height))),')
        s=once(s,'substitution.output_base_y, slot.codec_color_mode);',
            'substitution.output_base_y, slot.codec_color_mode, width, height,\n'
            '                   work_width, work_height, 1.0f, 1);')
        # Optional edit-field construction uses the effective post colour
        # policy even when the requested mode was pre-SR with RR fallback.
        s=s.replace('slot.codec_color_mode,width,height,context.width,context.height);',
                    'slot.codec_color_mode,width,height,context.width,context.height,1.0f,1);')
        return s
    text=edit(text,'bool FinishAfterSr(',finish)

    # Existing shader and root signature are reused. No additional NR invocation.
    helper=(ROOT/'src/interop/nr_post_area.hlsl').read_text(encoding='utf-8')
    text=once(text,'[numthreads(8, 8, 1)]\nvoid EncodeCS(',helper+'\n[numthreads(8, 8, 1)]\nvoid EncodeCS(')
    text=edit(text,'void EncodeCS(',lambda s:once(s,
        '    float4 source = OriginalColor.Load(int3(sourceCoord, 0));',
        '    float4 source = (SourceWidth == Width && SourceHeight == Height)\n'
        '        ? OriginalColor.Load(int3(sourceCoord, 0)) : LoadPostArea(tid.xy);'))
    def combine(s):
        s=once(s,'    float3 storedProxy = ProxyColor.Load(int3(tid.xy, 0)).rgb;',
            '    const bool scaled = ModelWidth != Width || ModelHeight != Height;\n'
            '    float3 storedProxy = scaled ? SampleProxyStoredV2(float2(tid.xy)+0.5)\n'
            '        : ProxyColor.Load(int3(tid.xy, 0)).rgb;')
        s=once(s,'    float3 storedNeural = NeuralColor.Load(int3(tid.xy, 0)).rgb;',
            '    float3 storedNeural = scaled ? SampleNeuralStoredV2(float2(tid.xy)+0.5)\n'
            '        : NeuralColor.Load(int3(tid.xy, 0)).rgb;')
        return once(s,'    bool finiteInputs = all(storedOriginal.rgb == storedOriginal.rgb) &&',
            '    bool finiteInputs = (!scaled || ModelNeighborhoodFiniteV2(float2(tid.xy)+0.5)) &&\n'
            '        all(storedOriginal.rgb == storedOriginal.rgb) &&')
    text=edit(text,'void CombineCS(',combine)
    def capture(s):
        s=once(s,'asuint(ProxyColor.Load(int3(coord, 0))));',
            'asuint((ModelWidth == Width && ModelHeight == Height)\n'
            '            ? ProxyColor.Load(int3(coord, 0))\n'
            '            : float4(SampleProxyStoredV2(float2(coord)+0.5),1.0)));')
        return once(s,'asuint(NeuralColor.Load(int3(coord, 0))));',
            'asuint((ModelWidth == Width && ModelHeight == Height)\n'
            '            ? NeuralColor.Load(int3(coord, 0))\n'
            '            : float4(SampleNeuralStoredV2(float2(coord)+0.5),1.0)));')
    text=edit(text,'void WriteCombined(',capture)

    text=once(text,'NgxResult ProcessEvaluate(',
        '#include "interop/nr_rr_depth_evidence.inl"\n#include "interop/nr_rr_post_runtime.inl"\n\nNgxResult ProcessEvaluate(')
    def process(s):
        # Replace only the exact old RR branch; all Feature1/RR exclusion and
        # resource/queue checks below it remain unchanged.
        a=s.index('    if (feature.feature_id == 13) {')
        b=s.index('    if (!IsUpscalingCarrierFeature(feature.feature_id)',a)
        old=s[a:b]
        if old.count('return original(command_list, handle, params, callback);')!=1:
            raise ValueError('RR pass-through branch changed')
        return s[:a]+'''    if (feature.feature_id == 13) {
        return ProcessRrPost(original,command_list,handle,params,callback,feature);
    }
'''+s[b:]
    text=edit(text,'NgxResult ProcessEvaluate(',process)
    return edit(text,'void DrawTechnicalDiagnostics(',patch_diagnostics)


def patch_diagnostics(text: str) -> str:
    # The original panel treats any historical RR handle as permanent bypass.
    # Only an actual guard reason means that in the post-compatible runtime.
    text=once(text,'    } else if (rr_protected ||\n               bypass_reason == BypassReason::RayReconstructionGuard) {',
        '    } else if (bypass_reason == BypassReason::RayReconstructionGuard) {')
    text=once(text,'    } else if (rr_protected) {',
        '    } else if (bypass_reason == BypassReason::RayReconstructionGuard) {')
    start=text.index('        ImGui::Text(ui("Perfect chain:')
    end=text.index('\n    if (generic_renodx_conflict) {',start)
    text=text[:start]+'''        ImGui::Text(ui("Historical NR raster: %ux%u | reference grid %ux%u | %.0f%%",
                       "历史 NR 尺寸：%u x %u | 参考网格 %u x %u | %.0f%%"),
                    width, height, nr_render_width, nr_render_height,
                    static_cast<double>(nr_linear_scale * 100.0f));
        ImGui::TextDisabled("%s", ui(
            "Last successful NR dimensions; may precede current mode/settings. Game R/O above is a separate observation.",
            "最后一次 NR 成功时的尺寸，可能早于当前模式或设置；上方游戏 R/O 是独立观察。"));
    }
    if (scoped_mode == 1 || scoped_mode == 2) {
        ImGui::Text("%s", scoped_mode == 1 ? ui(
            "Observed scope: native SR/RR output -> NR -> output composition",
            "已观察统计域：原生 SR/RR 输出 -> NR -> 输出合成") : ui(
            "Observed scope: NR -> native SR -> output",
            "已观察统计域：NR -> 原生 SR -> 输出"));
        ImGui::Text(ui("Scope NR success / recorded / submitted: %llu / %llu / %llu",
                       "统计域 NR 成功 / 录制 / 提交：%llu / %llu / %llu"),
                    scoped_success, scoped_recorded, scoped_submitted);
        ImGui::TextDisabled("%s", ui(
            "Counters belong to the displayed historical scope. Recording/submission is not proof of GPU completion or visible effect.",
            "计数属于显示的历史统计域；录制或提交不代表 GPU 完成或可见效果。"));
    }
    if (carrier_feature == 13 || rr_protected) {
        ImGui::Text("%s", ui(
            "Observed carrier/ownership: Ray Reconstruction (Feature 13); see post attempts and actual bypass reason.",
            "已观察载体或所有权：光线重构（Feature 13）；请结合后置尝试与实际旁路原因。"));
    } else if (carrier_feature == 1) {
        ImGui::Text("%s", ui("Last observed carrier: DLSS Super Resolution (Feature 1)",
                              "最后观察到的载体：DLSS 超分（Feature 1）"));
    } else {
        ImGui::TextDisabled("%s", ui("No identified SR/RR carrier observed yet",
                                      "尚未观察到可识别的 SR/RR 载体"));
    }
    if (live_rr_handles != 0) {
        ImGui::TextDisabled(ui("Live RR handles: %u; ownership alone does not establish post-NR activity.",
                              "存活 RR 句柄：%u 个；所有权本身不代表后置 NR 已生效。"), live_rr_handles);
    }'''+text[end:]
    return text

def patch_ui(text: str) -> str:
    # The refreshed base UI already owns the effective-route slider and RR
    # protection presentation. Keep generation idempotent instead of layering
    # the historical post placeholder transform over those controls.
    refreshed = '    const bool rr_guarded = IsRayReconstructionRouteObserved();'
    if refreshed in text:
        for required in (
                '"##nr-processing-precision", &precision, 50.0f, 100.0f',
                'DrawSimpleFeedbackControls();',
                'nr_f8::TakeSettingsFeedbackPageRequest()',
                '"Feedback and diagnostics###nr-feedback-diagnostics"',
                '"Developer options", "开发者选项"'):
            if text.count(required) != 1:
                raise PatchError(f'refreshed UI contract missing or duplicated: {required}')
        for removed in ('"Experimental###nr-experimental"',
                        '"Diagnostics###nr-diagnostics"',
                        'NR_HAS_F8_SEQUENCE'):
            if removed in text:
                raise PatchError(f'refreshed UI retains removed tab: {removed}')
        return text

    # Preserve #134's actual main-panel model controls and front precision
    # logic. Replace only its unimplemented post placeholder, in place.
    text=once(text,'    const bool before_sr = next.mode == 2;',
        '    const bool before_sr = next.mode == 2;\n'
        '    const bool effective_post = !before_sr ||\n'
        '        ShouldGuardFeature1ForRayReconstruction();\n'
        '    if (effective_post) {\n'
        '        DrawPostResolutionControl();\n'
        '    } else {')
    text=once(text,'    if (!before_sr) ImGui::BeginDisabled();\n','')
    text=once(text,'''    if (!before_sr) {
        ImGui::EndDisabled();
        ImGui::TextWrapped("%s", ui(
            "Post-SR still uses the full SR output. Reduced post-SR precision is not implemented in this build; the disabled value is your saved pre-SR request, not an applied post-SR scale.",
            "后置仍使用完整 SR 输出。本版尚未实现低精度后置；灰色数值仅为保存的前置偏好，不代表后置实际比例。"));
    } else {''','    {')
    text=once(text,'#endif\n    int style = static_cast<int>(next.style);',
        '    } // requested front precision is kept separately from effective post precision\n'
        '#endif\n    int style = static_cast<int>(next.style);')
    return once(text,'    if (ImGui::CollapsingHeader(ui("Advanced picture controls", "高级画质"))) {',
        '    DrawPostCompatibility();\n    if (ImGui::CollapsingHeader(ui("Advanced picture controls", "高级画质"))) {')


def patch_descriptors(text: str) -> str:
    # Main/snapshot/F7 descriptor rebuilds must retain the same typed view as
    # the first dispatch; physical TYPELESS is never a legal texture SRV.
    text=once(text,'srv(0,slot.source,slot.source_format);',
        'srv(0,slot.source,slot.source_srv_format);')
    return once(text,
        'slot.motion_source?ID3D12Resource_GetDesc(slot.motion_source).Format:DXGI_FORMAT_R16G16_FLOAT',
        'slot.motion_source?NrMotionReadFormat(ID3D12Resource_GetDesc(slot.motion_source).Format):DXGI_FORMAT_R16G16_FLOAT')
