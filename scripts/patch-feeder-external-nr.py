#!/usr/bin/env python3
"""Stage jlrouzies-fr/DLSS5-Feeder D3D12 FeedFrame12 onto the external NR ABI.

Read-only source tree is never modified. The transformer copies into an output
stage and patches production so Feeder:
  1) verifies the pinned git commit,
  2) claims NRExternalProvider_* and skips private NGX Init/Create/Evaluate,
  3) submits color/depth/MV/exposure with monotonic frame ids,
  4) EndCommands-trains core queue affinity on starter declines (no AbortCommands),
  5) never runs a second NR owner when ownership is claimed.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

SUPPORTED_RELEASES = {
    'v0.15.1': '3f624855276c4bde55145c712782477639b30e85',
    'v1.16.0-beta.1': '55c5bca0117b04870a0def76fc0d96e87605e0be',
}
FEED_CPP = 'src/dlss5-feed.cpp'

INCLUDES_ANCHOR = '#include "feed_ngx.h"  // NGX result names and DLL identity, shared with host64\n'

PRODUCER_INCLUDES = (
    '#include "feed_ngx.h"  // NGX result names and DLL identity, shared with host64\n'
    '// MC1: optional external NR consumer (this lab). Headers are staged beside\n'
    '// the Feeder tree; when absent at compile time the legacy evaluate remains.\n'
    '#if 1 // staged provider build: missing headers must fail compilation\n'
    '#include "nr_external_provider_producer.h"\n'
    '#include "nr_external_provider_feeder_map.h"\n'
    '#include "backend.hpp" // staged from experiments/fullres_guides\n'
    '#define NR_FEED_EXTERNAL_PROVIDER 1\n'
    '#endif\n'
)

# Skip private NGX Init when the lab consumer already owns NR.
INIT_NGX_ANCHOR = (
    '    Breadcrumb("initialising NGX on the game\'s device");\n'
    '    FeedSetNgxProvenance("same-device D3D12", "none -- this is the game\'s own device, not one we created");\n'
    '    DWORD ngx_code = 0;\n'
    '    NVSDK_NGX_Result r = SafeNgxInit12(data_path, g.dev12, &ngx_code);\n'
)

INIT_NGX_REPLACEMENT = r'''#if defined(NR_FEED_EXTERNAL_PROVIDER)
    {
        nr_external_provider_client::Claim(kNrExternalProviderIdDl5f);
    }
    if (nr_external_provider_client::ShouldDisableFeederNr())
    {
        Breadcrumb("skipping private NGX init (external NR consumer owns Feature18)");
        Log("[feed] provider-only session: private NGX Init/Create/Evaluate skipped");
        g.ngx_inited = false;
        g.params = nullptr;
        g.feature = nullptr;
    }
    else
#endif
    {
    Breadcrumb("initialising NGX on the game's device");
    FeedSetNgxProvenance("same-device D3D12", "none -- this is the game's own device, not one we created");
    DWORD ngx_code = 0;
    NVSDK_NGX_Result r = SafeNgxInit12(data_path, g.dev12, &ngx_code);
'''

# Close the else-block after AllocateParameters success path reaches allocators.
# Anchor: first allocator creation after params allocated.
ALLOC_ANCHOR = (
    '    // Our own allocators + list on the game\'s device; submission goes to the game\'s queue.\n'
    '    for (int i = 0; i < Feed::kFrames; ++i)\n'
)

MAKE_TEX_FLAGS_ANCHOR = (
    '    rd.Flags            = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;\n'
)
MAKE_TEX_FLAGS_REPLACEMENT = (
    '    rd.Flags            = (uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE) |\n'
    '                          (i == SLOT_OUTPUT ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAG_NONE);\n'
)
PQ_OUTPUT_FLAGS_ANCHOR = (
    '    // Output: DLSS writes it through a UAV, then our encode reads it.\n'
    '    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;\n'
)
PQ_OUTPUT_FLAGS_REPLACEMENT = (
    '    // Output: Core V1 writes it through an RTV; the PQ encode reads it.\n'
    '    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |\n'
    '               D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;\n'
)

ALLOC_REPLACEMENT = (
    '#if defined(NR_FEED_EXTERNAL_PROVIDER)\n'
    '    } // end private NGX init else\n'
    '#endif\n'
    '    // Our own allocators + list on the game\'s device; submission goes to the game\'s queue.\n'
    '    for (int i = 0; i < Feed::kFrames; ++i)\n'
)

BUILD_CREATE_ANCHOR = (
    '    if (!MakeTex12(SLOT_COLOR, w, h, g.color_fmt, false,\n'
    '                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) ||\n'
    '        !MakeTex12(SLOT_OUTPUT, w, h, g.output_fmt, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))\n'
    '    {\n'
    '        ReleaseFrameResources();\n'
    '        return false;\n'
    '    }\n'
    '\n'
    '    if (g_cfg.mode < 2) { g.frame_ready = true; g.need_reset = true; Log("[feed] transport ready (mode %d, no NGX feature)", g_cfg.mode); return true; }\n'
    '\n'
    '    bool crashed = false;\n'
    '    if (!CreateDlssFeature(w, h, inverted, &crashed))\n'
    '        return OnCreateFeatureFailed(crashed);\n'
    '    return true;\n'
    '}\n'
)

BUILD_CREATE_REPLACEMENT = (
    '    if (!MakeTex12(SLOT_COLOR, w, h, g.color_fmt, false,\n'
    '                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) ||\n'
    '        !MakeTex12(SLOT_OUTPUT, w, h, g.output_fmt, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))\n'
    '    {\n'
    '        ReleaseFrameResources();\n'
    '        return false;\n'
    '    }\n'
    '\n'
    '    if (g_cfg.mode < 2) { g.frame_ready = true; g.need_reset = true; Log("[feed] transport ready (mode %d, no NGX feature)", g_cfg.mode); return true; }\n'
    '#if defined(NR_FEED_EXTERNAL_PROVIDER)\n'
    '    if (nr_external_provider_client::ShouldDisableFeederNr()) {\n'
    '        g.frame_ready = true; g.need_reset = true;\n'
    '        Log("[feed] provider-only textures ready (%ux%u); private CreateFeature skipped", w, h);\n'
    '        return true;\n'
    '    }\n'
    '#endif\n'
    '\n'
    '    bool crashed = false;\n'
    '    if (!CreateDlssFeature(w, h, inverted, &crashed))\n'
    '        return OnCreateFeatureFailed(crashed);\n'
    '    return true;\n'
    '}\n'
)

REPLACEMENT = r'''                Breadcrumb("running the same-device evaluate");
                DWORD ecode = 0;
                NVSDK_NGX_Result re = NVSDK_NGX_Result_Fail;
                bool external_attempted = false;
                bool external_delegated = false;
                bool external_train_only = false;
#if defined(NR_FEED_EXTERNAL_PROVIDER)
                {
                    nr_external_provider_client::Claim(kNrExternalProviderIdDl5f);
                }
                if (nr_external_provider_client::ShouldDisableFeederNr())
                {
                    external_attempted = true;
                    const bool inverted = g_cfg.depth_inverted >= 0 ? g_cfg.depth_inverted != 0 : g.depth_reversed;
                    ID3D12Resource *external_color = g.tex12[SLOT_COLOR];
                    ID3D12Resource *external_output = g.tex12[SLOT_OUTPUT];
                    D3D12_RESOURCE_STATES external_color_state =
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                    const auto of_result = nr_fullres_feeder_adapter::Submit(
                        external_color, external_color_state, depth,
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        external_output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        inverted, reset != 0);
                    if (of_result == nr_fullres_feeder_adapter::Result::Delivered)
                    {
                        external_delegated = true;
                        re = static_cast<NVSDK_NGX_Result>(0x1);
                    }
                    else if (of_result == nr_fullres_feeder_adapter::Result::Declined)
                    {
                        external_train_only = true;
                        ecode = 0;
                        re = NVSDK_NGX_Result_Fail;
                        Log("[feed] selected OF path retained the original frame; no ordinary-motion V1 retry");
                    }
                    else
                    {
                    nr_external_provider_feeder::FeedInputs feed_in = {};
                    feed_in.command_list = reinterpret_cast<std::uint64_t>(g.list);
                    feed_in.color = reinterpret_cast<std::uint64_t>(g.tex12[SLOT_COLOR]);
                    feed_in.output = reinterpret_cast<std::uint64_t>(g.tex12[SLOT_OUTPUT]);
                    feed_in.depth = reinterpret_cast<std::uint64_t>(depth);
                    feed_in.motion = reinterpret_cast<std::uint64_t>(mv);
                    feed_in.width = g.width;
                    feed_in.height = g.height;
                    feed_in.color_format = static_cast<std::uint32_t>(g.color_fmt);
                    feed_in.output_format = static_cast<std::uint32_t>(g.output_fmt);
                    feed_in.depth_format = static_cast<std::uint32_t>(dd.Format);
                    feed_in.motion_format = static_cast<std::uint32_t>(md.Format);
                    feed_in.color_state = static_cast<std::uint32_t>(
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    feed_in.output_state = static_cast<std::uint32_t>(
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    feed_in.depth_state = static_cast<std::uint32_t>(
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    feed_in.motion_state = feed_in.depth_state;
                    feed_in.mv_scale_x = g_cfg.mv_scale_x;
                    feed_in.mv_scale_y = g_cfg.mv_scale_y;
                    feed_in.pre_exposure = 1.0f;
                    feed_in.exposure_scale = 1.0f;
                    feed_in.reset = reset != 0;
                    feed_in.depth_inverted = inverted;
                    feed_in.hdr_color = g.hdr;
                    feed_in.view_id = 1;
                    feed_in.epoch = nr_external_resource_epoch;
                    feed_in.frame_id = ++nr_external_frame_seq; // monotonic across ordinary/OF submissions
                    feed_in.provider_id = kNrExternalProviderIdDl5f;
                    feed_in.queue = reinterpret_cast<std::uint64_t>(g.queue);
                    feed_in.fence = reinterpret_cast<std::uint64_t>(g.fence12);
                    // Submit records into this still-open list. EndCommands below
                    // increments once and signals this exact future value.
                    feed_in.fence_value = g.fence_value + 1;
                    NrExternalFrameV1 ext_frame = {};
                    NrExternalSubmitResultV1 ext_result = {};
                    if (nr_external_provider_feeder::BuildSyntheticFrame(feed_in, ext_frame) &&
                        nr_external_provider_client::Submit(ext_frame, ext_result) &&
                        ext_result.writeback_performed)
                    {
                        external_delegated = EndCommands() != 0;
                        re = external_delegated ? static_cast<NVSDK_NGX_Result>(0x1) : NVSDK_NGX_Result_Fail;
                    }
                    else
                    {
                        // Owned consumer declined: execute starter list so core
                        // queue hooks observe real submits. No AbortCommands.
                        // No copy-home / no second private NGX NR.
                        EndCommands();
                        external_train_only = true;
                        ecode = 0;
                        re = NVSDK_NGX_Result_Fail;
                        Log("[feed] external NR declined (%s); identity retain, affinity train submit, Feeder NR skipped",
                            ext_result.bypass[0] ? ext_result.bypass : "unavailable");
                    }
                    }
                }
                else
#endif
                {
                    re = SafeEvaluateDLSS(&ep, &ecode);
                }
                if (!external_attempted)
                {
                    if (ecode != 0)
                        AbortCommands();  // never execute a list NGX crashed while recording
                    else
                        EndCommands();
                }

                if (external_attempted && !external_delegated)
                {
                    // Ownership claimed; consumer declined — keep original via !restored path.
                    g.frame_ready = true;
                    (void)external_train_only;
                }
                else if (!external_delegated && ecode != 0)
                {
                    Log("[feed] evaluate raised exception 0x%08X (caught; nothing was submitted)", ecode);
                    FeedDisable("the DLSS evaluate crashed (the DLSS 5 add-on may be incompatible with this game/resolution)");
                    g.frame_ready = false;
                }
                else if (!external_delegated && NVSDK_NGX_FAILED(re))
                {
                    Log("[feed] evaluate failed 0x%08X (%s)", re, NgxResultName(re));
                    FeedFail("evaluate");
                    g.frame_ready = false;
                }
                else
'''


def assert_feeder_release(feeder_root: Path, release_tag: str, commit: str) -> str:
    expected = SUPPORTED_RELEASES.get(release_tag)
    if not expected or commit != expected:
        raise ValueError(f'Unsupported Feeder release identity: {release_tag}@{commit}')
    version = (feeder_root / 'src/version.rc').read_text(encoding='utf-8')
    token = '0,15,1,0' if release_tag == 'v0.15.1' else '1,16,0,1'
    if f'FILEVERSION    {token}' not in version:
        raise ValueError(f'Feeder version.rc does not match {release_tag}')
    return commit


def patch_of_config_and_ui(text: str) -> str:
    def once(old: str, new: str) -> None:
        nonlocal text
        if text.count(old) != 1:
            raise ValueError(f'Feeder OF config/UI anchor changed: {old[:80]!r}')
        text = text.replace(old, new, 1)

    once(
        '    float hdr_paper_white; // nits that the bridge maps to linear 1.0 (BT.2408 reference white\n'
        '                           // is 203). Highlights run above 1.0, up to 10000/this.\n'
        '};\n',
        '    float hdr_paper_white; // nits that the bridge maps to linear 1.0 (BT.2408 reference white\n'
        '                           // is 203). Highlights run above 1.0, up to 10000/this.\n'
        '    int   of_enabled;      // integrated same-owner optical-flow input; default off\n'
        '    int   of_backend;      // 1 AMD FidelityFX, 2 NVIDIA Optical Flow\n'
        '    int   of_scale;        // explicit 100 or 50 percent input extent\n'
        '};\n')
    once(
        '                     /* hdr_bridge */ -1, /* hdr_paper_white */ 203.0f };',
        '                     /* hdr_bridge */ -1, /* hdr_paper_white */ 203.0f,\n'
        '                     /* of_enabled */ 0, /* of_backend */ 1, /* of_scale */ 100 };')
    once(
        '            "stall_log_ms=%d\\n"\n'
        '            "hdr_bridge=%d\\n"\n'
        '            "hdr_paper_white=%.0f\\n",',
        '            "stall_log_ms=%d\\n"\n'
        '            "hdr_bridge=%d\\n"\n'
        '            "hdr_paper_white=%.0f\\n"\n'
        '            "of_enabled=%d\\nof_backend=%d\\nof_scale=%d\\n",')
    once(
        '            g_cfg.hdr_bridge, g_cfg.hdr_paper_white);\n    fclose(f);',
        '            g_cfg.hdr_bridge, g_cfg.hdr_paper_white,\n'
        '            g_cfg.of_enabled, g_cfg.of_backend, g_cfg.of_scale);\n    fclose(f);')
    parser_anchor = (
        '        else if (_stricmp(key, "hdr_paper_white") == 0) next.hdr_paper_white = val;\n')
    once(parser_anchor, parser_anchor +
        '        else if (_stricmp(key, "of_enabled")     == 0) next.of_enabled = iv;\n'
        '        else if (_stricmp(key, "of_backend")     == 0) next.of_backend = iv;\n'
        '        else if (_stricmp(key, "of_scale")       == 0) next.of_scale = iv;\n')
    once(
        '    if (next.stall_log_ms < 0 || next.stall_log_ms > 10000) next.stall_log_ms = g_cfg.stall_log_ms;\n',
        '    if (next.stall_log_ms < 0 || next.stall_log_ms > 10000) next.stall_log_ms = g_cfg.stall_log_ms;\n'
        '    if (next.of_enabled != 0 && next.of_enabled != 1) next.of_enabled = g_cfg.of_enabled;\n'
        '    if (next.of_backend != 1 && next.of_backend != 2) next.of_backend = g_cfg.of_backend;\n'
        '    if (next.of_scale != 50 && next.of_scale != 100) next.of_scale = g_cfg.of_scale;\n')
    once(
        '    "mv_scale_x", "mv_scale_y", "stall_log_ms", "hdr_bridge", "hdr_paper_white",\n',
        '    "mv_scale_x", "mv_scale_y", "stall_log_ms", "hdr_bridge", "hdr_paper_white",\n'
        '    "of_enabled", "of_backend", "of_scale",\n')
    once(
        '            "buffer_home=%d\\nasync_home=%d\\nsync_home=%d\\nmv_scale_x=%.3f\\nmv_scale_y=%.3f\\nstall_log_ms=%d\\nhdr_bridge=%d\\nhdr_paper_white=%.0f\\n",',
        '            "buffer_home=%d\\nasync_home=%d\\nsync_home=%d\\nmv_scale_x=%.3f\\nmv_scale_y=%.3f\\nstall_log_ms=%d\\nhdr_bridge=%d\\nhdr_paper_white=%.0f\\n"\n'
        '            "of_enabled=%d\\nof_backend=%d\\nof_scale=%d\\n",')
    once(
        '            g_cfg.hdr_bridge, g_cfg.hdr_paper_white);\n    if (!carried.empty())',
        '            g_cfg.hdr_bridge, g_cfg.hdr_paper_white,\n'
        '            g_cfg.of_enabled, g_cfg.of_backend, g_cfg.of_scale);\n    if (!carried.empty())')
    ui_anchor = (
        '    ImGui::Separator();\n'
        '    ImGui::TextUnformatted("Motion vectors");\n')
    ui = r'''    ImGui::Separator();
    ImGui::TextUnformatted("Optical-flow input (experimental)");
    bool of_enabled = g_cfg.of_enabled != 0;
    if (ImGui::Checkbox("Use optical flow for this Feeder frame", &of_enabled)) {
        g_cfg.of_enabled = of_enabled ? 1 : 0; dirty = true;
    }
    static const char *kOfBackends[] = { "AMD FidelityFX OF", "NVIDIA Optical Flow" };
    int of_backend = g_cfg.of_backend == 2 ? 1 : 0;
    if (ImGui::Combo("OF backend", &of_backend, kOfBackends, 2)) {
        g_cfg.of_backend = of_backend + 1; dirty = true;
    }
#if NR_OF_ENABLE_NVOF
    if (g_cfg.of_backend == 2) ImGui::TextDisabled("NVIDIA Optical Flow is compiled into this variant.");
#else
    if (g_cfg.of_backend == 2)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
            "NVIDIA Optical Flow is unavailable in this AMD-only build; the frame is retained unchanged.");
#endif
    static const char *kOfScales[] = { "100%", "50%" };
    int of_scale = g_cfg.of_scale == 50 ? 1 : 0;
    if (ImGui::Combo("OF input size", &of_scale, kOfScales, 2)) {
        g_cfg.of_scale = of_scale ? 50 : 100; dirty = true;
    }
    ImGui::TextWrapped("Uses this Feeder's Color and Depth snapshot, replaces MotionVectors, and performs one Core Submit. Changes retire through exact GPU fences.");

'''
    once(ui_anchor, ui + ui_anchor)
    return text


def patch_feed_cpp(text: str, release_tag: str) -> str:
    if text.count(INCLUDES_ANCHOR) != 1:
        raise ValueError('Feeder feed_ngx include anchor missing or duplicated')
    if 'NR_FEED_EXTERNAL_PROVIDER' in text:
        raise ValueError('Feeder source already contains external provider patch')
    if text.count(INIT_NGX_ANCHOR) != 1:
        raise ValueError('InitSession12 NGX init anchor missing or duplicated')
    if text.count(ALLOC_ANCHOR) != 1:
        raise ValueError('InitSession12 allocator anchor missing or duplicated')
    # BuildResources12 appears once for D3D12; match the exact trailing block.
    if text.count(BUILD_CREATE_ANCHOR) != 1:
        raise ValueError('BuildResources12 CreateFeature anchor missing or duplicated')
    if text.count(MAKE_TEX_FLAGS_ANCHOR) != 1:
        raise ValueError('MakeTex12 output-flag anchor missing or duplicated')

    old_mid = (
        '                Breadcrumb("running the same-device evaluate");\n'
        '                DWORD ecode = 0;\n'
        '                NVSDK_NGX_Result re = SafeEvaluateDLSS(&ep, &ecode);\n'
        '                if (ecode != 0)\n'
        '                    AbortCommands();  // never execute a list NGX crashed while recording\n'
        '                else\n'
        '                    EndCommands();\n'
        '\n'
        '                if (ecode != 0)\n'
        '                {\n'
        '                    Log("[feed] evaluate raised exception 0x%08X (caught; nothing was submitted)", ecode);\n'
        '                    FeedDisable("the DLSS evaluate crashed (the DLSS 5 add-on may be incompatible with this game/resolution)");\n'
        '                    g.frame_ready = false;\n'
        '                }\n'
        '                else if (NVSDK_NGX_FAILED(re))\n'
        '                {\n'
        '                    Log("[feed] evaluate failed 0x%08X (%s)", re, NgxResultName(re));\n'
        '                    FeedFail("evaluate");\n'
        '                    g.frame_ready = false;\n'
        '                }\n'
        '                else\n'
    )
    if release_tag == 'v1.16.0-beta.1':
        old_mid = old_mid.replace(
            '                else\n                    EndCommands();\n',
            '                else\n                {\n'
            '                    // linear -> PQ on this same list, so it is submitted with the evaluate and\n'
            '                    // lands before the copy home that ReShade records next on the same queue.\n'
            '                    BridgeEncodePrivate12();\n'
            '                    EndCommands();\n'
            '                }\n')
    if text.count(old_mid) != 1:
        raise ValueError('FeedFrame12 evaluate mid-block anchor changed')

    text = text.replace(INCLUDES_ANCHOR, PRODUCER_INCLUDES, 1)
    text = text.replace(INIT_NGX_ANCHOR, INIT_NGX_REPLACEMENT, 1)
    text = text.replace(ALLOC_ANCHOR, ALLOC_REPLACEMENT, 1)
    text = text.replace(BUILD_CREATE_ANCHOR, BUILD_CREATE_REPLACEMENT, 1)
    text = text.replace(MAKE_TEX_FLAGS_ANCHOR, MAKE_TEX_FLAGS_REPLACEMENT, 1)
    begin_anchor = 'static bool BeginCommands()\n{\n'
    if text.count(begin_anchor) != 1:
        raise ValueError('BeginCommands anchor changed')
    text = text.replace(begin_anchor,
        'static bool nr_external_gpu_quarantined = false;\n' + begin_anchor +
        '    if (nr_external_gpu_quarantined) return false;\n', 1)
    drain_start = text.index('static void DrainGpu()\n{\n')
    drain_end = text.index('\n}\n', drain_start) + 3
    text = text[:drain_start] + r'''static bool DrainGpu()
{
    if (g.queue == nullptr || g.fence12 == nullptr) return !nr_external_gpu_quarantined;
    const UINT64 v = ++g.fence_value;
    const HRESULT signalled = g.queue->Signal(g.fence12, v);
    if (FAILED(signalled)) {
        nr_external_gpu_quarantined = true;
        FeedDisable("external provider teardown Signal failed; resources retained");
        return false;
    }
    const UINT64 before = g.fence12->GetCompletedValue();
    if (before == UINT64_MAX) return true; // removed device has cancelled its work
    if (before < v) {
        if (g.fence_event == nullptr || FAILED(g.fence12->SetEventOnCompletion(v, g.fence_event)) ||
            WaitForSingleObject(g.fence_event, 5000) != WAIT_OBJECT_0 ||
            (g.fence12->GetCompletedValue() != UINT64_MAX && g.fence12->GetCompletedValue() < v)) {
            nr_external_gpu_quarantined = true;
            FeedDisable("external provider teardown completion unconfirmed; resources retained");
            return false;
        }
    }
    for (int i = 0; i < Feed::kFrames; ++i) g.alloc_fence[i] = 0;
    return true;
}
''' + text[drain_end:]
    replacement = REPLACEMENT
    if release_tag == 'v1.16.0-beta.1':
        if text.count(PQ_OUTPUT_FLAGS_ANCHOR) != 1:
            raise ValueError('HDR10 linear output flag anchor missing or duplicated')
        text = text.replace(PQ_OUTPUT_FLAGS_ANCHOR, PQ_OUTPUT_FLAGS_REPLACEMENT, 1)
        replacement = replacement.replace(
            'ID3D12Resource *external_color = g.tex12[SLOT_COLOR];',
            'ID3D12Resource *external_color = BridgeColorIn();').replace(
            'ID3D12Resource *external_output = g.tex12[SLOT_OUTPUT];',
            'ID3D12Resource *external_output = BridgeColorOut();').replace(
            'D3D12_RESOURCE_STATES external_color_state =\n'
            '                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |\n'
            '                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;',
            'D3D12_RESOURCE_STATES external_color_state = g.pq_bridge\n'
            '                        ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE\n'
            '                        : (D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |\n'
            '                           D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);').replace(
            'reinterpret_cast<std::uint64_t>(g.tex12[SLOT_COLOR])',
            'reinterpret_cast<std::uint64_t>(BridgeColorIn())').replace(
            'reinterpret_cast<std::uint64_t>(g.tex12[SLOT_OUTPUT])',
            'reinterpret_cast<std::uint64_t>(BridgeColorOut())').replace(
            'feed_in.color_format = static_cast<std::uint32_t>(g.color_fmt);',
            'feed_in.color_format = static_cast<std::uint32_t>(BridgeColorIn()->GetDesc().Format);').replace(
            'feed_in.output_format = static_cast<std::uint32_t>(g.output_fmt);',
            'feed_in.output_format = static_cast<std::uint32_t>(BridgeColorOut()->GetDesc().Format);').replace(
            'feed_in.color_state = static_cast<std::uint32_t>(\n'
            '                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |\n'
            '                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);',
            'feed_in.color_state = static_cast<std::uint32_t>(g.pq_bridge\n'
            '                        ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE\n'
            '                        : (D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |\n'
            '                           D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));').replace(
            '                        external_delegated = EndCommands() != 0;',
            '                        BridgeEncodePrivate12();\n'
            '                        external_delegated = EndCommands() != 0;')
    text = text.replace(old_mid, replacement, 1)
    # Scope every frame seam to same-device D3D12; Legacy paths stay unchanged.
    start = text.index('static void FeedFrame12(')
    end = text.index('static void FeedFrameVk(', start)
    d3d12 = text[start:end]
    early = '    if (!g_cfg.enabled || g_cfg.mode == 0) return;\n'
    if d3d12.count(early) != 1:
        raise ValueError('D3D12 preflight anchor changed')
    d3d12 = d3d12.replace(early, early +
        '#if defined(NR_FEED_EXTERNAL_PROVIDER)\n'
        '    if (g_cfg.mode >= 2 &&\n'
        '        (!nr_external_provider_client::Claim(kNrExternalProviderIdDl5f) ||\n'
        '         !nr_external_provider_client::CanPrepareFrame())) return;\n'
        '#endif\n', 1)
    d3d12 = d3d12.replace('FeatureMissingForMode();',
        '(FeatureMissingForMode() && !nr_external_provider_client::ShouldDisableFeederNr());')
    text = text[:start] + d3d12 + text[end:]
    release_anchor = 'static void ReleaseFrameResources()\n{\n'
    if text.count(release_anchor) != 1:
        raise ValueError('resource teardown anchor changed')
    text = text.replace(release_anchor,
        'static std::uint64_t nr_external_resource_epoch = 1;\n'
        'static UINT64 nr_external_frame_seq = 0;\n'
        '#include "feeder_adapter.inl" // staged in nr_fullres include directory\n'
        'static bool ReleaseFrameResources()\n{\n', 1)
    drain_anchor = '    DrainGpu();\n    FeedVkProbeRelease();\n'
    if text.count(drain_anchor) != 1:
        raise ValueError('resource teardown DrainGpu anchor changed')
    text = text.replace(drain_anchor,
        '    if (!DrainGpu()) return false;\n'
        '    ++nr_external_resource_epoch;\n'
        '    if (!nr_fullres_feeder_adapter::Retire()) {\n'
        '        nr_external_gpu_quarantined = true;\n'
        '        FeedDisable("OF exact completion is still pending; resources retained");\n'
        '        return false;\n'
        '    }\n'
        '    if (!nr_external_provider_client::Release()) {\n'
        '        nr_external_gpu_quarantined = true;\n'
        '        FeedDisable("Core still owns exact-completion work; resources retained");\n'
        '        return false;\n'
        '    }\n'
        '    FeedVkProbeRelease();\n', 1)
    release_start = text.index('static bool ReleaseFrameResources()\n{\n')
    release_end = text.index('\n}\n', release_start)
    text = text[:release_end] + '\n    return true;' + text[release_end:]
    shutdown = 'static void ShutdownSession()\n{\n    ReleaseFrameResources();\n'
    if text.count(shutdown) != 1:
        raise ValueError('ShutdownSession teardown anchor changed')
    text = text.replace(shutdown,
        'static void ShutdownSession()\n{\n    if (!ReleaseFrameResources()) return;\n', 1)
    for breadcrumb in (
        '    Breadcrumb("building shared textures");\n    ReleaseFrameResources();\n',
        '    Breadcrumb("building same-device textures");\n    ReleaseFrameResources();\n',
        '    Breadcrumb("building the Vulkan-shared textures");\n    ReleaseFrameResources();\n',
        '    Breadcrumb("building the OpenGL-shared textures");\n    ReleaseFrameResources();\n',
    ):
        if text.count(breadcrumb) != 1:
            raise ValueError(f'build teardown anchor changed: {breadcrumb[:60]!r}')
        text = text.replace(breadcrumb,
            breadcrumb.replace('    ReleaseFrameResources();',
                               '    if (!ReleaseFrameResources()) return false;'), 1)
    return patch_of_config_and_ui(text)


def stage_feeder(feeder_root: Path, output: Path, producer_headers: Path,
                 release_tag: str, source_commit: str) -> dict:
    commit = assert_feeder_release(feeder_root, release_tag, source_commit)
    if output.exists():
        raise ValueError('Feeder stage output already exists')
    feed_src = feeder_root / FEED_CPP
    if not feed_src.is_file():
        raise ValueError(f'Missing {FEED_CPP}')
    original = feed_src.read_text(encoding='utf-8', errors='strict')
    patched = patch_feed_cpp(original, release_tag)
    output.mkdir(parents=True)
    shutil.copytree(feeder_root, output, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('.git', 'build', 'bin', 'obj'))
    (output / FEED_CPP).write_text(patched, encoding='utf-8', newline='\n')
    build = output / 'build.bat'
    build_text = build.read_text(encoding='utf-8')
    build_text = build_text.replace(
        'call "%~dp0tools\\vcvars.bat" x64 || exit /b 1\n',
        'call "%~dp0tools\\vcvars.bat" x64 || exit /b 1\n'
        'if "%FFX_SDK_ROOT%"=="" (echo FFX_SDK_ROOT is required & exit /b 2)\n'
        'if "%FFX_LIB_DIR%"=="" (echo FFX_LIB_DIR is required & exit /b 2)\n')
    build_text = build_text.replace(
        '/Iexternal\\reshade',
        '/DNR_OF_ENABLE_NVOF=0 /Inr_lab_interop /Inr_fullres '
        '/I"%FFX_SDK_ROOT%\\sdk\\include" /Iexternal\\reshade')
    build_text = build_text.replace('/std:c++20', '/utf-8 /std:c++20')
    build_text = build_text.replace(
        'src\\dlss5-feed.cpp ', 'src\\dlss5-feed.cpp nr_fullres\\backend.cpp ')
    build_text = build_text.replace(
        'version.lib kernel32.lib user32.lib advapi32.lib ole32.lib',
        '/LIBPATH:"%FFX_LIB_DIR%" ffx_opticalflow_x64.lib ffx_backend_dx12_x64.lib '
        'd3d12.lib dxgi.lib d3dcompiler.lib version.lib kernel32.lib user32.lib advapi32.lib ole32.lib')
    build.write_text(build_text, encoding='utf-8')
    interop = output / 'nr_lab_interop'
    interop.mkdir(exist_ok=True)
    for name in (
        'nr_external_provider_abi.h',
        'nr_external_provider_producer.h',
        'nr_external_provider_feeder_map.h',
    ):
        src = producer_headers / name
        if not src.is_file():
            raise ValueError(f'Missing producer header {name}')
        shutil.copyfile(src, interop / name)
    fullres_source = Path(__file__).resolve().parents[1] / 'experiments' / 'fullres_guides'
    fullres = output / 'nr_fullres'
    fullres.mkdir(exist_ok=True)
    fullres_files = (
        'backend.cpp', 'backend.hpp', 'geometry.hpp', 'policy.hpp',
        'probe_state.hpp', 'shader_source.hpp', 'feeder_adapter.inl',
    )
    for name in fullres_files:
        src = fullres_source / name
        if not src.is_file():
            raise ValueError(f'Missing full-resolution OF source {name}')
        shutil.copyfile(src, fullres / name)
    manifest = {
        'schema': 'nr-external-provider-source-stage-v2',
        'feeder_release_tag': release_tag,
        'feeder_commit_expected': SUPPORTED_RELEASES[release_tag],
        'feeder_commit_actual': commit,
        'git_verified': True,
        'patched_file': FEED_CPP,
        'producer_headers': sorted(p.name for p in interop.glob('*.h')),
        'of_sources': list(fullres_files),
        'feed_cpp_sha256': hashlib.sha256(patched.encode('utf-8')).hexdigest(),
        'external_nr': 'FeedFrame12 provider-only: skip private NGX Init/Create/'
                       'Evaluate; SubmitFrameV1 with exact future fence; RTV-capable output; '
                       'starter EndCommands trains affinity; integrated AMD OF 100/50 '
                       'reuses the same owner/snapshot/Submit path and is default-off; '
                       'no ordinary-motion retry while OF is selected',
    }
    (output / 'feeder-external-nr-manifest.json').write_text(
        json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return manifest


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--feeder', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--producer-headers', type=Path, required=True)
    ap.add_argument('--release-tag', choices=sorted(SUPPORTED_RELEASES), required=True)
    ap.add_argument('--source-commit', required=True)
    args = ap.parse_args()
    manifest = stage_feeder(args.feeder.resolve(), args.output.resolve(),
                            args.producer_headers.resolve(), args.release_tag,
                            args.source_commit)
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError) as exc:
        raise SystemExit(str(exc)) from exc
