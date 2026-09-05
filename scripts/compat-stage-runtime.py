from pathlib import Path
import subprocess

BASE = 'dc876fc81ba831089f407f01a498e4b04d616122'

def text(path):
    return Path(path).read_text(encoding='utf-8-sig')

def save(path, value):
    Path(path).write_text(value, encoding='utf-8', newline='\n')

def replace(path, old, new, count=1):
    s = text(path)
    assert s.count(old) == count, (path, old[:80], s.count(old))
    save(path, s.replace(old, new))

for name in ['src/nr_before_sr.cpp','src/nr_command_state.inl','src/nr_settings_ui.inl','src/nr_feedback_capture.inl','src/nr_feedback_ticket.h']:
    before = subprocess.check_output(['git','show',BASE+':'+name]).decode('utf-8-sig')
    assert text(name) == before.replace('\r\n','\n'), name

replace('src/nr_command_state.inl', '    bool null_pipeline_baseline = false;', '''    bool null_pipeline_baseline = false;
    // Coverage is not inferred from a null pointer or an unobserved zero mask.
    bool null_guard_covered = false;
    bool null_guard_reset_observed = false;
    bool null_guard_tainted = false;''')
replace('src/nr_command_state.inl', '    state.null_pipeline_baseline = false;', '''    state.null_pipeline_baseline = false;
    state.null_guard_covered = false;
    state.null_guard_reset_observed = false;
    state.null_guard_tainted = false;''')
replace('src/nr_command_state.inl', '    const UINT64 serial = state.reset_serial + 1;\n    ReleaseNativeCommandState(state);', '''    const UINT64 serial = state.reset_serial + 1;
    const bool covered = state.null_guard_covered;
    const bool fresh_reset = std::strcmp(origin, "Reset") == 0;
    const bool reset_observed = fresh_reset ? covered : state.null_guard_reset_observed;
    const bool tainted = !fresh_reset && state.null_guard_tainted;
    ReleaseNativeCommandState(state);
    state.null_guard_covered = covered;
    state.null_guard_reset_observed = reset_observed;
    state.null_guard_tainted = tainted;''')
replace('src/nr_command_state.inl','    destination.null_pipeline_baseline = source.null_pipeline_baseline;', '''    destination.null_pipeline_baseline = source.null_pipeline_baseline;
    destination.null_guard_covered = source.null_guard_covered;
    destination.null_guard_reset_observed = source.null_guard_reset_observed;
    destination.null_guard_tainted = source.null_guard_tainted;''')
replace('src/nr_command_state.inl','bool CanRestoreNativeCommandState(','''// Full dynamic graphics replay stays experimental. The automatic path requires
// complete guard publication followed by a fresh successful native Reset.
bool CanAutoRestorePristineNull(const NativeCommandState &state) {
    return state.known && state.null_pipeline_baseline &&
        state.pipeline == nullptr && state.raytracing_pipeline == nullptr &&
        state.null_guard_covered && state.null_guard_reset_observed &&
        !state.null_guard_tainted && state.unknown_domains == 0 &&
        state.dynamic.recorded == 0 && state.dynamic.vertex_mask == 0 &&
        state.dynamic.stream_mask == 0;
}
bool CanRestoreNativeCommandState(''')
replace('src/nr_command_state.inl','    bool hooks_ready = false;', '''    bool hooks_ready = false;
    bool null_guard_requested = false;
    ULONGLONG next_hook_attempt = 0;''')
replace('src/nr_command_state.inl','std::atomic<unsigned long long> g_command_state_bypasses{0};', '''std::atomic<unsigned long long> g_command_state_bypasses{0};
std::atomic<bool> g_command_state_registration_repair_needed{false};
#define NR_HAS_COMMAND_STATE_EVIDENCE 1
struct CommandStateEvidence {
    bool available = false, captured = false, registered = false, hooks_ready = false;
    bool guard_requested = false, guard_covered = false, fresh_reset = false, tainted = false;
    bool meta_known = false, has_pipeline = false, has_ray_pipeline = false, null_baseline = false;
    bool roots[2] = {}, heaps[2] = {};
    const char *reason = "no-carrier-sample";
    const char *baseline = "unobserved";
    const char *route = "native-bypass";
    UINT dirty = 0, meta_init = 0, meta_execute = 0, vertex_mask = 0, stream_mask = 0;
    UINT arguments[2] = {};
    UINT64 sequence = 0, token = 0, reset_serial = 0, pipeline_writes = 0, dynamic = 0;
    ULONGLONG sampled_at = 0;
};
CommandStateEvidence g_command_state_evidence;
CommandStateEvidence SnapshotCommandStateEvidence() {
    CommandStateEvidence copy;
    if (!TryAcquireSRWLockShared(&g_command_state_lock)) {
        copy.reason = "snapshot-busy";
        return copy;
    }
    copy = g_command_state_evidence;
    ReleaseSRWLockShared(&g_command_state_lock);
    return copy;
}''')
for reason in ['execute-bundle','protected-resource-session','render-pass']:
    path='src/nr_command_state.inl'
    s=text(path)
    old=('state.unknown_reason = "'+reason+'")') if reason=='execute-bundle' else ('state.unknown_reason="'+reason+'")')
    assert s.count(old)==1
    save(path,s.replace(old,old[:-1]+'; state.null_guard_tainted=true)'))
replace('src/nr_command_state.inl','state.unknown_reason = "work-graph-SetProgram";', 'state.unknown_reason = "work-graph-SetProgram"; state.null_guard_tainted = true;')
replace('src/nr_command_state.inl','bool InstallNativeCommandStateHooks(ID3D12GraphicsCommandList *native) {','bool InstallNativeCommandStateHooks(ID3D12GraphicsCommandList *native, bool observe_pristine_null = false) {')
replace('src/nr_command_state.inl','    const bool allow_null_pso = AllowExperimentalNullPsoCarrier();','    const bool allow_null_pso = AllowExperimentalNullPsoCarrier() || observe_pristine_null;')

# Replace whole lifecycle/capture functions from the reviewed separate payload.
extra = text('scripts/compat-stage-functions.txt')
parts=extra.split('\n// FILE-SECTION: ')
for section in parts[1:]:
    signature,body=section.split('\n',1)
    path='src/nr_command_state.inl'
    s=text(path);start=s.index(signature);opening=s.index('{',start);depth=1;end=opening+1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    save(path,s[:start]+body.rstrip()+'\n'+s[end:])

replace('src/nr_before_sr.cpp','struct PerfSignature {', '''// Pending UI edits are transactions against the generation they displayed.
struct OverlayConfigSnapshot { ChainConfig config; unsigned generation; };
OverlayConfigSnapshot SnapshotOverlayConfig() {
    EnterCriticalSection(&g_chain_cs);
    ReloadConfig();
    OverlayConfigSnapshot snapshot{g_config, g_config_generation.load(std::memory_order_relaxed)};
    LeaveCriticalSection(&g_chain_cs);
    return snapshot;
}
enum class OverlayApplyResult { Applied, Busy, Conflict };
OverlayApplyResult ApplyOverlayConfig(ChainConfig next, unsigned expected_generation) {
    EnterCriticalSection(&g_chain_cs);
    if (nr_f8::g_config_hold.load(std::memory_order_acquire)) {
        LeaveCriticalSection(&g_chain_cs); return OverlayApplyResult::Busy;
    }
    if (g_config_generation.load(std::memory_order_relaxed) != expected_generation) {
        LeaveCriticalSection(&g_chain_cs); return OverlayApplyResult::Conflict;
    }
    ApplyConfig(next, "ReShade simplified settings");
    LeaveCriticalSection(&g_chain_cs);
    return OverlayApplyResult::Applied;
}

struct PerfSignature {''')
replace('src/nr_before_sr.cpp','bool CreateHook(void *target, void *detour, void **original, const char *name) {', text('scripts/compat-stage-hook.txt')+'\nbool CreateHook(void *target, void *detour, void **original, const char *name) {')
replace('src/nr_before_sr.cpp','        static_cast<int>(status), MH_StatusToString(status));\n    return status == MH_OK;','        static_cast<int>(status), MH_StatusToString(status));\n    if (status != MH_OK) RecordNgxHookFailure(target, status, name);\n    return status == MH_OK;')
replace('src/nr_before_sr.cpp','    if (observe_affinity) {\n        EnterCriticalSection(&g_chain_cs);','    if (observe_affinity) {\n        OnCommandStateObserveSubmission(command_list);\n        EnterCriticalSection(&g_chain_cs);')
replace('src/nr_before_sr.cpp','DLSS 5 NR Perfect Chain beta0.4.3 (Chinese UI)','DLSS 5 NR Perfect Chain beta0.4.3r2 (Chinese UI)')
replace('src/nr_before_sr.cpp','DLSS 5 AI Rendering Perfect Chain beta0.4.3 |','DLSS 5 AI Rendering Perfect Chain beta0.4.3r2 |')
replace('src/nr_feedback_capture.inl','void FeedbackAppendActivity(FeedbackSession &s, const char *prefix) {','void FeedbackAppendActivity(FeedbackSession &s, const char *prefix) {\n'+text('scripts/compat-stage-activity.txt'))
replace('src/nr_feedback_ticket.h','        "start.nrSuccess", "end.nrSuccess", "start.transferSubmitted", "end.transferSubmitted"};','''        "start.nrSuccess", "end.nrSuccess", "start.transferSubmitted", "end.transferSubmitted",
        "end.commandState.available", "end.commandState.ageMs", "end.commandState.reason",
        "end.commandState.route", "end.ngxHookFailure.available", "end.ngxHookFailure.operation",
        "end.ngxHookFailure.status", "end.ngxHookFailure.entry"};''')
replace('src/nr_settings_ui.inl','    ChainConfig next = has_pending ? pending : SnapshotConfig();','''    static unsigned pending_generation = 0;
    static bool edit_conflict = false;
    const auto current = SnapshotOverlayConfig();
    if (has_pending && pending_generation != current.generation) {
        has_pending = false; edit_conflict = true;
    }
    ChainConfig next = has_pending ? pending : current.config;''')
replace('src/nr_settings_ui.inl','    else if (reason != BypassReason::None)',text('scripts/compat-stage-status.txt')+'    else if (reason != BypassReason::None)')
replace('src/nr_settings_ui.inl','    if (g_config_persistence_failed.load())','''    if (next.identity_use_original)
        ImGui::TextWrapped("%s", ui(
            "Diagnostic identity mode is ON: NR may run, but its picture is deliberately discarded. Turn off 'Run NR but discard its picture' to compare the effect.",
            "诊断原图模式已开启：NR 可以运行，但效果会被主动丢弃。请关闭兼容性选项中的“运行 NR 但丢弃其画面”再对比。"));
    if (g_config_persistence_failed.load())''')
replace('src/nr_settings_ui.inl','    if (changed) { pending = next; has_pending = true; }','''    if (changed) {
        pending = next; pending_generation = current.generation;
        has_pending = true; edit_conflict = false;
    }''')
replace('src/nr_settings_ui.inl','        ApplyConfig(pending, "ReShade simplified settings"); has_pending = false;','''        const auto result = ApplyOverlayConfig(pending, pending_generation);
        if (result != OverlayApplyResult::Busy) has_pending = false;
        if (result == OverlayApplyResult::Conflict) edit_conflict = true;''')
s=text('src/nr_settings_ui.inl');index=s.rfind('\n}')
s=s[:index]+'''\n    if (edit_conflict)
        ImGui::TextWrapped("%s", ui(
            "Settings changed elsewhere. The stale edit was discarded; adjust again using the current values.",
            "设置已被快捷键、配置重载或采集恢复更新；旧编辑未覆盖新值，请在当前值上重新调整。"));'''+s[index:]
save('src/nr_settings_ui.inl',s)
