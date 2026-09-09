"""Bounded regression checks for Feature 1 recovery after native RR activity.

The C++ smoke case compiles the two small predicates copied verbatim from the
production source at test time.  The remaining checks are source-contract
checks around the real ProcessEvaluate/DispatchEvaluate path.  They deliberately
do not claim a game, GPU, resource-alias, or full Win32/ReShade integration
result; those require the existing Windows/WARP and game gates.
"""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "src" / "nr_before_sr.cpp"
RR_POST = ROOT / "src" / "interop" / "nr_rr_post_runtime.inl"


def _function_block(source: str, marker: str) -> str:
    """Return one C++ function using its balanced brace, without rewriting it."""
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 0
    quote = None
    escaped = False
    line_comment = False
    block_comment = False
    i = opening
    while i < len(source):
        ch = source[i]
        nxt = source[i + 1] if i + 1 < len(source) else ""
        if line_comment:
            if ch == "\n":
                line_comment = False
        elif block_comment:
            if ch == "*" and nxt == "/":
                block_comment = False
                i += 1
        elif quote is not None:
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == quote:
                quote = None
        elif ch == "/" and nxt == "/":
            line_comment = True
            i += 1
        elif ch == "/" and nxt == "*":
            block_comment = True
            i += 1
        elif ch in ('"', "'"):
            quote = ch
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return source[start : i + 1]
        i += 1
    raise AssertionError(f"unclosed C++ function: {marker}")


def _compact(text: str) -> str:
    return " ".join(text.split())


def _process_evaluate(source: str) -> str:
    start = source.index("NgxResult ProcessEvaluate(")
    end = source.index("\nNgxResult DispatchEvaluate(", start)
    return source[start:end]


def _compiler_command():
    configured = os.environ.get("RR_ROUTE_TEST_CXX") or os.environ.get("CXX")
    candidates = []
    if configured:
        candidates.append(configured)
    candidates.extend(("clang++", "g++", "c++"))
    for candidate in candidates:
        # The optional test is intentionally host-executable only.  `cl` needs
        # a different command line and is covered by the Windows production
        # build gate rather than silently pretending that this smoke ran.
        if Path(candidate).name.lower() in {"cl", "cl.exe"}:
            continue
        resolved = shutil.which(candidate)
        if resolved:
            return resolved
    return None


class RrRouteRecovery(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.core = CORE.read_text(encoding="utf-8")
        cls.rr_post = RR_POST.read_text(encoding="utf-8")
        cls.process = _process_evaluate(cls.core)

    def test_actual_predicates_recover_from_cached_rr_handle(self):
        """Exercise exact production predicate bodies for the route matrix."""
        compiler = _compiler_command()
        if compiler is None:
            self.skipTest("no host C++ compiler; source-contract tests still run")

        should_guard = _function_block(
            self.core, "bool ShouldGuardFeature1ForRayReconstruction()"
        )
        route_observed = _function_block(
            self.core, "bool IsRayReconstructionRouteObserved()"
        )
        harness = f"""
#include <atomic>
#include <cstdlib>
#include <iostream>

std::atomic<unsigned int> g_rr_create_in_progress{{0}};
std::atomic<int> g_rr_ownership_gate_owner{{0}};
std::atomic<int> g_last_carrier_feature{{-1}};
std::atomic<unsigned int> g_live_rr_handles{{0}};

{should_guard}
{route_observed}

static void check(bool value, const char* name) {{
    if (!value) {{ std::cerr << name << "\\n"; std::exit(1); }}
}}

int main() {{
    // A retained inactive RR handle, including several cached handles, does
    // not own a later Feature 1 call.
    g_live_rr_handles.store(1);
    g_last_carrier_feature.store(13);
    check(!ShouldGuardFeature1ForRayReconstruction(), "cached-one-F1-admitted");
    check(IsRayReconstructionRouteObserved(), "last13-is-observed");
    g_live_rr_handles.store(7);
    check(!ShouldGuardFeature1ForRayReconstruction(), "cached-many-F1-admitted");

    // A real ownership transition/create still protects the ordinary route;
    // releasing that transition makes the next Feature 1 call admissible.
    g_rr_ownership_gate_owner.store(2);
    check(ShouldGuardFeature1ForRayReconstruction(), "owner2-native-guard");
    g_rr_ownership_gate_owner.store(0);
    check(!ShouldGuardFeature1ForRayReconstruction(), "released-gate-admitted");
    g_rr_create_in_progress.store(1);
    check(ShouldGuardFeature1ForRayReconstruction(), "create-active-guard");
    g_rr_create_in_progress.store(0);

    // UI route observation follows the last actual carrier call.  Feature 1
    // clears the observed RR state while the inventory still retains handles.
    g_last_carrier_feature.store(1);
    check(!IsRayReconstructionRouteObserved(), "F1-clears-observed-RR");
    check(g_live_rr_handles.load() == 7, "inactive-handle-retained");
    g_last_carrier_feature.store(13);
    g_live_rr_handles.store(0);
    check(!IsRayReconstructionRouteObserved(), "no-live-RR-is-not-observed");
    return 0;
}}
"""
        with tempfile.TemporaryDirectory(prefix="rr-route-recovery-") as temp:
            temp_path = Path(temp)
            source_path = temp_path / "route_recovery.cpp"
            exe_path = temp_path / ("route_recovery.exe" if os.name == "nt" else "route_recovery")
            source_path.write_text(harness, encoding="utf-8")
            built = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-O0",
                    "-static",
                    "-static-libgcc",
                    "-static-libstdc++",
                    str(source_path),
                    "-o",
                    str(exe_path),
                ],
                capture_output=True,
                text=True,
                timeout=45,
            )
            self.assertEqual(
                built.returncode,
                0,
                msg=f"production predicate extraction did not compile:\n{built.stdout}\n{built.stderr}",
            )
            ran = subprocess.run(
                [str(exe_path)], capture_output=True, text=True, timeout=15
            )
            self.assertEqual(
                ran.returncode,
                0,
                msg=f"route matrix failed: {ran.stdout}\n{ran.stderr}",
            )

    def test_extracted_process_evaluate_prefix_admits_each_recognized_sr_handle(self):
        """Run the exact ProcessEvaluate prefix through its Track probe boundary."""
        compiler = _compiler_command()
        if compiler is None:
            self.skipTest("no host C++ compiler; source-contract tests still run")

        track = "TrackDlssResolution(params, feature);"
        prefix = self.process[: self.process.index(track)]
        native_forward = 'template <class NativeEvaluate>\n' + _function_block(
            (CORE.parent / 'nr_native_format_runtime.inl').read_text(encoding='utf-8'),
            'NgxResult NrCallNativeWithFormatObservation('
        )
        # The generated function is the production prefix verbatim, with only
        # the post-boundary return supplied by the test seam.  Metadata lookup
        # and the native callback are fixtures; the admission logic is not.
        is_carrier = _function_block(
            self.core, "bool IsUpscalingCarrierFeature(int feature_id)"
        )
        should_guard = _function_block(
            self.core, "bool ShouldGuardFeature1ForRayReconstruction()"
        )
        harness = f"""
#include <atomic>
#include <cstdarg>
#include <cstdlib>
#include <iostream>

struct ID3D12GraphicsCommandList {{}};
struct NgxHandle {{ int id = 0; }};
struct NgxParams {{}};
using NgxResult = int;
using EvaluateFeatureFn = NgxResult (*)(ID3D12GraphicsCommandList*,
    const NgxHandle*, const NgxParams*, void*);
constexpr NgxResult kProbeSuccess = 0x1234;
constexpr NgxResult kNativeResult = 0x5678;
constexpr NgxResult kNgxSuccess = 1;
// Keep the actual one-call native wrapper; observer storage is a dependency seam.
struct NrNativeFormatScope {{
    NrNativeFormatScope(ID3D12GraphicsCommandList*,const NgxParams*,unsigned long long) {{}}
    void Finish(bool) {{}}
}};
void NrPrepareFormatObservation(ID3D12GraphicsCommandList*) {{}}
struct NrRecoveryCarrierScope {{
    NrRecoveryCarrierScope(ID3D12GraphicsCommandList*,unsigned long long) {{}}
}};
{native_forward}

enum class BypassReason {{
    FailedCreateQuarantine, LifecycleIsolation, MasterDisabled,
    ModuleObserverInstalling, ModuleObserverUncertain, RayReconstructionGuard
}};

std::atomic<bool> g_enabled_fast{{true}};
std::atomic<bool> g_module_rescan_in_progress{{false}};
std::atomic<bool> g_evaluate_target_overflow{{false}};
std::atomic<bool> g_lifecycle_tracking_uncertain{{false}};
std::atomic<bool> g_nr_failed_create_process_latched{{false}};
std::atomic<unsigned long long> g_all_evaluations{{0}};
std::atomic<bool> g_feature_tracking_uncertain{{false}};
std::atomic<bool> g_module_observer_gap_detected{{false}};
std::atomic<int> g_last_carrier_feature{{-1}};
std::atomic<unsigned long long> g_rr_passthrough_evaluations{{0}};
std::atomic<unsigned long long> g_rr_guarded_sr_evaluations{{0}};
std::atomic<unsigned int> g_live_rr_handles{{0}};
std::atomic<bool> g_rr_guard_was_active{{false}};
std::atomic<unsigned int> g_rr_create_in_progress{{0}};
std::atomic<int> g_rr_ownership_gate_owner{{0}};
BypassReason g_reason = BypassReason::LifecycleIsolation;
int g_native_calls = 0;

struct FeatureRecord {{
    const NgxHandle* handle = nullptr;
    int feature_id = -1;
    bool pending = false, retiring = false;
    unsigned long long generation = 1;
}};
NgxHandle g_sr_first, g_sr_second, g_rr, g_unknown;
FeatureRecord FindFeature(const NgxHandle* handle) {{
    if (handle == &g_sr_first) return {{handle, 1}};
    if (handle == &g_sr_second) return {{handle, 1}};
    if (handle == &g_rr) return {{handle, 13}};
    return {{handle, 99}};
}}
bool OtherNrOwnerActive() {{ return false; }}
BypassReason OtherNrOwnerBypassReason() {{ return BypassReason::LifecycleIsolation; }}
void SetBypassReason(BypassReason reason) {{ g_reason = reason; }}
const char* FeatureName(int) {{ return "fixture"; }}
const char* ResultName(NgxResult) {{ return "fixture"; }}
void Log(const char*, ...) {{}}

{is_carrier}
{should_guard}

{prefix}
    return kProbeSuccess;
}}

NgxResult Native(ID3D12GraphicsCommandList*, const NgxHandle*,
                 const NgxParams*, void*) {{
    ++g_native_calls;
    return kNativeResult;
}}
static void check(bool value, const char* name) {{
    if (!value) {{ std::cerr << name << "\\n"; std::exit(1); }}
}}

int main() {{
    ID3D12GraphicsCommandList command;
    NgxParams params;
    void* callback = nullptr;
    g_live_rr_handles.store(3);

    // Two independently recognized Feature 1 handles both reach the same
    // probe boundary even while an inactive RR inventory entry remains.
    auto result = ProcessEvaluate(&Native, &command, &g_sr_first, &params, callback);
    check(result == kProbeSuccess && g_native_calls == 0 &&
              g_last_carrier_feature.load() == 1, "first-SR-admitted");
    result = ProcessEvaluate(&Native, &command, &g_sr_second, &params, callback);
    check(result == kProbeSuccess && g_native_calls == 0 &&
              g_last_carrier_feature.load() == 1, "second-SR-admitted");

    // A real RR transition/create still takes the native guard branch.
    g_rr_ownership_gate_owner.store(2);
    result = ProcessEvaluate(&Native, &command, &g_sr_first, &params, callback);
    check(result == kNativeResult && g_native_calls == 1 &&
              g_reason == BypassReason::RayReconstructionGuard,
          "owner2-native-guard");
    g_rr_ownership_gate_owner.store(0);
    g_rr_create_in_progress.store(1);
    result = ProcessEvaluate(&Native, &command, &g_sr_first, &params, callback);
    check(result == kNativeResult && g_native_calls == 2 &&
              g_reason == BypassReason::RayReconstructionGuard,
          "create-native-guard");
    g_rr_create_in_progress.store(0);

    // Lifecycle uncertainty fails closed before FindFeature/probe tracking.
    g_last_carrier_feature.store(-1);
    g_feature_tracking_uncertain.store(true);
    result = ProcessEvaluate(&Native, &command, &g_sr_first, &params, callback);
    check(result == kNativeResult && g_native_calls == 3 &&
              g_last_carrier_feature.load() == -1,
          "unknown-tracking-fail-closed");
    return 0;
}}
"""
        with tempfile.TemporaryDirectory(prefix="rr-route-prefix-") as temp:
            temp_path = Path(temp)
            source_path = temp_path / "route_prefix.cpp"
            exe_path = temp_path / (
                "route_prefix.exe" if os.name == "nt" else "route_prefix"
            )
            source_path.write_text(harness, encoding="utf-8")
            built = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-O0",
                    "-static",
                    "-static-libgcc",
                    "-static-libstdc++",
                    str(source_path),
                    "-o",
                    str(exe_path),
                ],
                capture_output=True,
                text=True,
                timeout=45,
            )
            self.assertEqual(
                built.returncode,
                0,
                msg=f"ProcessEvaluate prefix did not compile:\n{built.stdout}\n{built.stderr}",
            )
            ran = subprocess.run(
                [str(exe_path)], capture_output=True, text=True, timeout=15
            )
            self.assertEqual(
                ran.returncode,
                0,
                msg=f"ProcessEvaluate prefix matrix failed: {ran.stdout}\n{ran.stderr}",
            )

        # Negative control: substitute only the pre-fix lifetime-handle
        # condition in the extracted production guard.  With one retained RR
        # handle and no active owner/create, that old body must reject the same
        # Feature 1 call that the current body admitted above.
        old_condition = (
            "g_rr_ownership_gate_owner.load(std::memory_order_acquire) == 2"
        )
        self.assertIn(old_condition, should_guard)
        old_should_guard = should_guard.replace(
            old_condition,
            "g_live_rr_handles.load(std::memory_order_acquire) != 0",
            1,
        )
        self.assertNotEqual(old_should_guard, should_guard)
        old_main = r'''int main() {
    ID3D12GraphicsCommandList command;
    NgxParams params;
    void* callback = nullptr;
    g_live_rr_handles.store(1);
    auto result = ProcessEvaluate(&Native, &command, &g_sr_first, &params, callback);
    check(result == kNativeResult && g_native_calls == 1 &&
              g_reason == BypassReason::RayReconstructionGuard &&
              g_last_carrier_feature.load() == 13,
          "old-lifetime-guard-rejects-cached-RR");
    return 0;
}
'''
        old_harness = harness.replace(should_guard, old_should_guard, 1)
        old_harness = old_harness[: old_harness.index("int main() {")] + old_main
        with tempfile.TemporaryDirectory(prefix="rr-route-old-guard-") as temp:
            temp_path = Path(temp)
            source_path = temp_path / "route_prefix_old_guard.cpp"
            exe_path = temp_path / (
                "route_prefix_old_guard.exe"
                if os.name == "nt"
                else "route_prefix_old_guard"
            )
            source_path.write_text(old_harness, encoding="utf-8")
            built = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-O0",
                    "-static",
                    "-static-libgcc",
                    "-static-libstdc++",
                    str(source_path),
                    "-o",
                    str(exe_path),
                ],
                capture_output=True,
                text=True,
                timeout=45,
            )
            self.assertEqual(
                built.returncode,
                0,
                msg=f"old-guard control did not compile:\n{built.stdout}\n{built.stderr}",
            )
            ran = subprocess.run(
                [str(exe_path)], capture_output=True, text=True, timeout=15
            )
            self.assertEqual(
                ran.returncode,
                0,
                msg=f"old-guard negative control failed: {ran.stdout}\n{ran.stderr}",
            )

    def test_process_evaluate_admission_prefix_is_the_production_boundary(self):
        """Keep probe success and the second recognized SR handle on the real path."""
        track = "TrackDlssResolution(params, feature);"
        self.assertIn(track, self.process)
        prefix = self.process[: self.process.index(track)]
        compact = _compact(prefix)

        self.assertIn("const FeatureRecord feature = FindFeature(handle);", prefix)
        self.assertIn("if (feature.feature_id == 13)", prefix)
        self.assertIn("!IsUpscalingCarrierFeature(feature.feature_id) || params == nullptr", compact)
        self.assertIn("ShouldGuardFeature1ForRayReconstruction()", prefix)
        self.assertIn("g_last_carrier_feature.store(feature.feature_id", prefix)

        # Unknown lifecycle tracking fails closed before handle classification,
        # and no probe/resolution counter is consumed on that branch.
        uncertain = self.process.index("if (g_feature_tracking_uncertain.load(")
        find = self.process.index("const FeatureRecord feature = FindFeature(handle);")
        self.assertLess(uncertain, find)
        self.assertLess(find, self.process.index(track))
        uncertain_prefix = self.process[uncertain:find]
        self.assertIn("return original(command_list, handle, params, callback);", uncertain_prefix)

        # Route identity comes from the per-handle record, so another
        # recognized Feature 1 handle takes the same admission path; this is a
        # source contract, not a claim that a game supplied two handles here.
        self.assertEqual(self.process.count("FindFeature(handle)"), 1)
        self.assertLess(
            self.process.index("FindFeature(handle)"),
            self.process.index("IsUpscalingCarrierFeature(feature.feature_id)"),
        )

    def test_actual_dispatch_and_win32_gate(self):
        """Run the extracted dispatcher and ScopedRrOwnershipGate on Windows."""
        if os.name != "nt":
            self.skipTest("the extracted gate uses Windows SRWLOCK semantics")
        compiler = _compiler_command()
        if compiler is None:
            self.skipTest("no host C++ compiler; source-contract tests still run")

        dispatch = _function_block(self.core, "NgxResult DispatchEvaluate(")
        gate = _function_block(self.core, "class ScopedRrOwnershipGate")
        harness = f"""
#include <windows.h>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>

struct ID3D12GraphicsCommandList {{}};
struct NgxHandle {{ int id = 0; }};
struct NgxParams {{}};
using NgxResult = int;
using EvaluateFeatureFn = NgxResult (*)(ID3D12GraphicsCommandList*,
    const NgxHandle*, const NgxParams*, void*);
constexpr NgxResult kNativeResult = 0x101;
constexpr NgxResult kProcessedResult = 0x202;

SRWLOCK g_rr_ownership_gate = SRWLOCK_INIT;
std::atomic<int> g_rr_ownership_gate_owner{{0}};
std::atomic<bool> g_feature_tracking_uncertain{{false}};
thread_local unsigned int g_rr_ownership_gate_depth = 0;
thread_local bool g_inside_feature1_rr_transaction = false;
thread_local bool g_feature1_rr_transaction_aborted = false;

{gate};

bool g_inside_ngx_evaluate = false;
std::atomic<int> g_nested_latches{{0}};
std::atomic<int> g_process_calls{{0}};
std::atomic<int> g_native_calls{{0}};

struct FeatureRecord {{ int feature_id = -1; }};
FeatureRecord FindFeature(const NgxHandle* handle) {{
    return {{handle != nullptr && handle->id == 13 ? 13 : 1}};
}}
void LatchNestedRrOwnershipChange(const char*) {{ ++g_nested_latches; }}
NgxResult ProcessEvaluate(EvaluateFeatureFn, ID3D12GraphicsCommandList*,
                          const NgxHandle*, const NgxParams*, void*) {{
    ++g_process_calls;
    return kProcessedResult;
}}

{dispatch}

NgxResult Native(ID3D12GraphicsCommandList*, const NgxHandle*,
                 const NgxParams*, void*) {{
    ++g_native_calls;
    return kNativeResult;
}}
static void check(bool value, const char* name) {{
    if (!value) {{ std::cerr << name << "\\n"; std::exit(1); }}
}}

int main() {{
    ID3D12GraphicsCommandList command;
    NgxParams params;
    void* callback = nullptr;
    NgxHandle rr{{13}}, sr{{1}};

    // Nested Feature 13 is passed through once and latches the active outer
    // transaction; nested ordinary SR is also passed through once.
    g_inside_ngx_evaluate = true;
    auto result = DispatchEvaluate(&Native, &command, &rr, &params, callback);
    check(result == kNativeResult && g_native_calls == 1 &&
              g_process_calls == 0 && g_nested_latches == 1,
          "nested-RR-latch-once");
    g_nested_latches.store(0);
    result = DispatchEvaluate(&Native, &command, &sr, &params, callback);
    check(result == kNativeResult && g_native_calls == 2 &&
              g_process_calls == 0 && g_nested_latches == 0,
          "nested-SR-native-once");

    // The outer dispatcher invokes the actual ProcessEvaluate seam once.
    g_inside_ngx_evaluate = false;
    result = DispatchEvaluate(&Native, &command, &sr, &params, callback);
    check(result == kProcessedResult && g_process_calls == 1 &&
              g_native_calls == 2 && !g_inside_ngx_evaluate,
          "outer-dispatch-once");

    // The production gate uses real SRWLOCK ownership, including nested
    // same-thread entry and a cross-thread try-only failure.
    {{
        ScopedRrOwnershipGate first(true, true, false);
        check(first.acquired() && !first.is_nested() &&
                  g_rr_ownership_gate_owner.load() == 1,
              "feature1-owns-win32-gate");
        ScopedRrOwnershipGate nested(true, true, true);
        check(nested.acquired() && nested.is_nested() &&
                  g_rr_ownership_gate_owner.load() == 1,
              "nested-gate-entry");
    }}
    check(g_rr_ownership_gate_owner.load() == 0 &&
              g_rr_ownership_gate_depth == 0,
          "gate-release");
    std::atomic<bool> failed_as_expected{{false}};
    {{
        ScopedRrOwnershipGate first(true, true, false);
        std::thread contender([&]() {{
            ScopedRrOwnershipGate failed(true, false, true);
            failed_as_expected.store(
                failed.acquisition_failed() &&
                    g_rr_ownership_gate_owner.load() == 1);
        }});
        contender.join();
        check(failed_as_expected.load(), "cross-thread-try-only-fails");
    }}
    {{
        ScopedRrOwnershipGate rr_gate(true, false, true);
        check(rr_gate.acquired() && !rr_gate.is_nested() &&
                  g_rr_ownership_gate_owner.load() == 2,
              "RR-owns-released-gate");
    }}
    check(g_rr_ownership_gate_owner.load() == 0 &&
              !g_feature_tracking_uncertain.load(),
          "RR-gate-restored");
    return 0;
}}
"""
        with tempfile.TemporaryDirectory(prefix="rr-dispatch-gate-") as temp:
            temp_path = Path(temp)
            source_path = temp_path / "dispatch_gate.cpp"
            exe_path = temp_path / "dispatch_gate.exe"
            source_path.write_text(harness, encoding="utf-8")
            built = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-O0",
                    "-static",
                    "-static-libgcc",
                    "-static-libstdc++",
                    "-pthread",
                    str(source_path),
                    "-o",
                    str(exe_path),
                ],
                capture_output=True,
                text=True,
                timeout=45,
            )
            self.assertEqual(
                built.returncode,
                0,
                msg=f"dispatcher/gate extraction did not compile:\n{built.stdout}\n{built.stderr}",
            )
            ran = subprocess.run(
                [str(exe_path)], capture_output=True, text=True, timeout=15
            )
            self.assertEqual(
                ran.returncode,
                0,
                msg=f"dispatcher/gate matrix failed: {ran.stdout}\n{ran.stderr}",
            )

    def test_dispatch_nested_rr_latch_and_gate_owner_contract(self):
        dispatch = _function_block(self.core, "NgxResult DispatchEvaluate(")
        dispatch_c = _compact(dispatch)
        self.assertIn("if (g_inside_ngx_evaluate)", dispatch)
        self.assertIn("if (nested_feature.feature_id == 13)", dispatch)
        self.assertIn("LatchNestedRrOwnershipChange(\"Feature 13 Evaluate\")", dispatch)
        self.assertLess(
            dispatch.index("LatchNestedRrOwnershipChange"),
            dispatch.index("return original(command_list, handle, params, callback);"),
        )
        self.assertEqual(dispatch.count("ProcessEvaluate("), 1)
        self.assertIn("g_inside_ngx_evaluate = true", dispatch_c)

        gate = _function_block(self.core, "class ScopedRrOwnershipGate")
        gate_c = _compact(gate)
        self.assertIn("TryAcquireSRWLockExclusive(&g_rr_ownership_gate)", gate_c)
        self.assertIn("feature1_transaction_ ? 1 : 2", gate_c)
        self.assertIn("bool transaction_aborted() const", gate)

        # ProcessRrPost uses the real gate's nonblocking failure branch.  If a
        # Feature 1 transaction owns it, the uncertainty latch happens before
        # native RR is returned; the existing runtime harness covers its exact
        # once-only native call separately.
        rr_c = _compact(self.rr_post)
        self.assertIn("ScopedRrOwnershipGate ownership(true,false,true)", rr_c)
        self.assertIn("g_rr_ownership_gate_owner.load(std::memory_order_acquire)==1", rr_c)
        latch = self.rr_post.index("LatchConcurrentRrOwnershipChange")
        gate_return = self.rr_post.index(
            "return original(command_list,handle,params,callback);", latch
        )
        self.assertLess(latch, gate_return)
        self.assertEqual(self.rr_post.count("const NgxResult native_result=original("), 1)

    def test_post_native_abort_quarantines_before_finish(self):
        """Verify the latch checkpoint cannot run FinishAfterSr on an aborted call."""
        native = "NgxResult result = NrCallNativeWithFormatObservation(original, command_list, handle, params, callback, feature.generation);"
        native_pos = self.process.index(native)
        after_native = self.process[native_pos:]
        restore_color = after_native.index("RestoreSubstitution(before_sr);")
        restore_post = after_native.index("RestorePostSrParameters(after_sr);", restore_color)
        checkpoint = after_native.index(
            "if (rr_transaction.transaction_aborted() ||", restore_post
        )
        retry = after_native.index("if (used_before_sr && result != kNgxSuccess", checkpoint)
        finish = after_native.index("FinishAfterSr(", checkpoint)

        self.assertLess(native_pos, native_pos + restore_color)
        self.assertLess(restore_post, checkpoint)
        self.assertLess(checkpoint, retry)
        self.assertLess(checkpoint, finish)
        checkpoint_region = after_native[checkpoint:retry]
        for required in (
            "g_feature_tracking_uncertain.load(std::memory_order_acquire)",
            "QuarantineAbortedRrTransactionLocked(command_list)",
            "LeaveCriticalSection(&g_chain_cs)",
            "rr_transaction.Release()",
            "return result;",
        ):
            self.assertIn(required, checkpoint_region)
        self.assertNotIn("FinishAfterSr(", checkpoint_region)


if __name__ == "__main__":
    unittest.main()
