#!/usr/bin/env python3
"""Compare fixed auxiliary bytecode and actual Core pipeline creation on WARP."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from temporal_test_objects import objects_for

ROOT = Path(__file__).resolve().parents[1]
OVERLAYS = ("nr_compat_present_resources.inl", "nr_dual_spatial_d3d12.cpp",
            "nr_native_guide_snapshot.inl", "nr_rr_depth_adapter.inl",
            "nr_aux_shader_runtime.hpp", "nr_aux_shader_runtime.cpp")

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("compiler", "reshade", "core", "artifacts", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--run-warp", action="store_true", help="Execute only after coordinating the GPU validation slot.")
    parser.add_argument("--runtime-negative", action="store_true", help="Build the fallback but require zero actual D3DCompile calls.")
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        raise SystemExit("Preserve evidence; choose a new output directory")
    output.mkdir(parents=True)
    core = args.core.resolve()
    source = output / "core/src"
    shutil.copytree(core / "src", source)
    for name in OVERLAYS:
        shutil.copyfile(ROOT / "src" / name, source / name)
    artifacts = args.artifacts.resolve()
    artifact_receipt = json.loads((artifacts / "aux-artifacts.json").read_text())
    if artifact_receipt["count"] != 9:
        raise ValueError("Expected nine auxiliary artifacts")
    for name, digest in artifact_receipt["headers_sha256"].items():
        if sha(artifacts / name) != digest:
            raise ValueError("Embedded auxiliary header does not match receipt: " + name)
    fixture = (ROOT / "tests/aux_shader_core_harness.inl").read_text()
    if args.runtime_negative:
        old = "compile_calls==(NR_AUX_PRECOMPILED?0u:9u)"
        if fixture.count(old) != 1:
            raise ValueError("Runtime negative oracle drift")
        fixture = fixture.replace(old, "compile_calls==0u")
    (output / "fixture.inl").write_text(fixture, encoding="utf-8")
    wrapper = output / "aux-core.cpp"
    wrapper.write_text('#define CINTERFACE\n#define COBJMACROS\n#include "aux_shader_compile_observer.h"\n#include <dxgi1_4.h>\n' +
        "".join('#include "' + name + '"\n' for name in artifact_receipt["headers_sha256"]) +
        '#include "' + (source / "nr_before_sr.cpp").as_posix() + '"\n#include "nr_dual_embedded.hpp"\n#include "fixture.inl"\n' +
        'int main(){std::setvbuf(stdout,nullptr,_IONBF,0);aux_shader_test::Run();return 0;}\n', encoding="utf-8")
    compiler = args.compiler.resolve()
    c_compiler = compiler.with_name(compiler.name.replace("clang++", "clang"))
    common = ["-O1", "-DNDEBUG", "-DUNICODE", "-D_UNICODE", "-D_WIN64", "-ffunction-sections", "-fdata-sections",
              "-I" + str(ROOT / "third_party/minhook/include"), "-I" + str(args.reshade.resolve() / "include"),
              "-I" + str(args.reshade.resolve() / "deps/imgui"), "-I" + str(ROOT / "tests"),
              "-I" + str(source), "-I" + str(artifacts), "-DNR_AUX_TIMING=1"]
    record = {"schema": 1, "passed": False, "complete_core_translation_unit": True, "explicit_auxiliary_overlay": True,
              "base_core": str(core), "base_translation_unit_sha256": sha(core / "src/nr_before_sr.cpp"),
              "overlay_sha256": {name: sha(source / name) for name in OVERLAYS},
              "artifact_receipt_sha256": sha(artifacts / "aux-artifacts.json"), "fixture_sha256": sha(output / "fixture.inl"),
              "runtime_negative": args.runtime_negative, "warp_requested": args.run_warp,
              "gpu_dispatch": False, "ngx_verified": False, "game_verified": False, "cases": []}
    def save():
        (output / "validation.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    def run(command, name):
        result = subprocess.run(list(map(str, command)), cwd=output, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)
        (output / (name + ".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
        if result.returncode:
            print((result.stdout + result.stderr)[-10000:])
        return result
    objects = list(objects_for(core / "src/nr_before_sr.cpp"))
    for relative in ("buffer.c", "hook.c", "trampoline.c", "hde/hde64.c"):
        target = output / (Path(relative).stem + ".o")
        result = run([c_compiler, *common, "-std=c17", "-c", ROOT / "third_party/minhook/src" / relative, "-o", target], "compile-" + target.stem)
        if result.returncode:
            save();raise SystemExit(result.returncode)
        objects.append(target)
    for name, origin in (("timing", source / "nr_aux_shader_runtime.cpp"), ("observer", ROOT / "tests/aux_shader_compile_observer.cpp")):
        target = output / (name + ".o")
        result = run([compiler, *common, "-std=c++20", "-c", origin, "-o", target], "compile-" + name)
        if result.returncode:
            save();raise SystemExit(result.returncode)
        objects.append(target)
    for precompiled in ([0] if args.runtime_negative else [0, 1]):
        define = "-DNR_AUX_PRECOMPILED=" + str(precompiled)
        dual = output / ("dual-" + str(precompiled) + ".o")
        result = run([compiler, *common, define, "-include", ROOT / "tests/aux_shader_compile_observer.h", "-std=c++20", "-c",
                      source / "nr_dual_spatial_d3d12.cpp", "-o", dual], "compile-dual-" + str(precompiled))
        if result.returncode:
            save();raise SystemExit(result.returncode)
        exe = output / ("aux-core-" + str(precompiled) + ".exe")
        result = run([compiler, *common, define, "-std=c++20", "-static", "-Wl,--gc-sections", wrapper, dual, *objects, "-o", exe,
                      "-lkernel32", "-luser32", "-ld3d12", "-ldxgi", "-ldxguid", "-lole32", "-luuid", "-ld3dcompiler", "-lshell32"],
                     "compile-core-" + str(precompiled))
        if result.returncode:
            save();raise SystemExit(result.returncode)
        case = {"precompiled": bool(precompiled), "binary_sha256": sha(exe), "run_exit": None}
        record["cases"].append(case)
        if args.run_warp:
            result = run([exe], "run-" + str(precompiled));case["run_exit"] = result.returncode
            print(result.stdout, end="", flush=True)
            if args.runtime_negative:
                case["negative_rejected"] = result.returncode == 41 and "FAIL AUX_ACTUAL_D3DCOMPILE_CALLS_ORACLE" in result.stdout
                if not case["negative_rejected"]:
                    save();raise SystemExit(1)
            elif result.returncode:
                save();raise SystemExit(result.returncode)
        save()
    record["passed"] = True
    record["warp_verified"] = args.run_warp and not args.runtime_negative
    save()

if __name__ == "__main__":
    main()
