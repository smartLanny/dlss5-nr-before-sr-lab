#!/usr/bin/env python3
"""r3 successor to #46. Stages reviewed patches, never changes the checkout.
The retained 0.4.2 generator is a pinned transformation library/negative-control
fixture, not the build entry point for this experiment.
"""
import hashlib
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("legacy_r3", ROOT / "scripts/prepare-native-bridge.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
m.BASE = "d42a8eb23778bc602d0d638193872d41216c2203"
m.EXPERIMENT = "beta0.4.3r3-dx11-exp2"
m.CORE_BLOBS = {
    "src/nr_before_sr.cpp": "abaf4eec0c5da804712466674ef2604196a347f4",
    "src/nr_settings_ui.inl": "b28a034def4af05f02966151930a10ac227a2296",
    "src/nr_command_state.inl": "922f0cff2b64eb56fbf229d8b3a579b44f91bc43",
    "src/nr_before_sr.rc": "2486c22848a34083c975059262bc0f15ea5aaf30",
    "scripts/build.ps1": "2bd4ec2482fd9315edd9ce67b61d5a284716142a",
}
old_ui, old_bridge, old_build, old_prepare = m.patch_ui, m.patch_bridge, m.patch_build, m.prepare

def patch_core(text):
    for name in ("kAddonVersion", "kBuildIdentity"):
        if f'{name} = "beta0.4.3r3";' not in text:
            raise ValueError("Expected exact r3 release identity")
    text = text.replace("beta0.4.3r3", m.EXPERIMENT)
    text = m.replace_once(text, '#include "nr_diagnostic_bundle.inl"',
        'void NrRenderControlPoll();\n#include "nr_diagnostic_bundle.inl"')
    text = m.replace_once(text, '    PollNgxLifecycleModules(false);',
        '    PollNgxLifecycleModules(false);\n    NrRenderControlPoll();')
    text = m.replace_once(text, '#include "nr_settings_ui.inl"',
        '#include "interop/nr_render_control.inl"\n#include "nr_settings_ui.inl"')
    return text + '\n#include "interop/nr_native_bridge_consumer.inl"\n'

def patch_ui(text):
    text = old_ui(text)
    return m.replace_once(text,
        '    if (ImGui::CollapsingHeader(ui("Advanced picture controls", "高级画质"))) {',
        '    DrawRenderControl();\n\n    if (ImGui::CollapsingHeader(ui("Advanced picture controls", "高级画质"))) {')

def patch_bridge(text):
    text = old_bridge(text)
    text = '#include "nr_bridge_baseline.h"\n' + text
    return m.replace_once(text, '    __try { return fn(list, feature, p, nullptr); }',
        '''    __try {
        // This list belongs to this carrier, not to the game. A known empty
        // compute baseline lets the unchanged r3 observer restore it exactly.
        if (!nr_bridge_baseline::Bind(list)) {
            Log("[native-carrier] private baseline unavailable; keep native SR");
            return static_cast<NVSDK_NGX_Result>(0xBAD00005u);
        }
        return fn(list, feature, p, nullptr);
    }''')

def patch_build(text):
    return old_build(text).replace("-beta0.4.3r3-", "-" + m.EXPERIMENT + "-")

def prepare(core, upstream, output):
    old_prepare(core, upstream, output)
    # The transport helper is our source, not an unreviewed upstream change.
    helper = output / "core/src/interop/nr_bridge_baseline.h"
    (output / "carrier/src/nr_bridge_baseline.h").write_bytes(helper.read_bytes())
    rc = output / "core/src/nr_before_sr.rc"
    rc.write_text(rc.read_text(encoding="utf-8").replace("beta0.4.3r3", m.EXPERIMENT),
                  encoding="utf-8", newline="\n")
    # Safe defaults; never turn on the broad null-PSO experiment globally.
    ini = output / "core/config/nr_before_sr.ini"
    with ini.open("a", encoding="utf-8", newline="\n") as stream:
        stream.write("\n; Experimental true-render query override; 0 = game-owned.\nRenderScalePercent=0\n")
    manifest_file = output / "source-manifest.json"
    manifest = json.loads(manifest_file.read_text())
    manifest["core_pins"] = m.CORE_BLOBS
    manifest["verification"] = "generated source identity; not game validation"
    manifest["files"] = {p.relative_to(output).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sorted(output.rglob("*")) if p.is_file() and p != manifest_file}
    manifest_file.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

m.patch_core, m.patch_ui, m.patch_bridge, m.patch_build, m.prepare = patch_core, patch_ui, patch_bridge, patch_build, prepare
if __name__ == "__main__": m.main()
