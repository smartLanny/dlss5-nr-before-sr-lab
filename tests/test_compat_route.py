"""CPU-only compatibility-route checks against the actual ProcessEvaluate source.

The source-contract assertions cover the early fail-closed guards.  The C++
smoke harness extracts the production suffix beginning at NativeCommandStateScope
and supplies only state/callback stubs, so the routing order and FinishAfterSr
decision remain production code rather than a copied implementation.
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


def _function_block(source: str, marker: str) -> str:
    """Return a C++ function/class block using balanced braces and no rewrite."""
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
    raise AssertionError(f"unclosed C++ block: {marker}")


def _compact(text: str) -> str:
    return " ".join(text.split())


def _compiler_command():
    configured = os.environ.get("COMPAT_ROUTE_TEST_CXX") or os.environ.get("CXX")
    candidates = [configured] if configured else []
    candidates.extend(("clang++", "g++", "c++"))
    for candidate in candidates:
        if not candidate or Path(candidate).name.lower() in {"cl", "cl.exe"}:
            continue
        resolved = candidate if Path(candidate).exists() else shutil.which(candidate)
        if resolved:
            return resolved
    return None


def _process_suffix(source: str) -> str:
    process = _function_block(source, "NgxResult ProcessEvaluate(")
    start = process.index("    NativeCommandStateScope before_state(command_list);")
    end = process.rfind("\n}")
    return process[start:end]


class CompatibilityRoute(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = CORE.read_text(encoding="utf-8")
        cls.process = _function_block(cls.source, "NgxResult ProcessEvaluate(")
        cls.before = _function_block(cls.source, "Substitution PrepareBeforeSr(")
        cls.after = _function_block(cls.source, "PostSrSubstitution PrepareAfterSr(")

    def test_actual_preparation_and_early_guard_contract(self):
        """Keep fail-closed guards and the four pre-Color fallback sites exact."""
        self.assertEqual(self.before.count("compatibility_fallback ="), 4)
        self.assertEqual(self.before.count("compatibility_fallback = true"), 3)
        self.assertIn("compatibility_fallback = render_extent_valid", self.before)
        self.assertIn("compatibility_fallback && g_config.mode == 2", self.after)

        before_state = self.process.index(
            "    NativeCommandStateScope before_state(command_list);"
        )
        early = self.process[:before_state]
        native_return = "return original(command_list, handle, params, callback);"
        observed_return = "return NrCallNativeWithFormatObservation(original, command_list, handle, params, callback, feature.generation);"
        observation = early.index("NrPrepareFormatObservation(command_list);")
        for guard in (
            "!g_enabled_fast.load(std::memory_order_acquire)",
            "OtherNrOwnerActive()",
            "g_module_rescan_in_progress.load(std::memory_order_acquire)",
            "g_evaluate_target_overflow.load(std::memory_order_acquire)",
            "g_lifecycle_tracking_uncertain.load(std::memory_order_acquire)",
            "g_nr_failed_create_process_latched.load(std::memory_order_acquire)",
            "g_feature_tracking_uncertain.load(std::memory_order_acquire)",
            "ShouldGuardFeature1ForRayReconstruction()",
            "g_rr_create_in_progress.load(std::memory_order_acquire)",
            "zero_transfer_native",
        ):
            self.assertIn(guard, early)
            guard_pos = early.index(guard)
            self.assertNotEqual(
                early.find(native_return if guard_pos < observation else observed_return, guard_pos),
                -1,
                guard,
            )
        self.assertEqual(early.count(native_return), 5)
        self.assertEqual(early.count(observed_return), 6)

        state_start = self.process.index(
            "    NativeCommandStateScope before_state(command_list);"
        )
        before_prepare = self.process.index(
            "    Substitution before_sr = PrepareBeforeSr(", state_start
        )
        state_boundary = self.process[state_start:before_prepare]
        self.assertIn("if (!before_state.active)", state_boundary)
        self.assertIn(observed_return, state_boundary)

        after_prepare = self.process.index(
            "    PostSrSubstitution after_sr = PrepareAfterSr("
        )
        restore = self.process.index("    before_state.Restore();", after_prepare)
        no_substitution = self.process.index(
            "    if (!before_sr.active && !after_sr.active)", restore
        )
        finish = self.process.index("FinishAfterSr(", no_substitution)
        self.assertLess(after_prepare, restore)
        self.assertLess(restore, no_substitution)
        self.assertLess(no_substitution, finish)
        self.assertIn("if (used_after_sr)", self.process[no_substitution:finish])
        self.assertIn(
            "before_state.command_list, handle, params, feature, compatibility_fallback",
            self.process,
        )
        self.assertIn("original(command_list, handle, params, callback)", self.process)
        self.assertIn(
            "FinishAfterSr(after_state.command_list, params, feature, after_sr)",
            self.process,
        )

    def test_extracted_process_evaluate_suffix_route_matrix(self):
        """Run the exact post-preparation route with dependency-only stubs."""
        compiler = _compiler_command()
        if compiler is None:
            self.skipTest("no host C++ compiler; source-contract test still runs")

        suffix = _process_suffix(self.source)
        native_forward = 'template <class NativeEvaluate>\n' + _function_block(
            (CORE.parent / 'nr_native_format_runtime.inl').read_text(encoding='utf-8'),
            'NgxResult NrCallNativeWithFormatObservation('
        )
        harness = f'''
#include <atomic>
#include <cstdarg>
#include <cstdlib>
#include <iostream>

struct ID3D12GraphicsCommandList {{}};
struct NgxHandle {{ int id = 1; }};
struct NgxParams {{}};
using NgxResult = int;
using EvaluateFeatureFn = NgxResult (*)(ID3D12GraphicsCommandList*,
    const NgxHandle*, const NgxParams*, void*);
constexpr NgxResult kNgxSuccess = 0;
constexpr NgxResult kNgxFailure = 0x801;
// Preserve the production native forwarding body, with value-only observer seams.
struct NrNativeFormatScope {{
    NrNativeFormatScope(ID3D12GraphicsCommandList*,const NgxParams*,unsigned long long) {{}}
    void Finish(bool) {{}}
}};
void NrRecoveryCommandObserved() {{}}
{native_forward}
enum class BypassReason {{ LifecycleIsolation = 1 }};

struct FeatureRecord {{ int feature_id = 1; unsigned long long generation = 1; }};
struct Substitution {{
    bool active = false;
    bool compatibility_fallback = false;
}};
struct PostSrSubstitution {{
    bool active = false;
    bool redirected_output = false;
}};
struct ChainConfig {{
    bool enabled = true;
    bool identity_use_original = false;
    float post_transfer_strength = 1.0f;
    bool retry_original_on_sr_failure = true;
}};
ChainConfig g_config;
std::atomic<int> g_last_bypass_reason{{0}};
std::atomic<bool> g_feature_tracking_uncertain{{false}};
std::atomic<unsigned int> g_config_generation{{1}};
std::atomic<unsigned long long> g_compat_fallback_tick{{0}};
std::atomic<int> g_compat_fallback_reason{{0}};
std::atomic<unsigned int> g_compat_fallback_config{{0}};
std::atomic<unsigned long long> g_compat_fallback_attempts{{0}};
std::atomic<unsigned long long> g_compat_fallback_recorded{{0}};
std::atomic<unsigned long long> g_pipeline_eligible_carriers{{0}};
int g_chain_cs = 0;
unsigned long long g_now = 1;
Substitution g_before;
PostSrSubstitution g_after;
bool g_after_admitted = false;
bool g_after_state_active = true;
bool g_finish_result = true;
int g_prepare_after_calls = 0;
int g_native_calls = 0;
int g_finish_calls = 0;
int g_restore_calls = 0;
NgxResult g_native_result = kNgxSuccess;

void EnterCriticalSection(int*) {{}}
void LeaveCriticalSection(int*) {{}}
unsigned long long GetTickCount64() {{ return g_now++; }}
void SetBypassReason(BypassReason reason) {{
    g_last_bypass_reason.store(static_cast<int>(reason));
}}
void Log(const char*, ...) {{}}
const char* FeatureName(int) {{ return "Feature 1"; }}
const char* ResultName(NgxResult) {{ return "fixture"; }}
void TrackDlssResolution(const NgxParams*, const FeatureRecord&) {{}}
bool RecordVisibleTransferLocked(ID3D12GraphicsCommandList*, unsigned long long, int) {{ return true; }}
void RestoreSubstitution(const Substitution&) {{ ++g_restore_calls; }}
void RestorePostSrParameters(const PostSrSubstitution&) {{ ++g_restore_calls; }}
void QuarantineAbortedRrTransactionLocked(ID3D12GraphicsCommandList*) {{}}
struct CommandStateEvidence {{ const char* reason = "fixture"; }};
CommandStateEvidence SnapshotCommandStateEvidence() {{ return {{"fixture"}}; }}

struct NativeCommandStateScope {{
    bool active = true;
    ID3D12GraphicsCommandList* command_list = nullptr;
    explicit NativeCommandStateScope(ID3D12GraphicsCommandList* list)
        : active(g_after_state_active), command_list(list) {{}}
    void Restore() {{ ++g_restore_calls; }}
}};
struct StubTransaction {{
    bool transaction_aborted() const {{ return false; }}
    void Release() {{}}
}};
StubTransaction rr_transaction;

Substitution PrepareBeforeSr(ID3D12GraphicsCommandList*, const NgxHandle*,
                             const NgxParams*, const FeatureRecord&) {{
    return g_before;
}}
PostSrSubstitution PrepareAfterSr(ID3D12GraphicsCommandList*, const NgxHandle*,
                                  const NgxParams*, const FeatureRecord&,
                                  bool compatibility_fallback = false) {{
    ++g_prepare_after_calls;
    if (!compatibility_fallback || !g_after_admitted) return {{}};
    return g_after;
}}
bool FinishAfterSr(ID3D12GraphicsCommandList*, const NgxParams*,
                   const FeatureRecord&, const PostSrSubstitution&) {{
    ++g_finish_calls;
    return g_finish_result;
}}
NgxResult original(ID3D12GraphicsCommandList*, const NgxHandle*,
                   const NgxParams*, void*) {{
    ++g_native_calls;
    return g_native_result;
}}

NgxResult ProcessRoute(ID3D12GraphicsCommandList* command_list,
                       const NgxHandle* handle, const NgxParams* params,
                       void* callback) {{
    FeatureRecord feature{{1, 1}};
{suffix}
}}

static void check(bool value, const char* name) {{
    if (!value) {{ std::cerr << name << "\\n"; std::exit(1); }}
}}
static void reset() {{
    g_before = {{}}; g_after = {{}}; g_after_admitted = false;
    g_after_state_active = true; g_finish_result = true;
    g_prepare_after_calls = g_native_calls = g_finish_calls = g_restore_calls = 0;
    g_compat_fallback_attempts.store(0); g_compat_fallback_recorded.store(0);
    g_native_result = kNgxSuccess; g_config = {{}};
}}

int main() {{
    ID3D12GraphicsCommandList command;
    NgxHandle handle;
    NgxParams params;
    void* callback = nullptr;

    // Pre-SR active means this is an ordinary pre route; post admission is not
    // enabled by the compatibility flag and FinishAfterSr is unreachable.
    reset();
    g_before.active = true;
    g_after.active = true; g_after_admitted = true;
    auto result = ProcessRoute(&command, &handle, &params, callback);
    check(result == kNgxSuccess && g_prepare_after_calls == 1 &&
          g_native_calls == 1 && g_finish_calls == 0 &&
          g_compat_fallback_attempts.load() == 0,
          "pre-active-post-ineligible");

    // Early pre Color rejection selects a post attempt; successful native SR
    // is the only path that may call FinishAfterSr and record the fallback.
    reset();
    g_before.compatibility_fallback = true;
    g_after.active = true; g_after.redirected_output = true;
    g_after_admitted = true;
    result = ProcessRoute(&command, &handle, &params, callback);
    check(result == kNgxSuccess && g_prepare_after_calls == 1 &&
          g_native_calls == 1 && g_finish_calls == 1 &&
          g_compat_fallback_attempts.load() == 1 &&
          g_compat_fallback_recorded.load() == 1,
          "pre-reject-post-success-finishes-once");

    // Post preparation rejection returns to native SR without FinishAfterSr.
    reset();
    g_before.compatibility_fallback = true;
    g_after_admitted = false;
    result = ProcessRoute(&command, &handle, &params, callback);
    check(result == kNgxSuccess && g_prepare_after_calls == 1 &&
          g_native_calls == 1 && g_finish_calls == 0 &&
          g_compat_fallback_attempts.load() == 1,
          "post-reject-native-once-no-finish");

    // A native SR failure also cannot FinishAfterSr.  Keep redirected_output
    // false so this seam does not exercise the separate output retry branch.
    reset();
    g_before.compatibility_fallback = true;
    g_after.active = true; g_after.redirected_output = false;
    g_after_admitted = true; g_native_result = kNgxFailure;
    result = ProcessRoute(&command, &handle, &params, callback);
    check(result == kNgxFailure && g_native_calls == 1 &&
          g_finish_calls == 0 && g_compat_fallback_recorded.load() == 0,
          "native-failure-no-finish");
    return 0;
}}
'''
        with tempfile.TemporaryDirectory(prefix="compat-route-") as temp:
            temp_path = Path(temp)
            source_path = temp_path / "compat_route.cpp"
            exe_path = temp_path / (
                "compat_route.exe" if os.name == "nt" else "compat_route"
            )
            source_path.write_text(harness, encoding="utf-8")
            built = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-O0",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
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
                msg=f"production suffix did not compile:\n{built.stdout}\n{built.stderr}",
            )
            ran = subprocess.run(
                [str(exe_path)], capture_output=True, text=True, timeout=15
            )
            self.assertEqual(
                ran.returncode,
                0,
                msg=f"compatibility route matrix failed:\n{ran.stdout}\n{ran.stderr}",
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
