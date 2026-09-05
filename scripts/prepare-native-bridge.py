#!/usr/bin/env python3
"""Build-input staging only: pinned native DX11 carrier for beta0.4.2.

No network, deployment, binary patching or source-tree mutation. Run against a
local checkout of the specified upstream commit. The result is an explicitly
experimental core and companion source tree, not a signed release package.
"""
from __future__ import annotations
import argparse
import hashlib
import io
import json
import subprocess
import tarfile
import tempfile
from pathlib import Path, PurePosixPath

BASE = "15909ef10914fcdf151ee193460451667fbf31dc"
UPSTREAM = "29baa4cc56e0aeb21219de16528e697b91522efc"
CORE_BLOBS = {
    "src/nr_before_sr.cpp": "f83dce580dc2c619b696d0d7046bd7c72d863b8c",
    "src/nr_settings_ui.inl": "c68be30e0c892012f1c065b17106c557a38be424",
    "scripts/build.ps1": "db037e84d473f4d8c0d0e895da6e5706afaa8397",
}
UPSTREAM_BLOBS = {
    "src/bridge.inc": "244e3ae373b33b40763dc36e8ce967e152cbb505",
    "src/dlss5-bridge.cpp": "7a0365a35727d41506e3cce038d2fd6daca402f6",
}
EXPERIMENT = "beta0.4.2-native-bridge-exp1"


def git(root: Path, *args: str) -> bytes:
    return subprocess.run(["git", "-C", str(root), *args], check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


def blob(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


def replace_once(text: str, old: str, new: str) -> str:
    count = text.count(old)
    if count != 1:
        raise ValueError(f"Patch anchor count {count}, expected 1: {old[:100]!r}")
    return text.replace(old, new, 1)


def patch_core(text: str) -> str:
    for name in ("kAddonVersion", "kBuildIdentity"):
        text = replace_once(text, f'{name} = "beta0.4.2";', f'{name} = "{EXPERIMENT}";')
    return text + '\n#include "interop/nr_native_bridge_consumer.inl"\n'


def patch_ui(text: str) -> str:
    text = replace_once(text,
        "This ReShade window is using Direct3D 11, so DLSS 5 NR cannot run. Switch the game to Direct3D 12, then install ReShade Full Add-on for Direct3D 12.",
        "DX11 host: this experimental build needs the native-DLSS carrier. Native or mod-provided DLSS supplies the inputs; check actual NR submissions in diagnostics. Without the carrier, this core is still D3D12-only.")
    return replace_once(text,
        "当前 ReShade 画面使用 Direct3D 11，本插件无法运行。请把游戏切换到 Direct3D 12，并为 D3D12 安装 ReShade Full Add-on。",
        "DX11 实验路径需要配套原生 DLSS 桥；输入可来自游戏或已有 DLSS 模组。请在诊断中核对 NR 实际提交；未接桥时本核心仍仅支持 D3D12。")


def patch_build(text: str) -> str:
    # Staged source differs from the tracked release source by design.
    text = replace_once(text, "$SourceDirty = if ($TrackedChanges.Count -eq 0) { '0' } else { '1' }",
                        "$SourceDirty = '1' # Generated experimental source; see source-manifest.json")
    return text.replace("-beta0.4.2-", "-beta0.4.2-native-bridge-exp1-")


def patch_bridge(text: str) -> str:
    text = '#include "nr_native_bridge_client.h"\n' + text
    defaults = "static BridgeCfg g_cfg = { 2, -1, 1, 0, 0, 3, 1, 1, 1, 1, CFG_SRC_AUTO, 0, 1, 0, 0, 0, 2, 20, 0, 0, 0, 0, 0, 0 };"
    policy = """static void NrNativeBridgePolicy(BridgeCfg& c) {
    c.unwrap = 0; c.unwrap_list = 0; c.ngx_loader = 1;
    c.synth = 0; c.synth_after = 0; c.flags = -1; c.shape = 0;
    c.source = c.source == CFG_SRC_OFF ? CFG_SRC_OFF : CFG_SRC_MIRROR;
}
static BridgeCfg g_cfg = [] {
    BridgeCfg c = { 2, -1, 1, 0, 0, 3, 1, 1, 1, 0, CFG_SRC_MIRROR, 0, 0, 0, 0, 0, 2, 20, 0, 0, 0, 1, 0, 0 };
    NrNativeBridgePolicy(c); return c;
}();"""
    text = replace_once(text, defaults, policy)
    text = replace_once(text, "    if (next.stage < 0 || next.stage > 3) next.stage = g_cfg.stage;",
                        "    NrNativeBridgePolicy(next);\n    if (next.stage < 0 || next.stage > 3) next.stage = g_cfg.stage;")
    text = replace_once(text,
        "    if (g_bridge.disabled || !g_bridge.session_ready || gp == nullptr) return;",
        """    if (g_bridge.disabled || !g_bridge.session_ready || gp == nullptr) return;
    // Read the live config even when source=off, so it can be turned on again.
    CfgReload();
    if (g_cfg.source == CFG_SRC_OFF ||
        !nr_native_bridge_client::AllowsFrame(!g_bridge.frame_ready)) {
        g_bridge.need_reset = true;
        return;
    }""")
    text = replace_once(text, "    if (!ok) { BridgeReleaseTextures(); return false; }",
        """    if (g_bridge.depth_placeholder) {
        Log("[native-carrier] real depth transport failed; native SR retained.");
        ok = false;
    }
    if (!ok) { BridgeReleaseTextures(); return false; }""")
    # Refuse both conversion and interface failure; a stale exposure is not a
    # substitute for an exposure resource the game actually supplied.
    old_exposure = """            ID3D11Texture2D *gt = nullptr;
            if (SUCCEEDED(ge->QueryInterface(__uuidof(ID3D11Texture2D),
                                             reinterpret_cast<void **>(&gt))) && gt != nullptr)
            {
                if (BridgeEnsureExposure(dev11, gt))
                    ctx->CopyResource(g_bridge.exp11, gt);
                gt->Release();
            }"""
    new_exposure = """            ID3D11Texture2D *gt = nullptr;
            const bool exposure_texture = SUCCEEDED(ge->QueryInterface(
                __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&gt))) && gt != nullptr;
            const bool exposure_ok = exposure_texture && BridgeEnsureExposure(dev11, gt);
            if (exposure_ok) ctx->CopyResource(g_bridge.exp11, gt);
            if (gt != nullptr) gt->Release();
            if (!exposure_ok) {
                g_bridge.need_reset = true;
                for (int i = 0; i < SLOT_COUNT; ++i) g[i]->Release();
                dev11->Release();
                return;
            }"""
    text = replace_once(text, old_exposure, new_exposure)
    text = replace_once(text, "        // mode 0 writes nothing back",
        "        // Set only after this call queued a real copy into the game output.\n"
        "        g_nr_native_delivery = mode == 2;\n        // mode 0 writes nothing back")
    text = replace_once(text,
        "    EnterCriticalSection(&g_bridge_cs);\n    int slot = -1;",
        "    EnterCriticalSection(&g_bridge_cs);\n"
        "    g_nr_native_history.Created(reinterpret_cast<std::uintptr_t>(handle));\n    int slot = -1;")
    return text


def patch_host(text: str, forward: str) -> str:
    start = '    Breadcrumb("forwarding the game\'s DLSS evaluate");\n'
    stop = "static NVSDK_NGX_Result ForwardCreate(Hook &h, ID3D11DeviceContext *ctx, int feature_id,"
    if text.count(start) != 1 or text.count(stop) != 1:
        raise ValueError("ForwardEvaluate boundary changed")
    a, b = text.index(start), text.index(stop)
    if b <= a or not text[a:b].rstrip().endswith("return r;\n}"):
        raise ValueError("ForwardEvaluate closing boundary changed")
    text = text[:a] + forward + text[b:]
    # Force native-only at the startup key reader as well as the live config.
    # Match a call, not the general-purpose helper definition.
    text = replace_once(text, 'g_vk_mirror = CfgKeyInt("vk_mirror", 1) != 0 ? 1 : 0;',
                        'g_vk_mirror = 0; // Native DX11 experiment; no Vulkan hooks.')
    return text


def archive_sources(root: Path, paths: list[str], destination: Path) -> None:
    data = git(root, "archive", "HEAD", *paths)
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        for member in archive.getmembers():
            path = PurePosixPath(member.name)
            if path.is_absolute() or ".." in path.parts or not (member.isfile() or member.isdir()):
                raise ValueError(f"Unsafe source archive member: {member.name}")
            out = destination.joinpath(*path.parts)
            if member.isdir(): out.mkdir(parents=True, exist_ok=True)
            else:
                out.parent.mkdir(parents=True, exist_ok=True)
                stream = archive.extractfile(member)
                if stream is None: raise ValueError("Missing archive member")
                out.write_bytes(stream.read())


def verified(root: Path, expected: dict[str, str]) -> None:
    for path, sha in expected.items():
        if blob(git(root, "show", f"HEAD:{path}")) != sha:
            raise ValueError(f"Unreviewed source revision: {path}")


def prepare(core: Path, upstream: Path, output: Path) -> None:
    if output.exists(): raise ValueError("Output must not exist; refusing to overwrite")
    if git(upstream, "rev-parse", "HEAD").decode().strip() != UPSTREAM:
        raise ValueError("Wrong upstream commit")
    for root in (core, upstream):
        if git(root, "status", "--porcelain", "--untracked-files=no").strip():
            raise ValueError("Commit tracked edits before staging; only committed source is built")
    git(core, "merge-base", "--is-ancestor", BASE, "HEAD")
    verified(core, CORE_BLOBS); verified(upstream, UPSTREAM_BLOBS)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="nr-native-stage-", dir=output.parent) as tmp:
        stage = Path(tmp) / "stage"; stage.mkdir()
        archive_sources(core, ["src", "scripts/build.ps1", "config", "third_party"], stage / "core")
        archive_sources(upstream, ["src", "LICENSE"], stage / "carrier")
        def transform(relative: str, fn) -> None:
            path = stage / relative
            path.write_text(fn(path.read_text(encoding="utf-8")), encoding="utf-8", newline="\n")
        transform("core/src/nr_before_sr.cpp", patch_core)
        transform("core/src/nr_settings_ui.inl", patch_ui)
        transform("core/scripts/build.ps1", patch_build)
        transform("carrier/src/bridge.inc", patch_bridge)
        forward = (stage / "core/src/interop/nr_native_bridge_forward.inl").read_text(encoding="utf-8")
        transform("carrier/src/dlss5-bridge.cpp", lambda s: patch_host(s, forward))
        for name in ("nr_native_bridge_abi.h", "nr_native_bridge_history.h", "nr_native_bridge_client.h"):
            (stage / "carrier/src" / name).write_bytes((stage / "core/src/interop" / name).read_bytes())
        # A staged archive has no Git metadata; build.ps1 needs the source identity.
        build = stage / "core/scripts/build.ps1"
        source = git(core, "rev-parse", "HEAD").decode().strip()
        text = build.read_text(encoding="utf-8")
        text = replace_once(text, "$SourceCommit = (& git -C $ProjectRoot rev-parse HEAD).Trim()",
                            f"$SourceCommit = '{source}'\n$LASTEXITCODE = 0")
        text = replace_once(text, "$TrackedChanges = @(& git -C $ProjectRoot status --porcelain --untracked-files=no)",
                            "$TrackedChanges = @('generated-native-carrier')\n$LASTEXITCODE = 0")
        build.write_text(text, encoding="utf-8", newline="\n")
        manifest = {"experiment": EXPERIMENT, "source_commit": source, "release_base": BASE,
                    "carrier_commit": UPSTREAM, "signed_release": False, "files": {}}
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                manifest["files"][path.relative_to(stage).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
        (stage / "source-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        stage.rename(output)
    print(f"Staged {EXPERIMENT}: {output}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--core", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    prepare(args.core.resolve(), args.upstream.resolve(), args.output.resolve())


if __name__ == "__main__": main()
