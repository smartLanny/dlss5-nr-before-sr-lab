"""Compile and run the current production F8 coordinator seam.

The harness is assembled from the current nr_f8_sequence.inl and the current
ChainConfig/NormalizeConfig definitions.  Only OS, config, GPU-grid and
packaging services are stubbed; the coordinator state machine and sequence
writer remain verbatim production code.  This deliberately does not exercise
the older nr_f8_runtime.inl model.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def definition(source: str, marker: str, trailing_semicolon: bool = False) -> str:
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end + int(trailing_semicolon)]


def enum_or_struct(source: str, marker: str) -> str:
    return definition(source, marker, True)


parser = argparse.ArgumentParser()
parser.add_argument("--compiler", required=True)
parser.add_argument("--source", type=Path)
parser.add_argument("--gpu", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (args.source or root / "src/nr_before_sr.cpp").read_text(encoding="utf-8-sig")
sequence = (root / "src/nr_f8_sequence.inl").read_text(encoding="utf-8-sig")

parts = {
    "CHAIN_CONFIG": enum_or_struct(source, "struct ChainConfig"),
    "TUNING_SIGNATURE": enum_or_struct(source, "struct NrTuningSignature"),
    "DIAGNOSTIC_IDENTITY": enum_or_struct(source, "struct NrDiagnosticIdentity"),
    "STAGE_INFO": enum_or_struct(source, "struct NrStageCaptureInfo"),
    "FINITE_OR": definition(source, "float FiniteOr("),
    "NORMALIZE_CONFIG": definition(source, "void NormalizeConfig("),
    "SAME_CONFIG": definition(source, "bool SameConfig("),
    "ACTIVE_TRANSFER": definition(source, "float ActiveTransferStrength("),
    "CAPTURE_GRID": definition((root / "src/nr_stage_capture.inl").read_text(encoding="utf-8-sig"), "bool ComputeStageCaptureGrid("),
    "PRODUCTION_SEQUENCE": sequence[:sequence.index("void F8Tooltip(")] + "\n} // namespace nr_f8\n",
}
harness = (root / "tests/f8_controller_harness.cpp.in").read_text(encoding="utf-8")
# The legacy template models the archive service only. F7 must explicitly ask
# for that archive; F8's real single-TXT path is exercised by validate-core-feedback.
legacy_request = "bool Request(){return true;}"
assert harness.count(legacy_request) == 1
harness = harness.replace(legacy_request, "bool Request(bool archive = false){return archive;}")
for marker, value in parts.items():
    harness = harness.replace(marker, value)
if args.gpu:
    gpu = (root / "tests/codec_gpu_harness.cpp.in").read_text(encoding="utf-8")
    gpu = gpu[:gpu.index("struct RunResult")]
    start = source.index("constexpr UINT kCodecSrvCount")
    end = source.index(";", source.index("constexpr UINT kCodecRootConstantCount",start))+1
    gpu = gpu.replace("// @PRODUCTION_DESCRIPTOR_CONSTANTS@",source[start:end])
    start=source.index('const char kCodecShader[] = R"HLSL(')
    end=source.index(')HLSL";',start)+len(')HLSL";')
    gpu=gpu.replace("// @PRODUCTION_SHADER@",source[start:end])
    gpu=gpu.replace("// @PRODUCTION_CONSTANTS@",enum_or_struct(source,"struct CodecConstants"))
    harness=harness.replace("namespace reshade {",gpu+"\nnamespace reshade {",1)
    start=harness.index("struct ID3D12Resource {");end=harness.index("struct FeedbackSession",start)
    wrappers=(root/"tests/f8_gpu_wrappers.h").read_text(encoding="utf-8")
    harness=harness[:start]+wrappers+"\n"+harness[end:]
    start=harness.index("void DeliverFrame(");end=harness.index("void FinishOff(",start)
    harness=harness[:start]+"void DeliverFrame(unsigned long long,unsigned=3840,unsigned=2160,bool=false) {}\n"+harness[end:]
    start=harness.index("void TestFailuresAndLateDelivery(");end=harness.index("void TestBudgetAndLegacyFailure(",start)
    harness=harness[:start]+harness[end:]
    start=harness.index("int main(")
    harness=harness[:start]+(root/"tests/f8_gpu_integration.inl").read_text(encoding="utf-8")+"\n"+harness[start:]
    harness=harness.replace("TestDefaultsAndPictures();TestContinuous(3840,2160);TestContinuous(3840,2400);TestPartialAndRestore();TestFailuresAndLateDelivery();TestBudgetAndLegacyFailure();","TestGpuPipeline();")
out = root / ("test-output/f8-gpu-v4" if args.gpu else "test-output/f8-controller-v4")
out.mkdir(parents=True, exist_ok=True)
cpp, exe = out / "controller.cpp", out / "controller.exe"
cpp.write_text(harness, encoding="utf-8")
subprocess.run([args.compiler, "-std=c++20", "-O0", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-static", "-I", str(root / "src"), str(cpp), "-o", str(exe)]+(["-ld3d12","-ldxgi","-ld3dcompiler","-ldxguid"] if args.gpu else []), check=True)
subprocess.run([str(exe), str(out)], check=True)
print("F8 controller: production Present/callback/readback/stream/restore/ZIP PASS; " + ("real WARP shader/ring/fence, synthetic NR input" if args.gpu else "GPU/NGX are seams"))
