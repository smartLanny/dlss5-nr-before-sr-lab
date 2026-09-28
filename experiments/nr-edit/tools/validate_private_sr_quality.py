"""Run controlled 16-frame private SR transport attribution on WARP.

--core accepts the generated stage/core tree from a unified build. All source
and shader inputs are taken from that tree and checked against its manifest.
This run makes no Feature1, hardware, game, or visual acceptance claim.
"""
import argparse
import hashlib
import json
import math
import subprocess
import sys
from pathlib import Path

PACKAGE = Path(__file__).resolve().parents[1]
ROOT = PACKAGE.parents[1]
MODULE = ("nr_edit_dx12", "nr_post_residual_dx12", "nr_post_private_sr_dx12")
SHADERS = ("nr_rr_edit.hlsl", "nr_deferred_edit.hlsl", "nr_normalize_guides.hlsl",
           "nr_post_residual.hlsl", "nr_edit_scalar.hlsli", "nr_edit_carrier.hlsli")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--carrier-range", type=float, default=1.0)
    parser.add_argument("--quality-dark", action="store_true", help="Add low-luminance 1px line, hair curve and TEXT bitmap ROI")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--source", type=Path, help="experimental nr-edit package root")
    source.add_argument("--core", type=Path, help="generated stage/core root")
    args = parser.parse_args()
    if not math.isfinite(args.carrier_range) or not 1e-6 < args.carrier_range <= 1e6:
        parser.error("carrier range must be finite and within (1e-6, 1e6]")
    compiler = args.compiler.resolve(strict=True)
    core = args.core.resolve(strict=True) if args.core else None
    package = core/"experiments/nr-edit" if core else args.source.resolve(strict=True)
    output = args.output.resolve()
    if output.exists():
        parser.error("Use a fresh output directory; keep failed evidence")
    output.mkdir(parents=True)
    files = [package/"src"/(name+extension) for name in MODULE
             for extension in (".cpp", ".hpp")]
    files += [package/"shaders"/name for name in SHADERS]
    files += [package/"tools/compile_edit_shader.cpp",
              package/"tools/compile_edit_artifacts.py",
              PACKAGE/"tests/post_residual_gpu.cpp",
              PACKAGE/"tests/post_private_sr_gpu.cpp",
              PACKAGE/"tests/windows_shader_validation.cpp",
              PACKAGE/"tools/analyze_private_sr_quality.py",
              ROOT/"src/interop/nr_ngx_parameter_abi.h"]
    report = {"schema": "private-sr-controlled-quality-warp-v1", "status": "running",
              "source_kind": "generated-core" if core else "experiment",
              "source_root": str(package), "compiler_sha256": sha(compiler),
              "carrier_range": args.carrier_range,
              "encoding_policy": "post_legacy_range4_neutral",
              "zero_preserve": True,
              "scene": "dark_text_hair_v2" if args.quality_dark else "legacy_v1",
              "source_hashes": {str(path): sha(path) for path in files}, "commands": [],
              "feature1": "not-run", "game": "not-run", "quality_acceptance": "unjudged"}

    def save():
        (output/"validation.json").write_text(json.dumps(report, indent=2)+"\n")

    def run(name, command, timeout=240):
        try:
            result = subprocess.run([str(item) for item in command], cwd=output,
                                    capture_output=True, timeout=timeout)
            code, log = result.returncode, result.stdout+result.stderr
        except subprocess.TimeoutExpired as error:
            code, log = 124, (error.stdout or b"")+(error.stderr or b"")+b"\nTIMEOUT\n"
        path = output/(name+".log")
        path.write_bytes(log)
        report["commands"].append({"name": name, "command": list(map(str, command)),
                                   "exit": code, "log_sha256": sha(path)})
        save()
        print(name, code, log.decode("utf-8", "replace")[-1000:], flush=True)
        if code:
            raise RuntimeError(name+" failed")

    save()
    try:
        if core:
            manifest_path = core.parent/"source-manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
            for relative, digest in manifest["files"].items():
                file = (manifest_path.parent/relative).resolve(strict=True)
                if not file.is_relative_to(manifest_path.parent) or sha(file) != digest:
                    raise RuntimeError("Generated source manifest mismatch: "+relative)
            report["source_commit"] = manifest["source_commit"]
            report["source_manifest_sha256"] = sha(manifest_path)
            for name in MODULE:
                staged = core/"src/private_sr"/(name+".hpp")
                if sha(staged) != sha(package/"src"/(name+".hpp")):
                    raise RuntimeError("Generated Core module header mismatch: "+name)
            save()
        shader_compiler = output/"compile-edit-shader.exe"
        run("build-shader-compiler", [compiler, "-std=c++20", "-O2", "-static", "-municode",
                                      package/"tools/compile_edit_shader.cpp", "-ld3dcompiler",
                                      "-o", shader_compiler])
        shaders = output/"shaders"
        run("compile-shaders", [sys.executable, package/"tools/compile_edit_artifacts.py",
                                "--compiler", shader_compiler, "--source", package/"shaders",
                                "--output", shaders])
        common = [compiler, "-std=c++20", "-O2", "-static", "-municode",
                  "-I"+str(ROOT), "-I"+str(package/"src")]
        libs = ["-ld3d12", "-ldxgi", "-ldxguid", "-ld3dcompiler", "-lole32", "-luuid"]
        residual = output/"post-residual.exe"
        run("build-residual", [*common, PACKAGE/"tests/post_residual_gpu.cpp",
                               package/"src/nr_edit_dx12.cpp",
                               package/"src/nr_post_residual_dx12.cpp", *libs, "-o", residual])
        run("warp-residual", [residual, shaders])
        private = output/"post-private-sr.exe"
        run("build-private", [*common, PACKAGE/"tests/post_private_sr_gpu.cpp",
                              *[package/"src"/(name+".cpp") for name in MODULE],
                              *libs, "-o", private])
        capture = output/"capture"
        run("warp-quality-capture", [private, shaders, "--quality-dir", capture,
                                     "--carrier-range", args.carrier_range,
                                     "--tiny-range", args.carrier_range,
                                     *(["--quality-dark"] if args.quality_dark else [])], timeout=300)
        run("analyze", [sys.executable, PACKAGE/"tools/analyze_private_sr_quality.py",
                        capture], timeout=300)
        report["capture_analysis_sha256"] = sha(capture/"analysis.json")
        report["binary_sha256"] = sha(private)
        report["sources_unchanged"] = all(sha(Path(path)) == digest for path, digest in
                                          report["source_hashes"].items())
        if not report["sources_unchanged"]:
            raise RuntimeError("Source changed during quality gate")
        report["status"] = "passed"
    except Exception as error:
        report["status"] = "failed"
        report["error"] = str(error)
    save()
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
