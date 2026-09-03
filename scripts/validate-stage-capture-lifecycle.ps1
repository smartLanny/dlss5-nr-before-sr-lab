param(
    [string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT,
    [string]$Revision = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([string]::IsNullOrWhiteSpace($ToolchainRoot)) { throw 'Provide -ToolchainRoot.' }
$project = Split-Path -Parent $PSScriptRoot
if ($Revision) {
    $source = (& git -C $project show "${Revision}:src/nr_before_sr.cpp") -join "`n"
    if ($LASTEXITCODE -ne 0) { throw 'Could not read baseline source.' }
    $capture = (& git -C $project show "${Revision}:src/nr_stage_capture.inl") -join "`n"
    if ($LASTEXITCODE -ne 0) { throw 'Could not read baseline capture source.' }
} else {
    $source = Get-Content -LiteralPath (Join-Path $project 'src\nr_before_sr.cpp') -Raw
    $capture = Get-Content -LiteralPath (Join-Path $project 'src\nr_stage_capture.inl') -Raw
}
function Extract([string]$Text, [string]$Signature) {
    $start = $Text.IndexOf($Signature, [StringComparison]::Ordinal)
    if ($start -lt 0) { throw "Missing function: $Signature" }
    $open = $Text.IndexOf('{', $start)
    $end = $open + 1
    $depth = 1
    while ($end -lt $Text.Length -and $depth -gt 0) {
        if ($Text[$end] -eq '{') { ++$depth }
        if ($Text[$end] -eq '}') { --$depth }
        ++$end
    }
    if ($depth -ne 0) { throw "Unterminated function: $Signature" }
    return $Text.Substring($start, $end - $start)
}
$types = ''
foreach ($name in @('NrTuningSignature', 'NrDiagnosticIdentity', 'NrStageCaptureInfo', 'NrStageCapture')) {
    $match = [regex]::Match($source, "struct $name \{[\s\S]*?\r?\n\};")
    if (-not $match.Success) { throw "Missing type: $name" }
    $types += $match.Value + "`n"
}
$exportType = [regex]::Match($capture, 'struct StageCaptureExport \{[\s\S]*?\r?\n\};').Value
$arm = Extract $capture 'void ArmStageCaptureLocked('
$guard = [regex]::Match($arm, '\{(?<guard>[\s\S]*?)NrStageCapture capture').Groups['guard'].Value.Replace('return;', 'return false;')
$functions = (Extract $capture 'unsigned int StageCaptureStatus(') + "`n"
$functions += (Extract $source 'bool CanMutateSlotLocked(') + "`n"
if ($capture.Contains('void PrepareStageCaptureContextLocked(')) {
    $functions += (Extract $capture 'void PrepareStageCaptureContextLocked(') + "`n"
    $functions += (Extract $capture 'unsigned long long PreparedStageCaptureRequestId(') + "`n"
    $functions += (Extract $capture 'void RetireStaleStageCaptureLocked(') + "`n"
    $functions += (Extract $capture 'void RetireStageCaptureOwnerLocked(') + "`n"
    $functions += (Extract $capture 'bool CanRecordStageCapture(') + "`n"
} else {
    # The baseline has no preparation/retirement steps. Exercise its actual
    # capture predicate while representing those absent steps as no-ops.
    $functions += "void PrepareStageCaptureContextLocked(const NgxHandle *, unsigned long long, int) {}`n"
    $functions += "unsigned long long PreparedStageCaptureRequestId(const NgxHandle *, unsigned long long, int) { return 0; }`n"
    $functions += "void RetireStaleStageCaptureLocked(NrContext &, const NrSlot &) {}`n"
    $functions += "void RetireStageCaptureOwnerLocked(NrSlot &) {}`n"
    $oldPredicate = [regex]::Match($source, 'const bool capture_stages = pipeline == g_codec.combine &&(?<predicate>[\s\S]*?);').Groups['predicate'].Value.Replace('slot.stage_capture', 'capture')
    $functions += "bool CanRecordStageCapture(const NrStageCapture &capture) { return $oldPredicate; }`n"
}
$functions += (Extract $capture 'void DestroyStageCapture(') + "`n"
$functions += (Extract $capture 'void CollectStageCaptureLocked(') + "`n"
$functions += "bool CanArm(NrContext &context, NrSlot &slot) {$guard return true; }`n"
$prepareWired = (Extract $source 'NrContext *FindOrCreateContext(').Contains('PrepareStageCaptureContextLocked(sr_handle, feature_generation, mode);')
$retireWired = (Extract $source 'void DispatchCodec(').Contains('RetireStageCaptureOwnerLocked(slot);')
$harness = @'
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <new>
using UINT64 = std::uint64_t;
struct ID3D12Resource {};
enum D3D12_RESOURCE_STATES { D3D12_RESOURCE_STATE_COMMON = 0 };
struct NgxHandle {};
'@ + "`n" + $types + $exportType + @'

struct NrSlot {
    NrStageCapture stage_capture{};
    unsigned int recording_command_lists = 0;
    bool awaiting_fence_signal = false;
    bool fence_complete = true;
};
struct NrContext {
    const NgxHandle *sr_handle = nullptr;
    NgxHandle *nr_handle = nullptr;
    unsigned long long feature_generation = 0, instance_id = 0;
    unsigned long long stage_capture_request_id = 0;
    unsigned int evaluations = 0;
    unsigned int successful_evaluations_since_reset = 0;
    int mode = 0;
    bool retired = false;
    NrSlot slots[8]{};
};
NrContext g_contexts[16]{};
struct { unsigned int warmup_after_create = 2; } g_config;
std::atomic<unsigned int> g_stage_capture_status{0};
std::atomic<int> g_stage_capture_last_result{0};
std::atomic<unsigned long long> g_stage_capture_request_id{1};
std::atomic<bool> g_stage_capture_rebuild_pending{false};
std::atomic<unsigned int> g_stage_capture_retry_count{0};
std::atomic<bool> g_lifecycle_tracking_uncertain{false};
constexpr unsigned int kStageCaptureMinimumEvaluations = 64;
constexpr unsigned int kStageCaptureMaximumRetries = 1;
struct StageCapturePreparedContext {
    unsigned long long request_id = 0;
    const NgxHandle *sr_handle = nullptr;
    unsigned long long feature_generation = 0;
    int mode = 0;
};
StageCapturePreparedContext g_stage_capture_prepared_context{};
constexpr unsigned int WT_EXECUTEDEFAULT = 0;
unsigned int releases = 0, descriptor_writes = 0;
void *export_job = nullptr;
void Log(const char *, ...) {}
bool IsSlotFenceCompleteLocked(const NrSlot &slot) { return slot.fence_complete; }
template<class T> void ReleaseCom(T *&object) { if (object) ++releases; object = nullptr; }
void BindStageCaptureDescriptor(NrSlot &, ID3D12Resource *, UINT64) { ++descriptor_writes; }
unsigned long ExportStageCapture(void *) { return 0; }
bool QueueUserWorkItem(unsigned long (*)(void *), void *job, unsigned int) {
    export_job = job; return true;
}
'@ + "`n" + $functions + @'

unsigned int failures = 0, checks = 0;
void Check(bool pass, const char *message) {
    ++checks;
    if (!pass) { ++failures; std::printf("FAIL: %s\n", message); }
}
int main() {
    NgxHandle carrier, runtime;
    ID3D12Resource gpu, readback;
    auto &old = g_contexts[0];
    old.sr_handle = &carrier; old.nr_handle = &runtime;
    old.mode = 1; old.feature_generation = 3; old.instance_id = 8;
    old.successful_evaluations_since_reset = 400;
    old.slots[0].recording_command_lists = 2;
    old.slots[0].fence_complete = false;
    auto &other_mode = g_contexts[1]; other_mode = old; other_mode.mode = 2;
    auto &race = g_contexts[3]; race = old;
    race.slots[0].recording_command_lists = 0; race.slots[0].fence_complete = true;
    g_stage_capture_status = 0;
    PrepareStageCaptureContextLocked(&carrier, 3, 1);
    // F7 arrives after Prepare observed status0, before this old slot's Arm.
    g_stage_capture_status = 1; g_stage_capture_rebuild_pending = true;
    Check(!CanArm(race, race.slots[0]), "F7 between Prepare and Arm cannot claim old idle history");
    Check(!CanArm(old, old.slots[0]), "in-flight heap mutation remains forbidden");
    PrepareStageCaptureContextLocked(&carrier, 3, 1);
    Check(old.retired && !other_mode.retired, "request retires only the selected carrier/mode");
    auto &fresh = g_contexts[2];
    fresh.sr_handle = &carrier; fresh.mode = 1; fresh.feature_generation = 3;
    fresh.stage_capture_request_id = PreparedStageCaptureRequestId(&carrier, 3, 1);
    NgxHandle another_carrier;
    Check(fresh.stage_capture_request_id == 1 &&
          PreparedStageCaptureRequestId(&another_carrier, 3, 1) == 0 &&
          PreparedStageCaptureRequestId(&carrier, 4, 1) == 0 &&
          PreparedStageCaptureRequestId(&carrier, 3, 2) == 0,
          "prepared request binds carrier, generation and mode even across allocation retry");
    PrepareStageCaptureContextLocked(&carrier, 3, 1);
    Check(!fresh.retired, "one request does not repeatedly retire its replacement");
    auto &slot = fresh.slots[0];
    Check(CanArm(fresh, slot), "new uncreated/unwarmed context can preallocate pristine capture heap");
    slot.stage_capture.armed = true;
    slot.stage_capture.info.request_id = 1;
    slot.stage_capture.buffer = &gpu; slot.stage_capture.readback = &readback;
    g_stage_capture_status = 2;
    slot.stage_capture.info.identity.warmed_up = true;
    for (unsigned int n = 0; n < 64; ++n) {
        slot.stage_capture.info.identity.since_reset = n;
        Check(!CanRecordStageCapture(slot.stage_capture), "capture waits for 64 successful evaluations since reset");
    }
    slot.stage_capture.info.identity.since_reset = 64;
    Check(CanRecordStageCapture(slot.stage_capture), "64th successful warmed evaluation can capture");
    slot.stage_capture.info.identity.warmed_up = false;
    Check(!CanRecordStageCapture(slot.stage_capture), "normal warmup gate is still required");
    slot.stage_capture.info.identity.warmed_up = true;
    slot.stage_capture.pending = true; slot.stage_capture.armed = false;
    Check(!CanRecordStageCapture(slot.stage_capture), "one request cannot record repeatedly");
    slot.recording_command_lists = 1; slot.fence_complete = false;
    RetireStageCaptureOwnerLocked(slot);
    Check(fresh.retired, "captured owner retires before continuous Output reuse can starve its fence");
    for (unsigned int frame = 0; frame < 100; ++frame) {
        if (!fresh.retired) ++slot.recording_command_lists;
    }
    Check(slot.recording_command_lists == 1, "future frames do not append references to capture owner");
    CollectStageCaptureLocked(slot);
    Check(!export_job && descriptor_writes == 0 && releases == 0,
          "recorded/inflight capture cannot export or change heap");
    slot.recording_command_lists = 0;
    CollectStageCaptureLocked(slot);
    Check(!export_job, "recording release alone does not replace fence proof");
    slot.fence_complete = true; slot.awaiting_fence_signal = true;
    CollectStageCaptureLocked(slot);
    Check(!export_job, "pre-Execute ownership pin blocks collection");
    slot.awaiting_fence_signal = false; g_lifecycle_tracking_uncertain = true;
    CollectStageCaptureLocked(slot);
    Check(!export_job, "uncertain lifecycle remains fail-closed");
    g_lifecycle_tracking_uncertain = false;
    CollectStageCaptureLocked(slot);
    Check(export_job && descriptor_writes == 1 && releases == 1 &&
          g_stage_capture_status == 3 && !slot.stage_capture.pending,
          "completed retired slot exports once and transfers readback ownership");
    auto *job = static_cast<StageCaptureExport *>(export_job);
    if (job) { Check(job->readback == &readback, "worker owns original readback"); delete job; }
    export_job = nullptr;
    slot.stage_capture.armed = true; slot.stage_capture.buffer = &gpu;
    slot.stage_capture.info.request_id = 1; g_stage_capture_status = 2;
    g_stage_capture_rebuild_pending = false;
    DestroyStageCapture(slot);
    Check(g_stage_capture_status == 1 && g_stage_capture_rebuild_pending,
          "failed/retired unfulfilled capture retries with another fresh context");
    Check(g_stage_capture_retry_count == 1, "retry budget belongs to request and is not reset by retirement");
    slot.stage_capture.armed = true; slot.stage_capture.buffer = &gpu;
    slot.stage_capture.info.request_id = 1; g_stage_capture_status = 2;
    DestroyStageCapture(slot);
    Check(g_stage_capture_status == 0 && g_stage_capture_last_result == -1,
          "second abandoned target fails explicitly and unlocks request");
    slot.stage_capture.armed = true; slot.stage_capture.buffer = &gpu;
    slot.stage_capture.info.request_id = 1;
    g_stage_capture_status = 1; g_stage_capture_request_id = 2;
    const unsigned int retries_before = g_stage_capture_retry_count;
    DestroyStageCapture(slot);
    Check(g_stage_capture_status == 1 && g_stage_capture_request_id == 2 &&
          g_stage_capture_retry_count == retries_before,
          "old capture destructor cannot alter a newer request");

    auto &rotating = g_contexts[4];
    rotating.stage_capture_request_id = 2;
    auto &a = rotating.slots[0]; auto &b = rotating.slots[1];
    a.stage_capture.armed = true; a.stage_capture.buffer = &gpu;
    a.stage_capture.info.request_id = 2;
    g_stage_capture_status = 2;
    for (unsigned int evaluation = 1; evaluation <= 70; ++evaluation) {
        rotating.evaluations = evaluation;
        if (evaluation % 3 == 0) a.stage_capture.info.identity.evaluation = evaluation;
        RetireStaleStageCaptureLocked(rotating, b);
        Check(!rotating.retired, "ordinary triple buffering does not discard armed target");
    }
    rotating.evaluations = a.stage_capture.info.identity.evaluation + 64;
    a.recording_command_lists = 1; a.fence_complete = false;
    const unsigned int writes_before = descriptor_writes, releases_before = releases;
    RetireStaleStageCaptureLocked(rotating, b);
    Check(rotating.retired && a.stage_capture.buffer == &gpu &&
          descriptor_writes == writes_before && releases == releases_before,
          "one-off A-to-stable-B retires owner without touching in-flight A");
    g_stage_capture_retry_count = 0;
    a.recording_command_lists = 0; a.fence_complete = true;
    Check(CanMutateSlotLocked(a), "stale reservation still needs complete fence and released recording");
    DestroyStageCapture(a);
    Check(g_stage_capture_status == 1 && g_stage_capture_rebuild_pending,
          "stale reservation retries only after safe retirement");
    PrepareStageCaptureContextLocked(&carrier, 3, 1);
    auto &stable = g_contexts[5];
    stable.stage_capture_request_id = PreparedStageCaptureRequestId(&carrier, 3, 1);
    Check(CanArm(stable, stable.slots[0]), "replacement stable B obtains pristine matching reservation");
    for (unsigned int variant = 0; variant < 3; ++variant) {
        g_stage_capture_status = 1; g_stage_capture_rebuild_pending = true;
        PrepareStageCaptureContextLocked(&carrier, 3, 1);
        // Queue admission/allocation prevented Arm, then the selected contract changed.
        PrepareStageCaptureContextLocked(variant == 0 ? &another_carrier : &carrier,
            variant == 1 ? 4 : 3, variant == 2 ? 2 : 1);
        Check(g_stage_capture_status == 0 && g_stage_capture_last_result == -1,
              "unarmed preparation cancels/unlocks on carrier, generation or mode replacement");
    }
'@ + "`nCheck(" + $prepareWired.ToString().ToLowerInvariant() + ', "context preparation is wired into real lookup");' +
    "`nCheck(" + $retireWired.ToString().ToLowerInvariant() + ', "retirement is wired immediately after real capture dispatch");' + @'

    std::printf("Stage capture lifecycle regression: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
'@
$output = Join-Path $project 'build\stage-capture-lifecycle-tests'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$cpp = Join-Path $output 'stage_capture_lifecycle.cpp'
$exe = Join-Path $output 'stage_capture_lifecycle.exe'
[IO.File]::WriteAllText($cpp, $harness, [Text.UTF8Encoding]::new($false))
$compiler = Join-Path $ToolchainRoot 'bin\x86_64-w64-mingw32-clang++.exe'
& $compiler '-std=c++20' '-O2' '-static' '-Wall' '-Wextra' $cpp '-o' $exe
if ($LASTEXITCODE -ne 0) { throw 'Lifecycle harness compilation failed.' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Stage capture lifecycle regression failed.' }
