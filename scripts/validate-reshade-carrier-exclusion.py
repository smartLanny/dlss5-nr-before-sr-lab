#!/usr/bin/env python3
"""Use ReShade's real INI parser/filter and a Windows DllMain sentinel.

This validates ordinary directory preloading, not a complete ReShade/game run.
Output is retained for inspection. Never point this harness at a game directory.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PIN = "4a50d1eddace85734871d91792ff214f13f66c01"


def run(args, **kwargs):
    result = subprocess.run([str(x) for x in args], check=False, text=True, encoding="utf-8",
                            capture_output=True, timeout=90, **kwargs)
    if result.returncode:
        print(result.stdout + result.stderr, file=sys.stderr)
        result.check_returncode()
    return result.stdout.strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--reshade", type=Path, required=True)
    ap.add_argument("--compiler", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    if os.name != "nt":
        ap.error("Requires Windows; skipped is not a passing loader test")
    reshade = args.reshade.resolve()
    if run(["git", "-C", reshade, "rev-parse", "HEAD"]) != PIN:
        ap.error("Wrong ReShade revision")
    if run(["git", "-C", reshade, "status", "--porcelain", "--untracked-files=no"]):
        ap.error("Pinned ReShade checkout must be clean")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source = (reshade / "source/addon_manager.cpp").read_text(encoding="utf-8")
    start = source.index("\t\t// Avoid loading library altogether when it is found in the disabled add-on list")
    end = source.index('\n\t\tlog::message(log::level::info, "Loading add-on from', start)
    block = source[start:end]
    template = (ROOT / "tests/native_bridge/reshade_preload_harness.cpp.in").read_text(encoding="utf-8")
    cpp = out / "preload.cpp"
    cpp.write_text(template.replace("// @PINNED_PRELOAD_FILTER@", block), encoding="utf-8")
    exe = out / "preload.exe"
    flags = ["-std=c++17", "-O2", "-static"]
    # ReShade's project precompiled header supplies the Windows sharing modes.
    run([args.compiler, *flags, "-municode", "-include", "share.h", "-I" + str(reshade / "source"),
         "-I" + str(reshade / "deps/utfcpp/source"), cpp,
         reshade / "source/ini_file.cpp", "-o", exe])
    dll = out / "sentinel.addon64"
    run([args.compiler, *flags, "-shared", ROOT / "tests/native_bridge/preload_sentinel.cpp", "-o", dll])
    results = []
    # The name-only negative control must map the DLL. ReShade can reject it
    # later at registration, but that is too late to avoid DllMain side effects.
    for name, ini_text, excluded in (
        ("enabled", "[ADDON]\n", False),
        ("name-only", "[ADDON]\nDisabledAddons=DLSS 5 Bridge 1.4.12\n", False),
        ("filename", "[ADDON]\nDisabledAddons=@dlss5-native-carrier-probe.addon64\n", True),
        ("named-filename", "[ADDON]\nDisabledAddons=Bridge@dlss5-native-carrier-probe.addon64\n", True),
        ("case-mismatch", "[ADDON]\nDisabledAddons=@DLSS5-native-carrier-probe.addon64\n", False),
        ("repeated-key", "[ADDON]\nDisabledAddons=Other,, add-on@other.addon64\n[ADDON]\nDisabledAddons=@dlss5-native-carrier-probe.addon64\n", True),
    ):
        folder = out / name
        folder.mkdir()
        ini = folder / "ReShade.ini"
        ini.write_text(ini_text, encoding="utf-8")
        target = folder / "dlss5-native-carrier-probe.addon64"
        shutil.copyfile(dll, target)
        marker = folder / "entered.txt"
        env = dict(os.environ, NR_PRELOAD_SENTINEL=str(marker))
        result = run([exe, ini, target], env=env)
        assert result == ("loaded=0 excluded=1" if excluded else "loaded=1 excluded=0"), (name, result)
        assert marker.exists() != excluded, (name, "DllMain marker disagrees")
        results.append({"case": name, "excluded_before_load": excluded, "dllmain_entered": marker.exists()})
    helper = ROOT / "scripts/configure-carrier-exclusion.py"
    # Exercise the shipped writer through its CLI, then let the real parser and
    # loader decide. Do not use the helper's own parser as the exclusion oracle.
    for api, case, filename, subdir, original in (
        ("d3d12", "tool-d3d12", "dlss5-native-carrier-probe.addon64", ".",
         b"[ADDON]\nDisabledAddons=DLSS 5 Bridge 1.4.12\n"),
        ("vulkan", "tool-vulkan-bom", "DLSS5-native-carrier-\u6d4b\u8bd5.addon64", ".",
         "\ufeff[GENERAL]\r\nLabel=\u4fdd\u7559\r\n".encode("utf-8")),
        ("d3d12", "tool-addonpath", "dlss5-native-carrier-one,two.addon64", "mods,custom",
         b"[ADDON]\nAddonPath=mods,,custom\nDisabledAddons=Other,, name@other.addon64\n"),
        ("d3d12", "tool-bridge-alias", "dlss5-bridge.addon64", ".", b"[ADDON]\n"),
        ("d3d11", "tool-d3d11-unchanged", "dlss5-native-carrier-probe.addon64", ".", b"[ADDON]\n"),
        ("unknown", "tool-unknown-unchanged", "dlss5-native-carrier-probe.addon64", ".", b"[ADDON]\n"),
    ):
        folder = out / case
        folder.mkdir()
        addon_dir = folder / subdir
        addon_dir.mkdir(exist_ok=True)
        ini = folder / "ReShade.ini"
        ini.write_bytes(original)
        target = addon_dir / filename
        shutil.copyfile(dll, target)
        untouched = addon_dir / "unrelated.addon64"
        shutil.copyfile(dll, untouched)
        marker = folder / "entered.txt"
        env = dict(os.environ, NR_PRELOAD_SENTINEL=str(marker), PYTHONIOENCODING="utf-8")
        cmd = [sys.executable, helper, "--ini", ini, "--api", api]
        preview = json.loads(run(cmd, env=env))
        assert ini.read_bytes() == original and not list(folder.glob("*.receipt.json"))
        applied = json.loads(run([*cmd, "--apply"], env=env))
        excluded = api in ("d3d12", "vulkan")
        assert applied["status"] == ("applied" if excluded else "no-change-api"), applied
        assert ini.read_bytes().startswith(original)
        result = run([exe, ini, target], env=env)
        assert result == ("loaded=0 excluded=1" if excluded else "loaded=1 excluded=0"), (case, result)
        assert marker.exists() != excluded, case
        entered = marker.exists()
        if marker.exists():
            marker.unlink()
        assert run([exe, ini, untouched], env=env) == "loaded=1 excluded=0"
        assert marker.exists(), "unrelated add-on must still enter DllMain"
        marker.unlink()
        if excluded:
            assert json.loads(run([*cmd, "--apply"], env=env))["status"] == "already-excluded"
            restored = json.loads(run([sys.executable, helper, "--ini", ini,
                                      "--restore", applied["receipt"]], env=env))
            assert restored["status"] == "restored" and ini.read_bytes() == original
            assert run([exe, ini, target], env=env) == "loaded=1 excluded=0"
            assert marker.exists(), "restored configuration must admit DllMain again"
        else:
            assert ini.read_bytes() == original and not list(folder.glob("*.receipt.json"))
        results.append({"case": case, "cli_preview_status": preview["status"],
                        "excluded_before_load": excluded, "dllmain_entered": entered,
                        "unrelated_addon_loaded": True, "byte_exact_restore_verified": excluded})
    evidence = {"reshade_commit": PIN, "production_ini_parser": True,
                "configuration_tool_sha256": hashlib.sha256(helper.read_bytes()).hexdigest(),
                "production_preload_filter_sha256": hashlib.sha256(block.encode()).hexdigest(),
                "cases": results, "full_reshade_runtime_verified": False,
                "game_runtime_verified": False}
    (out / "validation.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(f"PASS {len(results)} real Windows loader/production ReShade parser-filter cases; no game claim")


if __name__ == "__main__":
    main()
