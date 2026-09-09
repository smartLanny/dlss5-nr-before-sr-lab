// One event and at most one outstanding registration per existing retirement
// queue. The production queue/fence table is process-lived; an unfinished event
// is retained with it, including after a timeout or device removal.
#define NR_HAS_SUBMITTED_FENCE_EVENT 1
struct NrSubmittedFenceEvent {
    HANDLE event = nullptr;
    ID3D12Fence* registered_fence = nullptr; // owned until notification observed
    std::uint64_t registered_value = 0;
};
enum class NrFenceWaitFallback : unsigned { None, EventCreate, EventReset, Registration, EarlierRegistration, WaitFailed };
enum class NrFenceWaitMethod : unsigned { Polling, Event, PollingFallback, EventThenPolling, AlreadyComplete };
struct NrSubmittedFenceWaitResult {
    bool completed = false, removed = false;
    std::uint64_t before = 0, after = 0, elapsed_ms = 0;
    NrFenceWaitMethod method = NrFenceWaitMethod::AlreadyComplete;
    NrFenceWaitFallback fallback = NrFenceWaitFallback::None;
    HRESULT registration_result = S_OK;
};
void NrFenceEventNotification(NrSubmittedFenceEvent& event, bool notification_observed = false) {
    if (!event.registered_fence || !event.event) return;
    if (!notification_observed && WaitForSingleObject(event.event, 0) != WAIT_OBJECT_0) return;
    const auto value = ID3D12Fence_GetCompletedValue(event.registered_fence);
    // A stale/manual event signal is not completion of this registration.
    // Keep its fence and handle live until the actual target notification.
    if (value != UINT64_MAX && value < event.registered_value) return;
    ID3D12Fence_Release(event.registered_fence);
    event.registered_fence = nullptr; event.registered_value = 0;
}
bool ReleaseNrSubmittedFenceEvent(NrSubmittedFenceEvent& event) {
    NrFenceEventNotification(event);
    if (event.registered_fence) return false;
    if (event.event) CloseHandle(event.event);
    event.event = nullptr;
    return true;
}
bool ArmNrSubmittedFenceEvent(ID3D12Fence* fence, std::uint64_t target,
        NrSubmittedFenceEvent& event, NrSubmittedFenceWaitResult& result) {
    NrFenceEventNotification(event);
    if (event.registered_fence) {
        if (event.registered_fence == fence && event.registered_value == target) return true;
        // Never accumulate registrations or wait for a newer/other target in
        // place of the exact value this context needs. Polling remains safe.
        result.fallback = NrFenceWaitFallback::EarlierRegistration;
        return false;
    }
    if (!event.event) {
        event.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event.event) {
            result.fallback = NrFenceWaitFallback::EventCreate;
            result.registration_result = HRESULT_FROM_WIN32(GetLastError());
            return false;
        }
    }
    if (!ResetEvent(event.event)) {
        result.fallback = NrFenceWaitFallback::EventReset;
        result.registration_result = HRESULT_FROM_WIN32(GetLastError());
        return false;
    }
    ID3D12Fence_AddRef(fence);
    result.registration_result = ID3D12Fence_SetEventOnCompletion(fence, target, event.event);
    if (FAILED(result.registration_result)) {
        ID3D12Fence_Release(fence);
        result.fallback = NrFenceWaitFallback::Registration;
        return false;
    }
    event.registered_fence = fence; event.registered_value = target;
    return true;
}
NrSubmittedFenceWaitResult WaitForNrSubmittedFence(ID3D12Fence* fence, std::uint64_t target,
        NrSubmittedFenceEvent& event, nr_timing::WaitMode mode, unsigned budget_ms = 50) {
    NrSubmittedFenceWaitResult result{};
    if (!fence || !target) return result;
    const auto started = NrTimingCounter(), frequency = NrTimingFrequency();
    const auto started_ms = GetTickCount64();
    bool first = true, event_attempted = false, use_event = false, event_used = false;
    for (;;) {
        const auto value = ID3D12Fence_GetCompletedValue(fence);
        if (first) { result.before = value; first = false; }
        result.after = value; result.removed = value == UINT64_MAX;
        result.completed = !result.removed && value >= target;
        if (result.completed || result.removed) break;
        const auto now = NrTimingCounter();
        const long double elapsed = started && frequency && now >= started
            ? static_cast<long double>(now - started) * 1000.0L / frequency
            : static_cast<long double>(GetTickCount64() - started_ms);
        if (elapsed >= budget_ms) break;
        if (mode == nr_timing::WaitMode::Event && !event_attempted) {
            event_attempted = true;
            use_event = ArmNrSubmittedFenceEvent(fence, target, event, result);
            result.method = use_event ? NrFenceWaitMethod::Event : NrFenceWaitMethod::PollingFallback;
        }
        if (use_event) {
            event_used = true;
            const auto before_wait = NrTimingCounter();
            const long double elapsed_before_wait = started && frequency && before_wait >= started
                ? static_cast<long double>(before_wait - started) * 1000.0L / frequency
                : static_cast<long double>(GetTickCount64() - started_ms);
            if (elapsed_before_wait >= budget_ms) continue;
            // Round only the remaining timeout up to a Win32 millisecond.
            // Event delivery wakes immediately; no global timer change/spin.
            const auto remaining = static_cast<DWORD>(std::max(1.0L, std::ceil(static_cast<long double>(budget_ms) - elapsed_before_wait)));
            const DWORD waited = WaitForSingleObject(event.event, remaining);
            if (waited == WAIT_OBJECT_0) NrFenceEventNotification(event, true);
            else if (waited != WAIT_TIMEOUT) {
                result.fallback = NrFenceWaitFallback::WaitFailed;
                result.registration_result = HRESULT_FROM_WIN32(GetLastError());
                result.method = NrFenceWaitMethod::EventThenPolling; use_event = false;
            }
        } else {
            if (mode == nr_timing::WaitMode::Polling) result.method = NrFenceWaitMethod::Polling;
            Sleep(1); // diagnostic old strategy, or registration failure
        }
    }
    NrFenceEventNotification(event);
    result.elapsed_ms = GetTickCount64() - started_ms; // preserve legacy coarse counters
    if (event_used && result.method == NrFenceWaitMethod::PollingFallback)
        result.method = NrFenceWaitMethod::EventThenPolling;
    return result;
}
