// Executes the exact ForwardEvaluate replacement, with CPU-only driver doubles.
// This validates routing/Reset restoration, not real NGX, queues, locks or pixels.
#include "src/interop/nr_native_bridge_history.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using NVSDK_NGX_Result = unsigned;
constexpr NVSDK_NGX_Result NGX_SUCCESS = 1, FAILURE = 0xBAD00001u;
struct NVSDK_NGX_Handle {};
struct NVSDK_NGX_Parameter {
    int reset = 0;
    bool known = true;
    unsigned Get(const char* key, int* out) const {
        if (std::strcmp(key, "Reset") || !known) return FAILURE;
        *out = reset; return NGX_SUCCESS;
    }
    void Set(const char*, int value) { reset = value; }
};
struct ID3D11Device { void Release() {} } device;
struct ID3D11DeviceContext { void GetDevice(ID3D11Device** out) { *out = &device; } } context;
int g_bridge_cs = 0, g_ngx_cs = 0, g_hook_cs = 0;
bool g_ngx_cs_ready = true;
void EnterCriticalSection(int* depth) { ++*depth; }
void LeaveCriticalSection(int* depth) { if (--*depth < 0) std::abort(); }
long g_probe_done = 0;
long InterlockedCompareExchange(long* p, long value, long expected) {
    const auto old = *p; if (old == expected) *p = value; return old;
}
constexpr int SRC_MIRROR = 1, SRC_SYNTH = 2;
const NVSDK_NGX_Handle* g_eval_handle = nullptr;
struct { int stage = 3; bool skip = true, off = false; } g_cfg;
struct { bool frame_ready = false, need_reset = false, partial_output = false; } g_bridge;
static NrNativeBridgeHistory g_nr_native_history;
static bool g_nr_native_delivery = false;
namespace nr_native_bridge_client {
bool enabled = true;
bool AllowsFrame(bool) { return enabled; }
}
using PFN_Evaluate = NVSDK_NGX_Result (*)(ID3D11DeviceContext*, const NVSDK_NGX_Handle*,
                                       const NVSDK_NGX_Parameter*, void*);
struct Hook { PFN_Evaluate target = nullptr; bool installed = true; };
void HookRemove(Hook& h) { h.installed = false; }
void HookRestore(Hook& h) { h.installed = true; }
int native_calls = 0, mirror_calls = 0, init_calls = 0;
bool mirror_succeeds = true, make_partial = false, disable_during = false;
unsigned native_result = NGX_SUCCESS;
std::vector<int> resets;
std::vector<char> order;
unsigned Native(ID3D11DeviceContext*, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter* p, void*) {
    ++native_calls; resets.push_back(p->reset); order.push_back('N'); return native_result;
}
bool BridgeWillDeliver() {
    return g_cfg.skip && g_cfg.stage == 3 && !g_cfg.off &&
           g_bridge.frame_ready && !g_bridge.partial_output;
}
void BridgeInitSession(ID3D11Device*, ID3D11DeviceContext*) { ++init_calls; }
void BridgeFrame(ID3D11DeviceContext*, const NVSDK_NGX_Parameter*) {
    if (disable_during) nr_native_bridge_client::enabled = false;
    if (!nr_native_bridge_client::enabled || g_cfg.off || g_cfg.stage != 3) return;
    ++mirror_calls; order.push_back('M');
    if (!mirror_succeeds) return;
    g_bridge.frame_ready = true;
    g_bridge.partial_output = make_partial;
    g_nr_native_delivery = true;
}
unsigned Forward(Hook& h, ID3D11DeviceContext* ctx, const NVSDK_NGX_Handle* handle,
                 const NVSDK_NGX_Parameter* p, void* cb, int prev_source = SRC_MIRROR) {
#include "src/interop/nr_native_bridge_forward.inl"

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while(false)
int main() {
    Hook hook{Native}; NVSDK_NGX_Handle handle, other; NVSDK_NGX_Parameter p;
    const auto key = reinterpret_cast<std::uintptr_t>(&handle);
    CHECK(Forward(hook, &context, &handle, &p, nullptr) == NGX_SUCCESS);
    CHECK(native_calls == 1 && mirror_calls == 1 && init_calls == 1);
    CHECK(order == std::vector<char>({'N','M'}));
    CHECK(!g_nr_native_history.NeedsReset(key));
    order.clear();
    CHECK(Forward(hook, &context, &handle, &p, nullptr) == NGX_SUCCESS);
    CHECK(native_calls == 1 && mirror_calls == 2 && order == std::vector<char>({'M'}));
    CHECK(g_nr_native_history.NeedsReset(key));
    mirror_succeeds = false; native_result = FAILURE; order.clear();
    CHECK(Forward(hook, &context, &handle, &p, nullptr) == FAILURE);
    CHECK(order == std::vector<char>({'M','N'}) && resets.back() == 1 && p.reset == 0);
    CHECK(g_nr_native_history.NeedsReset(key));
    native_result = NGX_SUCCESS;
    CHECK(Forward(hook, &context, &handle, &p, nullptr) == NGX_SUCCESS);
    CHECK(!g_nr_native_history.NeedsReset(key) && p.reset == 0);
    mirror_succeeds = true;
    CHECK(Forward(hook, &context, &handle, &p, nullptr) == NGX_SUCCESS);
    const auto old_mirror = mirror_calls;
    nr_native_bridge_client::enabled = false;
    CHECK(Forward(hook, &context, &handle, &p, nullptr) == NGX_SUCCESS);
    CHECK(mirror_calls == old_mirror && resets.back() == 1 && p.reset == 0);
    nr_native_bridge_client::enabled = true; disable_during = true; order.clear();
    CHECK(Forward(hook, &context, &handle, &p, nullptr) == NGX_SUCCESS);
    CHECK(order == std::vector<char>({'N'}));
    disable_during = false; nr_native_bridge_client::enabled = true;
    // A newly partial output cannot justify skipping the native full output.
    make_partial = true; order.clear();
    CHECK(Forward(hook, &context, &handle, &p, nullptr) == NGX_SUCCESS);
    CHECK(order == std::vector<char>({'M','N'}));
    order.clear();
    CHECK(Forward(hook, &context, &handle, &p, nullptr) == NGX_SUCCESS);
    CHECK(order == std::vector<char>({'N','M'}));
    make_partial = false; g_bridge.partial_output = false;
    // Never skip a native handle whose Reset value cannot be restored.
    p.known = false; order.clear();
    CHECK(Forward(hook, &context, &other, &p, nullptr) == NGX_SUCCESS);
    CHECK(order == std::vector<char>({'N','M'}));
    p.known = true; p.reset = 1; g_cfg.skip = false; order.clear();
    CHECK(Forward(hook, &context, &handle, &p, nullptr) == NGX_SUCCESS);
    CHECK(order == std::vector<char>({'N','M'}) && p.reset == 1);
    g_cfg.off = true; order.clear();
    CHECK(Forward(hook, &context, &handle, &p, nullptr) == NGX_SUCCESS);
    CHECK(order == std::vector<char>({'N'}));
    g_cfg.off = false; native_result = FAILURE; order.clear();
    CHECK(Forward(hook, &context, &handle, &p, nullptr) == FAILURE);
    CHECK(order == std::vector<char>({'N'}));
    CHECK(g_bridge_cs == 0 && g_ngx_cs == 0 && g_hook_cs == 0 && hook.installed);
    CHECK(init_calls == 1);
    std::puts("native bridge exact forward body: passed (CPU doubles, no GPU)");
}
