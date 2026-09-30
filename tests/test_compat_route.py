"""CPU-only compatibility-route checks against the actual ProcessEvaluate source.

The source-contract assertions cover the early fail-closed guards.  The C++
smoke harness extracts the production suffix beginning at CarrierScope and uses
the real processing Session/runtime with dependency-only state/callback stubs.
The routing order and FinishAfterSr decision remain unchanged production code.
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
    start = process.index("    nr_processing_runtime::CarrierScope processing_carrier;")
    end = process.rfind("\n}")
    return process[start:end]


class CompatibilityRoute(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = CORE.read_text(encoding="utf-8")
        cls.process = _function_block(cls.source, "NgxResult ProcessEvaluate(")
        cls.dispatch = _function_block(cls.source, "NgxResult DispatchEvaluate(")
        cls.before = _function_block(cls.source, "Substitution PrepareBeforeSr(")
        cls.after = _function_block(cls.source, "PostSrSubstitution PrepareAfterSr(")
        source_runtime = (CORE.parent / "nr_source_frame_runtime.inl").read_text(encoding="utf-8")
        cls.source_runtime = source_runtime
        cls.recording = _function_block(source_runtime, "Recording RecordingOf(")
        support = source_runtime.index("struct CoreRecordingDevice")
        cls.recording_support = source_runtime[support:source_runtime.index("Recording RecordingOf(", support)]

    def test_source_scope_uses_validated_alias_for_its_full_lifetime(self):
        alias = self.dispatch.index("NrPostBoundaryAliasScope source_list_alias(command_list);")
        source = self.dispatch.index("nr_source_runtime::EvaluateScope source_scope(")
        process = self.dispatch.index("ProcessEvaluate(", source)
        finish = self.dispatch.index("source_scope.Finish(result);", process)
        self.assertLess(alias, source)
        self.assertLess(source, process)
        self.assertLess(process, finish)
        self.assertEqual(self.dispatch.count("NrPostBoundaryAliasScope source_list_alias(command_list);"), 1)
        constructor = self.source_runtime.index(
            "EvaluateScope(ID3D12GraphicsCommandList* commands,const NgxHandle* handle,const NgxParams* params,int feature_id,Id generation)"
        )
        seed = self.source_runtime.index("SeedCoreRecordingDevice(commands);", constructor)
        locked_lookup = self.source_runtime.index("{Lock lock;energy_device=RecordingOf(commands).device;", seed)
        self.assertLess(seed, locked_lookup)
        locked_body = self.source_runtime[locked_lookup:self.source_runtime.index('}',locked_lookup)]
        self.assertIn('ObserveNativeProducer(reinterpret_cast<Id>(handle),generation,energy_device);',locked_body)

    def test_recording_identity_falls_back_to_exact_core_receipt(self):
        compiler = _compiler_command()
        if compiler is None:
            self.skipTest("no host C++ compiler")
        harness = f'''
#include <cassert>
#include <cstdint>
#include <climits>
#include <iterator>
struct ID3D12GraphicsCommandList {{}};
struct ID3D12DeviceChild {{}};
using Id = std::uint64_t;
struct Recording {{
    Id list=0,lifetime=0,reset=0,device=0;
    explicit operator bool() const {{ return list&&lifetime&&reset&&device; }}
}};
namespace nr_post_boundary {{
struct List {{ Id address=0,token=0,reset=0,device=0;bool known=false; }};
}}
bool observer_exists=false;
unsigned has_observer_calls=0,snapshot_calls=0,device_calls=0;
Id device_cookie=44;
Recording owned{{}};
nr_post_boundary::List core{{}};
struct ObservedSourceList {{ID3D12GraphicsCommandList* native=nullptr;Id lifetime=0,device=0;}};
ObservedSourceList observed_lists[256]{{}};
int lock_depth=0;
struct Lock {{Lock(){{++lock_depth;}}~Lock(){{--lock_depth;}}}};
bool HasObservedList(ID3D12GraphicsCommandList*) {{ ++has_observer_calls;return observer_exists; }}
Recording ObservedRecording(ID3D12GraphicsCommandList*) {{ return owned; }}
nr_post_boundary::List SnapshotPostBoundaryList(ID3D12GraphicsCommandList*) {{ ++snapshot_calls;return core; }}
Id DeviceOf(ID3D12DeviceChild*) {{ ++device_calls;return device_cookie; }}
{self.recording_support}
{self.recording}
int main() {{
    ID3D12GraphicsCommandList list,proxy;
    owned={{11,12,13,14}};core={{}};
    auto value=RecordingOf(&list);
    assert(value.list==11&&value.lifetime==12&&value.reset==13&&value.device==14);
    assert(snapshot_calls==1&&device_calls==0);

    // The headless NativeFG contract: exact Core identity, no ReShade source
    // observer. Hot lookup fails closed until the real cold Evaluate boundary
    // reads and caches the device cookie once.
    owned={{}};core={{21,22,3,23,true}};
    value=RecordingOf(&list);assert(!value&&device_calls==0);
    SeedCoreRecordingDevice(&list);value=RecordingOf(&list);
    assert(value.list==21&&value.lifetime==22&&value.reset==4&&value.device==44);
    assert(device_calls==1);

    // A validated proxy seed is keyed by canonical Core list/token; the later
    // native pointer reads the same identity without another COM/device call.
    core={{31,32,4,33,true}};device_cookie=54;SeedCoreRecordingDevice(&proxy);
    const auto proxy_seeded=RecordingOf(&proxy);value=RecordingOf(&list);
    assert(proxy_seeded.list==31&&proxy_seeded.lifetime==32&&proxy_seeded.reset==5&&proxy_seeded.device==54);
    assert(value.list==proxy_seeded.list&&value.lifetime==proxy_seeded.lifetime&&
           value.reset==proxy_seeded.reset&&value.device==proxy_seeded.device&&device_calls==2);
    core.token=33;value=RecordingOf(&list);
    assert(!value&&device_calls==2);

    // A canonical Core identity wins before and after source-observer readiness
    // changes, so capture cannot switch generation domains mid-recording.
    owned={{}};observer_exists=true;core={{41,42,5,43,true}};
    observed_lists[0]={{&proxy,71,44}};
    const auto captured=RecordingOf(&proxy);
    owned={{91,92,93,44}};
    value=RecordingOf(&list);
    assert(captured.list==41&&captured.lifetime==42&&captured.reset==6&&captured.device==44);
    assert(value.list==captured.list&&value.lifetime==captured.lifetime&&
           value.reset==captured.reset&&value.device==captured.device);
    // Execute can arrive on the canonical native pointer without a source
    // observer entry; the exact Core token reuses the bounded device mapping.
    owned={{}};observed_lists[0]={{}};value=RecordingOf(&list);
    assert(value.list==captured.list&&value.lifetime==captured.lifetime&&
           value.reset==captured.reset&&value.device==captured.device);
    assert(has_observer_calls==0&&device_calls==2);

    core={{51,52,UINT64_MAX,53,true}};SeedCoreRecordingDevice(&list);value=RecordingOf(&list);
    assert(!value&&device_calls==2);
    owned={{91,92,93,94}};core={{51,52,0,53,false}};value=RecordingOf(&list);
    assert(value.list==91&&value.lifetime==92&&value.reset==93&&value.device==94);
}}
'''
        with tempfile.TemporaryDirectory(prefix="recording-identity-") as temp:
            temp_path = Path(temp)
            source_path = temp_path / "recording_identity.cpp"
            exe_path = temp_path / ("recording_identity.exe" if os.name == "nt" else "recording_identity")
            source_path.write_text(harness, encoding="utf-8")
            built = subprocess.run(
                [compiler, "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
                 "-static", "-static-libgcc", "-static-libstdc++", str(source_path), "-o", str(exe_path)],
                capture_output=True, text=True, timeout=45,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe_path)], capture_output=True, text=True, timeout=15)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_actual_preparation_and_early_guard_contract(self):
        """Keep fail-closed guards and the four pre-Color fallback sites exact."""
        self.assertEqual(self.before.count("compatibility_fallback ="), 4)
        self.assertEqual(self.before.count("compatibility_fallback = true"), 3)
        self.assertIn("compatibility_fallback = render_extent_valid", self.before)
        self.assertIn("!before_sr.active && !before_sr.fatal_recording && !nr_processing_runtime::Recorded()", self.process)
        self.assertLess(self.before.rindex("substitution.fallback_cause ="), self.before.index("const bool nr_ready"))
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
        # The canonical RR integration replaces exactly its native early return
        # with the real ProcessRrPost dispatcher; the other guards stay native.
        rr_return = "return ProcessRrPost(original,command_list,handle,params,callback,feature);"
        rr_returns = early.count(rr_return)
        self.assertIn(rr_returns, (0, 1))
        self.assertEqual(early.count(native_return) + rr_returns, 5)
        self.assertEqual(early.count(observed_return), 6)
        # Present adds a seventh observed native return, with compact formatting.
        self.assertEqual("".join(early.split()).count("".join(observed_return.split())), 7)

        state_start = self.process.index(
            "    NativeCommandStateScope before_state(command_list);"
        )
        before_prepare = self.process.index(
            "    const auto prepare_before = [&] { return PrepareBeforeSr(", state_start
        )
        state_boundary = self.process[state_start:before_prepare]
        self.assertIn("if (!before_state.active)", state_boundary)
        self.assertIn(observed_return, state_boundary)

        after_prepare = self.process.index("    bool compatibility_fallback=false;")
        self.assertIn("after_sr=PrepareAfterSr(", self.process[after_prepare:])
        self.assertIn("nr_processing_runtime::Effective()==nr_processing::Start::After", self.process[after_prepare:])
        self.assertIn("NrRouteAttemptPost();", self.process[after_prepare:])
        self.assertIn("NrRoutePostPrepared(after_sr.active,", self.process[after_prepare:])
        restore = self.process.index("    before_state.Restore();", after_prepare)
        no_substitution = self.process.index(
            "    if (!before_sr.active && !after_sr.active)", restore
        )
        finish = self.process.index("FinishAfterSr(", no_substitution)
        self.assertLess(after_prepare, restore)
        self.assertLess(restore, no_substitution)
        self.assertLess(no_substitution, finish)
        self.assertIn("if (used_after_sr)", self.process[no_substitution:finish])
        self.assertIn("before_state.command_list,handle,params,feature,compatibility_fallback", self.process)
        self.assertIn("original(command_list, handle, params, callback)", self.process)
        self.assertIn(
            "FinishAfterSr(after_state.command_list, params, feature, after_sr)",
            self.process,
        )
        before_success = self.process.index("if (used_before_sr && result == kNgxSuccess")
        before_ack = self.process.index(
            "nr_processing_runtime::Adopted(nr_processing::Start::Before);",
            before_success,
        )
        after_branch = self.process.index("if (used_after_sr)", before_ack)
        after_finish = self.process.index("const bool post_recorded = FinishAfterSr(", after_branch)
        after_ack = self.process.index(
            "nr_processing_runtime::Adopted(nr_processing::Start::After);",
            after_finish,
        )
        self.assertLess(before_success, before_ack)
        self.assertLess(before_ack, after_branch)
        self.assertLess(after_finish, after_ack)
        self.assertIn("if (post_recorded)", self.process[after_finish:after_ack])
        self.assertEqual(self.process.count("nr_processing_runtime::Adopted("), 2)

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
        processing_runtime = (CORE.parent / "nr_processing_runtime.inl").read_text(encoding="utf-8")
        harness = f'''
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <iostream>
#include "nr_pre_fallback.h"
#include "nr_native_clear_request.h"
#include "nr_processing_route.h"
#include "nr_fg_presentation.h"

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
struct NrSlot {{}};
namespace nr_acceptance_gpu {{
enum class PreReason {{ Lifecycle, Unavailable, Owner, Route }};
struct EntryScope {{ bool active=true; void Decline(PreReason) {{}} }};
void Stamp(ID3D12GraphicsCommandList*, NrSlot&, unsigned) {{}}
void Abort(NrSlot&, const char*) {{}}
}}
nr_acceptance_gpu::EntryScope acceptance_sr_entry;
void PrimeNativeOutputCapture(ID3D12GraphicsCommandList*) {{}}
int* ArmNativeOutputCapture(ID3D12GraphicsCommandList*, const NgxParams*,
                            const FeatureRecord&, NrSlot*) {{ return nullptr; }}
void FinishNativeOutputCapture(ID3D12GraphicsCommandList*, const NgxParams*,
                               NgxResult, int*) {{}}
namespace nr_dual_runtime {{void Stamp(ID3D12GraphicsCommandList*,NrSlot&,unsigned){{}} void ResolveTiming(ID3D12GraphicsCommandList*,NrSlot&,bool){{}}}}
struct Substitution {{
    NgxResult fatal_recording=0;NrSlot* timing_slot=nullptr;
    bool active = false;
    bool compatibility_fallback = false;
    nr_pre_fallback::Cause fallback_cause = nr_pre_fallback::Cause::None;
}};
struct PostSrSubstitution {{
    NgxResult fatal_recording=0;NrSlot* slot=nullptr;
    bool active = false;
    bool redirected_output = false;
    nr_native_clear::Receipt prepared_clear_request;
    nr_native_clear::Invocation current_clear_request;
}};
struct ChainConfig {{
    bool enabled = true;
    bool identity_use_original = false;
    float transfer_strength = 1.0f;
    float post_transfer_strength = 1.0f;
    bool retry_original_on_sr_failure = true;
    int mode = 2;
    nr_processing::Start processing_start = nr_processing::Start::Before;
    unsigned long long processing_selection = 1;
}};
ChainConfig g_config;
std::atomic<unsigned> g_history_generation{{1}};
std::atomic<int> g_last_bypass_reason{{0}};
std::atomic<bool> g_feature_tracking_uncertain{{false}};
std::atomic<unsigned int> g_config_generation{{1}};
std::atomic<unsigned long long> g_compat_fallback_tick{{0}};
std::atomic<int> g_compat_fallback_reason{{0}};
std::atomic<unsigned> g_compat_fallback_cause{{0}};
std::atomic<unsigned int> g_compat_fallback_config{{0}};
std::atomic<unsigned long long> g_compat_fallback_attempts{{0}};
std::atomic<unsigned long long> g_compat_fallback_recorded{{0}};
std::atomic<unsigned long long> g_pipeline_eligible_carriers{{0}};
nr_fg_presentation::Policy g_fg_presentation_policy;
int g_chain_cs = 0;
unsigned long long g_now = 1;
Substitution g_before;
PostSrSubstitution g_after;
bool g_after_admitted = false;
bool check_after_compatibility = false;
bool g_before_recorded = false, g_before_source_blocked = false;
bool g_after_recorded = false, g_after_source_blocked = false;
bool g_after_state_active = true;
bool g_before_state_active = true;
unsigned g_state_scopes = 0;
bool g_finish_result = true;
int g_prepare_after_calls = 0;
int g_prepare_before_calls = 0;
int g_native_calls = 0;
int g_finish_calls = 0;
int g_restore_calls = 0;
NgxResult g_native_result = kNgxSuccess;

void EnterCriticalSection(int*) {{}}
bool TryEnterCriticalSection(int*) {{ return true; }}
void LeaveCriticalSection(int*) {{}}
unsigned long long GetTickCount64() {{ return g_now++; }}
void SetBypassReason(BypassReason reason) {{
    g_last_bypass_reason.store(static_cast<int>(reason));
}}
void Log(const char*, ...) {{}}
const char* BypassReasonName(BypassReason) {{ return "fixture-no-private-work"; }}
namespace nr_source_runtime {{unsigned long long next_source=1;unsigned long long CurrentRetryToken(){{return next_source++;}}}}
{processing_runtime}
const char* FeatureName(int) {{ return "Feature 1"; }}
const char* ResultName(NgxResult) {{ return "fixture"; }}
void TrackDlssResolution(const NgxParams*, const FeatureRecord&) {{}}
bool RecordVisibleTransferLocked(ID3D12GraphicsCommandList*, unsigned long long, int) {{ return true; }}
namespace nr_compat_present_runtime {{
bool InlineDrainReady() {{ return true; }}
bool NativeNrDrained() {{ return false; }}
}}
void RestoreSubstitution(const Substitution&) {{ ++g_restore_calls; }}
void RestorePostSrParameters(const PostSrSubstitution&) {{ ++g_restore_calls; }}
void QuarantineAbortedRrTransactionLocked(ID3D12GraphicsCommandList*) {{}}
struct CommandStateEvidence {{ const char* reason = "fixture"; }};
CommandStateEvidence SnapshotCommandStateEvidence() {{ return {{"fixture"}}; }}

struct NativeCommandStateScope {{
    bool active = true;
    ID3D12GraphicsCommandList* command_list = nullptr;
    explicit NativeCommandStateScope(ID3D12GraphicsCommandList* list)
        : active(++g_state_scopes == 1 ? g_before_state_active : g_after_state_active), command_list(list) {{}}
    void Restore() {{ ++g_restore_calls; }}
    void AbandonAfterOpaqueFailure() {{active=false;}}
}};
struct StubTransaction {{
    bool transaction_aborted() const {{ return false; }}
    void Release() {{}}
}};
StubTransaction rr_transaction;

Substitution PrepareBeforeSr(ID3D12GraphicsCommandList*, const NgxHandle*,
                             const NgxParams*, const FeatureRecord&) {{
    ++g_prepare_before_calls;
    if (g_before_recorded || g_before.active) nr_processing_runtime::MarkRecorded();
    if (g_before_source_blocked) nr_processing_runtime::BlockSource();
    return g_before;
}}
PostSrSubstitution PrepareAfterSr(ID3D12GraphicsCommandList*, const NgxHandle*,
                                  const NgxParams*, const FeatureRecord&,
                                  bool compatibility_fallback = false) {{
    ++g_prepare_after_calls;
    check_after_compatibility = compatibility_fallback;
    if (g_after_recorded || g_after.active) nr_processing_runtime::MarkRecorded();
    if (g_after_source_blocked) nr_processing_runtime::BlockSource();
    if (!g_after_admitted) return {{}};
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

static unsigned checks = 0;
static void check(bool value, const char* name) {{
    ++checks;
    if (!value) {{ std::cerr << name << "\\n"; std::exit(1); }}
}}
static void reset() {{
    g_before = {{}}; g_after = {{}}; g_after_admitted = false;
    g_before_recorded = g_before_source_blocked = false;
    g_after_recorded = g_after_source_blocked = false;
    check_after_compatibility = false;
    g_after_state_active = true; g_finish_result = true;
    g_before_state_active = true; g_state_scopes = 0;
    g_prepare_before_calls = g_prepare_after_calls = g_native_calls = g_finish_calls = g_restore_calls = 0;
    g_compat_fallback_attempts.store(0); g_compat_fallback_recorded.store(0);
    g_native_result = kNgxSuccess; g_config = {{}};
    g_feature_tracking_uncertain.store(false);
    nr_processing_runtime::session = {{}};
    nr_processing_runtime::effective.store(0);
    nr_processing_runtime::private_recorded = nr_processing_runtime::source_blocked = false;
    nr_processing_runtime::carrier_serial = 0;
    nr_source_runtime::next_source = 1;
}}

int main() {{
    ID3D12GraphicsCommandList command;
    NgxHandle handle;
    NgxParams params;
    void* callback = nullptr;

    using Start = nr_processing::Start;
    // Active pre work owns this source. No post preparation or finish may run.
    reset();
    g_before.active = true;
    g_after.active = true; g_after_admitted = true;
    auto result = ProcessRoute(&command, &handle, &params, callback);
    check(result == kNgxSuccess && g_prepare_before_calls == 1 && g_prepare_after_calls == 0 &&
          g_native_calls == 1 && g_finish_calls == 0 &&
          g_compat_fallback_attempts.load() == 0 && nr_processing_runtime::Effective() == Start::Before,
          "pre-active-no-second-inline-attempt");

    reset();g_before.fatal_recording=kNgxFailure;
    result=ProcessRoute(&command,&handle,&params,callback);
    check(result==kNgxFailure&&!g_native_calls&&!g_prepare_after_calls&&!g_restore_calls,"opaque pre failure records no native SR or state restoration");
    reset();g_before.compatibility_fallback=true;g_before.fallback_cause=nr_pre_fallback::Cause::InputStorage;
    g_after.active=true;g_after_admitted=true;g_after.fatal_recording=kNgxFailure;g_finish_result=false;
    result=ProcessRoute(&command,&handle,&params,callback);
    check(result==kNgxFailure&&g_native_calls==1&&g_finish_calls==1&&g_restore_calls==3,"opaque post failure never retries native SR or restores GPU command state");

    // No-private-work rejection follows the requested suffix, independently of
    // the old five-cause Color whitelist; these causes remain diagnostics.
    for (unsigned cause : {{0u, 6u, 0xffffffffu}}) {{
        reset();
        g_before.compatibility_fallback = true;
        g_before.fallback_cause = static_cast<nr_pre_fallback::Cause>(cause);
        g_after.active = true; g_after_admitted = true;
        result = ProcessRoute(&command, &handle, &params, callback);
        check(result == kNgxSuccess && g_native_calls == 1 && g_finish_calls == 1 &&
              g_prepare_before_calls == 1 && g_prepare_after_calls == 1 &&
              nr_processing_runtime::Effective() == Start::After,
              "no-private-work-uses-suffix-not-colour-cause-whitelist");
    }}
    for (unsigned cause = 1; cause <= 5; ++cause) {{
        reset();
        g_before.compatibility_fallback = true;
        g_before.fallback_cause = static_cast<nr_pre_fallback::Cause>(cause);
        g_after.active = true; g_after_admitted = true;
        result = ProcessRoute(&command, &handle, &params, callback);
        check(result == kNgxSuccess && g_native_calls == 1 && g_finish_calls == 1 &&
              g_compat_fallback_recorded.load() == 1 && g_compat_fallback_cause.load() == cause &&
              nr_processing_runtime::Effective() == Start::After &&
              nr_processing_runtime::session.Requested() == Start::Before &&
              nr_processing_runtime::session.Retained() == Start::After,
              "eligible-colour-cause-post-once-and-reason-published");
        reset();
        g_before.active = true; g_before.compatibility_fallback = true;
        g_before.fallback_cause = static_cast<nr_pre_fallback::Cause>(cause);
        g_after.active = true; g_after_admitted = true;
        result = ProcessRoute(&command, &handle, &params, callback);
        check(result == kNgxSuccess && g_native_calls == 1 && !g_finish_calls &&
              !g_compat_fallback_attempts.load(), "active-pre-never-also-post");
    }}

    // Recorded work (including warmup without an active substitution) and
    // source rejection forbid retrying another route on this same source.
    for (bool blocked : {{false, true}}) {{
        reset(); g_before_recorded = !blocked; g_before_source_blocked = blocked;
        g_before.compatibility_fallback = true;
        g_after.active = true; g_after_admitted = true;
        result = ProcessRoute(&command, &handle, &params, callback);
        check(result == kNgxSuccess && g_prepare_before_calls == 1 && !g_prepare_after_calls &&
              g_native_calls == 1 && !g_finish_calls &&
              nr_processing_runtime::Effective() == Start::Before,
              blocked ? "source-blocked-pre-cannot-advance" : "recorded-or-warmup-pre-cannot-advance");
        check(!nr_processing_runtime::Recorded(), "carrier-private-facts-do-not-leak");
    }}

    // Before -> After -> Present changes route, with one native SR call. The
    // later Present boundary must independently acquire and prove its source.
    reset();
    result = ProcessRoute(&command, &handle, &params, callback);
    check(result == kNgxSuccess && g_prepare_before_calls == 1 && g_prepare_after_calls == 1 &&
          g_native_calls == 1 && !g_finish_calls && nr_processing_runtime::PresentSelected(),
          "before-after-present-no-private-work-native-once");
    check(g_config.processing_start == Start::Before && g_config.mode == 2 &&
          nr_processing_runtime::session.Requested() == Start::Before,
          "fallback-preserves-saved-start-and-legacy-mode");
    // No private Present work was recorded or adopted on the first carrier, so
    // the retained route is still the requested Before entry. A new source
    // retries the requested suffix once, then reaches Present again; the
    // native call remains exactly once per carrier.
    result = ProcessRoute(&command, &handle, &params, callback);
    check(result == kNgxSuccess && g_prepare_before_calls == 2 && g_prepare_after_calls == 2 &&
          g_native_calls == 2 && !g_finish_calls && nr_processing_runtime::PresentSelected(),
          "unadopted-present-retries-requested-route-on-new-source");

    for (bool blocked : {{false, true}}) {{
        reset(); g_after_recorded = !blocked; g_after_source_blocked = blocked;
        result = ProcessRoute(&command, &handle, &params, callback);
        check(result == kNgxSuccess && g_prepare_before_calls == 1 && g_prepare_after_calls == 1 &&
              g_native_calls == 1 && !g_finish_calls && nr_processing_runtime::Effective() == Start::After,
              blocked ? "source-blocked-after-cannot-advance" : "recorded-or-warmup-after-cannot-advance");
    }}

    // An After request tests only After -> Present, never an earlier route.
    for (bool admitted : {{false, true}}) {{
        reset(); g_config.processing_start = Start::After; g_config.mode = 1;
        g_before.active = true; g_after_admitted = admitted; g_after.active = admitted;
        result = ProcessRoute(&command, &handle, &params, callback);
        check(result == kNgxSuccess && !g_prepare_before_calls && g_prepare_after_calls == 1 &&
              g_native_calls == 1 && g_finish_calls == (admitted ? 1 : 0) && !check_after_compatibility &&
              nr_processing_runtime::Effective() == (admitted ? Start::After : Start::Present),
              "requested-after-only-after-present-suffix");
    }}
    reset(); g_config.processing_start = Start::Present;
    g_before.active = true; g_after.active = true; g_after_admitted = true;
    result = ProcessRoute(&command, &handle, &params, callback);
    check(result == kNgxSuccess && !g_prepare_before_calls && !g_prepare_after_calls &&
          g_native_calls == 1 && !g_finish_calls && !g_restore_calls,
          "requested-present-inline-native-once");

    for (Start start : {{Start::Before, Start::After}}) {{
        reset(); g_config.processing_start = start; g_before_state_active = false;
        result = ProcessRoute(&command, &handle, &params, callback);
        check(result == kNgxSuccess && !g_prepare_before_calls && !g_prepare_after_calls &&
              g_native_calls == 1 && !g_finish_calls && nr_processing_runtime::PresentSelected(),
              "unverified-inline-command-state-advances-to-present-without-work");
    }}

    // Early pre Color rejection selects a post attempt; successful native SR
    // is the only path that may call FinishAfterSr and record the fallback.
    reset();
    g_before.compatibility_fallback = true;
    g_before.fallback_cause = nr_pre_fallback::Cause::InputStorage;
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
    g_before.fallback_cause = nr_pre_fallback::Cause::InputStorage;
    g_after_admitted = false;
    result = ProcessRoute(&command, &handle, &params, callback);
    check(result == kNgxSuccess && g_prepare_after_calls == 1 &&
          g_native_calls == 1 && g_finish_calls == 0 &&
          g_compat_fallback_attempts.load() == 1 && nr_processing_runtime::PresentSelected(),
          "post-reject-native-once-no-finish");

    // A legacy retry=true must not cause a second native call, including Color
    // and redirected Output failure or missing state after successful native SR.
    reset(); g_before.active = true; g_native_result = kNgxFailure;
    g_config.retry_original_on_sr_failure = true;
    result = ProcessRoute(&command, &handle, &params, callback);
    check(result == kNgxFailure && g_native_calls == 1 && !g_finish_calls &&
          g_restore_calls == 3 && nr_processing_runtime::Effective() == Start::Before,
          "native-pre-failure-restores-parameters-without-retry-or-route-advance");
    for (bool native_failure : {{false, true}}) {{
        reset(); g_config.processing_start = Start::After; g_config.mode = 1;
        g_config.retry_original_on_sr_failure = true;
        g_after.active = true; g_after.redirected_output = true; g_after_admitted = true;
        g_native_result = native_failure ? kNgxFailure : kNgxSuccess;
        g_after_state_active = native_failure;
        result = ProcessRoute(&command, &handle, &params, callback);
        check(result == (native_failure ? kNgxFailure : kNgxSuccess) && g_native_calls == 1 &&
              !g_finish_calls && g_restore_calls == 3 &&
              nr_processing_runtime::Effective() == Start::After,
              native_failure ? "redirected-native-failure-no-second-native-sr" :
                               "missing-post-state-no-second-native-sr");
    }}

    // Native failure with the original Output also cannot FinishAfterSr.
    reset();
    g_before.compatibility_fallback = true;
    g_before.fallback_cause = nr_pre_fallback::Cause::InputStorage;
    g_after.active = true; g_after.redirected_output = false;
    g_after_admitted = true; g_native_result = kNgxFailure;
    result = ProcessRoute(&command, &handle, &params, callback);
    check(result == kNgxFailure && g_native_calls == 1 &&
          g_finish_calls == 0 && g_compat_fallback_recorded.load() == 0 &&
          nr_processing_runtime::session.Retained() == Start::After,
          "native-failure-retains-recorded-after-route-without-finish");
    std::cout << "PASS: " << checks << " production suffix checks\\n";
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
                    "-I", str(ROOT / "src"),
                    "-O0",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-static",
                    "-static-libgcc",
                    "-static-libstdc++",
                    "-I", str(CORE.parent),
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
            print(ran.stdout.strip())
            # Prove the warmup/recorded and source-blocked cases exercise the
            # runtime guard, rather than passing only because of mock admission.
            recorded_body = "bool Recorded(){return private_recorded||source_blocked;}"
            self.assertEqual(processing_runtime.count(recorded_body), 1)
            for name, replacement, expected in (
                ("ignore-recorded", "bool Recorded(){return source_blocked;}", "recorded-or-warmup-pre-cannot-advance"),
                ("ignore-source-blocked", "bool Recorded(){return private_recorded;}", "source-blocked-pre-cannot-advance"),
            ):
                mutant_path = temp_path / f"{name}.cpp"
                mutant_exe = temp_path / (f"{name}.exe" if os.name == "nt" else name)
                mutant_path.write_text(harness.replace(recorded_body, replacement), encoding="utf-8")
                mutant_built = subprocess.run(
                    [compiler, "-std=c++17", "-I", str(CORE.parent), "-O0", "-Wall", "-Wextra", "-Werror",
                     "-static", "-static-libgcc", "-static-libstdc++", str(mutant_path), "-o", str(mutant_exe)],
                    capture_output=True, text=True, timeout=45,
                )
                self.assertEqual(mutant_built.returncode, 0, mutant_built.stderr)
                mutant_ran = subprocess.run([str(mutant_exe)], capture_output=True, text=True, timeout=15)
                self.assertNotEqual(mutant_ran.returncode, 0, f"negative control unexpectedly passed: {name}")
                self.assertIn(expected, mutant_ran.stderr)
                print(f"PASS: behavioral negative control {name} rejected at {expected}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
