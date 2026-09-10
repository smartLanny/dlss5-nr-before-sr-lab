"""Run the D13 real ReShade/D3D12 multi-frame in-flight fixture.

The executable is a small hidden-window-free WARP host.  It loads the pinned
ReShade dxgi proxy, unwraps its native device, records the production dual
spatial path, and blocks a real D3D12 queue with ID3D12CommandQueue::Wait so
completion is genuinely pending while several command lists are recorded.

The NGX boundary is an explicit success-returning stub.  The receipt therefore
never represents NVIDIA inference, a game run, or an image-quality result.
Production code that is exercised here is the dual spatial allocator and
recording/binding path; the old slot mutation predicate is extracted verbatim
from the current Core so its pending-fence behaviour cannot drift in the test.
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
# Keep the value obvious in the source and fail closed if the requested D12
# evidence DLL is not the exact ReShade build used by the prior identity test.
RESHADE_SHA256 = "0cee63f9c9f13f3ac909c5b4903f4dbb4b719a7ab3b4f13b0deaf83c814b94f7"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def extract_function(source: str, marker: str) -> str:
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 0
    end = brace
    while end < len(source):
        char = source[end]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : end + 1]
        end += 1
    raise ValueError(f"unterminated production function: {marker}")


def extract_line_block(source: str, start_marker: str, end_marker: str) -> str:
    """Extract complete source lines, preserving the production text verbatim."""
    start = source.index(start_marker)
    start = source.rfind("\n", 0, start) + 1
    end = source.index(end_marker, start)
    end = source.find("\n", end)
    return source[start:] if end < 0 else source[start : end + 1]


def identity_definition(source: str) -> str:
    begin = source.index("constexpr GUID kRrDepthUnwrappedObject")
    end = source.index("\nbool ResolveRrLinearDepth(", begin)
    return source[begin:end]


def declaration(source: str, marker: str) -> str:
    start = source.index(marker)
    end = source.index(";", start) + 1
    return source[start:end]


def lifecycle_definition(source: str) -> str:
    names = [
        "bool IsSlotFenceCompleteLocked(",
        "bool IsContextFenceCompleteLocked(",
        "bool CanMutateSlotLocked(",
        "LifecycleCommandList *FindLifecycleCommandListLocked(",
        "bool ReadLifecycleToken(",
        "bool ReadLastSubmissionQueueMarker(",
        "bool ClearLifecycleToken(",
        "LifecycleCommandList *FindOrCreateLifecycleCommandListLocked(",
        "void RollBackEmptyLifecycleCommandListLocked(",
        "bool CanBeginContextUseLocked(",
        "bool RecordNrUseLocked(",
    ]
    return "\n\n".join(extract_function(source, name) for name in names)


def run(command: list[str], *, cwd: Path, output: Path, timeout: int) -> None:
    with output.open("wb") as stream:
        subprocess.run(
            command,
            cwd=cwd,
            stdout=stream,
            stderr=subprocess.STDOUT,
            check=True,
            timeout=timeout,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--reshade-dll", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument(
        "--history-only",
        action="store_true",
        help="run only the controlled AuxHistory/Run history regression",
    )
    args = parser.parse_args()

    compiler = args.compiler.resolve(strict=True)
    reshade = args.reshade_dll.resolve(strict=True)
    if sha256(reshade) != RESHADE_SHA256:
        raise ValueError(
            "--reshade-dll is not the pinned ReShade identity fixture "
            f"(expected {RESHADE_SHA256})"
        )
    output = args.output.resolve()
    if output.exists():
        raise FileExistsError(f"preserve prior D13 evidence: {output}")
    output.mkdir(parents=True)

    adapter_source = (ROOT / "src/nr_rr_depth_adapter.inl").read_text(
        encoding="utf-8-sig"
    )
    core_source = (ROOT / "src/nr_before_sr.cpp").read_text(encoding="utf-8-sig")
    template = (ROOT / "tests/d13_inflight_host.cpp.in").read_text(
        encoding="utf-8-sig"
    )
    generated = template.replace(
        "// @CORE_DEVICE_IDENTITY@", identity_definition(adapter_source)
    )
    gates = "\n\n".join(
        [
            extract_function(core_source, "bool IsSlotFenceCompleteLocked("),
            extract_function(core_source, "bool CanMutateSlotLocked("),
        ]
    )
    generated = generated.replace("// @CORE_MUTATION_GATE@", gates)
    (output / "d13_inflight_host.cpp").write_text(generated, encoding="utf-8")
    binding_template = (ROOT / "tests/d13_inflight_binding_host.cpp.in").read_text(
        encoding="utf-8-sig"
    )
    binding_generated = binding_template.replace(
        "// @CORE_DEVICE_IDENTITY@", identity_definition(adapter_source)
    )
    binding_generated = binding_generated.replace(
        "// @CORE_LIFECYCLE_GUIDS@",
        "\n".join(
            [
                declaration(core_source, "constexpr GUID kLifecycleCommandListGuid"),
                declaration(core_source, "constexpr GUID kLastSubmissionQueueGuid"),
            ]
        ),
    )
    binding_generated = binding_generated.replace(
        "// @CORE_MUTATION_AND_LIFECYCLE@", lifecycle_definition(core_source)
    )
    (output / "d13_inflight_binding_host.cpp").write_text(
        binding_generated, encoding="utf-8"
    )
    region_template = (ROOT / "tests/d13_inflight_region_host.cpp.in").read_text(
        encoding="utf-8-sig"
    )
    region_generated = region_template.replace(
        "// @CORE_DEVICE_IDENTITY@", identity_definition(adapter_source)
    )
    region_generated = region_generated.replace(
        "// @CORE_LIFECYCLE_GUIDS@",
        "\n".join(
            [
                declaration(core_source, "constexpr GUID kLifecycleCommandListGuid"),
                declaration(core_source, "constexpr GUID kLastSubmissionQueueGuid"),
            ]
        ),
    )
    region_generated = region_generated.replace(
        "// @CORE_MUTATION_AND_LIFECYCLE@", lifecycle_definition(core_source)
    )
    (output / "d13_inflight_region_host.cpp").write_text(
        region_generated, encoding="utf-8"
    )
    dual_runtime_source = (ROOT / "src/nr_dual_runtime.inl").read_text(
        encoding="utf-8-sig"
    )
    history_template = (ROOT / "tests/d13_inflight_history_host.cpp.in").read_text(
        encoding="utf-8-sig"
    )
    history_generated = history_template.replace(
        "// @CORE_AUX_HISTORY_RESET@",
        extract_line_block(
            dual_runtime_source,
            "const nr_dual::LayerKey key{",
            "const bool reset=",
        ),
    )
    history_generated = history_generated.replace(
        "// @CORE_RUN_SUCCESS_SEGMENT@",
        extract_line_block(
            dual_runtime_source,
            "if(!attempt.HasSecond()){",
            "main.dual_reduced=attempt.selection==nr_dual::Selection::FusedResidual;",
        ),
    )
    history_generated = history_generated.replace(
        "// @CORE_RUN_ACTIVE_STATUS@",
        extract_line_block(dual_runtime_source, "SetStatus(Status::Active);", "SetStatus(Status::Active);"),
    )
    (output / "d13_inflight_history_host.cpp").write_text(
        history_generated, encoding="utf-8"
    )
    shutil.copyfile(reshade, output / "dxgi.dll")
    (output / "ReShade.ini").write_text(
        "[GENERAL]\nNoEffectCache=1\n", encoding="utf-8"
    )

    history_binary = output / "d13_inflight_history_host.exe"
    history_compile_log = output / "history-compile.log"
    history_command = [
        str(compiler),
        "-std=c++20",
        "-O2",
        "-static",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "src"),
        str(output / "d13_inflight_history_host.cpp"),
        "-o",
        str(history_binary),
    ]
    run(history_command, cwd=output, output=history_compile_log, timeout=120)
    history_run_log = output / "d13_inflight_history_host.log"
    run([str(history_binary)], cwd=output, output=history_run_log, timeout=30)
    history_text = history_run_log.read_text(encoding="utf-8", errors="replace")
    history_required = [
        "D13 production AuxHistory/Run history host",
        "OLD SCHEDULE: frames=32 successes=16 resets=16 warmup_discards=16 final_selected=0",
        "CONTINUOUS: queue=7001 frames=32 successes=32 resets=1 warmup_discards=2 adopted=30",
        "MISSED FRAME: parent_eval=34 after=32 reset=1 since_reset=1 selected=0",
        "PASS D13 production AuxHistory/Run history regression",
    ]
    history_missing = [line for line in history_required if line not in history_text]
    if history_missing:
        raise RuntimeError(
            "D13 history host output did not contain required evidence: "
            + ", ".join(history_missing)
        )
    history_receipt = {
        "source_segment_extracted_verbatim": True,
        "history_host_log_sha256": sha256(history_run_log),
        "history_compile_log_sha256": sha256(history_compile_log),
        "checks": {
            "old_intermittent_schedule_16_resets_zero_p2": True,
            "continuous_schedule_warmup_two_then_30_p2": True,
            "missed_parent_evaluation_resets_history": True,
        },
        "limitations": [
            "The host supplies CPU guide/slot/status interfaces and a controlled second-attempt result.",
            "It does not perform NGX inference, GPU submission, game execution, or image/performance validation.",
        ],
    }
    if args.history_only:
        receipt = {
            "schema": "nr-d13-aux-history-run-v1",
            "success": True,
            "source_commit": subprocess.check_output(
                ["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True
            ).strip(),
            "source_worktree_dirty": bool(
                subprocess.check_output(
                    ["git", "-C", str(ROOT), "status", "--short"], text=True
                ).strip()
            ),
            "inputs": {
                "src/nr_dual_pass.hpp": sha256(ROOT / "src/nr_dual_pass.hpp"),
                "src/nr_dual_runtime.inl": sha256(ROOT / "src/nr_dual_runtime.inl"),
                "tests/d13_inflight_history_host.cpp.in": sha256(
                    ROOT / "tests/d13_inflight_history_host.cpp.in"
                ),
            },
            "compiler": str(compiler),
            "compiler_sha256": sha256(compiler),
            "production_aux_history_run_segment_exercised": True,
            **history_receipt,
        }
        (output / "validation.json").write_text(
            json.dumps(receipt, indent=2) + "\n", encoding="utf-8"
        )
        print(history_text, end="")
        print(json.dumps(receipt, indent=2))
        return 0

    binary = output / "d13_inflight_host.exe"
    binding_binary = output / "d13_inflight_binding_host.exe"
    region_binary = output / "d13_inflight_region_host.exe"
    compile_log = output / "compile.log"
    binding_compile_log = output / "binding-compile.log"
    region_compile_log = output / "region-compile.log"
    command = [
        str(compiler),
        "-std=c++20",
        "-O2",
        "-static",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "src"),
        str(output / "d13_inflight_host.cpp"),
        str(ROOT / "src/nr_dual_spatial_d3d12.cpp"),
        "-ld3d12",
        "-ldxgi",
        "-ldxguid",
        "-ld3dcompiler",
        "-o",
        str(binary),
    ]
    run(command, cwd=output, output=compile_log, timeout=120)
    binding_command = [
        str(compiler),
        "-std=c++20",
        "-O2",
        "-static",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "src"),
        str(output / "d13_inflight_binding_host.cpp"),
        "-ld3d12",
        "-ldxgi",
        "-ldxguid",
        "-o",
        str(binding_binary),
    ]
    run(binding_command, cwd=output, output=binding_compile_log, timeout=120)
    region_command = [
        str(compiler),
        "-std=c++20",
        "-O2",
        "-static",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "src"),
        str(output / "d13_inflight_region_host.cpp"),
        "-ld3d12",
        "-ldxgi",
        "-ldxguid",
        "-ld3dcompiler",
        "-o",
        str(region_binary),
    ]
    run(region_command, cwd=output, output=region_compile_log, timeout=120)

    run_log = output / "d13_inflight_host.log"
    run([str(binary)], cwd=output, output=run_log, timeout=30)
    binding_run_log = output / "d13_inflight_binding_host.log"
    run([str(binding_binary)], cwd=output, output=binding_run_log, timeout=30)
    region_run_log = output / "d13_inflight_region_host.log"
    run([str(region_binary)], cwd=output, output=region_run_log, timeout=30)
    text = run_log.read_text(encoding="utf-8", errors="replace")
    binding_text = binding_run_log.read_text(encoding="utf-8", errors="replace")
    region_text = region_run_log.read_text(encoding="utf-8", errors="replace")
    required = [
        "D13 real ReShade queued WARP host",
        "RESHade identity:",
        "OLD GATE: attempts=4 rejected=4",
        "IN FLIGHT: records=3 evaluates=3 A/B/A=1",
        "QUEUE NEGATIVE: concrete second queue rejected before Evaluate",
        "DESCRIPTORS: A=0x4400 B=0x4000",
        "RECOVERY: A evaluated again",
        "DEVICE NEGATIVE: foreign resource rejected before allocation",
        "PASS D13 real ReShade queued WARP lifecycle",
    ]
    missing = [line for line in required if line not in text]
    if missing:
        raise RuntimeError(
            "D13 host output did not contain required evidence: "
            + ", ".join(missing)
        )
    binding_required = [
        "D13 production BindingPacket host",
        "IN FLIGHT:",
        "QUEUE NEGATIVE: marker=8002 rejected against queue=7001",
        "CAPACITY: valid_packets=8 bound=8",
        "CAPACITY NEGATIVE: ninth in-flight fingerprint rejected",
        "RETIREMENT:",
        "RECOVERY:",
        "DESCRIPTORS: A=0x4D00 B=0x4700 no in-flight overwrite",
        "PASS D13 production BindingPacket queued WARP fixture",
    ]
    binding_missing = [line for line in binding_required if line not in binding_text]
    if binding_missing:
        raise RuntimeError(
            "D13 BindingPacket host output did not contain required evidence: "
            + ", ".join(binding_missing)
        )
    region_required = [
        "D13 region production host",
        "IN FLIGHT: region packets same=1 changed=1",
        "X24 READBACK: depth=0.400000 padding=0xA5A5A5A5 status=3",
        "MISSING: depth-only flags=1 status=2",
        "MISSING: depth-normals flags=3 status=2",
        "MISSING: depth-reflection flags=13 status=3",
        "MISSING: all-guides flags=15 status=3",
        "PASS D13 production region Prepare/Seal/Dispatch WARP",
    ]
    region_missing = [line for line in region_required if line not in region_text]
    if region_missing:
        raise RuntimeError(
            "D13 region host output did not contain required evidence: "
            + ", ".join(region_missing)
        )

    inputs = [
        "src/nr_rr_depth_adapter.inl",
        "src/nr_before_sr.cpp",
        "src/nr_dual_pass.hpp",
        "src/nr_dual_runtime.inl",
        "src/nr_dual_spatial_d3d12.hpp",
        "src/nr_dual_spatial_d3d12.cpp",
        "tests/d13_inflight_host.cpp.in",
        "tests/d13_inflight_history_host.cpp.in",
        "src/nr_descriptor_plan.h",
        "src/nr_binding_types.inl",
        "src/nr_binding_runtime.inl",
        "tests/d13_inflight_binding_host.cpp.in",
        "tests/d13_inflight_region_host.cpp.in",
        "src/nr_region_runtime.inl",
        "src/nr_region_shader.h",
        "src/nr_optional_layer_probe.h",
    ]
    receipt = {
        "schema": "nr-d13-real-reshade-dual-inflight-v1",
        "success": True,
        "source_commit": subprocess.check_output(
            ["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True
        ).strip(),
        "source_worktree_dirty": bool(
            subprocess.check_output(
                ["git", "-C", str(ROOT), "status", "--short"], text=True
            ).strip()
        ),
        "inputs": {name: sha256(ROOT / name) for name in inputs},
        "compiler": str(compiler),
        "compiler_sha256": sha256(compiler),
        "reshade_dll_sha256": sha256(reshade),
        "host_log_sha256": sha256(run_log),
        "compile_log_sha256": sha256(compile_log),
        "binding_host_log_sha256": sha256(binding_run_log),
        "binding_compile_log_sha256": sha256(binding_compile_log),
        "region_host_log_sha256": sha256(region_run_log),
        "region_compile_log_sha256": sha256(region_compile_log),
        "history_host_log_sha256": history_receipt["history_host_log_sha256"],
        "history_compile_log_sha256": history_receipt["history_compile_log_sha256"],
        "reshade_log_sha256": sha256(output / "ReShade.log")
        if (output / "ReShade.log").exists()
        else None,
        "device": "ReShade proxy -> native WARP D3D12",
        "real_reshade_proxy": True,
        "real_d3d12_queue_wait_and_fence": True,
        "core_spatial_recording": True,
        "core_mutation_gate_extracted_verbatim": True,
        "core_lifecycle_admission_extracted_verbatim": True,
        "production_binding_functions_exercised": True,
        "production_region_prepare_seal_dispatch_exercised": True,
        "production_aux_history_run_segment_exercised": True,
        "ngx_provider": "stub-success-only",
        "nvidia_inference": False,
        "game_verified": False,
        "ui_verified": False,
        "descriptor_binding_seam": "nr_binding::AcquireBindingPacket",
        "checks": {
            "old_can_mutate_pending_fence_rejects": True,
            "three_queued_records_reach_stub": True,
            "ordered_same_queue_a_b_a_recording": True,
            "foreign_queue_admission_rejected_before_evaluate": True,
            "foreign_device_identity_rejected_before_allocation": True,
            "exact_fence_recovery_records_again": True,
            "tagged_outputs_read_back_after_fence": True,
            "immutable_binding_packet_reused_same_queue": True,
            "changed_binding_uses_free_packet": True,
            "bounded_eight_packet_capacity": True,
            "region_same_input_packet_reuse": True,
            "region_changed_input_new_packet": True,
            "region_x24_depth_view_and_readback": True,
            "region_missing_guide_degradation": True,
            "old_intermittent_schedule_16_resets_zero_p2": True,
            "continuous_schedule_warmup_two_then_30_p2": True,
            "missed_parent_evaluation_resets_history": True,
        },
        "limitations": [
            "The NGX callback is a stub and does not perform NVIDIA inference.",
            "The host does not start a game or claim game image/performance validation.",
            "The host adapts Execute/Reset publication with a small fixture helper; the extracted admission and RecordNrUse functions remain production code.",
        ],
    }
    (output / "validation.json").write_text(
        json.dumps(receipt, indent=2) + "\n", encoding="utf-8"
    )
    print(text, end="")
    print(binding_text, end="")
    print(region_text, end="")
    print(history_text, end="")
    print(json.dumps(receipt, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"D13 validation failed: {exc}", file=sys.stderr)
        raise
