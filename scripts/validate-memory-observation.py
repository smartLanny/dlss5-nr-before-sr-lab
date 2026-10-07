"""CPU execution of the actual #330 context wiring and bounded observation contracts.

No D3D device is created and no GPU/NGX call is made. The allocation-info COM
service supplies exact independent byte values to the production resource walk.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def run(command, output, name, expected=0, marker=None):
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                            encoding="utf-8", errors="replace", timeout=180)
    (output / f"{name}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode != expected or (marker and marker not in result.stderr):
        raise RuntimeError(f"{name}: exit {result.returncode}, expected {expected}: {result.stderr[-4000:]}")
    return {"name": name, "command": command, "exit": result.returncode,
            "expected": expected, "marker": marker}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--source-root", type=Path, default=ROOT,
                        help="Actual assembled Core containing src; fixtures remain in this runner's checkout.")
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source_root = args.source_root.resolve(strict=True)
    runtime = (source_root / "src/nr_allocation_ledger_runtime.inl").read_text(encoding="utf-8")
    body = runtime[runtime.index("void CoreSlot("):runtime.index("void CompatContext(")]
    fixture = (ROOT / "tests/memory_observation_test.cpp.in").read_text(encoding="utf-8")
    controls = [
        ("positive", None, None, None, 0, None),
        ("remove-context-observation", "runtime", "auto* detail=ledger.BeginContext(context.instance_id,context.feature_generation,context.sr_handle,\n        context.nr_handle,context.layer_index,context.mode,phase,\n        context.create_dependencies_quarantined||context.lifecycle_dependencies_quarantined,context.memory_max_slots);",
         "auto* detail=static_cast<ContextDetail*>(nullptr);", 41, "PER_CONTEXT_OBSERVATION_REQUIRED"),
        ("remove-slot-high-water", "nr_memory_observation.h", "maximum=std::max(maximum,SlotCount(slots));", "(void)maximum;(void)slots;", 41, "CONTEXT_IDENTITY_STATE_SLOT_HIGH_WATER"),
        ("wrong-primary-role", "runtime", "core(slot.motion_proxy,Role::MotionProxy);", "core(slot.motion_proxy,Role::Other);", 41, "PRIMARY_RESOURCE_ROLE_ATTRIBUTION"),
        ("mix-timing-identity", "nr_memory_observation.h", "if(!(key_==key)){key_=key;count_=cursor_=0;}", "key_=key;", 41, "NEW_INSTANCE_MUST_NOT_MIX_OLD_TIMING"),
        ("stale-timing", "nr_memory_observation.h", "key_.config!=config||key_.history!=history||!count_", "((void)config,(void)history,false)||!count_", 41, "STALE_CONFIG_OR_HISTORY_MISSING"),
        ("cross-node-delta", "nr_model_memory_events.h", "e.before.node==e.after.node", "true", 41, "CROSS_NODE_DELTA_MUST_BE_UNKNOWN"),
        ("failed-event-usage-as-valid", "nr_model_memory_events.h", "const bool after_valid=e.after.attempted&&SUCCEEDED(e.after.local_result);", "const bool after_valid=true;", 41, "FAILED_EVENT_USAGE_MUST_BE_UNKNOWN"),
        ("missing-as-zero", "nr_memory_observation.h", "samples:0,p50Ms:missing,p99Ms:missing", "samples:0,p50Ms:0,p99Ms:0", 41, "MISSING_TREND_F8_FORMAT"),
        ("redact-owned-bytes", "nr_feedback_text.h", 'if(allocation.substr(0,8)=="context."){', 'if(false){', 41, "TYPED_F8_BYTES_MUST_SURVIVE_ADDRESS_PRIVACY"),
    ]
    commands = []
    for name, target, before, after, expected, marker in controls:
        directory = out / name
        directory.mkdir()
        variant = body
        if target == "runtime":
            if variant.count(before) != 1:
                raise RuntimeError(f"{name}: runtime mutation anchor changed")
            variant = variant.replace(before, after)
        elif target:
            source = (source_root / "src" / target).read_text(encoding="utf-8")
            if source.count(before) != 1:
                raise RuntimeError(f"{name}: header mutation anchor changed")
            (directory / target).write_text(source.replace(before, after), encoding="utf-8")
            # Both headers must resolve through this directory, including their
            # mutual quoted includes, for the requested mutation to execute.
            for sibling in ("nr_memory_observation.h", "nr_model_memory_events.h"):
                if sibling != target:
                    (directory / sibling).write_bytes((source_root / "src" / sibling).read_bytes())
        source = directory / "test.cpp"
        source.write_text(fixture.replace("PRODUCTION_CORE_CONTEXT_FUNCTIONS", variant), encoding="utf-8")
        exe = directory / "test.exe"
        compile_command = [args.compiler, "-std=c++20", "-O0", "-Wall", "-Wextra", "-Werror", "-static",
                           "-I" + str(directory), "-I" + str(source_root / "src"), str(source),
                           str(source_root / "src/nr_dual_spatial_d3d12.cpp"), "-ld3d12", "-ldxgi", "-ldxguid", "-ld3dcompiler", "-o", str(exe)]
        commands.append(run(compile_command, out, name + "-compile"))
        commands.append(run([str(exe)], out, name + "-run", expected, marker))
    trend = runtime[runtime.index("void NrObserveMemoryTrend("):]
    trend = trend.replace("GetTickCount64()", "TestNow()").replace("nr_process_memory::Read(", "TestRead(")
    trend_fixture = (ROOT / "tests/memory_trend_runtime_test.cpp.in").read_text(encoding="utf-8")
    for name, before, after, expected, marker in (
        ("trend-runtime", None, None, 0, None),
        ("trend-no-cadence", "if(!due)return;", "(void)due;", 41, "DISABLED_RUNTIME_MUST_NOT_QUERY_DXGI"),
        ("trend-repeat-old-interval", "g_nr_memory_gpu_window.Clear();", "", 41, "ACTUAL_RUNTIME_MUST_CONSUME_INTERVAL_TIMING")):
        body = trend
        if before:
            if body.count(before) != 1:
                raise RuntimeError(f"{name}: production trend anchor changed")
            body = body.replace(before, after)
        source = out / (name + ".cpp")
        source.write_text(trend_fixture.replace("PRODUCTION_TREND_FUNCTION", body), encoding="utf-8")
        exe = out / (name + ".exe")
        command = [args.compiler, "-std=c++20", "-O0", "-Wall", "-Wextra", "-Werror", "-static", "-I" + str(source_root / "src"), str(source), "-o", str(exe)]
        commands.append(run(command, out, name + "-compile"))
        commands.append(run([str(exe)], out, name + "-run", expected, marker))
    (out / "validation.json").write_text(json.dumps({"scope": "CPU COM service, actual production context walk and formatters; no GPU/NGX/game",
        "passed": True, "source_root": str(source_root),
        "source_sha256": {str(p.relative_to(source_root)): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in (source_root / "src").rglob("*") if p.is_file()},
        "commands": commands}, indent=2), encoding="utf-8")
    print("PASS #330 actual CPU context/slot/F8 and trend runtime + delta/privacy contracts; eleven compiled exact mutation controls")


if __name__ == "__main__":
    main()
