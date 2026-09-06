#!/usr/bin/env python3
"""Pinned 0.4.5-fix1 DX11 experiment. No game deployment or vendor binary patch.
The 0.4.2 script is retained only as a reviewed source transformation library.
"""
from __future__ import annotations
import hashlib
import importlib.util
import json
import shutil
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = "61fe9a0fe527fb2304c382c332cbe62876228b8b"
OLD_DX11 = "0f714b32597e395d0684567ed9296dc1d2a1f35c"
EXPERIMENT = "beta0.4.5-dx11-exp3"
VERSION = "beta0.4.5-fix1"
spec = importlib.util.spec_from_file_location("native_bridge_library", ROOT / "scripts/prepare-native-bridge.py")
legacy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(legacy)
legacy.BASE = BASE
legacy.EXPERIMENT = EXPERIMENT
legacy.CORE_BLOBS = {
    "src/nr_before_sr.cpp": "c0a5acf0c9581af100c85aad5071b0ff5c91c833",
    "src/nr_settings_ui.inl": "55de5dd9d9278eb5be770a067404af0561e5757a",
    "src/nr_command_state.inl": "6bedfcc809cac7e600658085048a190c55279850",
    "src/nr_before_sr.rc": "bddeb76934df02ca51d8c469da6e0ba3a140bad0",
    "scripts/build.ps1": "b700b221c2fb948904d1bfd8c2f5d986c41121ea",
}
old_bridge, old_build, old_prepare = legacy.patch_bridge, legacy.patch_build, legacy.prepare
once = legacy.replace_once

def between(text: str, start: str, stop: str, new: str) -> str:
    if text.count(start) != 1 or text.count(stop) != 1:
        raise ValueError("Source block boundaries changed")
    a, b = text.index(start), text.index(stop)
    if b <= a:
        raise ValueError("Reversed source block boundaries")
    return text[:a] + new + text[b:]

def patch_core(text: str) -> str:
    for name in ("kAddonVersion", "kBuildIdentity"):
        if text.count(f'{name} = "{VERSION}";') != 1:
            raise ValueError("Expected pinned 0.4.5-fix1 identity")
    text = text.replace(VERSION, EXPERIMENT)
    text = once(text, '#include "MinHook.h"', '#include "MinHook.h"\n#include "interop/nr_deployment_names.h"')
    text = once(text, '#include "nr_diagnostic_bundle.inl"',
        'void NrRenderControlPoll();\nvoid NrDx11DeploymentPoll(bool force);\nbool NrDx11DeploymentBlocked();\n#include "nr_diagnostic_bundle.inl"')
    text = once(text, '    PollNgxLifecycleModules(false);',
        '    PollNgxLifecycleModules(false);\n    NrDx11DeploymentPoll(false);\n    NrRenderControlPoll();')
    text = once(text, '#include "nr_settings_ui.inl"',
        '#include "interop/nr_dx11_deployment.inl"\n#include "interop/nr_render_control.inl"\n#include "nr_settings_ui.inl"')
    return text + '\n#include "interop/nr_native_bridge_consumer.inl"\n'

def patch_ui(text: str) -> str:
    text = between(text,
        '    if (nr_present_api_status::IsD3d11PresentRuntime(runtime))\n',
        '    else if (!current.config.enabled || ActiveTransferStrength(current.config) <= 0.000001f)',
        '    if (nr_present_api_status::IsD3d11PresentRuntime(runtime)) {\n        DrawDx11DeploymentStatus();\n    }\n')
    return once(text,
        '    if (ImGui::CollapsingHeader(ui("Advanced picture controls", "高级画质"))) {',
        '    DrawRenderControl();\n    if (ImGui::CollapsingHeader(ui("Advanced picture controls", "高级画质"))) {')

def patch_bridge(text: str) -> str:
    text = old_bridge(text)
    text = '#include "nr_bridge_baseline.h"\n#include "nr_mirror_rect.h"\n' + text
    text = once(text, 'c.flags = -1; c.shape = 0;', 'c.flags = -1; c.shape = 1;')
    text = once(text, '    __try { return fn(list, feature, p, nullptr); }', '''    __try {
        if (!nr_bridge_baseline::Bind(list)) {
            Log("[native-carrier] private baseline unavailable; native SR retained; F8 for feedback");
            return static_cast<NVSDK_NGX_Result>(0xBAD00005u);
        }
        return fn(list, feature, p, nullptr);
    }''')
    # Keep per-handle flags with the original Create shape. Fresh Evaluate
    # blocks need not repeat flags; another preview handle may reuse a block.
    text = once(text,
        'struct BridgeCreateShape { const NVSDK_NGX_Handle *handle; unsigned int w, h, ow, oh; };',
        'struct BridgeCreateShape { const NVSDK_NGX_Handle *handle; unsigned int w, h, ow, oh; unsigned int flags; bool flags_known; };')
    text = once(text,
        'static void BridgeNoteCreate(const NVSDK_NGX_Handle *handle, const NVSDK_NGX_Parameter *p)\n{',
        '''static void BridgeNoteCreate(const NVSDK_NGX_Handle *handle, const NVSDK_NGX_Parameter *p)
{
    // Invalidate reused handle addresses even when this Create omits its size.
    // Otherwise an old preview's flags/shape could survive an incomplete Create.
    EnterCriticalSection(&g_bridge_cs);
    for (auto &record : g_create_shape)
        if (record.handle == handle) record = {};
    g_nr_native_history.Created(reinterpret_cast<std::uintptr_t>(handle));
    g_bridge.frame_ready = false;
    g_bridge.need_reset = true;
    LeaveCriticalSection(&g_bridge_cs);''')
    text = once(text, '    g_create_shape[slot] = { handle, w, h, ow, oh };',
        '''    unsigned int native_flags = 0;
    const bool flags_known = p != nullptr && p->Get("DLSS.Feature.Create.Flags", &native_flags) == NGX_SUCCESS;
    g_create_shape[slot] = { handle, w, h, ow, oh, native_flags, flags_known };''')
    marker = '// The block this add-on handed to the D3D12 evaluate, read back from it.'
    flags_impl = '''static bool NrSavedCreateFlags(unsigned int *value)
{
    if (g_eval_handle == nullptr) return false;
    for (const auto &record : g_create_shape)
        if (record.handle == g_eval_handle && record.flags_known) {
            *value = record.flags; return true;
        }
    return false;
}
struct NrMirrorParams {
    const NVSDK_NGX_Parameter *p;
    bool Get(const char *key, std::uint32_t &value) const {
        if (strcmp(key, "DLSS.Feature.Create.Flags") == 0 && NrSavedCreateFlags(&value)) return true;
        return p != nullptr && p->Get(key, &value) == NGX_SUCCESS;
    }
};

'''
    text = once(text, marker, flags_impl + marker)
    text = once(text,
        'static unsigned int ResolveCreateFlags(const NVSDK_NGX_Parameter *gp, FlagsFrom *from)\n{',
        '''static bool NrSavedCreateFlags(unsigned int *value);
static unsigned int ResolveCreateFlags(const NVSDK_NGX_Parameter *gp, FlagsFrom *from)
{
    unsigned int saved_flags = 0;
    if (g_source == SRC_MIRROR && NrSavedCreateFlags(&saved_flags)) {
        *from = FLAGS_GAME; return saved_flags;
    }''')
    # Replace size guessing, not just the texture copy dimensions. The private
    # Feature's creation and existing bounded rebuild logic consume this plan.
    text = between(text, '    // shape=1: the render region', "    // Gallipoli's map screen", '''    const unsigned int original_create_w = gw, original_create_h = gh;
    const NrMirrorParams native_params{gp};
    const auto nr_frame_plan = nr_mirror_rect::Resolve(native_params,
        {gw, gh}, {gow, goh},
        {d[SLOT_COLOR].Width, d[SLOT_COLOR].Height},
        {d[SLOT_DEPTH].Width, d[SLOT_DEPTH].Height},
        {d[SLOT_MV].Width, d[SLOT_MV].Height},
        {d[SLOT_OUTPUT].Width, d[SLOT_OUTPUT].Height});
    if (!nr_frame_plan) {
        static unsigned int reports = 0;
        if (reports++ < 6)
            Warn("native mirror rejected: %s; original Create=%ux%u active=%ux%u output=%ux%u. Native SR retained; collect F8 and bridge log for BUG feedback.",
                nr_mirror_rect::Name(nr_frame_plan.reason), original_create_w, original_create_h,
                nr_frame_plan.render.w, nr_frame_plan.render.h, gow, goh);
        g_bridge.need_reset = true;
        for (int i = 0; i < SLOT_COUNT; ++i) g[i]->Release();
        dev11->Release();
        return;
    }
    gw = nr_frame_plan.render.w; gh = nr_frame_plan.render.h;
    unsigned int nr_frame_flags = 0;
    native_params.Get("DLSS.Feature.Create.Flags", nr_frame_flags);

''')
    text = once(text,
        '    const bool partial = (gow != d[SLOT_OUTPUT].Width) || (goh != d[SLOT_OUTPUT].Height);',
        '    const bool partial = nr_frame_plan.partial_output;')
    text = once(text,
        '    if (!g_bridge.frame_ready || shape_changed)\n    {',
        '''    shape_changed = shape_changed || g_panel_flags < 0 ||
        static_cast<unsigned int>(g_panel_flags) != nr_frame_flags;
    if (!g_bridge.frame_ready || shape_changed)
    {
        Log("[native-contract] originalCreate=%ux%u Evaluate=%ux%u privateCreate=%ux%u output=%ux%u colorBase=%u,%u depthBase=%u,%u mvBase=%u,%u outputBase=%u,%u flags=%u; rebuild request, not proof of delivery",
            original_create_w, original_create_h, gw, gh, gw, gh, gow, goh,
            nr_frame_plan.color_base.x, nr_frame_plan.color_base.y,
            nr_frame_plan.depth_base.x, nr_frame_plan.depth_base.y,
            nr_frame_plan.motion_base.x, nr_frame_plan.motion_base.y,
            nr_frame_plan.output_base.x, nr_frame_plan.output_base.y, nr_frame_flags);''')
    text = between(text, '            if (g_bridge.partial_output)\n            {\n                // The region only.',
        '        else if (mode == 1) ctx->CopyResource(g[SLOT_OUTPUT], g_bridge.tex11[SLOT_COLOR]);',
        '''            nr_mirror_rect::CopyOutput<ID3D11DeviceContext, ID3D11Texture2D, D3D11_BOX>(
                ctx, g[SLOT_OUTPUT], g_bridge.tex11[SLOT_OUTPUT], nr_frame_plan);
        }
''')
    # Correct the old runtime explanation as well as the copy; no 0,0 claim.
    text = once(text, 'region at 0,0 is written and the rest of the texture left as it is, ',
        'declared region is written at its output base; other pixels are preserved, ')
    return text

def patch_build(text: str) -> str:
    if text.count('-' + VERSION + '-') != 2:
        raise ValueError("Expected two exact language output names")
    return old_build(text).replace('-' + VERSION + '-', '-' + EXPERIMENT + '-')

def prepare(core: Path, upstream: Path, output: Path) -> None:
    if output.exists():
        raise ValueError("Output must not exist")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="nr-045-stage-", dir=output.parent) as tmp:
        stage = Path(tmp) / "candidate"
        old_prepare(core, upstream, stage)
        for name in ("nr_bridge_baseline.h", "nr_mirror_rect.h"):
            shutil.copyfile(stage / "core/src/interop" / name, stage / "carrier/src" / name)
        rc = stage / "core/src/nr_before_sr.rc"
        rc.write_text(rc.read_text(encoding="utf-8").replace(VERSION, EXPERIMENT)
            .replace('0.4.5-fix1 beta', '0.4.5 DX11 exp3 beta'), encoding="utf-8", newline="\n")
        consumer = stage / "core/src/interop/nr_native_bridge_consumer.inl"
        value = consumer.read_text(encoding="utf-8")
        value = once(value, '    if (flags & 1u) PollNgxLifecycleModules(true);',
            '    if (flags & 1u) { NrDx11DeploymentPoll(true); PollNgxLifecycleModules(true); }')
        value = once(value, '    } else if (LatchGenericRenoDxConflictIfLoaded() ||',
            '    } else if (NrDx11DeploymentBlocked() || LatchGenericRenoDxConflictIfLoaded() ||')
        consumer.write_text(value, encoding="utf-8", newline="\n")
        ini = stage / "core/config/nr_before_sr.ini"
        with ini.open("a", encoding="utf-8", newline="\n") as stream:
            stream.write("\n; 0 = game owns actual render scale. This is not NRScale.\nRenderScalePercent=0\n")
        mf = stage / "source-manifest.json"
        manifest = json.loads(mf.read_text(encoding="utf-8"))
        manifest.update({"core_pins": legacy.CORE_BLOBS, "dx11_reference": OLD_DX11,
            "runtime_verification": "BG3 prior beta owner-confirmed; THIS integration not game-verified",
            "shape_policy": "Evaluate-active-input and independent bounded output base",
            "vendor_loader_modified": False})
        manifest["files"] = {p.relative_to(stage).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(stage.rglob("*")) if p.is_file() and p != mf}
        mf.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        stage.rename(output)

legacy.patch_core = patch_core
legacy.patch_ui = patch_ui
legacy.patch_bridge = patch_bridge
legacy.patch_build = patch_build
legacy.prepare = prepare
if __name__ == "__main__":
    legacy.main()
