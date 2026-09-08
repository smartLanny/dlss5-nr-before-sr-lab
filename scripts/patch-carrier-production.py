#!/usr/bin/env python3
"""Apply the production two-stage DX11 carrier lifetime and admission contract."""
from pathlib import Path


def once(text: str, old: str, new: str) -> str:
    count = text.count(old)
    if count != 1:
        raise ValueError(f'production carrier anchor count {count}, expected 1: {old[:100]!r}')
    return text.replace(old, new, 1)


STATE = r'''// Production carrier admission state. The loader callback only writes this
// fixed, lock-free ledger. AddonInit and ReShade callbacks do all active work.
static nr_carrier_activation::ModuleLedger g_carrier_modules;
static nr_carrier_activation::Registry g_carrier_registry;
static CRITICAL_SECTION g_carrier_cs;
static std::atomic<bool> g_carrier_observer_armed{false};
static std::atomic<bool> g_carrier_bridge_armed{false};
static std::atomic<LONG> g_carrier_initialized{0};
static std::atomic<bool> g_carrier_notifications_ready{false};
static std::uintptr_t g_carrier_bridge_owner;
static std::uint64_t g_carrier_rejection_epoch;
static void CarrierRetireLocked(std::uintptr_t device);

struct CarrierOperationGuard {
    CarrierOperationGuard() { EnterCriticalSection(&g_carrier_cs); }
    ~CarrierOperationGuard() { LeaveCriticalSection(&g_carrier_cs); }
};

struct HookCallGuard {
    Hook &hook;
    explicit HookCallGuard(Hook &value) : hook(value) { InterlockedIncrement(&hook.callbacks); }
    ~HookCallGuard() { InterlockedDecrement(&hook.callbacks); }
};

static bool CarrierDeviceOf(ID3D11DeviceContext *ctx, std::uintptr_t *value)
{
    if (ctx == nullptr || value == nullptr) return false;
    ID3D11Device *device = nullptr;
    ctx->GetDevice(&device);
    if (device == nullptr) return false;
    *value = reinterpret_cast<std::uintptr_t>(device);
    device->Release();
    return true;
}

static bool CarrierAllows(const NVSDK_NGX_Handle *handle, ID3D11DeviceContext *ctx,
                          nr_carrier_activation::FeatureRecord *record)
{
    std::uintptr_t device = 0;
    if (!CarrierDeviceOf(ctx, &device)) return false;
    EnterCriticalSection(&g_carrier_cs);
    const bool allowed = (g_carrier_bridge_owner == 0 || g_carrier_bridge_owner == device) &&
        g_carrier_registry.Allows(
        reinterpret_cast<std::uintptr_t>(handle), device, record);
    LeaveCriticalSection(&g_carrier_cs);
    return allowed;
}

'''


def patch_hooks(text: str) -> str:
    text = once(text, '''// Nothing on disk is patched. The only writes to foreign code are 14 bytes at
// each of three function entry points in every module that exports the NGX D3D11
// API -- six such modules in Baldur's Gate 3, twelve at most -- in memory,
// restored around every call. vk_mirror=1 adds four more per module.''', '''// Nothing on disk is patched. The generated production carrier uses the vendored
// MinHook trampoline implementation for observed D3D11 Create/Release entry points,
// then enables Evaluate only after same-device swapchain confirmation. Successfully
// hooked code modules and this carrier remain pinned until process exit.''')
    text = once(text, '#include <windows.h>',
        '#include <windows.h>\n#include "MinHook.h"\n#include "nr_carrier_activation_runtime.h"')
    text = once(text, '''struct Hook
{
    BYTE *target;
    BYTE  saved[14];
    BYTE  patch[14];
    bool  active;
};''', '''struct Hook
{
    BYTE *entry;
    BYTE *target; // MinHook trampoline; never the patched entry.
    HMODULE module_lease;
    volatile LONG callbacks;
    bool  active;
};''')
    start = text.index('static bool WriteCode(')
    stop = text.index('// ---------------------------------------------------------------------------\n// Parameter dumping', start)
    replacement = r'''static bool HookInstall(Hook &h, void *target, void *detour)
{
    if (target == nullptr || detour == nullptr) return false;
    HMODULE held = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(target), &held)) return false;
    void *trampoline = nullptr;
    const MH_STATUS created = MH_CreateHook(target, detour, &trampoline);
    if (created != MH_OK || trampoline == nullptr) {
        FreeLibrary(held);
        return false;
    }
    // Publish the trampoline and its module lease before enabling the detour.
    h.entry = static_cast<BYTE *>(target);
    h.target = static_cast<BYTE *>(trampoline);
    h.module_lease = held;
    h.callbacks = 0;
    if (MH_EnableHook(target) != MH_OK) {
        MH_RemoveHook(target);
        h.entry = h.target = nullptr;
        h.module_lease = nullptr;
        FreeLibrary(held);
        return false;
    }
    h.active = true;
    // Only successfully enabled hook code is pinned. The temporary pre-enable
    // reference closes the target-unload window without pinning scan results.
    HMODULE pinned = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN |
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(target), &pinned)) {
        FreeLibrary(held);
        h.module_lease = nullptr;
    }
    return true;
}

static bool HookUninstall(Hook &h)
{
    // A thread can have branched into the detour but not yet incremented the
    // callback counter. There is no sound finite quiescence proof for that
    // instruction window, so enabled hooks are retained until process exit.
    if (h.active || InterlockedCompareExchange(&h.callbacks, 0, 0) != 0)
        return false;
    if (h.entry != nullptr && MH_RemoveHook(h.entry) != MH_OK) return false;
    HMODULE lease = h.module_lease;
    h.entry = h.target = nullptr;
    h.module_lease = nullptr;
    if (lease != nullptr) FreeLibrary(lease);
    return true;
}

// Forwarders call the published MinHook trampoline. There is no unpatch window.
static void HookRemove(Hook &) {}
static void HookRestore(Hook &) {}

'''
    return text[:start] + replacement + text[stop:]


def patch_release_and_admission(text: str) -> str:
    text = once(text, '    Hook    create;\n', '    Hook    create;\n    Hook    release;\n')
    text = once(text, 'static volatile LONG g_create_count = 0;\n',
                'static volatile LONG g_create_count = 0;\n' + STATE)
    text = once(text, '''static NVSDK_NGX_Result ForwardEvaluate(Hook &h, const char *tag, ID3D11DeviceContext *ctx,
                                        const NVSDK_NGX_Handle *handle,
                                        const NVSDK_NGX_Parameter *p, void *cb)
{''', '''static NVSDK_NGX_Result ForwardEvaluate(Hook &h, const char *tag, ID3D11DeviceContext *ctx,
                                        const NVSDK_NGX_Handle *handle,
                                        const NVSDK_NGX_Parameter *p, void *cb)
{
    HookCallGuard callback(h);
    CarrierOperationGuard operation;''')
    text = once(text, '''static NVSDK_NGX_Result ForwardCreate(Hook &h, ID3D11DeviceContext *ctx, int feature_id,
                                      NVSDK_NGX_Parameter *p, NVSDK_NGX_Handle **out)
{''', '''static NVSDK_NGX_Result ForwardCreate(Hook &h, ID3D11DeviceContext *ctx, int feature_id,
                                      NVSDK_NGX_Parameter *p, NVSDK_NGX_Handle **out)
{
    HookCallGuard callback(h);
    CarrierOperationGuard operation;''')
    # A handle without a successful observed Create on this exact D3D11 device
    # is forwarded natively and never reaches the private bridge.
    admission_anchor = '    const LONG n = InterlockedIncrement(&g_eval_count);'
    admission = r'''    nr_carrier_activation::FeatureRecord admitted{};
    if (!CarrierAllows(handle, ctx, &admitted)) {
        static volatile LONG incomplete_reports;
        if (InterlockedIncrement(&incomplete_reports) <= 6)
            Log("[native-carrier] incomplete native Create/device/swapchain history for handle=%p; native path retained. Use F8 BUG feedback.", handle);
        if (g_ngx_cs_ready) EnterCriticalSection(&g_ngx_cs);
        const NVSDK_NGX_Result native = reinterpret_cast<PFN_Evaluate>(h.target)(ctx, handle, p, cb);
        if (g_ngx_cs_ready) LeaveCriticalSection(&g_ngx_cs);
        return native;
    }

'''
    text = once(text, admission_anchor, admission + admission_anchor)
    text = once(text, '''    // Opt-in integration: retain the existing native hook/parameter ABI, but
    // use this call's queued CopyResource as evidence, never last-frame health.
    EnterCriticalSection(&g_bridge_cs);''', '''    // Serialize admission, bridge mutation and device/swapchain destruction.
    EnterCriticalSection(&g_carrier_cs);
    if (!g_carrier_registry.StillCurrent(admitted)) {
        LeaveCriticalSection(&g_carrier_cs);
        if (g_ngx_cs_ready) EnterCriticalSection(&g_ngx_cs);
        const NVSDK_NGX_Result native = reinterpret_cast<PFN_Evaluate>(h.target)(ctx, handle, p, cb);
        if (g_ngx_cs_ready) LeaveCriticalSection(&g_ngx_cs);
        return native;
    }
    // Opt-in integration: retain the existing native hook/parameter ABI, but
    // use this call's queued CopyResource as evidence, never last-frame health.
    EnterCriticalSection(&g_bridge_cs);''')
    text = once(text, '''    LeaveCriticalSection(&g_bridge_cs);
    return r;
}''', '''    LeaveCriticalSection(&g_bridge_cs);
    LeaveCriticalSection(&g_carrier_cs);
    return r;
}''')
    text = once(text, '''    // Opening a private device is one-off bootstrap work. Per-frame copies and
    // evaluation are separately gated by the consumer's actual master switch.''', '''    if (bridge_requested && g_carrier_bridge_owner == 0)
        g_carrier_bridge_owner = admitted.device;
    // Opening a private device is one-off bootstrap work. Per-frame copies and
    // evaluation are separately gated by the consumer's actual master switch.''')
    text = once(text, '''    Breadcrumb("forwarding the game's DLSS create");
    const LONG n = InterlockedIncrement(&g_create_count);''', '''    std::uintptr_t carrier_create_device = 0;
    const bool carrier_create_device_known = CarrierDeviceOf(ctx, &carrier_create_device);
    unsigned int carrier_create_flags = 0;
    const bool carrier_create_flags_known = p != nullptr &&
        p->Get("DLSS.Feature.Create.Flags", &carrier_create_flags) == NGX_SUCCESS;
    Breadcrumb("forwarding the game's DLSS create");
    const LONG n = InterlockedIncrement(&g_create_count);''')
    create_anchor = '''    if (feature_id == 1 && r == NGX_SUCCESS && out != nullptr && *out != nullptr)
        BridgeNoteCreate(*out, p);'''
    create = r'''    if (feature_id == 1 && r == NGX_SUCCESS && out != nullptr && *out != nullptr)
    {
        if (carrier_create_device_known) {
            EnterCriticalSection(&g_carrier_cs);
            const std::uint64_t generation = g_carrier_registry.Created(
                reinterpret_cast<std::uintptr_t>(*out), carrier_create_device,
                carrier_create_flags, carrier_create_flags_known);
            LeaveCriticalSection(&g_carrier_cs);
            if (generation != 0) BridgeNoteCreate(*out, p, carrier_create_flags, carrier_create_flags_known);
            else Log("[native-carrier] successful Create could not be admitted; native path retained. Use F8 BUG feedback.");
        }
    }'''
    text = once(text, create_anchor, create)
    insert = text.index('// Defined in vkmirror.inc, which is included after synth.inc')
    release_impl = r'''typedef NVSDK_NGX_Result (*PFN_Release)(NVSDK_NGX_Handle *);
static NVSDK_NGX_Result ForwardRelease(Hook &h, NVSDK_NGX_Handle *handle)
{
    HookCallGuard callback(h);
    CarrierOperationGuard operation;
    NestGuard nest;
    if (g_ngx_cs_ready) EnterCriticalSection(&g_ngx_cs);
    const NVSDK_NGX_Result result = reinterpret_cast<PFN_Release>(h.target)(handle);
    if (g_ngx_cs_ready) LeaveCriticalSection(&g_ngx_cs);
    if (g_nest == 1 && handle != nullptr) {
        std::uintptr_t device = 0;
        EnterCriticalSection(&g_carrier_cs);
        const bool removed = g_carrier_registry.Released(
            reinterpret_cast<std::uintptr_t>(handle), &device);
        const bool retire = removed && !g_carrier_registry.HasLiveFeature(device);
        if (retire) CarrierRetireLocked(device);
        LeaveCriticalSection(&g_carrier_cs);
        if (result != NGX_SUCCESS)
            Log("[native-carrier] native Release returned 0x%08X; handle is no longer trusted for private bridge use.", result);
    }
    return result;
}

'''
    text = text[:insert] + release_impl + text[insert:]
    text = once(text, '''    static NVSDK_NGX_Result Detour_Create_##i(                                       \\
        ID3D11DeviceContext *c, int f, NVSDK_NGX_Parameter *p, NVSDK_NGX_Handle **o) \\
    { return ForwardCreate(g_layer[i].create, c, f, p, o); }                         \\''', '''    static NVSDK_NGX_Result Detour_Create_##i(                                       \\
        ID3D11DeviceContext *c, int f, NVSDK_NGX_Parameter *p, NVSDK_NGX_Handle **o) \\
    { return ForwardCreate(g_layer[i].create, c, f, p, o); }                         \\
    static NVSDK_NGX_Result Detour_Release_##i(NVSDK_NGX_Handle *h)                  \\
    { return ForwardRelease(g_layer[i].release, h); }                                \\''')
    text = once(text, '    PFN_Create   create;\n', '    PFN_Create   create;\n    PFN_Release  release;\n')
    text = once(text, '''#define LAYER_ENTRY(i) { &Detour_Evaluate_##i, &Detour_Evaluate_C_##i, &Detour_Create_##i, \\
                         reinterpret_cast<void *>(&Detour_VkEvaluate_##i),''', '''#define LAYER_ENTRY(i) { &Detour_Evaluate_##i, &Detour_Evaluate_C_##i, &Detour_Create_##i, \\
                         &Detour_Release_##i,                                           \\
                         reinterpret_cast<void *>(&Detour_VkEvaluate_##i),''')
    return text


def patch_discovery(text: str) -> str:
    text = once(text, 'static volatile bool g_scan_pending;',
                'static std::atomic<bool> g_scan_pending{false};')
    text = once(text, '    Hook    release;\n',
                '    Hook    release;\n    HMODULE source_lease; // Retained only for installed observer layers.\n')
    text = once(text, '''// One detour per layer, because a detour has to know which layer's saved bytes
// to restore before forwarding, and the entry point itself carries no way to
// tell. Eight sets of three is duplication a macro can carry; the alternative is
// hand-written thunks in assembly.''', '''// One detour per layer selects that layer's permanent MinHook trampoline.
// The entry point carries no layer identity, so the fixed macro table supplies it.''')
    text = once(text, '''        void *create = reinterpret_cast<void *>(GetProcAddress(mods[i], "NVSDK_NGX_D3D11_CreateFeature"));''', '''        void *create = reinterpret_cast<void *>(GetProcAddress(mods[i], "NVSDK_NGX_D3D11_CreateFeature"));
        void *release = reinterpret_cast<void *>(GetProcAddress(mods[i], "NVSDK_NGX_D3D11_ReleaseFeature"));''')
    text = once(text, '        const bool has_d11 = create != nullptr && (eval != nullptr || eval_c != nullptr);',
                '        const bool has_d11 = create != nullptr && release != nullptr && (eval != nullptr || eval_c != nullptr);')
    text = once(text, '''        Log("  NVSDK_NGX_D3D11_CreateFeature     = %p", create);
        Log("  NVSDK_NGX_D3D11_EvaluateFeature   = %p", eval);''', '''        Log("  NVSDK_NGX_D3D11_CreateFeature     = %p", create);
        Log("  NVSDK_NGX_D3D11_ReleaseFeature    = %p", release);
        Log("  NVSDK_NGX_D3D11_EvaluateFeature   = %p", eval);''')
    text = once(text, '''        const bool ok_create = create != nullptr &&
            HookInstall(L.create, create, reinterpret_cast<void *>(kDetour[slot].create));
        const bool ok_eval = eval != nullptr &&
            HookInstall(L.eval, eval, reinterpret_cast<void *>(kDetour[slot].eval));
        const bool ok_eval_c = eval_c != nullptr &&
            HookInstall(L.eval_c, eval_c, reinterpret_cast<void *>(kDetour[slot].eval_c));''', '''        const bool ok_release = release != nullptr &&
            HookInstall(L.release, release, reinterpret_cast<void *>(kDetour[slot].release));
        // Release is installed first: a retained partial observer can remove
        // no records, whereas Create without Release could admit a stale handle.
        const bool ok_create = ok_release && create != nullptr &&
            HookInstall(L.create, create, reinterpret_cast<void *>(kDetour[slot].create));
        if (!ok_create || !ok_release) {
            const bool create_removed = HookUninstall(L.create);
            const bool release_removed = HookUninstall(L.release);
            if (create_removed && release_removed) L.mod = nullptr;
            else {
                L.source_lease = source_lease.Detach();
                if (slot == g_layer_count) InterlockedIncrement(&g_layer_count);
                Log("[native-carrier] incomplete observer retained safely without Evaluate; native path retained. Use F8 BUG feedback.");
            }
            LeaveCriticalSection(&g_hook_cs);
            continue;
        }
        L.source_lease = source_lease.Detach();
        const bool full = g_carrier_bridge_armed.load(std::memory_order_acquire);
        const bool ok_eval = full && eval != nullptr &&
            HookInstall(L.eval, eval, reinterpret_cast<void *>(kDetour[slot].eval));
        const bool ok_eval_c = full && eval_c != nullptr &&
            HookInstall(L.eval_c, eval_c, reinterpret_cast<void *>(kDetour[slot].eval_c));''')
    text = once(text, '''        Log("  hooked: CreateFeature=%s EvaluateFeature=%s EvaluateFeature_C=%s",
            create != nullptr ? (ok_create ? "yes" : "FAILED") : "absent",
            eval   != nullptr ? (ok_eval   ? "yes" : "FAILED") : "absent",
            eval_c != nullptr ? (ok_eval_c ? "yes" : "FAILED") : "absent");''', '''        Log("  observer: CreateFeature=%s ReleaseFeature=%s; full bridge EvaluateFeature=%s EvaluateFeature_C=%s",
            create != nullptr ? (ok_create ? "yes" : "FAILED") : "absent",
            release != nullptr ? (ok_release ? "yes" : "FAILED") : "absent",
            !full ? "deferred" : (eval != nullptr ? (ok_eval ? "yes" : "FAILED") : "absent"),
            !full ? "deferred" : (eval_c != nullptr ? (ok_eval_c ? "yes" : "FAILED") : "absent"));''')
    # Existing observer layers are promoted only after a same-device swapchain.
    marker = 'static int HookNewNgxModules()\n'
    promote = MODULE_LEASE + r'''static void PromoteObservedLayers()
{
    if (!g_carrier_bridge_armed.load(std::memory_order_acquire)) return;
    for (LONG i = 0; i < g_layer_count; ++i) {
        Layer &layer = g_layer[i];
        if (layer.mod == nullptr || !layer.create.active || !layer.release.active) continue;
        // source_lease keeps the export table alive, including forwarding DLLs.
        const auto epoch = g_carrier_modules.Epoch();
        void *eval = reinterpret_cast<void *>(GetProcAddress(layer.mod, "NVSDK_NGX_D3D11_EvaluateFeature"));
        void *eval_c = reinterpret_cast<void *>(GetProcAddress(layer.mod, "NVSDK_NGX_D3D11_EvaluateFeature_C"));
        CarrierModuleLease eval_lease, eval_c_lease;
        if (!eval_lease.Acquire(eval) || !eval_c_lease.Acquire(eval_c) ||
            epoch != g_carrier_modules.Epoch()) {
            g_scan_pending.store(true, std::memory_order_release);
            continue;
        }
        if (!layer.eval.active && eval != nullptr)
            HookInstall(layer.eval, eval, reinterpret_cast<void *>(kDetour[i].eval));
        if (!layer.eval_c.active && eval_c != nullptr)
            HookInstall(layer.eval_c, eval_c, reinterpret_cast<void *>(kDetour[i].eval_c));
    }
}

'''
    text = once(text, marker, promote + marker)
    # EnumProcessModules yields addresses, not references. Capture the event epoch
    # before enumeration, lease each image before export lookup, and reject a load
    # generation change even when the bounded per-module ledger has overflowed.
    start = text.index(marker)
    stop = text.index('static void LogPrologue(', start)
    scan = text[start:stop]
    scan = once(scan, '    HMODULE mods[1024];', '''    const auto scan_epoch = g_carrier_modules.Epoch();
    if (g_carrier_rejection_epoch != scan_epoch) {
        g_rejected_count = 0;
        g_carrier_rejection_epoch = scan_epoch;
    }
    HMODULE mods[1024];''')
    scan = once(scan, '        void *eval   =', '''        CarrierModuleLease source_lease;
        if (!source_lease.Acquire(mods[i], mods[i]) ||
            scan_epoch != g_carrier_modules.Epoch()) {
            g_scan_pending.store(true, std::memory_order_release);
            continue;
        }
        void *eval   =''')
    scan = once(scan, '        // Mandatory on Vulkan rather than optional:', '''        // Forwarded exports can belong to another DLL. Lease their code before
        // inspecting prologues, and keep these references until hooks own them.
        CarrierModuleLease code_leases[8];
        void *targets[] = {create, release, eval, eval_c, vk_create, vk_create1, vk_eval, vk_eval_c};
        bool code_live = true;
        for (unsigned target = 0; target < 8; ++target)
            if (!code_leases[target].Acquire(targets[target])) code_live = false;
        if (!code_live || scan_epoch != g_carrier_modules.Epoch()) {
            g_scan_pending.store(true, std::memory_order_release);
            continue;
        }

        // Mandatory on Vulkan rather than optional:''')
    scan = once(scan, 'const bool filler = IsFillerStub(create) ||',
                      'const bool filler = IsFillerStub(release) || IsFillerStub(create) ||')
    scan = once(scan, '        if ((eval != nullptr && eval == create) ||',
        '''        if ((release != nullptr && (release == create || release == eval || release == eval_c)) ||
            (eval != nullptr && eval == create) ||''')
    text = text[:start] + scan + text[stop:]
    text = once(text, 'static void TryInstallHooks()\n{', '''static void TryInstallHooks()
{
    if (!g_carrier_observer_armed.load(std::memory_order_acquire) ||
        !g_carrier_notifications_ready.load(std::memory_order_acquire)) return;''')
    text = once(text, '    if (g_layer_count >= kMaxLayers) return;',
                '    // A full table still needs to promote its existing observer layers.')
    text = once(text, '''    if (!TryEnterCriticalSection(&g_hook_cs)) return;
    const int added = HookNewNgxModules();''', '''    if (!TryEnterCriticalSection(&g_hook_cs)) {
        g_scan_pending.store(true, std::memory_order_release);
        return;
    }
    PromoteObservedLayers();
    const int added = HookNewNgxModules();''')
    text = once(text, '''    if (g_scan_pending && !g_ngx_init_in_flight)
    { g_scan_pending = false; TryInstallHooks(); }''', '''    if (!g_ngx_init_in_flight && g_scan_pending.exchange(false, std::memory_order_acq_rel))
        TryInstallHooks();''')
    return text


MODULE_LEASE = r'''class CarrierModuleLease {
public:
    CarrierModuleLease() = default;
    CarrierModuleLease(const CarrierModuleLease &) = delete;
    CarrierModuleLease &operator=(const CarrierModuleLease &) = delete;
    ~CarrierModuleLease() { if (module_ != nullptr) FreeLibrary(module_); }
    bool Acquire(void *address, HMODULE expected = nullptr) {
        if (address == nullptr) return true; // Absent optional export.
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(address), &module_)) return false;
        if (expected != nullptr && module_ != expected) {
            FreeLibrary(module_);
            module_ = nullptr;
            return false;
        }
        return true;
    }
    HMODULE Detach() { HMODULE module = module_; module_ = nullptr; return module; }
private:
    HMODULE module_ = nullptr;
};

'''


LEDGER = r'''static void CALLBACK OnDllLoaded(ULONG reason, const void *data, void *)
{
    if ((reason == 1 || reason == 2) && data != nullptr) {
        const auto *event = static_cast<const LdrDllNotificationData *>(data);
        g_carrier_modules.Record(reinterpret_cast<std::uintptr_t>(event->DllBase), reason == 1);
        g_scan_pending.store(true, std::memory_order_release);
    }
}

static void CarrierPumpModuleEvents()
{
    bool scan = false;
    g_carrier_modules.Drain([&](const nr_carrier_activation::ModuleEvent &event) {
        if (event.loaded) scan = true;
    });
    if (g_carrier_modules.NeedsRescan()) {
        g_carrier_modules.BeginFullRescan();
        scan = true;
    }
    scan = g_scan_pending.exchange(false, std::memory_order_acq_rel) || scan;
    if (scan && g_carrier_observer_armed.load(std::memory_order_acquire))
        TryInstallHooks();
}

static void StartWatchingForNgx()
{
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    auto reg = nt != nullptr ? reinterpret_cast<PFN_LdrRegisterDllNotification>(
        GetProcAddress(nt, "LdrRegisterDllNotification")) : nullptr;
    g_ldr_unregister = nt != nullptr ? reinterpret_cast<PFN_LdrUnregisterDllNotification>(
        GetProcAddress(nt, "LdrUnregisterDllNotification")) : nullptr;
    const bool registered = reg != nullptr &&
        reg(0, &OnDllLoaded, nullptr, &g_ldr_cookie) >= 0 && g_ldr_cookie != nullptr;
    g_carrier_notifications_ready.store(registered, std::memory_order_release);
    if (!registered)
        Log("[native-carrier] loader generation notifications unavailable; native path retained. Use F8 BUG feedback.");
    g_carrier_modules.BeginFullRescan();
    g_scan_pending.store(true, std::memory_order_release);
}

static void StopWatchingForNgx() {}

'''


CALLBACKS = r'''static HMODULE g_addon_init_reshade;

static std::uintptr_t CarrierNativeDevice(reshade::api::device *device)
{
    return device != nullptr ? static_cast<std::uintptr_t>(device->get_native()) : 0;
}

static std::uintptr_t CarrierNativeSwapchain(reshade::api::swapchain *swapchain)
{
    return swapchain != nullptr ? static_cast<std::uintptr_t>(swapchain->get_native()) : 0;
}

static void OnCarrierInitDevice(reshade::api::device *device)
{
    if (device == nullptr || device->get_api() != reshade::api::device_api::d3d11) return;
    EnterCriticalSection(&g_carrier_cs);
    const bool accepted = g_carrier_registry.AddD3d11Device(CarrierNativeDevice(device));
    LeaveCriticalSection(&g_carrier_cs);
    if (accepted) {
        g_carrier_observer_armed.store(true, std::memory_order_release);
        g_scan_pending.store(true, std::memory_order_release);
        CarrierPumpModuleEvents();
    } else Log("[native-carrier] D3D11 device observer capacity exhausted; native path retained. Use F8 BUG feedback.");
}

static void CarrierRetireLocked(std::uintptr_t device)
{
    if (g_carrier_bridge_owner != device) return;
    EnterCriticalSection(&g_bridge_cs);
    const bool released = BridgeReleaseSession();
    if (released) g_carrier_bridge_owner = 0;
    LeaveCriticalSection(&g_bridge_cs);
    if (released)
        Log("[native-carrier] retired the complete bridge session for D3D11 device %p.", reinterpret_cast<void *>(device));
    else
        Log("[native-carrier] bridge session for D3D11 device %p remains quarantined.", reinterpret_cast<void *>(device));
}

static void OnCarrierDestroyDevice(reshade::api::device *device)
{
    const auto native = CarrierNativeDevice(device);
    EnterCriticalSection(&g_carrier_cs);
    const bool removed = g_carrier_registry.RemoveDevice(native);
    if (removed) CarrierRetireLocked(native);
    LeaveCriticalSection(&g_carrier_cs);
}

static void OnCarrierInitSwapchain(reshade::api::swapchain *swapchain, bool resize)
{
    auto *device = swapchain != nullptr ? swapchain->get_device() : nullptr;
    if (device == nullptr || device->get_api() != reshade::api::device_api::d3d11) return;
    EnterCriticalSection(&g_carrier_cs);
    const bool confirmed = g_carrier_registry.AddSwapchain(
        CarrierNativeSwapchain(swapchain), CarrierNativeDevice(device), resize);
    LeaveCriticalSection(&g_carrier_cs);
    if (confirmed) {
        g_carrier_bridge_armed.store(true, std::memory_order_release);
        g_scan_pending.store(true, std::memory_order_release);
        CarrierPumpModuleEvents();
    }
}

static void OnCarrierDestroySwapchain(reshade::api::swapchain *swapchain, bool resize)
{
    auto *device = swapchain != nullptr ? swapchain->get_device() : nullptr;
    if (device == nullptr || device->get_api() != reshade::api::device_api::d3d11) return;
    const auto native = CarrierNativeDevice(device);
    EnterCriticalSection(&g_carrier_cs);
    const bool last = g_carrier_registry.RemoveSwapchain(
        CarrierNativeSwapchain(swapchain), native, resize);
    if (last) CarrierRetireLocked(native);
    LeaveCriticalSection(&g_carrier_cs);
}

static void OnCarrierPresent(reshade::api::command_queue *, reshade::api::swapchain *,
                             const reshade::api::rect *, const reshade::api::rect *,
                             uint32_t, const reshade::api::rect *)
{
    if (g_scan_pending.load(std::memory_order_acquire)) CarrierPumpModuleEvents();
}

static bool CarrierRegisterEvents()
{
    if (g_reshade_dll == nullptr) return false;
    auto reg = reinterpret_cast<PFN_ReShadeRegisterEvent>(
        GetProcAddress(g_reshade_dll, "ReShadeRegisterEvent"));
    if (reg == nullptr) return false;
    reg(reshade::addon_event::init_device, reinterpret_cast<void *>(&OnCarrierInitDevice));
    reg(reshade::addon_event::destroy_device, reinterpret_cast<void *>(&OnCarrierDestroyDevice));
    reg(reshade::addon_event::init_swapchain, reinterpret_cast<void *>(&OnCarrierInitSwapchain));
    reg(reshade::addon_event::destroy_swapchain, reinterpret_cast<void *>(&OnCarrierDestroySwapchain));
    reg(reshade::addon_event::present, reinterpret_cast<void *>(&OnCarrierPresent));
    return true;
}

'''


def patch_loader_and_entry(text: str) -> str:
    start = text.index('static void CALLBACK OnDllLoaded(')
    stop = text.index('// Said at unload, when the answer is finally known', start)
    text = text[:start] + LEDGER + text[stop:]
    registration = text.index('static bool RegisterWithReShade(')
    # Callback definitions need the ReShade register-event typedef from synth.inc.
    text = text[:registration] + CALLBACKS + text[registration:]
    text = once(text, '''    HMODULE mods[1024];
    DWORD   needed = 0;
    if (!enum_modules(GetCurrentProcess(), mods, sizeof(mods), &needed))
        return false;''', '''    HMODULE mods[1024];
    DWORD needed = 0;
    if (g_addon_init_reshade != nullptr) {
        mods[0] = g_addon_init_reshade;
        needed = sizeof(HMODULE);
    } else if (!enum_modules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
        return false;
    }''')
    text = once(text, '        InitializeCriticalSection(&g_bridge_cs);',
                '        InitializeCriticalSection(&g_bridge_cs);\n        InitializeCriticalSection(&g_carrier_cs);')
    text = once(text, '        if (!RegisterWithReShade(module))\n            return FALSE;',
                '''        if (!RegisterWithReShade(module))
            return FALSE;
        if (!CarrierRegisterEvents()) return FALSE;
        HMODULE pinned_carrier = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN |
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(&CarrierRegisterEvents), &pinned_carrier))
            return FALSE;''')
    text = once(text, '        g_hook_time = GetTickCount64();\n        StartWatchingForNgx();',
                '''        g_hook_time = GetTickCount64();
        StartWatchingForNgx();''')
    signature = 'BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)\n'
    begin = text.index(signature)
    attach_body_start = text.index('    {\n', text.index('if (reason == DLL_PROCESS_ATTACH)', begin)) + 6
    detach = text.index('    else if (reason == DLL_PROCESS_DETACH)', attach_body_start)
    attach_body = text[attach_body_start:detach]
    # Remove the closing brace belonging to the attach branch.
    if not attach_body.rstrip().endswith('}'):
        raise ValueError('attach body boundary changed')
    attach_body = attach_body.rstrip()[:-1].rstrip()
    # Keep all inherited locals and early returns inside a lambda, so failure
    # cleanup does not jump across C++ initializations.
    entry = '''extern "C" __declspec(dllexport) bool AddonInit(HMODULE module, HMODULE reshade)
{
    if (module == nullptr || reshade == nullptr || module != g_self) return false;
    LONG expected = 0;
    if (!g_carrier_initialized.compare_exchange_strong(expected, 1,
            std::memory_order_acq_rel))
        return expected == 2;
    if (GetProcAddress(reshade, "ReShadeRegisterAddon") == nullptr ||
        GetProcAddress(reshade, "ReShadeRegisterEvent") == nullptr ||
        MH_Initialize() != MH_OK) {
        g_carrier_initialized.store(0, std::memory_order_release);
        return false;
    }
    g_addon_init_reshade = reshade;
    const bool initialized = [&]() -> bool {
''' + attach_body + '''
        return true;
    }();
    if (!initialized) {
        if (g_unregister != nullptr) g_unregister(module);
        MH_Uninitialize();
        g_carrier_initialized.store(0, std::memory_order_release);
        return false;
    }
    g_carrier_initialized.store(2, std::memory_order_release);
    return true;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
'''
    return text[:begin] + entry


def patch_host(text: str) -> str:
    text = patch_hooks(text)
    text = patch_release_and_admission(text)
    text = patch_discovery(text)
    return patch_loader_and_entry(text)


def apply(stage) -> None:
    path = Path(stage) / 'carrier/src/dlss5-bridge.cpp'
    path.write_text(patch_host(path.read_text(encoding='utf-8')), encoding='utf-8', newline='\n')
    bridge = Path(stage) / 'carrier/src/bridge.inc'
    text = once(bridge.read_text(encoding='utf-8'),
        'static void BridgeNoteCreate(const NVSDK_NGX_Handle *handle, const NVSDK_NGX_Parameter *p)',
        'static void BridgeNoteCreate(const NVSDK_NGX_Handle *handle, const NVSDK_NGX_Parameter *p,\n'
        '                             unsigned int native_flags, bool flags_known)')
    text = once(text, '    unsigned int native_flags = 0;\n'
        '    const bool flags_known = p != nullptr && p->Get("DLSS.Feature.Create.Flags", &native_flags) == NGX_SUCCESS;\n',
        '    // Use flags captured before the native Create can mutate its parameter object.\n')
    bridge.write_text(text, encoding='utf-8', newline='\n')
