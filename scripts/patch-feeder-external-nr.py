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

FEEDER_COMMIT = '26c002d5156d178c2db438327194077c9ad94418'
FEED_CPP = 'src/dlss5-feed.cpp'

INCLUDES_ANCHOR = '#include "feed_ngx.h"  // NGX result names and DLL identity, shared with host64\n'

PRODUCER_INCLUDES = (
    '#include "feed_ngx.h"  // NGX result names and DLL identity, shared with host64\n'
    '// MC1: optional external NR consumer (this lab). Headers are staged beside\n'
    '// the Feeder tree; when absent at compile time the legacy evaluate remains.\n'
    '#if 1 // staged provider build: missing headers must fail compilation\n'
    '#include "nr_external_provider_producer.h"\n'
    '#include "nr_external_provider_feeder_map.h"\n'
    '#define NR_FEED_EXTERNAL_PROVIDER 1\n'
    '#endif\n'
)

# Skip private NGX Init when the lab consumer already owns NR.
INIT_NGX_ANCHOR = (
    '    Breadcrumb("initialising NGX on the game\'s device");\n'
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
    DWORD ngx_code = 0;
    NVSDK_NGX_Result r = SafeNgxInit12(data_path, g.dev12, &ngx_code);
'''

# Close the else-block after AllocateParameters success path reaches allocators.
# Anchor: first allocator creation after params allocated.
ALLOC_ANCHOR = (
    '    // Our own allocators + list on the game\'s device; submission goes to the game\'s queue.\n'
    '    for (int i = 0; i < Feed::kFrames; ++i)\n'
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
                    static UINT64 s_ext_frame_seq = 0;
                    const bool inverted = g_cfg.depth_inverted >= 0 ? g_cfg.depth_inverted != 0 : g.depth_reversed;
                    nr_external_provider_feeder::FeedInputs feed_in = {};
                    feed_in.command_list = reinterpret_cast<std::uint64_t>(g.list);
                    feed_in.color = reinterpret_cast<std::uint64_t>(g.tex12[SLOT_COLOR]);
                    feed_in.output = reinterpret_cast<std::uint64_t>(g.tex12[SLOT_OUTPUT]);
                    feed_in.depth = reinterpret_cast<std::uint64_t>(depth);
                    feed_in.motion = reinterpret_cast<std::uint64_t>(mv);
                    feed_in.width = g.width;
                    feed_in.height = g.height;
                    feed_in.color_format = static_cast<std::uint32_t>(g.color_fmt);
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
                    feed_in.frame_id = ++s_ext_frame_seq; // monotonic; independent of frames_done
                    feed_in.provider_id = kNrExternalProviderIdDl5f;
                    feed_in.queue = reinterpret_cast<std::uint64_t>(g.queue);
                    feed_in.fence = reinterpret_cast<std::uint64_t>(g.fence12);
                    feed_in.fence_value = g.fence_value;
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


def assert_feeder_commit(feeder_root: Path) -> str:
    proc = subprocess.run(
        ['git', '-C', str(feeder_root), 'rev-parse', 'HEAD'],
        check=False, capture_output=True, text=True)
    if proc.returncode != 0:
        raise ValueError(f'Feeder git rev-parse failed: {proc.stderr.strip()}')
    actual = proc.stdout.strip()
    if actual != FEEDER_COMMIT:
        raise ValueError(
            f'Feeder commit mismatch: expected {FEEDER_COMMIT}, got {actual}')
    dirty = subprocess.run(
        ['git', '-C', str(feeder_root), 'status', '--porcelain',
         '--untracked-files=no'],
        check=False, capture_output=True, text=True)
    if dirty.returncode != 0:
        raise ValueError('Feeder git status failed')
    if dirty.stdout.strip():
        raise ValueError('Feeder checkout is dirty; refusing to stage')
    return actual


def patch_feed_cpp(text: str) -> str:
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
    if text.count(old_mid) != 1:
        raise ValueError('FeedFrame12 evaluate mid-block anchor changed')

    text = text.replace(INCLUDES_ANCHOR, PRODUCER_INCLUDES, 1)
    text = text.replace(INIT_NGX_ANCHOR, INIT_NGX_REPLACEMENT, 1)
    text = text.replace(ALLOC_ANCHOR, ALLOC_REPLACEMENT, 1)
    text = text.replace(BUILD_CREATE_ANCHOR, BUILD_CREATE_REPLACEMENT, 1)
    text = text.replace(old_mid, REPLACEMENT, 1)
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
        'static std::uint64_t nr_external_resource_epoch = 1;\n' + release_anchor +
        '    ++nr_external_resource_epoch;\n'
        '    nr_external_provider_client::Release();\n', 1)
    return text


def stage_feeder(feeder_root: Path, output: Path, producer_headers: Path) -> dict:
    commit = assert_feeder_commit(feeder_root)
    if output.exists():
        raise ValueError('Feeder stage output already exists')
    feed_src = feeder_root / FEED_CPP
    if not feed_src.is_file():
        raise ValueError(f'Missing {FEED_CPP}')
    original = feed_src.read_text(encoding='utf-8', errors='strict')
    patched = patch_feed_cpp(original)
    output.mkdir(parents=True)
    shutil.copytree(feeder_root, output, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('.git', 'build', 'bin', 'obj'))
    (output / FEED_CPP).write_text(patched, encoding='utf-8', newline='\n')
    build = output / 'build.bat'
    build.write_text(build.read_text(encoding='utf-8').replace('/Iexternal\\reshade', '/Inr_lab_interop /Iexternal\\reshade'), encoding='utf-8')
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
    manifest = {
        'feeder_commit_expected': FEEDER_COMMIT,
        'feeder_commit_actual': commit,
        'git_verified': True,
        'patched_file': FEED_CPP,
        'producer_headers': sorted(p.name for p in interop.glob('*.h')),
        'feed_cpp_sha256': hashlib.sha256(patched.encode('utf-8')).hexdigest(),
        'external_nr': 'FeedFrame12 provider-only: skip private NGX Init/Create/'
                       'Evaluate; SubmitFrameV1; starter EndCommands trains affinity; '
                       'no second NR on decline',
    }
    (output / 'feeder-external-nr-manifest.json').write_text(
        json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return manifest


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--feeder', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--producer-headers', type=Path, required=True)
    args = ap.parse_args()
    manifest = stage_feeder(args.feeder.resolve(), args.output.resolve(),
                            args.producer_headers.resolve())
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError) as exc:
        raise SystemExit(str(exc)) from exc
