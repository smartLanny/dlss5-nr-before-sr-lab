"""Host regression for the production guarded-null admission and rejection text.

Extracts the actual predicate and CaptureNativeCommandState's reason expression;
no D3D calls are simulated. This verifies diagnosis only, not GPU restoration.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def definition(source: str, marker: str) -> str:
    start = source.index(marker)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class NullGuardReasons(unittest.TestCase):
    def test_production_admission_and_capture_reason(self):
        source = (ROOT / "src/nr_command_state.inl").read_text(encoding="utf-8-sig")
        model = ""
        if "bool CommandStateBlendFactorAdmissible(" in source:
            model += definition(source, "bool CommandStateBlendFactorAdmissible(") + "\n"
        if "const char *RasterNullRejectionReason(" in source:
            model += definition(source, "const char *RasterNullRejectionReason(") + "\n"
        model += definition(source, "bool CanAutoRestoreRasterNull(")
        capture = definition(source, "bool CaptureNativeCommandState(")
        reason = re.search(r"unavailable_reason = !meta_covered .*?;", capture, re.S)
        self.assertIsNotNone(reason, "production capture must publish its rejection reason")
        harness = r'''
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <limits>
using UINT = unsigned int;
using UINT64 = uint64_t;
constexpr int D3D12_VIEWPORT_BOUNDS_MIN = -32768;
constexpr int D3D12_VIEWPORT_BOUNDS_MAX = 32767;
constexpr UINT64 kDynamicViewport=2, kDynamicScissor=4, kDynamicBlend=8,
    kDynamicTargets=64, kDynamicDepthBias=8192, kDynamicStripCut=16384;
struct Viewport { float TopLeftX=0, TopLeftY=0, Width=1, Height=1, MinDepth=0, MaxDepth=1; };
struct Rect { long left=0, top=0, right=1, bottom=1; };
struct Handle { uintptr_t ptr=0; };
struct Dynamic {
    UINT64 recorded=kDynamicBlend;
    UINT vertex_mask=0, stream_mask=0, viewport_count=0, scissor_count=0, target_count=0;
    bool contiguous_targets=false, has_depth_target=false;
    Handle targets[8]{}, depth_target{};
    Viewport viewports[16]; Rect scissors[16];
    float blend[4]={0,0,0,0};
};
struct NativeCommandState {
    bool known=true, null_pipeline_baseline=true, null_guard_covered=true,
        null_guard_reset_observed=true, null_guard_tainted=false;
    void *pipeline=nullptr, *raytracing_pipeline=nullptr, *rtv_cache=nullptr, *dsv_cache=nullptr;
    UINT unknown_domains=0;
    const char *unknown_reason="awaiting-native-reset";
    Dynamic dynamic;
};
@MODEL@
struct Record { NativeCommandState *state; bool hooks_ready=true; };
const char *CaptureReason(Record *record, bool meta_covered=true, bool experimental=false) {
    const bool pristine=false;
    const bool raster=record->state && CanAutoRestoreRasterNull(*record->state);
    const char *unavailable_reason=nullptr;
    @REASON@
    return unavailable_reason;
}
unsigned checks=0, failures=0;
void Check(bool ok, const char *label) {
    ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n",label); }
}
int main() {
    NativeCommandState s; Record record{&s};
    const float values[]={0.f,-0.f,0.1f,0.5f,1.f};
    for(float a:values) for(float b:values) for(float c:values) for(float d:values) {
        s.dynamic.blend[0]=a; s.dynamic.blend[1]=b;
        s.dynamic.blend[2]=c; s.dynamic.blend[3]=d;
        Check(CanAutoRestoreRasterNull(s),"valid blend remains admissible");
    }
    const float finite[]={-3.f,-0.01f,1.01f,2.f,std::numeric_limits<float>::max(),
        std::numeric_limits<float>::lowest(),std::numeric_limits<float>::denorm_min()};
    for(unsigned component=0;component<4;++component) for(float value:finite) {
        s=NativeCommandState{}; s.dynamic.blend[component]=value;
        Check(CanAutoRestoreRasterNull(s),"every finite blend remains admissible");
    }
    const float invalid[]={std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()};
    for(unsigned component=0;component<4;++component) for(unsigned kind=0;kind<3;++kind) {
        s=NativeCommandState{}; s.dynamic.blend[component]=invalid[kind];
        Check(!CanAutoRestoreRasterNull(s),"nonfinite blend remains rejected");
        const char *expected="null-guard-blend-nonfinite";
        const char *actual=CaptureReason(&record);
        if(std::strcmp(actual,expected)!=0)
            std::printf("component=%u kind=%u expected=%s actual=%s\n",component,kind,expected,actual);
        Check(std::strcmp(actual,expected)==0,"capture identifies blend failure, not generic graphics use");
    }
    s=NativeCommandState{}; s.null_guard_tainted=true;
    Check(!CanAutoRestoreRasterNull(s) && std::strcmp(CaptureReason(&record),"null-guard-recording-tainted")==0,
          "taint retains precedence");
    s=NativeCommandState{}; s.null_guard_reset_observed=false;
    Check(!CanAutoRestoreRasterNull(s) && std::strcmp(CaptureReason(&record),"null-guard-awaiting-reset")==0,
          "fresh native reset is still required");
    s=NativeCommandState{}; s.dynamic.blend[0]=std::numeric_limits<float>::infinity(); record.hooks_ready=false;
    Check(std::strcmp(CaptureReason(&record),"native-hooks-unavailable")==0,
          "installation stage outranks uninterpretable payload; not terminal blend failure");
    record.hooks_ready=true;
    Check(std::strcmp(CaptureReason(&record),"null-guard-blend-nonfinite")==0,
          "later ready sample publishes current nonfinite failure instead of sticking to startup");
    Check(std::strcmp(CaptureReason(&record,false),"meta-command-coverage")==0,"meta coverage retains precedence");
    s=NativeCommandState{}; s.dynamic.recorded|=1;
    Check(!CanAutoRestoreRasterNull(s),"unsupported topology stays rejected");
    s=NativeCommandState{}; s.dynamic.vertex_mask=1;
    Check(!CanAutoRestoreRasterNull(s),"vertex bindings stay rejected");
    s=NativeCommandState{}; s.dynamic.stream_mask=1;
    Check(!CanAutoRestoreRasterNull(s),"stream output stays rejected");
    s=NativeCommandState{}; s.null_guard_covered=false;
    Check(!CanAutoRestoreRasterNull(s),"unpublished guard stays rejected");
    s=NativeCommandState{}; s.known=false;
    Check(!CanAutoRestoreRasterNull(s),"unknown state stays rejected");
    s=NativeCommandState{}; s.unknown_domains=1;
    Check(!CanAutoRestoreRasterNull(s),"unknown domains stay rejected");
    s=NativeCommandState{}; s.dynamic.recorded=kDynamicTargets; s.dynamic.target_count=1;
    Check(!CanAutoRestoreRasterNull(s),"unowned descriptors stay rejected");
    s=NativeCommandState{}; s.dynamic.recorded=kDynamicViewport; s.dynamic.viewport_count=1;
    Check(CanAutoRestoreRasterNull(s),"bounded viewport remains admissible");
    s.dynamic.viewports[0].Width=std::numeric_limits<float>::quiet_NaN();
    Check(!CanAutoRestoreRasterNull(s),"nonfinite viewport stays rejected");
    s=NativeCommandState{}; s.dynamic.recorded=kDynamicScissor; s.dynamic.scissor_count=1;
    Check(CanAutoRestoreRasterNull(s),"bounded scissor remains admissible");
    s.dynamic.scissors[0].left=2;
    Check(!CanAutoRestoreRasterNull(s),"inverted scissor stays rejected");
    std::printf("null-guard diagnosis: %u checks, %u failures (host predicate only)\n",checks,failures);
    return failures?1:0;
}
'''.replace("@MODEL@", model).replace("@REASON@", reason.group(0))
        compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
        self.assertTrue(compiler, "a host C++ compiler is required for the predicate regression")
        with tempfile.TemporaryDirectory(prefix="nr-null-reason-") as temporary:
            cpp = Path(temporary) / "test.cpp"
            exe = Path(temporary) / ("test.exe" if os.name == "nt" else "test")
            cpp.write_text(harness, encoding="utf-8")
            subprocess.run([compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", str(cpp), "-o", str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            print(result.stdout, end="")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
