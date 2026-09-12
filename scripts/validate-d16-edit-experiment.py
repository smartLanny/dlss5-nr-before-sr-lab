#!/usr/bin/env python3
"""Host tests and optional REAL SM5 compilation, never game/GPU validation."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

def run(argv: list[str]) -> None:
    print("+", " ".join(argv), flush=True)
    subprocess.run(argv, check=True)

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("rr", "deferred"), required=True)
    parser.add_argument("--cxx", default="g++", help="GCC or LLVM/MinGW C++ compiler")
    parser.add_argument("--shader-compiler", type=Path,
                        help="existing build/compile-codec-artifact.exe, from scripts/build.ps1")
    parser.add_argument("--output", type=Path, help="generated test output, outside tracked source")
    args = parser.parse_args()
    repository = Path(__file__).resolve().parents[1]
    output = (args.output or repository / "build" / ("d16-edit-" + args.mode)).resolve()
    output.mkdir(parents=True, exist_ok=True)
    tests = ("d16_edit_frame_contract_test", f"d16_{args.mode}_edit_math_test")
    suffix = ".exe" if sys.platform == "win32" else ""
    for name in tests:
        executable = output / (name + suffix)
        run([args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror",
             "-I" + str(repository / "src/experiments"), str(repository / "tests" / (name + ".cpp")),
             "-o", str(executable)])
        run([str(executable)])
    shader_dir = repository / "shaders/experiments"
    main_shader = shader_dir / ("nr_rr_edit.hlsl" if args.mode == "rr" else "nr_deferred_edit.hlsl")
    scalar = shader_dir / "nr_edit_scalar.hlsli"
    text = main_shader.read_text(encoding="utf-8")
    include = '#include "nr_edit_scalar.hlsli"'
    if text.count(include) != 1:
        raise ValueError("Expected exactly one reviewed local scalar include")
    flattened = output / "flattened-edit.hlsl"
    flattened.write_text(text.replace(include, scalar.read_text(encoding="utf-8")), encoding="utf-8")
    entries = ("PrepareGuidesCS", "AccumulateCS", "ComposeCS") if args.mode == "rr" else ("EncodeCS", "ComposeCS")
    shader_results: dict[str, str] = {}
    if args.shader_compiler:
        compiler = args.shader_compiler.resolve(strict=True)
        for entry in entries:
            destination = output / (entry + ".cso")
            # CLI verified against the pinned D16 scripts/compile-codec-artifact.cpp.
            run([str(compiler), str(flattened), entry, "0", "-1", str(destination)])
            if not destination.is_file() or destination.stat().st_size == 0:
                raise RuntimeError(f"Compiler did not emit {entry}")
            shader_results[entry] = hashlib.sha256(destination.read_bytes()).hexdigest()
    else:
        print("SKIPPED: SM5 compilation. Supply --shader-compiler on Windows.", flush=True)
    report = {
        "mode": args.mode, "host_tests": "passed", "host_platform": sys.platform,
        "source_sha256": {str(p.relative_to(repository)): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in (main_shader, scalar)},
        "sm5_compilation": "passed" if shader_results else "not-run",
        "cso_sha256": shader_results, "gpu_execution": "not-run",
        "nvidia_nr_sr": "not-run", "d16_runtime_integration": "not-implemented",
        "game_quality_performance": "not-run",
    }
    (output / "validation.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"FAILED: {error}", file=sys.stderr)
        raise SystemExit(1)
