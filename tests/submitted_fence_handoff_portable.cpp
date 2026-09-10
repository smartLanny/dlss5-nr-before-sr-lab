// Deterministic fake-Win32 execution of the PRODUCTION helper. No GPU claim.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <vector>
using DWORD = unsigned long;
using HRESULT = std::int32_t;
constexpr HRESULT S_OK = 0;
constexpr bool FALSE = false;
constexpr DWORD WAIT_OBJECT_0 = 0, WAIT_TIMEOUT = 258, WAIT_FAILED = 0xffffffff;
#define FAILED(x) ((x) < 0)
#define HRESULT_FROM_WIN32(x) static_cast<HRESULT>(0x80070000u | (x))
struct FakeEvent { bool signal = false; };
using HANDLE = FakeEvent*;
struct ID3D12Fence { std::uint64_t value = 0; int refs = 1; };
struct Registration { ID3D12Fence* fence; std::uint64_t target; HANDLE event; };
std::vector<Registration> pending;
std::vector<std::pair<unsigned, std::function<void()>>> changes;
unsigned clock_ms, sleeps, waits, arms, maximum_pending;
bool fail_create, fail_reset, fail_arm, fail_wait;
unsigned fail_arm_at;
void progress() {
    for (auto& item : changes) if (item.first <= clock_ms && item.second) {
        auto fn = std::move(item.second); item.second = {}; fn();
    }
    for (auto it = pending.begin(); it != pending.end();) {
        if (it->fence->value >= it->target) { it->event->signal = true; it = pending.erase(it); }
        else ++it;
    }
}
std::uint64_t ID3D12Fence_GetCompletedValue(ID3D12Fence* f) { progress(); return f->value; }
void ID3D12Fence_AddRef(ID3D12Fence* f) { ++f->refs; }
void ID3D12Fence_Release(ID3D12Fence* f) { assert(f->refs > 1); --f->refs; }
HRESULT ID3D12Fence_SetEventOnCompletion(ID3D12Fence* f, std::uint64_t v, HANDLE h) {
    ++arms;
    if (fail_arm || (fail_arm_at && arms == fail_arm_at)) return -1;
    assert(std::none_of(pending.begin(), pending.end(), [h](auto& p) { return p.event == h; }));
    pending.push_back({f, v, h}); maximum_pending = std::max(maximum_pending, unsigned(pending.size()));
    progress(); return S_OK;
}
HANDLE CreateEventW(void*, bool, bool, void*) { return fail_create ? nullptr : new FakeEvent; }
bool ResetEvent(HANDLE h) { if (fail_reset) return false; h->signal = false; return true; }
void CloseHandle(HANDLE h) { assert(std::none_of(pending.begin(), pending.end(), [h](auto& p) { return p.event == h; })); delete h; }
DWORD GetLastError() { return 5; }
DWORD WaitForSingleObject(HANDLE h, DWORD timeout) {
    if (timeout) ++waits;
    if (timeout && fail_wait) { fail_wait = false; return WAIT_FAILED; }
    progress();
    for (DWORD i = 0; !h->signal && i < timeout; ++i) { ++clock_ms; progress(); }
    if (!h->signal) return WAIT_TIMEOUT;
    h->signal = false; return WAIT_OBJECT_0;
}
void Sleep(unsigned n) { ++sleeps; clock_ms += n; progress(); }
std::uint64_t GetTickCount64() { return clock_ms; }
std::uint64_t NrTimingCounter() { return clock_ms + 1; }
std::uint64_t NrTimingFrequency() { return 1000; }
namespace nr_timing { enum class WaitMode { Event, Polling }; }
#include "../src/nr_submitted_fence_wait.inl"
void reset() {
    assert(pending.empty()); changes.clear(); clock_ms = sleeps = waits = arms = maximum_pending = 0;
    fail_create = fail_reset = fail_arm = fail_wait = false; fail_arm_at = 0;
}
void at(unsigned time, ID3D12Fence& f, std::uint64_t value) { changes.emplace_back(time, [&f, value] { f.value = value; }); }
void clean(NrSubmittedFenceEvent& e, ID3D12Fence& f) {
    f.value = UINT64_MAX; progress(); assert(ReleaseNrSubmittedFenceEvent(e)); assert(f.refs == 1);
}
int main() {
    using Mode = nr_timing::WaitMode;
    unsigned cases = 0;
    // Earlier same-fence event at 2ms, exact target at 4ms: no 1ms polling.
    {
        reset(); ID3D12Fence f; NrSubmittedFenceEvent e; NrSubmittedFenceWaitResult a;
        assert(ArmNrSubmittedFenceEvent(&f, 5, e, a)); at(2,f,5); at(4,f,10);
        auto r = WaitForNrSubmittedFence(&f,10,e,Mode::Event,50);
        assert(r.completed && r.after == 10 && clock_ms == 4 && sleeps == 0);
        assert(r.earlier_event_waits == 1 && arms == 2 && maximum_pending == 1);
        assert(r.method == NrFenceWaitMethod::Event && r.fallback == NrFenceWaitFallback::None);
        clean(e,f); ++cases;
    }
    // The original deadline is NOT restarted after the first notification.
    {
        reset(); ID3D12Fence f; NrSubmittedFenceEvent e; NrSubmittedFenceWaitResult a;
        ArmNrSubmittedFenceEvent(&f,5,e,a); at(4,f,5); at(8,f,10);
        auto r=WaitForNrSubmittedFence(&f,10,e,Mode::Event,5);
        assert(!r.completed && clock_ms == 5 && sleeps == 0 && e.registered_value == 10);
        assert(!ReleaseNrSubmittedFenceEvent(e)); clean(e,f); ++cases;
    }
    // Timeout before earlier event: preserve the original live registration.
    {
        reset(); ID3D12Fence f; NrSubmittedFenceEvent e; NrSubmittedFenceWaitResult a;
        ArmNrSubmittedFenceEvent(&f,5,e,a);
        auto r=WaitForNrSubmittedFence(&f,10,e,Mode::Event,3);
        assert(!r.completed && clock_ms == 3 && e.registered_value == 5 && arms == 1);
        clean(e,f); ++cases;
    }
    // A newer registration is NOT a substitute for the requested target.
    {
        reset(); ID3D12Fence f; NrSubmittedFenceEvent e; NrSubmittedFenceWaitResult a;
        ArmNrSubmittedFenceEvent(&f,20,e,a); at(2,f,10);
        auto r=WaitForNrSubmittedFence(&f,10,e,Mode::Event,5);
        assert(r.completed && clock_ms == 2 && waits == 0 && sleeps == 2 && e.registered_value == 20);
        assert(r.fallback == NrFenceWaitFallback::EarlierRegistration); clean(e,f); ++cases;
    }
    // Foreign fence: do not wait on it or drop its ownership.
    {
        reset(); ID3D12Fence f, foreign; NrSubmittedFenceEvent e; NrSubmittedFenceWaitResult a;
        ArmNrSubmittedFenceEvent(&foreign,1,e,a); at(2,f,10);
        auto r=WaitForNrSubmittedFence(&f,10,e,Mode::Event,5);
        assert(r.completed && waits == 0 && sleeps == 2 && foreign.refs == 2);
        clean(e,foreign); assert(f.refs == 1); ++cases;
    }
    // Already armed exact target needs no second registration.
    {
        reset(); ID3D12Fence f; NrSubmittedFenceEvent e; NrSubmittedFenceWaitResult a;
        ArmNrSubmittedFenceEvent(&f,10,e,a); at(2,f,10);
        auto r=WaitForNrSubmittedFence(&f,10,e,Mode::Event,5);
        assert(r.completed && arms == 1 && sleeps == 0 && r.earlier_event_waits == 0);
        clean(e,f); ++cases;
    }
    // The GPU may advance past both values on the first wake; no re-arm.
    {
        reset(); ID3D12Fence f; NrSubmittedFenceEvent e; NrSubmittedFenceWaitResult a;
        ArmNrSubmittedFenceEvent(&f,5,e,a); at(2,f,10);
        auto r=WaitForNrSubmittedFence(&f,10,e,Mode::Event,5);
        assert(r.completed && arms == 1 && sleeps == 0); clean(e,f); ++cases;
    }
    // Stale/manual signal is consumed, but must not free unfinished ownership.
    {
        reset(); ID3D12Fence f; NrSubmittedFenceEvent e; NrSubmittedFenceWaitResult a;
        ArmNrSubmittedFenceEvent(&f,5,e,a); e.event->signal=true; at(2,f,5); at(4,f,10);
        auto r=WaitForNrSubmittedFence(&f,10,e,Mode::Event,5);
        assert(r.completed && clock_ms==4 && maximum_pending==1 && sleeps==0);
        clean(e,f); ++cases;
    }
    // Removal is never success, even though UINT64_MAX exceeds any target.
    {
        reset(); ID3D12Fence f; NrSubmittedFenceEvent e; NrSubmittedFenceWaitResult a;
        ArmNrSubmittedFenceEvent(&f,5,e,a); at(2,f,UINT64_MAX);
        auto r=WaitForNrSubmittedFence(&f,10,e,Mode::Event,5);
        assert(r.removed && !r.completed && clock_ms==2); clean(e,f); ++cases;
    }
    // Failure to register exact target after the handoff falls back, with one budget.
    {
        reset(); ID3D12Fence f; NrSubmittedFenceEvent e; NrSubmittedFenceWaitResult a;
        ArmNrSubmittedFenceEvent(&f,5,e,a); fail_arm_at=2; at(2,f,5); at(4,f,10);
        auto r=WaitForNrSubmittedFence(&f,10,e,Mode::Event,5);
        assert(r.completed && r.method==NrFenceWaitMethod::EventThenPolling && sleeps==2);
        assert(r.fallback==NrFenceWaitFallback::Registration && f.refs==1); clean(e,f); ++cases;
    }
    // Wait failure keeps registration alive until its notification arrives.
    {
        reset(); ID3D12Fence f; NrSubmittedFenceEvent e; NrSubmittedFenceWaitResult a;
        ArmNrSubmittedFenceEvent(&f,5,e,a); fail_wait=true; at(2,f,5); at(4,f,10);
        auto r=WaitForNrSubmittedFence(&f,10,e,Mode::Event,5);
        assert(r.completed && r.fallback==NrFenceWaitFallback::WaitFailed && sleeps==4);
        clean(e,f); ++cases;
    }
    // Diagnostic legacy polling intentionally stays legacy polling.
    {
        reset(); ID3D12Fence f; NrSubmittedFenceEvent e; NrSubmittedFenceWaitResult a;
        ArmNrSubmittedFenceEvent(&f,5,e,a); at(2,f,5); at(4,f,10);
        auto r=WaitForNrSubmittedFence(&f,10,e,Mode::Polling,5);
        assert(r.completed && sleeps==4 && waits==0 && r.earlier_event_waits==0);
        clean(e,f); ++cases;
    }
    for (int failure=0; failure<3; ++failure) {
        reset(); ID3D12Fence f; NrSubmittedFenceEvent e;
        fail_create=failure==0; fail_reset=failure==1; fail_arm=failure==2; at(2,f,10);
        auto r=WaitForNrSubmittedFence(&f,10,e,Mode::Event,5);
        assert(r.completed && sleeps==2 && f.refs==1); clean(e,f); ++cases;
    }
    {
        reset(); ID3D12Fence f; NrSubmittedFenceEvent e;
        auto r=WaitForNrSubmittedFence(&f,10,e,Mode::Event,0);
        assert(!r.completed && !arms && !sleeps && !waits); clean(e,f); ++cases;
    }
    {
        reset(); ID3D12Fence f; f.value=10; NrSubmittedFenceEvent e;
        auto r=WaitForNrSubmittedFence(&f,10,e,Mode::Event,0);
        assert(r.completed && !arms && !waits); clean(e,f); ++cases;
    }
    {
        reset(); NrSubmittedFenceEvent e;
        assert(!WaitForNrSubmittedFence(nullptr,10,e,Mode::Event).completed);
        ID3D12Fence f;
        assert(!WaitForNrSubmittedFence(&f,0,e,Mode::Event).completed); ++cases;
    }
    std::cout << cases << " production-helper fake-Win32 cases passed; no Windows/GPU validation\n";
}
