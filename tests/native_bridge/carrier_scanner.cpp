#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <thread>

struct Hook { void *entry{}, *target{}; bool active{}; };
struct Layer {
    HMODULE mod{}, source_lease{};
    Hook create, release, eval, eval_c, vk_create, vk_create1, vk_eval, vk_eval_c;
};
struct Detours { void *eval{}, *eval_c{}, *create{}, *release{}, *vk_eval{}, *vk_eval_c{}, *vk_create{}, *vk_create1{}; };
constexpr LONG kMaxLayers = 8;
static Layer g_layer[kMaxLayers];
static Detours kDetour[kMaxLayers];
static LONG g_layer_count, g_rejected_count;
static CRITICAL_SECTION g_hook_cs;
static std::atomic<bool> g_carrier_bridge_armed{false}, g_scan_pending{false};
static std::uint64_t g_carrier_rejection_epoch;
struct Ledger { unsigned value = 1; unsigned Epoch() const { return value; } } g_carrier_modules;
static struct { int skip_exe = 0; } g_cfg;
static int g_vk_mirror;
static bool g_force_exe;
static HMODULE g_deferred_exe;
static int refs[4], installs, unsafe_exports, unsafe_reads;
static bool fail_create, fail_release, forwarded, ordinary, reload, mismatch;
static HMODULE Module(int n) { return reinterpret_cast<HMODULE>(std::uintptr_t(n * 0x1000)); }
static int Owner(void *p) { return int(reinterpret_cast<std::uintptr_t>(p) / 0x1000); }
static void *Code(int n, int offset) { return reinterpret_cast<void *>(std::uintptr_t(n * 0x1000 + offset)); }
static void Log(const char *, ...) {}
static bool AlreadyRejected(HMODULE m) { return g_rejected_count && m == Module(1); }
static void RememberRejected(HMODULE) { ++g_rejected_count; }
static bool FileVersionString(const wchar_t *, char *, size_t) { return false; }
static void NoteSnippetVersion(const wchar_t *, const char *) {}
static bool IsFillerStub(void *p) { if (p && refs[Owner(p)] == 0) ++unsafe_reads; return false; }
static void LogPrologue(const char *, const BYTE *p) { if (p && refs[Owner(const_cast<BYTE *>(p))] == 0) ++unsafe_reads; }
using PFN_EnumProcessModules = BOOL (*)(HANDLE, HMODULE *, DWORD, DWORD *);
static BOOL Enumerate(HANDLE, HMODULE *mods, DWORD, DWORD *needed) {
    mods[0] = Module(1); mods[1] = Module(2);
    *needed = DWORD(sizeof(HMODULE) * (fail_create ? 2 : 1));
    if (reload) ++g_carrier_modules.value; // Unload/reload after the enumeration snapshot.
    return TRUE;
}
static HMODULE TestGetModuleHandleW(LPCWSTR name) { return name ? Module(3) : nullptr; }
static FARPROC TestGetProcAddress(HMODULE mod, LPCSTR name) {
    if (mod == Module(3)) return reinterpret_cast<FARPROC>(&Enumerate);
    if (refs[Owner(mod)] == 0) ++unsafe_exports;
    if (ordinary || std::strstr(name, "VULKAN")) return nullptr;
    const int owner = forwarded ? 2 : Owner(mod);
    int offset = 0;
    if (std::strstr(name, "Release")) offset = 32;
    else if (std::strstr(name, "Create")) offset = 64;
    else if (std::strstr(name, "EvaluateFeature_C")) offset = 96;
    else if (std::strstr(name, "Evaluate")) offset = 128;
    return reinterpret_cast<FARPROC>(Code(owner, offset));
}
static BOOL TestGetModuleHandleExW(DWORD, LPCWSTR address, HMODULE *out) {
    const int owner = Owner(const_cast<wchar_t *>(address));
    *out = Module(mismatch ? 2 : owner);
    ++refs[Owner(*out)];
    return TRUE;
}
static BOOL TestFreeLibrary(HMODULE m) { --refs[Owner(m)]; return TRUE; }
static DWORD TestGetModuleFileNameW(HMODULE, LPWSTR path, DWORD) {
    std::wcscpy(path, L"fixture.dll"); return 11;
}
static bool HookInstall(Hook &h, void *target, void *) {
    const auto offset = reinterpret_cast<std::uintptr_t>(target) % 0x1000;
    if ((fail_release && offset == 32) || (fail_create && Owner(target) == 1 && offset == 64)) return false;
    if (h.active) return false;
    h.entry = h.target = target; h.active = true;
    ++installs; ++refs[Owner(target)]; // Successful hook owns its code dependency.
    return true;
}
static bool HookUninstall(Hook &h) { return !h.active; }
#define GetModuleHandleW TestGetModuleHandleW
#define GetProcAddress TestGetProcAddress
#define GetModuleHandleExW TestGetModuleHandleExW
#define FreeLibrary TestFreeLibrary
#define GetModuleFileNameW TestGetModuleFileNameW

// ACTUAL_GENERATED_FUNCTIONS

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(int argc, char **argv) {
    CHECK(argc == 2);
    InitializeCriticalSection(&g_hook_cs);
    const char *scenario = argv[1];
    fail_create = std::strcmp(scenario, "partial") == 0;
    fail_release = std::strcmp(scenario, "failed") == 0;
    forwarded = std::strcmp(scenario, "forwarded") == 0;
    ordinary = std::strcmp(scenario, "ordinary") == 0;
    reload = std::strcmp(scenario, "reload") == 0;
    mismatch = std::strcmp(scenario, "mismatch") == 0;
    if (std::strcmp(scenario, "rejected-reload") == 0) {
        g_rejected_count = 1; g_carrier_rejection_epoch = 1;
        ++g_carrier_modules.value;
    }
    EnterCriticalSection(&g_hook_cs); // Same outer mutation ownership as TryInstallHooks.
    HookNewNgxModules();
    LeaveCriticalSection(&g_hook_cs);
    bool other_thread_entered = false;
    std::thread other([&] {
        if (TryEnterCriticalSection(&g_hook_cs)) {
            other_thread_entered = true;
            LeaveCriticalSection(&g_hook_cs);
        }
    });
    other.join();
    CHECK(other_thread_entered);
    if (fail_create) {
        CHECK(g_layer_count == 2);
        CHECK(g_layer[0].mod == Module(1) && g_layer[0].release.target == Code(1, 32));
        CHECK(!g_layer[0].create.active && !g_layer[0].eval.active);
        CHECK(g_layer[1].mod == Module(2) && g_layer[1].create.active);
    } else if (fail_release || ordinary) {
        CHECK(g_layer_count == 0 && refs[1] == 0 && refs[2] == 0);
    } else if (reload || mismatch) {
        CHECK(installs == 0 && unsafe_exports == 0);
        CHECK(g_scan_pending.load() && refs[1] == 0 && refs[2] == 0);
    } else {
        CHECK(g_layer_count == 1 && g_layer[0].create.active);
        CHECK(g_layer[0].source_lease == Module(1) && refs[1] > 0);
        g_carrier_bridge_armed.store(true);
        EnterCriticalSection(&g_hook_cs);
        PromoteObservedLayers();
        LeaveCriticalSection(&g_hook_cs);
        CHECK(g_layer[0].eval.active && g_layer[0].eval_c.active);
    }
    CHECK(unsafe_exports == 0 && unsafe_reads == 0);
    std::puts("PASS: actual scanner control flow, real Windows lock, API doubles");
    return 0;
}
