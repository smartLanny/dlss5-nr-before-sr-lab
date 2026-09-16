// Included after the existing NGX lifecycle installer, before HookWorker.
// No hooks are created/enabled from GetProcAddress or a DLL notification.
// Only a D3D12 CreateFeature request for the actual _nvngx image is redirected.
// The returned typed thunk closes lifecycle coverage BEFORE the native Create.
using CoreBoundaryCreateFn = NgxResult (*)(ID3D12GraphicsCommandList *, int,
                                         NgxParams *, NgxHandle **);
using CoreBoundaryGetProcFn = FARPROC (WINAPI *)(HMODULE, LPCSTR);
CoreBoundaryGetProcFn g_core_boundary_getproc_original = nullptr;
std::atomic<CoreBoundaryCreateFn> g_core_boundary_create_target{nullptr};
std::atomic<bool> g_core_boundary_hook_ready{false};
std::atomic<bool> g_core_boundary_install_started{false};
std::atomic<unsigned long long> g_core_boundary_resolutions{0};
std::atomic<unsigned long long> g_core_boundary_entries{0};
std::atomic<unsigned long long> g_core_boundary_covered{0};
std::atomic<unsigned long long> g_core_boundary_uncovered{0};
std::atomic<unsigned long long> g_core_boundary_conflicts{0};
std::atomic<unsigned long long> g_core_boundary_pin_failures{0};
std::atomic<int> g_core_boundary_install_status{0};
thread_local unsigned int g_core_boundary_depth = 0;

struct CoreBoundaryDepthScope {
    CoreBoundaryDepthScope() { ++g_core_boundary_depth; }
    ~CoreBoundaryDepthScope() { --g_core_boundary_depth; }
    CoreBoundaryDepthScope(const CoreBoundaryDepthScope &) = delete;
    CoreBoundaryDepthScope &operator=(const CoreBoundaryDepthScope &) = delete;
};

NgxResult CoreCreateBeforeNative(ID3D12GraphicsCommandList *list, int feature,
                                 NgxParams *params, NgxHandle **output) {
    const auto native = g_core_boundary_create_target.load(std::memory_order_acquire);
    if (native == nullptr) return kNgxFeatureNotFound; // No fabricated success.
    if (g_core_boundary_depth != 0 || g_private_nr_call_depth != 0)
        return native(list, feature, params, output);
    const DWORD incoming_error = GetLastError();
    CoreBoundaryDepthScope scope;
    const auto entry = g_core_boundary_entries.fetch_add(1, std::memory_order_relaxed) + 1;
    if (!g_core_lifecycle_hooks_ready.load(std::memory_order_acquire))
        PollNgxLifecycleModules(true);
    const bool covered = g_core_lifecycle_hooks_ready.load(std::memory_order_acquire);
    (covered ? g_core_boundary_covered : g_core_boundary_uncovered)
        .fetch_add(1, std::memory_order_relaxed);
    if (entry <= 3) {
        Log("[core-entry] before native CreateFeature #%llu feature=%d lifecycle=%d; "
            "native result/handle and layered Create evidence remain authoritative.",
            entry, feature, covered ? 1 : 0);
    }
    // This is the exact resolved native entry. If installation succeeded its
    // owned detour calls the trampoline once; otherwise preserve native work.
    // No synthetic Create receipt, handle alias or quarantine reset is used.
    SetLastError(incoming_error);
    return native(list, feature, params, output);
}

FARPROC ResolveCoreCreateBoundary(HMODULE module, LPCSTR name, const void *caller) {
    const FARPROC resolved = g_core_boundary_getproc_original(module, name);
    const DWORD native_error = GetLastError();
    FARPROC result = resolved;
    // Ordinals are not strings. Other symbols retain their exact return value.
    if (!g_core_lifecycle_hooks_ready.load(std::memory_order_acquire) &&
        resolved != nullptr && reinterpret_cast<std::uintptr_t>(name) > 0xffffu &&
        std::strcmp(name, "NVSDK_NGX_D3D12_CreateFeature") == 0 &&
        g_private_nr_call_depth == 0 && g_core_boundary_depth == 0) {
        HMODULE caller_module = nullptr;
        const HMODULE self = g_addon_module.load(std::memory_order_acquire);
        const bool caller_known = self != nullptr && caller != nullptr &&
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(caller), &caller_module) != FALSE;
        // Our own export inspections must see the genuine address, not a thunk
        // that MinHook would then accidentally hook instead of the native entry.
        if (caller_known && caller_module != self) {
            wchar_t path[MAX_PATH] = {};
            const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
            path[MAX_PATH - 1] = L'\0'; // Bounded even on a truncated path.
            const wchar_t *leaf = wcsrchr(path, L'\\');
            leaf = leaf != nullptr ? leaf + 1 : path;
            if (length != 0 && length < MAX_PATH && _wcsicmp(leaf, L"_nvngx.dll") == 0) {
                HMODULE owner = nullptr;
                // Pin, do not load: returned function addresses must remain
                // callable after the source SDK caches our dispatch thunk.
                const bool pinned = GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                    reinterpret_cast<LPCWSTR>(resolved), &owner) != FALSE;
                if (pinned && owner == module) {
                    const auto target = reinterpret_cast<CoreBoundaryCreateFn>(resolved);
                    auto expected = static_cast<CoreBoundaryCreateFn>(nullptr);
                    const bool first = g_core_boundary_create_target.compare_exchange_strong(
                        expected, target, std::memory_order_acq_rel, std::memory_order_acquire);
                    if (first || expected == target) {
                        g_core_boundary_resolutions.fetch_add(1, std::memory_order_relaxed);
                        result = reinterpret_cast<FARPROC>(&CoreCreateBeforeNative);
                    } else {
                        // The current runtime supports one core owner. Never
                        // redirect a different module/target to the first one.
                        g_core_boundary_conflicts.fetch_add(1, std::memory_order_relaxed);
                    }
                } else {
                    g_core_boundary_pin_failures.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    }
    SetLastError(native_error);
    return result;
}

__attribute__((noinline)) FARPROC WINAPI HookCoreCreateGetProcAddress(HMODULE module, LPCSTR name) {
    // This project uses clang/GCC Windows x64; no guessed stack walk is needed.
    return ResolveCoreCreateBoundary(module, name,
        __builtin_extract_return_addr(__builtin_return_address(0)));
}

bool InstallCoreCreateBoundaryHook() {
    if (g_core_boundary_install_started.exchange(true, std::memory_order_acq_rel))
        return g_core_boundary_hook_ready.load(std::memory_order_acquire);
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    void *target = kernel == nullptr ? nullptr : reinterpret_cast<void *>(
        GetProcAddress(kernel, "GetProcAddress"));
    if (target == nullptr || !PinHookAddress(target, "core Create resolver")) {
        g_core_boundary_install_status.store(-1, std::memory_order_release);
        return false;
    }
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        g_core_boundary_install_status.store(static_cast<int>(init), std::memory_order_release);
        return false;
    }
    const MH_STATUS created = MH_CreateHook(target,
        reinterpret_cast<void *>(&HookCoreCreateGetProcAddress),
        reinterpret_cast<void **>(&g_core_boundary_getproc_original));
    if (created != MH_OK) {
        g_core_boundary_install_status.store(static_cast<int>(created), std::memory_order_release);
        Log("[core-entry] resolver hook unavailable status=%d; existing Init/preflight remain active.",
            static_cast<int>(created));
        return false; // Do not adopt someone else's pre-existing trampoline.
    }
    const MH_STATUS enabled = MH_EnableHook(target);
    const bool ready = enabled == MH_OK || enabled == MH_ERROR_ENABLED;
    g_core_boundary_install_status.store(static_cast<int>(enabled), std::memory_order_release);
    g_core_boundary_hook_ready.store(ready, std::memory_order_release);
    Log("[core-entry] Create resolver boundary ready=%d status=%d; no Evaluate/Draw/Dispatch hook added.",
        ready ? 1 : 0, static_cast<int>(enabled));
    return ready;
}
