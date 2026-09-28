"""Exercise primary RR depth preparation/binding with real queued ReShade/WARP.

Reuses the existing in-flight host environment and extracts production functions
and the primary call expressions. This is not a full Core, NGX, or game test.
Each output directory is immutable evidence; failed runs also receive a receipt.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("inflight", ROOT / "scripts/validate-dual-inflight.py")
inflight = importlib.util.module_from_spec(spec)
assert spec.loader
spec.loader.exec_module(inflight)


def replace_once(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise ValueError(f"host seam changed: {old!r}")
    return text.replace(old, new, 1)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--compiler", required=True, type=Path)
    p.add_argument("--reshade-dll", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    args = p.parse_args()
    compiler = args.compiler.resolve(strict=True)
    reshade = args.reshade_dll.resolve(strict=True)
    if inflight.sha256(reshade) != inflight.RESHADE_SHA256:
        raise ValueError("ReShade identity fixture hash mismatch")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    paths = ["src/nr_before_sr.cpp", "src/nr_rr_depth_adapter.inl",
             "src/nr_rr_depth_shader.h", "src/nr_region_runtime.inl",
             "src/nr_binding_runtime.inl", "src/nr_binding_types.inl",
             "src/nr_descriptor_plan.h", "src/nr_aux_shader_runtime.hpp",
             "tests/d13_inflight_region_host.cpp.in",
             "tests/primary_rr_depth_host.cpp.in", "scripts/validate-primary-rr-depth.py",
             "scripts/validate-dual-inflight.py"]
    sources = {name: (ROOT/name).read_text(encoding="utf-8-sig") for name in paths}
    core, adapter = sources[paths[0]], sources[paths[1]]
    host = sources["tests/d13_inflight_region_host.cpp.in"]
    # Preserve all shared region and binding environment helpers, replace only main.
    host = host[:host.index("int main() {")]
    host = host.replace("// @CORE_DEVICE_IDENTITY@", inflight.identity_definition(adapter))
    host = host.replace("// @CORE_LIFECYCLE_GUIDS@", "\n".join(
        inflight.declaration(core, marker) for marker in
        ("constexpr GUID kLifecycleCommandListGuid", "constexpr GUID kLastSubmissionQueueGuid")))
    host = host.replace("// @CORE_MUTATION_AND_LIFECYCLE@", inflight.lifecycle_definition(core))
    host = host.replace("// @CORE_QUEUE_PROOF@", inflight.extract_function(core, "enum class RecordingQueueProof") + ";")
    # Add only fields used by the production RR preparation/dispatch functions.
    host = replace_once(host, "struct NrSlot {", """struct NrSlot {
    ID3D12Resource *rr_depth=nullptr,*rr_depth_source=nullptr;
    ID3D12DescriptorHeap *rr_depth_heap=nullptr;
    D3D12_RESOURCE_STATES rr_depth_state=D3D12_RESOURCE_STATE_COMMON;
    unsigned long long rr_depth_bytes=0;""")
    host = replace_once(host, "bool linear_depth = false;", """bool linear_depth = false;
        unsigned depth_format=DXGI_FORMAT_R32_FLOAT;
        float depth_projection_a=1.f,depth_projection_b=-.5f;
        int depth_inverted=0;bool rr_depth_placeholder=false;""")
    host = replace_once(host, "struct CodecState {", """struct CodecState {
        ID3D12RootSignature *rr_depth_root=nullptr;
        ID3D12PipelineState *rr_depth_pipeline=nullptr;bool rr_depth_failed=false;""")
    # The region host already exercises C ABI struct-return helpers. The depth
    # pipeline additionally needs the same ID3DBlob C++ convenience shim as Core.
    definitions = adapter[adapter.index("enum class RrDepthReject"):adapter.index("// ReShade 6.8")]
    definitions += "\n" + inflight.extract_function(adapter, "bool EnsureRrDepthPipeline(")
    definitions += "\n" + inflight.extract_function(adapter, "bool EnsureRrDepthSlot(")
    definitions += "\n" + inflight.extract_function(adapter, "bool DispatchRrDepth(")
    definitions = """\n#include "nr_rr_depth_shader.h"
#include "nr_aux_shader_runtime.hpp"
std::atomic<unsigned long long> g_rr_depth_conversions{0};
namespace nr_rr_depth {struct {std::atomic<unsigned long long> placeholder_recorded{0};}observations;}
""" + definitions
    host = replace_once(host, 'void Transition(ID3D12GraphicsCommandList *list,',
                        'void TransitionValue(ID3D12GraphicsCommandList *list,')
    host = replace_once(host, '#include "nr_region_runtime.inl"', r'''void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,
        D3D12_RESOURCE_STATES& state,D3D12_RESOURCE_STATES after) {
        TransitionValue(list,resource,state,after);state=after;
    }
    void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,
        D3D12_RESOURCE_STATES&& before,D3D12_RESOURCE_STATES after) {
        TransitionValue(list,resource,before,after);
    }
    #include "nr_region_runtime.inl"''')
    host += definitions
    prepare = inflight.extract_function(core, "PostSrSubstitution PrepareAfterSr(")
    ensure = re.findall(r"EnsureRrDepthSlot\(device,\*slot,guides(?:,true)?\)", prepare)
    seal = re.findall(r"nr_regions::Seal\(device,command_list,\*context,\*slot(?:,&rr_depth_bindings)?\)", prepare)
    if len(ensure) != 1 or len(seal) != 1:
        raise ValueError("primary production call expressions changed")
    harness = sources["tests/primary_rr_depth_host.cpp.in"]
    harness = harness.replace("@ENSURE_CALL@", ensure[0]).replace("@SEAL_CALL@", seal[0])
    plan = ""
    if "nr_binding::DescriptorSet rr_depth_bindings{};" in prepare:
        start = prepare.index("nr_binding::DescriptorSet rr_depth_bindings{};")
        end = prepare.index("const bool primary_bindings_ready", start)
        plan = prepare[start:end]
    harness = harness.replace("@DEPTH_PLAN@", plan)
    host += harness
    if "@CORE_" in host or "@ENSURE_" in host or "@SEAL_" in host:
        raise ValueError("unresolved source seam")
    source = out/"primary_rr_depth.cpp"
    source.write_text(host, encoding="utf-8")
    # Freeze all included production headers too; a later worktree edit must not
    # alter the receipt's source identity while compilation is running.
    frozen = out/"src"
    shutil.copytree(ROOT/"src", frozen)
    shutil.copyfile(reshade, out/"dxgi.dll")
    (out/"ReShade.ini").write_text("[GENERAL]\nNoEffectCache=1\n", encoding="utf-8")
    exe = out/"primary_rr_depth.exe"
    command = [str(compiler), "-std=c++17", "-O1", "-static", "-I", str(frozen),
               str(source), "-o", str(exe), "-ld3d12", "-ldxgi", "-ldxguid", "-ld3dcompiler", "-luser32"]
    result = {"schema": "nr-primary-rr-depth-v1", "success": False,
              "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
              "source_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip()),
              "source_sha256": {name: inflight.sha256(ROOT/name) for name in paths},
              "generated_sha256": inflight.sha256(source), "ensure_call": ensure[0], "seal_call": seal[0],
              "reshade_sha256": inflight.sha256(reshade), "compiler": str(compiler), "command": command,
              "limits": ["WARP with real ReShade proxy, production depth shader and immutable bindings.",
                         "Host adapts native Execute/fence publication and Reset bookkeeping.",
                         "Not full Core, NGX inference, hardware GPU, frame-time or game acceptance."]}
    try:
        for stage, cmd, timeout in [("compile", command, 120), ("run", [str(exe)], 60)]:
            with (out/f"{stage}.log").open("wb") as log:
                run = subprocess.run(cmd, cwd=out, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
            result[stage+"_exit"] = run.returncode
            result[stage+"_log_sha256"] = inflight.sha256(out/f"{stage}.log")
            if run.returncode:
                raise RuntimeError(f"{stage} failed; see {out/stage}.log")
        text = (out/"run.log").read_text(encoding="utf-8", errors="replace")
        result["success"] = "PASS primary RR depth" in text
        if not result["success"]:
            raise RuntimeError("missing PASS sentinel")
    except Exception as exc:
        result["error"] = str(exc)
    (out/"validation.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({k: result[k] for k in ("success", "ensure_call", "seal_call")}, ensure_ascii=False))
    if result.get("error"):
        print(result["error"])
    return 0 if result["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
