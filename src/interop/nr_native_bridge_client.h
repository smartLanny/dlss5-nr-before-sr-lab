#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include "nr_native_bridge_abi.h"
#include "nr_native_bridge_history.h"

// Included by the pinned external bridge. No code or ABI from its hook layer is
// imported into the core. Only one consumer is accepted. Loader references are
// retained to process exit, matching the core's already-pinned hook lifetime.
namespace nr_native_bridge_client {
using Query = std::int32_t (WINAPI*)(std::uint32_t, std::uint32_t,
                                   std::uint32_t, NrNativeBridgeStatusV1*);
inline SRWLOCK lock = SRWLOCK_INIT;
inline Query query = nullptr;
inline HMODULE retained = nullptr;
inline ULONGLONG nextScan = 0, nextPrepare = 0;
inline bool unique = false;

inline void Discover(ULONGLONG now) {
    if (now < nextScan) return;
    nextScan = now + 1000;
    unique = false;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) return;
    MODULEENTRY32W e{}; e.dwSize = sizeof(e);
    HMODULE found = nullptr; Query candidate = nullptr; bool duplicate = false;
    for (BOOL ok = Module32FirstW(snapshot, &e); ok; ok = Module32NextW(snapshot, &e)) {
        // Hold an ordinary loader reference before querying a possibly unloading
        // module from the snapshot. A forwarded export is not this contract.
        HMODULE owner = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(e.modBaseAddr), &owner)) continue;
        auto fn = reinterpret_cast<Query>(GetProcAddress(owner, "NRNativeBridge_QueryV1"));
        if (!fn) { FreeLibrary(owner); continue; }
        HMODULE fnOwner = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(fn), &fnOwner) || fnOwner != owner) {
            if (fnOwner) FreeLibrary(fnOwner);
            FreeLibrary(owner); continue;
        }
        FreeLibrary(fnOwner);
        if (found) { duplicate = true; FreeLibrary(owner); break; }
        found = owner; candidate = fn;
    }
    const bool scanComplete = !duplicate && GetLastError() == ERROR_NO_MORE_FILES;
    CloseHandle(snapshot);
    if (!scanComplete || !found) { if (found) FreeLibrary(found); return; }
    if (retained && retained != found) { FreeLibrary(found); return; }
    if (retained) FreeLibrary(found); else retained = found;
    query = candidate; unique = true;
}
inline bool AllowsFrame(bool prepare) {
    if (!TryAcquireSRWLockExclusive(&lock)) return false;
    const auto now = GetTickCount64();
    Discover(now);
    NrNativeBridgeStatusV1 status{};
    bool allowed = false;
    if (unique && query) {
        // First attempt is cheap. Reconcile only when starting/rebuilding, and
        // at most once a second while the consumer is not yet ready.
        const bool reconcile = prepare && now >= nextPrepare;
        if (reconcile) nextPrepare = now + 1000;
        if (query(kNrNativeBridgeAbi, sizeof(status), reconcile ? 1u : 0u, &status))
            allowed = NrNativeBridgeStatusAllows(status);
    }
    ReleaseSRWLockExclusive(&lock);
    return allowed;
}
} // namespace nr_native_bridge_client

// Both fields and history are accessed only under the upstream g_bridge_cs.
static bool g_nr_native_delivery = false;
static NrNativeBridgeHistory g_nr_native_history;
