"""Verify that Reset(NULL) carriers are fail-closed unless explicitly enabled."""

from pathlib import Path
import re
import subprocess
import tempfile


root = Path(__file__).resolve().parents[1]
state_source = (root / "src/nr_command_state.inl").read_text(encoding="utf-8-sig")
main_source = (root / "src/nr_before_sr.cpp").read_text(encoding="utf-8-sig")
config_source = (root / "config/nr_before_sr.ini").read_text(encoding="utf-8-sig")


def function(source: str, marker: str) -> str:
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


policy = function(state_source, "bool CanRestoreNativeCommandState(")
installer = function(state_source, "bool InstallNativeCommandStateHooks(")
capture = function(state_source, "bool CaptureNativeCommandState(")
loader = function(main_source, "bool AllowExperimentalNullPsoCarrier(")

if "bool allow_null_pso = false" not in policy:
    raise SystemExit("null-PSO policy does not default to fail-closed")
if "allow_null_pso && state.null_pipeline_baseline" not in policy:
    raise SystemExit("proven null baseline is not gated by the explicit policy")
if "if (allow_null_pso)" not in installer:
    raise SystemExit("wide dynamic-state hooks are not gated by the null-PSO policy")
if "CanRestoreNativeCommandState(*record->state," not in capture or \
        "AllowExperimentalNullPsoCarrier()" not in capture:
    raise SystemExit("production capture does not apply the frozen null-PSO policy")
if 'L"ExperimentalNullPsoCarrier", 0' not in loader:
    raise SystemExit("production null-PSO policy is not default-off")
if not re.search(r"(?m)^ExperimentalNullPsoCarrier=0\s*$", config_source):
    raise SystemExit("packaged configuration does not default null-PSO admission off")

harness = r'''
#include <cstdint>
#include <cstdio>
using UINT = unsigned;
using UINT64 = std::uint64_t;
constexpr UINT D3D12_GRAPHICS_STATE_PIPELINE_STATE = 0x20;
constexpr UINT64 kDynamicDepthBias = 1ull << 19;
constexpr UINT64 kDynamicStripCut = 1ull << 20;
struct Dynamic { UINT64 recorded = 0; };
struct NativeCommandState {
    bool known = false;
    UINT unknown_domains = 0;
    void *pipeline = nullptr;
    void *raytracing_pipeline = nullptr;
    bool null_pipeline_baseline = false;
    Dynamic dynamic;
};
POLICY
int main() {
    NativeCommandState state{};
    state.known = true;
    state.null_pipeline_baseline = true;
    if (CanRestoreNativeCommandState(state)) return 1;
    if (!CanRestoreNativeCommandState(state, true)) return 2;
    state.dynamic.recorded = kDynamicDepthBias;
    if (CanRestoreNativeCommandState(state, true)) return 3;
    int pso = 1;
    state.pipeline = &pso;
    if (!CanRestoreNativeCommandState(state)) return 4;
    std::puts("null-PSO policy: stable default refused, explicit experiment admitted");
    return 0;
}
'''.replace("POLICY", policy)

compiler = root / "build" / "null-pso-policy" / "clang++.exe"
# Prefer the configured LLVM-MinGW compiler; the caller may supply it through
# the same environment used by all repository validators.
import os
toolchain = os.environ.get("DLSS5_LLVM_MINGW_ROOT")
if not toolchain:
    raise SystemExit("Set DLSS5_LLVM_MINGW_ROOT before running this validator")
compiler = Path(toolchain) / "bin" / "x86_64-w64-mingw32-clang++.exe"
with tempfile.TemporaryDirectory(prefix="dlss5-null-pso-") as temporary:
    cpp = Path(temporary) / "policy.cpp"
    exe = Path(temporary) / "policy.exe"
    cpp.write_text(harness, encoding="utf-8")
    subprocess.run([str(compiler), "-std=c++20", "-O2", "-static", str(cpp), "-o", str(exe)], check=True)
    raise SystemExit(subprocess.run([str(exe)]).returncode)
