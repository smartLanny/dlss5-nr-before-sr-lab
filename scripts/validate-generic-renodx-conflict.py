"""Validate the Generic RenoDX collision latch and every outer NGX hook guard."""
from pathlib import Path
import argparse
import datetime
import json
import os
import subprocess


HOOKS = {
    "NgxResult HookCreate(": ("g_create_hook_depth == 0", "g_wrapper_create_entries.fetch_add"),
    "NgxResult HookCoreCreate(": ("g_create_hook_depth == 0", "g_core_create_entries.fetch_add"),
    "NgxResult HookDlssdCreate(": ("g_create_hook_depth == 0", "g_dlssd_create_entries.fetch_add"),
    "NgxResult HookEvaluate(": ("!g_inside_ngx_evaluate", "ObserveNullEvaluateHandle"),
    "NgxResult HookEvaluateC(": ("!g_inside_ngx_evaluate", "ObserveNullEvaluateHandle"),
    "NgxResult HookCoreEvaluate(": ("!g_inside_ngx_evaluate", "ObserveNullEvaluateHandle"),
    "NgxResult HookDlssdEvaluate(": ("!g_inside_ngx_evaluate", "ObserveNullEvaluateHandle"),
    "NgxResult HookRelease(": ("g_release_hook_depth == 0", "g_release_hook_depth++"),
    "NgxResult HookCoreRelease(": ("g_release_hook_depth == 0", "g_release_hook_depth++"),
    "NgxResult HookDlssdRelease(": ("g_release_hook_depth == 0", "g_release_hook_depth++"),
}


def extract_function(source: str, signature: str) -> str:
    start = source.index(signature)
    begin = source.index("{", start)
    depth = 0
    i = begin
    mode = "code"
    while i < len(source):
        c = source[i]
        pair = source[i:i + 2]
        if mode == "line":
            if c == "\n":
                mode = "code"
        elif mode == "block":
            if pair == "*/":
                mode = "code"
                i += 1
        elif mode in ('"', "'"):
            if c == "\\":
                i += 1
            elif c == mode:
                mode = "code"
        elif pair == "//":
            mode = "line"
            i += 1
        elif pair == "/*":
            mode = "block"
            i += 1
        elif c in ('"', "'"):
            mode = c
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
        i += 1
    raise ValueError("unterminated function " + signature)


def discover_toolchain(explicit: str | None) -> Path:
    candidates = [explicit, os.environ.get("DLSS5_LLVM_MINGW_ROOT")]
    root = Path(__file__).resolve().parents[1]
    candidates += [
        root.parent / "toolchain" / "llvm-mingw",
        root.parents[1] / "toolchains" / "llvm-mingw",
        Path("C:/llvm-mingw"),
    ]
    candidates += sorted((root.parents[1] / "toolchains").glob("llvm-mingw-*"))
    for candidate in candidates:
        if not candidate:
            continue
        path = Path(candidate)
        if path.name.lower().endswith("clang++.exe") and path.is_file():
            return path
        compiler = path / "bin" / "x86_64-w64-mingw32-clang++.exe"
        if compiler.is_file():
            return compiler
    raise SystemExit("--toolchain-root is required (or set DLSS5_LLVM_MINGW_ROOT)")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--toolchain-root")
    parser.add_argument("--output")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = (root / "src/nr_before_sr.cpp").read_text(encoding="utf-8-sig")
    feedback = (root / "src/nr_feedback_capture.inl").read_text(encoding="utf-8-sig")
    ui = (root / "src/nr_settings_ui.inl").read_text(encoding="utf-8-sig")

    latch = extract_function(source, "bool LatchGenericRenoDxConflictIfLoaded(")
    set_bypass = extract_function(source, "void SetBypassReason(")
    create_passthrough = extract_function(source, "NgxResult CallNativeCreatePassThrough(")
    assert 'GetModuleHandleW(kGenericRenoDxModuleName)' in latch
    assert 'candidate == g_addon_module.load' in latch
    assert 'compare_exchange_strong' in latch
    assert 'BypassReason::GenericRenoDxConflict' in latch
    assert 'MH_DisableHook' not in latch and 'MH_RemoveHook' not in latch
    first_exchange = set_bypass.index('g_last_bypass_reason.exchange')
    assert set_bypass.index('g_generic_renodx_conflict.load') < first_exchange
    assert set_bypass.find('g_generic_renodx_conflict.load', first_exchange) > first_exchange
    assert set_bypass.count('g_last_bypass_reason.exchange') >= 2
    assert '*out_handle = nullptr' in create_passthrough
    assert 'g_generic_renodx_conflict.store(false' not in source
    assert 'g_generic_renodx_conflict = false' not in source

    guarded = []
    for signature, (outer, first_side_effect) in HOOKS.items():
        body = extract_function(source, signature)
        guard = body.index("LatchGenericRenoDxConflictIfLoaded()")
        assert outer in body[:body.index(first_side_effect)], signature + " lacks outermost guard"
        assert guard < body.index(first_side_effect), signature + " observes/counters before conflict guard"
        assert "return" in body[guard:body.index(first_side_effect)], signature + " does not directly pass through"
        if "Create(" in signature:
            guarded_prefix = body[guard:body.index(first_side_effect)]
            assert 'CallNativeCreatePassThrough' in guarded_prefix, signature + " does not clear the native output slot"
        guarded.append(signature)

    reason_branch = 'else if (reason == BypassReason::GenericRenoDxConflict)'
    if reason_branch in ui:
        generic_ui_start = ui.index(reason_branch)
        generic_ui_end = ui.index('else if', generic_ui_start + len('else if'))
        generic_ui = ui[generic_ui_start:generic_ui_end]
    else:
        generic_ui_start = ui.index(
            'if (g_generic_renodx_conflict.load')
        generic_ui_end = ui.index('else if', generic_ui_start)
        generic_ui = ui[generic_ui_start:generic_ui_end]
    assert 'renodx-dlss5.addon64' in generic_ui
    assert 'restart the game' in generic_ui
    assert '重启游戏' in generic_ui
    for token in (
        'genericRenoDxConflict',
        'competingAddon.module=%s',
        'renodx-dlss5.addon64',
        'remove it and restart',
    ):
        assert token in feedback, "feedback is missing " + token

    compiler = discover_toolchain(args.toolchain_root)
    out = Path(args.output).resolve() if args.output else root / "test-output" / (
        "generic-renodx-conflict-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    out.mkdir(parents=True, exist_ok=False)
    template = (root / "tests/generic_renodx_conflict_harness.cpp.in").read_text(encoding="utf-8")
    generated = template.replace("// @production_set_bypass", set_bypass)
    generated = generated.replace("// @production_latch", latch)
    generated = generated.replace("// @production_create_passthrough", create_passthrough)
    cpp = out / "generic-renodx-conflict-harness.cpp"
    cpp.write_text(generated, encoding="utf-8")
    dummy_cpp = out / "generic-renodx-dummy.cpp"
    dummy_cpp.write_text('extern "C" __declspec(dllexport) int fixture() { return 1; }\n', encoding="utf-8")
    dummy = out / "renodx-dlss5.addon64"
    exe = out / "generic-renodx-conflict-harness.exe"
    commands = [
        [str(compiler), "-std=c++20", "-O2", "-shared", str(dummy_cpp), "-o", str(dummy)],
        [str(compiler), "-std=c++20", "-O2", "-static", "-municode", str(cpp), "-o", str(exe)],
    ]
    logs = []
    for command in commands:
        result = subprocess.run(command, capture_output=True, text=True)
        logs.append(result.stdout + result.stderr)
        if result.returncode:
            (out / "build.log").write_text("\n".join(logs), encoding="utf-8")
            raise SystemExit(result.returncode)
    (out / "build.log").write_text("\n".join(logs), encoding="utf-8")
    ran = subprocess.run([str(exe), str(dummy)], capture_output=True, text=True, timeout=10)
    (out / "native.log").write_text(ran.stdout + ran.stderr, encoding="utf-8")
    summary = {
        "status": "PASS" if ran.returncode == 0 else "FAIL",
        "guardedHooks": guarded,
        "native": ran.stdout.strip(),
        "limitations": [
            "The production latch is compiled unchanged against a real LoadLibrary/GetModuleHandle pair.",
            "A renamed copy of the add-on is excluded by exact HMODULE identity.",
            "Hook guard placement is source-structural; no live NGX runtime or MinHook trampoline is executed.",
        ],
    }
    (out / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))
    print("Evidence: " + str(out))
    raise SystemExit(ran.returncode)


if __name__ == "__main__":
    main()
