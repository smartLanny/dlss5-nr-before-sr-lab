#pragma once
#include <windows.h>
#include <d3d12.h>
#include <cstdint>

// Completion is a fence observation, never the return from recording commands.
// A timed-out event can subsequently wake a different wait, so always recheck
// the fence and keep a single deadline. UINT64_MAX means device removal.
namespace nr_external_provider_completion {
enum class Result { Complete, Invalid, SignalFailed, Removed, EventFailed, Timeout };

template<class Ops>
Result WaitWith(Ops &ops, UINT64 value, DWORD timeout) {
    if (!ops.Valid() || !value || value == UINT64_MAX) return Result::Invalid;
    const ULONGLONG deadline = ops.Now() + timeout;
    for (;;) {
        const UINT64 completed = ops.Completed();
        if (completed == UINT64_MAX) return Result::Removed;
        if (completed >= value) return Result::Complete;
        const ULONGLONG now = ops.Now();
        if (now >= deadline) return Result::Timeout;
        if (!ops.Reset() || FAILED(ops.Arm(value)))
            return Result::EventFailed;
        const DWORD wait = ops.Wait(static_cast<DWORD>(deadline - now));
        if (wait != WAIT_OBJECT_0 && wait != WAIT_TIMEOUT) return Result::EventFailed;
    }
}

struct Win32Ops {
    ID3D12Fence *fence;
    HANDLE event;
    bool Valid() const { return fence && event; }
    ULONGLONG Now() const { return GetTickCount64(); }
    UINT64 Completed() const { return fence->GetCompletedValue(); }
    bool Reset() const { return ResetEvent(event) != FALSE; }
    HRESULT Arm(UINT64 value) const { return fence->SetEventOnCompletion(value, event); }
    DWORD Wait(DWORD timeout) const { return WaitForSingleObject(event, timeout); }
};

inline Result Wait(ID3D12Fence *fence, HANDLE event, UINT64 value, DWORD timeout) {
    Win32Ops ops{fence, event};
    return WaitWith(ops, value, timeout);
}

inline Result SignalAndWait(ID3D12CommandQueue *queue, ID3D12Fence *fence,
                            HANDLE event, UINT64 value, DWORD timeout) {
    if (!queue || !fence || !event || !value || value == UINT64_MAX) return Result::Invalid;
    if (FAILED(queue->Signal(fence, value))) return Result::SignalFailed;
    return Wait(fence, event, value, timeout);
}
}
