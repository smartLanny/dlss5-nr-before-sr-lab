"""Build and run a fresh bounded real-Feature1 attribution host.

Only copies explicitly supplied existing NGX dependencies. No game injection,
game launch/termination, runtime download, or publication. Requires NumPy and
Pillow for offline attribution. Preserve failed output directories.
"""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

PACKAGE = Path(__file__).resolve().parents[1]
ROOT = PACKAGE.parents[1]


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("compiler", "nrchain", "sr-runtime", "output"):
        p.add_argument("--"+name, type=Path, required=True)
    p.add_argument("--output-size", type=int, nargs=2, default=(640, 384))
    p.add_argument("--work-size", type=int, nargs=2, default=(320, 192))
    p.add_argument("--scenario", choices=("frozen", "threshold", "matched-jitter"), default="frozen")
    p.add_argument("--legacy-rgb-support", action="store_true", help="Frozen legacy all-RGB support negative control")
    a = p.parse_args()
    compiler, bridge, runtime = (v.resolve(strict=True) for v in (a.compiler, a.nrchain, a.sr_runtime))
    out = a.output.resolve()
    if out.exists():
        p.error("Fresh output required; preserve previous evidence")
    if any(v <= 0 or v > 4096 for v in (*a.output_size, *a.work_size)):
        p.error("Bounded dimensions must be within 1..4096")
    if any(n > o or n < max(1, o//4) for n, o in zip(a.work_size, a.output_size)):
        p.error("Work must be within product transport's 25..100 percent contract")
    out.mkdir(parents=True)
    frozen = out/"frozen-source"
    package = frozen/"experiments/nr-edit"
    files = [PACKAGE/"src"/n for n in ("nr_edit_dx12.cpp", "nr_edit_dx12.hpp", "nr_private_sr_runtime.hpp",
             "nr_post_residual_dx12.cpp", "nr_post_residual_dx12.hpp", "nr_post_private_sr_dx12.cpp",
             "nr_post_private_sr_dx12.hpp", "nr_rr_dx12.hpp", "nr_deferred_dx12.hpp")]
    files += [PACKAGE/"tests"/n for n in ("private_sr_attribution_host.cpp", "post_private_sr_gpu.cpp", "windows_shader_validation.cpp")]
    files += [PACKAGE/"tools"/n for n in ("compile_edit_shader.cpp", "compile_edit_artifacts.py", "analyze_private_sr_attribution.py")]
    files += list((PACKAGE/"shaders").glob("*.hlsl"))+list((PACKAGE/"shaders").glob("*.hlsli"))
    files += [ROOT/"src/interop/nr_ngx_parameter_abi.h"]
    hashes = {}
    for file in files:
        relative = file.relative_to(ROOT)
        target = frozen/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file, target)
        hashes[str(relative)] = sha(target)
    shutil.copy2(bridge, out/"nrchain_nvngx.dll")
    shutil.copy2(runtime, out/"nvngx_dlss.dll")
    receipt = {"schema": "bounded-warmed-private-feature1-validation-v1", "status": "running",
               "game": "not-run", "quality_acceptance": "unjudged", "frames": 16, "warmup": 16,
               "scenario": a.scenario, "output": a.output_size, "work": a.work_size,
               "per_channel_support": not a.legacy_rgb_support,
               "jitter_scope": "test-only matched moving raster over static world signal; not fixed B/Z and not product normalization" if a.scenario == "matched-jitter" else "production post phase zero",
               "source_sha256": hashes, "compiler_sha256": sha(compiler),
               "private_dependency_sha256": {"nrchain_nvngx.dll": sha(bridge), "nvngx_dlss.dll": sha(runtime)},
               "commands": []}

    def save():
        (out/"validation.json").write_text(json.dumps(receipt, indent=2)+"\n", encoding="utf-8")

    def run(name, command, timeout=240):
        result = subprocess.run(list(map(str, command)), cwd=out, capture_output=True, timeout=timeout)
        log = out/(name+".log")
        log.write_bytes(result.stdout+result.stderr)
        receipt["commands"].append({"name": name, "command": list(map(str, command)),
                                    "exit": result.returncode, "log_sha256": sha(log)})
        save()
        print(name, result.returncode, (result.stdout+result.stderr).decode("utf-8", "replace")[-1000:], flush=True)
        if result.returncode:
            raise RuntimeError(name+" failed")
        return result

    save()
    try:
        shader_compiler = out/"compile-edit-shader.exe"
        run("build-shader-compiler", [compiler, "-std=c++20", "-O2", "-static", "-municode",
                                      package/"tools/compile_edit_shader.cpp", "-ld3dcompiler", "-o", shader_compiler])
        shaders = out/"shaders"
        run("compile-shaders", [sys.executable, package/"tools/compile_edit_artifacts.py",
                                "--compiler", shader_compiler, "--source", package/"shaders", "--output", shaders])
        binary = out/"private-sr-attribution.exe"
        run("build-host", [compiler, "-std=c++20", "-O2", "-static", "-municode", "-I"+str(frozen),
                           "-I"+str(package/"src"), package/"tests/private_sr_attribution_host.cpp",
                           *[package/"src"/(n+".cpp") for n in ("nr_edit_dx12", "nr_post_residual_dx12", "nr_post_private_sr_dx12")],
                           "-ld3d12", "-ldxgi", "-ldxguid", "-ld3dcompiler", "-lole32", "-luuid", "-o", binary])
        # The NTE launcher is a separate process; HTGame is its recorded renderer.
        guard = run("game-process-guard", ["powershell", "-NoProfile", "-Command",
                    "@(Get-CimInstance Win32_Process | Where-Object { $_.Name -match '^(HTGame|Cyberpunk2077|SOTTR|ShadowOfTheTombRaider)\\.exe$' } | Select-Object -ExpandProperty Name) | ConvertTo-Json -Compress"])
        if guard.stdout.decode("utf-8-sig", "replace").strip():
            raise RuntimeError("An actual game process is running; hardware host not started")
        run("actual-feature1", [binary, shaders, out/"nrchain_nvngx.dll", out/"capture",
                               *a.output_size, *a.work_size, a.scenario, "--bounded16",
                               *(["--legacy-rgb-support"] if a.legacy_rgb_support else [])], timeout=300)
        run("offline-attribution", [sys.executable, package/"tools/analyze_private_sr_attribution.py", out/"capture"], timeout=600)
        receipt["feature1"] = "actual-inference-completed"
        receipt["binary_sha256"] = sha(binary)
        receipt["analysis_sha256"] = sha(out/"capture/attribution.json")
        receipt["frozen_sources_unchanged"] = all(sha(frozen/r) == h for r, h in hashes.items())
        if not receipt["frozen_sources_unchanged"]:
            raise RuntimeError("Frozen source changed")
        receipt["status"] = "passed-controlled-contracts"
    except Exception as error:
        receipt["status"] = "failed"
        receipt["error"] = str(error)
    save()
    return 0 if receipt["status"] == "passed-controlled-contracts" else 1


if __name__ == "__main__":
    raise SystemExit(main())
