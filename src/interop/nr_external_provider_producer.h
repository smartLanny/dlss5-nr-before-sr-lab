#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <atomic>
#include "nr_external_provider_abi.h"

// Producer-side client for jlrouzies-fr/DLSS5-Feeder (and host tests).
// Discovers the unique NR consumer export set. On successful ClaimOwnership the
// producer MUST disable its private Feature1-based NR implementation; a direct
// SR feature may be created only after native-observer preflight. Never install
// a second NR owner or spoof Feature1 for this core.
namespace nr_external_provider_client {

using QueryFn = std::int32_t (WINAPI *)(std::uint32_t, std::uint32_t,
                                        std::uint32_t, NrExternalStatusV1 *);
using ClaimFn = std::int32_t (WINAPI *)(std::uint32_t, std::uint32_t,
                                        std::uint64_t, NrExternalStatusV1 *);
using ReleaseFn = std::int32_t (WINAPI *)(std::uint32_t, std::uint64_t);
using SubmitFn = std::int32_t (WINAPI *)(std::uint32_t, const NrExternalFrameV1 *,
                                         NrExternalSubmitResultV1 *);

// Call outside loader callbacks, after official NGX Init/capability loading and
// before Claim/Create of an injected SR feature. Retain the consumer module.
// A deadline declines startup; it never authorizes Create. Re-run before each
// new SR lifecycle. This is separate from NR-only Feeder admission.
inline bool WaitForNativeObserver(QueryFn query_fn, DWORD timeout_ms,
                                 NrExternalStatusV1 &status) noexcept {
    const ULONGLONG start = GetTickCount64();
    for (;;) {
        status = {};
        if (!query_fn || !query_fn(kNrExternalProviderAbi, sizeof(status),
                kNrExternalQueryNativeObserver, &status)) return false;
        if (status.size != sizeof(status) || status.abi != kNrExternalProviderAbi ||
            status.reserved) return false;
        if (status.reason == NrExternalProviderReason::Ready)
            return !(status.observer_epoch & 1u);
        if (status.reason != NrExternalProviderReason::Starting &&
            status.reason != NrExternalProviderReason::Busy) return false;
        const ULONGLONG elapsed = GetTickCount64() - start;
        if (elapsed >= timeout_ms) return false;
        const auto remaining = static_cast<DWORD>(timeout_ms - elapsed);
        Sleep(remaining < 5 ? remaining : 5);
    }
}

inline SRWLOCK lock = SRWLOCK_INIT;
inline HMODULE retained = nullptr;
inline QueryFn query = nullptr;
inline ClaimFn claim = nullptr;
inline ReleaseFn release = nullptr;
inline SubmitFn submit = nullptr;
inline bool unique = false;
inline bool ownership = false;
// Once delegation succeeds, discovery failure can only decline work.
// It must never re-enable a second private NR implementation.
inline std::atomic<bool> delegated_process{false};
inline std::uint64_t provider_id = 0;
inline ULONGLONG next_scan = 0;

inline void Discover(ULONGLONG now) {
    if (now < next_scan) return;
    next_scan = now + 1000;
    unique = false;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) return;
    MODULEENTRY32W e{};
    e.dwSize = sizeof(e);
    HMODULE found = nullptr;
    QueryFn q = nullptr;
    ClaimFn c = nullptr;
    ReleaseFn r = nullptr;
    SubmitFn s = nullptr;
    bool duplicate = false;
    for (BOOL ok = Module32FirstW(snapshot, &e); ok; ok = Module32NextW(snapshot, &e)) {
        HMODULE owner = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(e.modBaseAddr), &owner))
            continue;
        auto *qn = reinterpret_cast<QueryFn>(
            GetProcAddress(owner, "NRExternalProvider_QueryV1"));
        auto *cn = reinterpret_cast<ClaimFn>(
            GetProcAddress(owner, "NRExternalProvider_ClaimOwnershipV1"));
        auto *rn = reinterpret_cast<ReleaseFn>(
            GetProcAddress(owner, "NRExternalProvider_ReleaseOwnershipV1"));
        auto *sn = reinterpret_cast<SubmitFn>(
            GetProcAddress(owner, "NRExternalProvider_SubmitFrameV1"));
        if (!qn || !cn || !rn || !sn) {
            FreeLibrary(owner);
            continue;
        }
        if (found) {
            duplicate = true;
            FreeLibrary(owner);
            break;
        }
        found = owner;
        q = qn;
        c = cn;
        r = rn;
        s = sn;
    }
    const bool complete = !duplicate && GetLastError() == ERROR_NO_MORE_FILES;
    CloseHandle(snapshot);
    if (!complete || !found) {
        if (found) FreeLibrary(found);
        return;
    }
    if (retained && retained != found) {
        FreeLibrary(found);
        return;
    }
    if (retained) FreeLibrary(found);
    else retained = found;
    query = q;
    claim = c;
    release = r;
    submit = s;
    unique = true;
}

// True when Feeder must skip its private NGX Feature1 NR owner.
inline bool ShouldDisableFeederNr() noexcept {
    return delegated_process.load(std::memory_order_acquire);
}

// CPU-only preflight. Run before texture lookups, copies, allocators or probes.
inline bool CanPrepareFrame() noexcept {
    if (!TryAcquireSRWLockExclusive(&lock)) return false;
    Discover(GetTickCount64());
    NrExternalStatusV1 status{};
    const bool ready = ownership && unique && query && submit &&
        query(kNrExternalProviderAbi, sizeof(status), 0u, &status) &&
        status.owner_provider_id == provider_id &&
        NrExternalStatusAllowsSubmit(status);
    ReleaseSRWLockExclusive(&lock);
    return ready;
}

inline bool Claim(std::uint64_t id) noexcept {
    if (!id) return false;
    if (!TryAcquireSRWLockExclusive(&lock)) return false;
    if (ownership) {
        const bool same = provider_id == id;
        ReleaseSRWLockExclusive(&lock);
        return same;
    }
    Discover(GetTickCount64());
    bool ok = false;
    if (unique && claim) {
        NrExternalStatusV1 status{};
        if (claim(kNrExternalProviderAbi, sizeof(status), id, &status) &&
            status.owner_provider_id == id &&
            (status.reason == NrExternalProviderReason::Ready ||
             status.reason == NrExternalProviderReason::Disabled)) {
            ownership = true;
            delegated_process.store(true, std::memory_order_release);
            provider_id = id;
            ok = true;
        }
    }
    ReleaseSRWLockExclusive(&lock);
    return ok;
}

inline void Release() noexcept {
    AcquireSRWLockExclusive(&lock);
    if (ownership && release && provider_id)
        release(kNrExternalProviderAbi, provider_id);
    ownership = false;
    provider_id = 0;
    ReleaseSRWLockExclusive(&lock);
}

inline bool Submit(const NrExternalFrameV1 &frame, NrExternalSubmitResultV1 &result) noexcept {
    if (!TryAcquireSRWLockExclusive(&lock)) return false;
    Discover(GetTickCount64());
    bool ok = false;
    if (unique && ownership && submit && frame.provider_id == provider_id)
        ok = submit(kNrExternalProviderAbi, &frame, &result) != 0;
    ReleaseSRWLockExclusive(&lock);
    return ok;
}

} // namespace nr_external_provider_client
