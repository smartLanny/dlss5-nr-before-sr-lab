#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define CINTERFACE
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include "nr_runtime_timing.h"
namespace {
#include "nr_runtime_timing.inl"
#include "nr_submitted_fence_wait.inl"
unsigned checks = 0, failures = 0;
void Check(bool value, const char* name) { ++checks; if (!value) { ++failures; std::printf("FAIL %s\n", name); } }

// The fence is a transparent CPU COM fixture. Win32 event creation, waits,
// wakeups and QPC measurement are real; no D3D device or GPU is created.
struct Fence {
    ID3D12Fence api{};
    std::atomic<ULONG> references{1};
    std::atomic<UINT64> completed{0};
    std::atomic<unsigned> queries{0}, registrations{0};
    SRWLOCK lock = SRWLOCK_INIT;
    HANDLE notification = nullptr, registration_observed = nullptr;
    UINT64 notification_value = 0;
    bool reject_registration = false, complete_in_registration = false;
    static Fence* From(ID3D12Fence* value) { return reinterpret_cast<Fence*>(value); }
    static ULONG STDMETHODCALLTYPE AddRef(ID3D12Fence* value) { return ++From(value)->references; }
    static ULONG STDMETHODCALLTYPE Release(ID3D12Fence* value) { return --From(value)->references; }
    static UINT64 STDMETHODCALLTYPE Completed(ID3D12Fence* value) {
        auto& f = *From(value); ++f.queries; return f.completed.load();
    }
    static HRESULT STDMETHODCALLTYPE Register(ID3D12Fence* value, UINT64 target, HANDLE event) {
        auto& f = *From(value); ++f.registrations;
        if (f.reject_registration) { SetEvent(f.registration_observed); return E_OUTOFMEMORY; }
        AcquireSRWLockExclusive(&f.lock);
        if (f.complete_in_registration) f.completed.store(target);
        if (f.completed.load() >= target) SetEvent(event);
        else { f.notification = event; f.notification_value = target; }
        ReleaseSRWLockExclusive(&f.lock);
        SetEvent(f.registration_observed);
        return S_OK;
    }
    Fence() {
        static ID3D12FenceVtbl table = [] {
            ID3D12FenceVtbl v{}; v.AddRef = AddRef; v.Release = Release;
            v.GetCompletedValue = Completed; v.SetEventOnCompletion = Register; return v;
        }();
        api.lpVtbl = &table;
        registration_observed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        Check(registration_observed != nullptr, "fixture registration notification created");
    }
    void Complete(UINT64 value) {
        AcquireSRWLockExclusive(&lock);
        completed.store(value);
        if (notification && value >= notification_value) {
            SetEvent(notification); notification = nullptr; notification_value = 0;
        }
        ReleaseSRWLockExclusive(&lock);
    }
    ~Fence() { Check(references.load() == 1, "fixture fence registration reference retired"); CloseHandle(registration_observed); }
};
struct Completion {
    Fence* fence;
    NrSubmittedFenceEvent* event;
    UINT64 value;
    bool wait_registration = true, spurious_first = false;
    DWORD result = 99;
    HANDLE thread = nullptr;
    static DWORD WINAPI Run(void* opaque) {
        auto& job = *static_cast<Completion*>(opaque);
        if (job.wait_registration) {
            if (WaitForSingleObject(job.fence->registration_observed, 2000) != WAIT_OBJECT_0) return job.result = 2;
        } else {
            const auto started = GetTickCount64();
            while (!job.fence->queries.load()) {
                if (GetTickCount64() - started > 2000) return job.result = 3;
                SwitchToThread();
            }
        }
        if (job.spurious_first) {
            SetEvent(job.event->event);
            Sleep(3);
        } else Sleep(1);
        job.fence->Complete(job.value);
        return job.result = 0;
    }
    void Start() { thread = CreateThread(nullptr, 0, Run, this, 0, nullptr); Check(thread != nullptr, "bounded completion worker starts"); }
    void Join() { Check(WaitForSingleObject(thread, 3000) == WAIT_OBJECT_0 && result == 0, "bounded completion worker finishes"); CloseHandle(thread); thread = nullptr; }
};
void Release(Fence& fence, NrSubmittedFenceEvent& event) {
    if (event.registered_fence) {
        fence.Complete(UINT64_MAX);
        const auto begin = GetTickCount64();
        while (!ReleaseNrSubmittedFenceEvent(event) && GetTickCount64() - begin < 1000) SwitchToThread();
    }
    Check(ReleaseNrSubmittedFenceEvent(event) && !event.event && !event.registered_fence,
          "completed event and extra fence reference released exactly once");
}
void Immediate() {
    Fence fence; NrSubmittedFenceEvent event;
    fence.Complete(4);
    const auto result = WaitForNrSubmittedFence(&fence.api, 4, event, nr_timing::WaitMode::Event);
    Check(result.completed && !result.removed && !fence.registrations.load() && !event.event,
          "already completed exact fence needs no event or registration");
    Release(fence, event);
}
void Delayed(bool reject, bool spurious, nr_timing::WaitMode mode) {
    Fence fence; NrSubmittedFenceEvent event; fence.reject_registration = reject;
    Completion job{&fence, &event, 7, mode == nr_timing::WaitMode::Event, spurious}; job.Start();
    const auto result = WaitForNrSubmittedFence(&fence.api, 7, event, mode);
    job.Join();
    Check(result.completed && !result.removed && result.before < 7 && result.after == 7,
          "EXACT_FENCE_ORACLE: only the exact completed target admits the delayed call");
    if (mode == nr_timing::WaitMode::Polling)
        Check(!fence.registrations.load() && !event.event && result.method == NrFenceWaitMethod::Polling,
              "diagnostic polling uses the old Sleep1 strategy without event registration");
    else if (reject)
        Check(result.method == NrFenceWaitMethod::PollingFallback && result.fallback == NrFenceWaitFallback::Registration &&
              result.registration_result == E_OUTOFMEMORY && !event.registered_fence,
              "real registration HRESULT preserved while safe polling completes");
    else
        Check(result.method == NrFenceWaitMethod::Event && fence.registrations.load() == 1,
              "EVENT_WAIT_ORACLE: accepted wait uses one real event registration");
    Release(fence, event);
}
void RegistrationRace() {
    Fence fence; NrSubmittedFenceEvent event; fence.complete_in_registration = true;
    const auto result = WaitForNrSubmittedFence(&fence.api, 9, event, nr_timing::WaitMode::Event);
    Check(result.completed && result.after == 9 && fence.registrations.load() == 1,
          "completion during registration cannot lose the notification");
    Release(fence, event);
}
void TimeoutLateAndOtherTarget() {
    Fence fence; NrSubmittedFenceEvent event;
    const auto timeout = WaitForNrSubmittedFence(&fence.api, 12, event, nr_timing::WaitMode::Event, 3);
    Check(!timeout.completed && !timeout.removed && event.registered_fence == &fence.api &&
          fence.references.load() == 2 && !ReleaseNrSubmittedFenceEvent(event),
          "PENDING_EVENT_ORACLE: timeout retains the registered event and fence lifetime");
    const auto handle = event.event;
    const auto again = WaitForNrSubmittedFence(&fence.api, 12, event, nr_timing::WaitMode::Event, 1);
    Check(!again.completed && event.event == handle && fence.registrations.load() == 1,
          "same pending target reuses its single accepted registration");
    const auto other = WaitForNrSubmittedFence(&fence.api, 11, event, nr_timing::WaitMode::Event, 1);
    Check(!other.completed && other.fallback == NrFenceWaitFallback::EarlierRegistration && fence.registrations.load() == 1,
          "another exact target polls instead of accumulating or waiting on the wrong registration");
    SetEvent(event.event);
    NrFenceEventNotification(event);
    Check(event.registered_fence == &fence.api && fence.references.load() == 2,
          "a spurious signal cannot retire a still-unfinished registration");
    fence.Complete(12);
    const auto late = WaitForNrSubmittedFence(&fence.api, 12, event, nr_timing::WaitMode::Event);
    Check(late.completed && !event.registered_fence && fence.references.load() == 1,
          "late completion retires the old registration without inventing another model attempt");
    fence.complete_in_registration = true;
    const auto next = WaitForNrSubmittedFence(&fence.api, 20, event, nr_timing::WaitMode::Event);
    Check(next.completed && event.event == handle && fence.registrations.load() == 2,
          "retired notification permits safe reuse of the same bounded handle");
    Release(fence, event);
}
void Removed() {
    Fence fence; NrSubmittedFenceEvent event;
    Completion job{&fence, &event, UINT64_MAX}; job.Start();
    const auto result = WaitForNrSubmittedFence(&fence.api, 7, event, nr_timing::WaitMode::Event);
    job.Join();
    Check(result.removed && !result.completed && result.after == UINT64_MAX,
          "device removal wakes the event but is never successful completion");
    Release(fence, event);
}
nr_timing::Snapshot Snapshot() { nr_timing::Snapshot value; SnapshotNrTiming(value); return value; }
void Timing() {
    NrTimingSetProbePolicy(0, 10); NrTimingSetWaitPolicy(0, 10);
    auto policies = NrTimingPolicySnapshot();
    Check(policies.probe.mode == 0 && policies.probe.phase == 10 && policies.wait.mode == 0,
          "cheap policy snapshot preserves nonpersistent modes and phases");
    nr_timing::Identity old_origin;
    {
        NrTimingCarrierScope frame; frame.Begin(100, 2, 4, 1, 3);
        NrTimingSetResource(3, 7, 7, 1, 1, 4); NrTimingSetTarget(8, 8, 0);
        CRITICAL_SECTION chain{}; InitializeCriticalSection(&chain);
        NrTimingEnterChainLock(&chain); LeaveCriticalSection(&chain); DeleteCriticalSection(&chain);
        NrTimingCommandObserved(); NrTimingResetBound(true, nr_timing::GameReset);
        old_origin = NrTimingProbeDispatched(); NrTimingDiagnosticRecorded();
        unsigned called = 0;
        const auto result = NrTimingCallNr([&] { ++called; return 0xBAD0000Du; });
        Check(result == 0xBAD0000Du && called == 1, "actual timed NR callback boundary forwards once and preserves the original result");
        NrTimingNativeResult(static_cast<std::int32_t>(0xBAD0000Eu)); NrTimingNativeResult(1);
        NrTimingBypass(14);
    }
    auto value = Snapshot();
    const auto& carrier = value.recent[value.recent_count - 1];
    Check(value.clock_available && value.captured_ticks >= value.epoch_started_ticks && value.qpc_frequency,
          "segment duration uses real QPC origin and snapshot clocks");
    Check(carrier.stage == nr_timing::Stage::Carrier && carrier.identity.frame == 100 && carrier.identity.resource == 7 &&
          carrier.identity.target == 8 && carrier.detail.nr_calls == 1 && carrier.detail.native_calls == 2 &&
          carrier.detail.first_native_result == static_cast<std::int32_t>(0xBAD0000Eu) && carrier.detail.native_result == 1 &&
          carrier.detail.reset_reasons == nr_timing::GameReset && carrier.detail.bypass == 14 &&
          (carrier.detail.flags & (nr_timing::ProbeDispatched | nr_timing::DeltaStatsRecorded | nr_timing::CarrierFinished)) ==
          (nr_timing::ProbeDispatched | nr_timing::DeltaStatsRecorded | nr_timing::CarrierFinished),
          "completed carrier retains exact resource, actual callback results, retry history, reset, probe and bypass facts");
    const auto prior_epoch = value.epoch;
    {
        NrTimingScope late(nr_timing::Stage::Readback);
        NrTimingSetProbePolicy(1, 11);
    }
    value = Snapshot();
    Check(value.epoch > prior_epoch && value.probe.mode == 1 && !value.observed && value.excluded_old_epoch == 1,
          "TIMING_EPOCH_ORACLE: late old-phase completion cannot enter the new phase histogram");
    {
        NrTimingCarrierScope frame; frame.Begin(101, 2, 4, 1, 3);
        NrTimingSetResource(3, 9, 9, 1, 1, 4);
        NrTimingScope readback(nr_timing::Stage::Readback);
        readback.record.readback_origin = old_origin;
        NrTimingReadbackOrigin(old_origin, true, true);
    }
    value = Snapshot();
    Check(value.recent[0].identity.probe.mode == 1 && value.recent[0].readback_origin.probe.mode == 0 &&
          value.recent[0].readback_origin.frame == 100 && value.recent[0].identity.frame == 101,
          "current CPU readback cost retains its old recording phase instead of relabeling the pending sample");
    {
        NrTimingCarrierScope frame; frame.Begin(102, 2, 4, 1, 3); NrTimingSetResource(3, 10, 10, 1, 1, 4);
    }
    Check(!Snapshot().mixed_scopes, "normal resource-ring rotation does not fabricate a scope change");
    { NrTimingCarrierScope frame; frame.Begin(103, 2, 5, 1, 3); }
    { NrTimingScope unscoped(nr_timing::Stage::ChainLock); }
    value = Snapshot();
    Check(value.mixed_scopes && value.aggregate_scope.config == 4 && value.unscoped_records == 1,
          "mixed and unscoped aggregates cannot masquerade as current-config-only statistics");
    const auto before = NrTimingPolicySnapshot(); NrTimingSetWaitPolicy(1, 11);
    Check(NrTimingPolicySnapshot().epoch > before.epoch && NrTimingPolicySnapshot().probe.phase == 11,
          "wait strategy change starts an independent measurement segment without changing probe policy");
    for (unsigned i = 0; i < 80; ++i) { NrTimingCarrierScope frame; frame.Begin(1000 + i, 2, 5, 1, 3); }
    value = Snapshot();
    std::uint64_t bins = 0; for (auto count : value.stages[0].histogram) bins += count;
    Check(value.recent_count == nr_timing::kRecentCount && value.recent_overwritten == 16 && value.observed == 80 &&
          value.recent.front().identity.frame == 1016 && value.recent.back().identity.frame == 1079 &&
          bins == value.stages[0].measured, "bounded recent history reports loss while complete phase histograms retain every measured carrier");
}
}
int main() {
    Immediate(); Delayed(false, false, nr_timing::WaitMode::Event); Delayed(false, true, nr_timing::WaitMode::Event);
    Delayed(true, false, nr_timing::WaitMode::Event); Delayed(false, false, nr_timing::WaitMode::Polling);
    RegistrationRace(); TimeoutLateAndOtherTarget(); Removed(); Timing();
    std::printf("SUBMITTED_FENCE_CPU %s checks=%u failures=%u; real Win32 events/QPC, CPU fence callback fixture, no GPU/device/NGX\n",
                failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 41 : 0;
}
