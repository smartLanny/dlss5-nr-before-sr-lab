// Included inside the r3 private namespace, after feedback/state helpers.
#include "nr_render_scale_policy.h"

namespace nr_render_control {
using Getter = NgxResult (*)(NgxParams**);
using Callback = NgxResult (*)(NgxParams*);
constexpr unsigned kSlots = 8;
struct Hook { void* target = nullptr; std::atomic<void*> original{nullptr}; };
Hook getters[kSlots], callbacks[kSlots];
std::atomic<void*> pending[kSlots]{};
std::atomic<unsigned long long> request{0}; // generation:32 | linear percent:32
std::atomic<unsigned> getter_count{0}, callback_count{0}, hook_failures{0};
std::atomic<bool> save_failed{false};
SRWLOCK status_lock = SRWLOCK_INIT;
struct Status {
    unsigned long long generation = 0, queries = 0, accepted = 0, at = 0;
    unsigned percent = 0, output_w = 0, output_h = 0, render_w = 0, render_h = 0;
    nr_render_scale::Decision decision = nr_render_scale::Decision::Disabled;
};
Status status;
bool loaded = false; // Present observer serializes Poll; never written by hooks.
ULONGLONG next_poll = 0;

void QueueCallback(NgxParams* p) {
    void* address = nullptr;
    auto get = ParamSlot<GetVoidFn>(p, 8);
    if (!get || get(p, "DLSSOptimalSettingsCallback", &address) != kNgxSuccess || !address) return;
    for (auto& slot : pending) {
        void* found = slot.load(std::memory_order_acquire);
        if (found == address) return;
        if (!found && slot.compare_exchange_strong(found, address,
                std::memory_order_release, std::memory_order_relaxed)) return;
    }
}

template<unsigned I> NgxResult GetParameters(NgxParams** p) {
    auto original = reinterpret_cast<Getter>(getters[I].original.load(std::memory_order_acquire));
    if (!original) return 0xBAD00007u; // unpublished hooks are never enabled
    const auto result = original(p);
    // Do not retain parameters or alter their vtable. No hook install, vendor
    // initialization or allocation is performed inside this NGX call.
    if (result == kNgxSuccess && p && *p) QueueCallback(*p);
    return result;
}

bool CanApply() {
    // Query-only control. No RR/FG options, feature creation or resources are
    // changed. Observed RR, unknown ownership and active F8 collection exclude it.
    return g_evaluate_hooks_ready.load(std::memory_order_acquire) &&
        !g_module_rescan_in_progress.load(std::memory_order_acquire) &&
        !g_module_observer_gap_detected.load(std::memory_order_acquire) &&
        !g_feature_tracking_uncertain.load(std::memory_order_acquire) &&
        !g_lifecycle_tracking_uncertain.load(std::memory_order_acquire) &&
        !IsRayReconstructionRouteObserved() &&
        !g_shortfuse_nr_enabled.load(std::memory_order_acquire) &&
        !g_generic_renodx_conflict.load(std::memory_order_acquire) &&
        !nr_f8::IsActive() && SnapshotConfig().enabled;
}

struct Params {
    NgxParams* p;
    bool Get(const char* key, std::uint32_t& value) { return GetUInt(p, key, &value); }
    void Set(const char* key, std::uint32_t value) { SetUInt(p, key, value); }
};

template<unsigned I> NgxResult Optimal(NgxParams* p) {
    auto original = reinterpret_cast<Callback>(callbacks[I].original.load(std::memory_order_acquire));
    if (!original) return 0xBAD00007u;
    static thread_local unsigned depth = 0;
    const bool outer = depth++ == 0;
    const auto before = request.load(std::memory_order_acquire);
    const auto result = original(p); // vendor error/status ALWAYS preserved
    --depth;
    if (!outer || result != kNgxSuccess || !p) return result;
    const auto epoch = g_module_observer_epoch.load(std::memory_order_acquire);
    const bool safe = !(epoch & 1u) && CanApply() &&
        before == request.load(std::memory_order_acquire) &&
        epoch == g_module_observer_epoch.load(std::memory_order_acquire) &&
        ParamSlot<SetUIntFn>(p, 4) != nullptr;
    const unsigned percent = static_cast<unsigned>(before);
    Params adapter{p};
    unsigned output_w = 0, output_h = 0;
    GetUInt(p, "Width", &output_w); GetUInt(p, "Height", &output_h);
    const auto plan = nr_render_scale::ApplyOptimal(adapter, percent, safe);
    if (TryAcquireSRWLockExclusive(&status_lock)) {
        ++status.queries;
        if (plan.decision == nr_render_scale::Decision::Accepted) ++status.accepted;
        status.generation = before >> 32;
        status.percent = percent;
        status.output_w = output_w; status.output_h = output_h;
        status.render_w = plan.render.w; status.render_h = plan.render.h;
        status.decision = plan.decision; status.at = GetTickCount64();
        ReleaseSRWLockExclusive(&status_lock);
    }
    return result;
}

const Getter getter_hooks[kSlots] = {GetParameters<0>, GetParameters<1>, GetParameters<2>, GetParameters<3>,
    GetParameters<4>, GetParameters<5>, GetParameters<6>, GetParameters<7>};
const Callback callback_hooks[kSlots] = {Optimal<0>, Optimal<1>, Optimal<2>, Optimal<3>,
    Optimal<4>, Optimal<5>, Optimal<6>, Optimal<7>};

// Called only from the serialized one-second Present poll, NOT a getter hook.
// A retained module keeps each trampoline/callback valid until process exit.
void Install(void* target, Hook* slots, const void* const* detours, bool is_callback) {
    if (!target) return;
    unsigned free = kSlots;
    for (unsigned i=0; i<kSlots; ++i) {
        if (slots[i].target == target) return;
        if (!slots[i].target && free == kSlots) free = i;
    }
    if (free == kSlots) return;
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(target), &owner)) return;
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(owner, path, MAX_PATH);
    const wchar_t* leaf = wcsrchr(path, L'\\'); leaf = leaf ? leaf+1 : path;
    // External OptiScaler/private callbacks require a separate negotiated
    // adapter. Never overwrite another mod's callback or guess its semantics.
    if (is_callback && (!length || length >= MAX_PATH-1 ||
            _wcsicmp(leaf, L"nvngx_dlss.dll") != 0)) {
        FreeLibrary(owner); return;
    }
    slots[free].target = target;
    void* trampoline = nullptr;
    const MH_STATUS created = MH_CreateHook(target, const_cast<void*>(detours[free]), &trampoline);
    if (created != MH_OK) {
        ++hook_failures;
        Log("[render-scale] hook refused status=%d kind=%s; native query retained",
            static_cast<int>(created), is_callback ? "optimal" : "parameters");
        FreeLibrary(owner); return;
    }
    // Publish before enable; never expose a callable detour with no original.
    slots[free].original.store(trampoline, std::memory_order_release);
    const MH_STATUS enabled = MH_EnableHook(target);
    // Keep owner even after an enable failure: a prepared trampoline references it.
    if (enabled != MH_OK) {
        ++hook_failures;
        Log("[render-scale] enable refused status=%d; no global hook changes", static_cast<int>(enabled));
        return;
    }
    if (is_callback) ++callback_count; else ++getter_count;
    Log("[render-scale] %s observer installed; engine re-query still required",
        is_callback ? "optimal-settings" : "parameter-provider");
}

void Poll() {
    const auto now = GetTickCount64();
    if (now < next_poll || !g_evaluate_hooks_ready.load(std::memory_order_acquire)) return;
    next_poll = now + 1000;
    if (!loaded) {
        unsigned percent = GetPrivateProfileIntW(L"NRBeforeSR", L"RenderScalePercent", 0, g_ini_path);
        if (percent && (percent < 50 || percent > 100)) percent = 0;
        request.store((1ull << 32) | percent, std::memory_order_release);
        // The core and its optional detours have process-lifetime ownership.
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&Poll), &pinned)) return;
        loaded = true;
    }
    if (!TryAcquireSRWLockExclusive(&g_evaluate_hook_lock)) return;
    const void* getter_detours[kSlots]; const void* callback_detours[kSlots];
    for (unsigned i=0;i<kSlots;++i) {
        getter_detours[i] = reinterpret_cast<const void*>(getter_hooks[i]);
        callback_detours[i] = reinterpret_cast<const void*>(callback_hooks[i]);
    }
    constexpr const wchar_t* names[] = {L"_nvngx.dll", L"nvngx.dll", L"nvngx_dlss.dll"};
    constexpr const char* exports[] = {"NVSDK_NGX_D3D11_GetCapabilityParameters",
        "NVSDK_NGX_D3D12_GetCapabilityParameters", "NVSDK_NGX_D3D11_GetParameters",
        "NVSDK_NGX_D3D12_GetParameters"};
    for (const auto* name : names) {
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(0, name, &module)) continue;
        for (const auto* entry : exports)
            Install(reinterpret_cast<void*>(GetProcAddress(module, entry)), getters, getter_detours, false);
        FreeLibrary(module);
    }
    for (auto& slot : pending)
        Install(slot.load(std::memory_order_acquire), callbacks, callback_detours, true);
    ReleaseSRWLockExclusive(&g_evaluate_hook_lock);
    static unsigned long long logged_queries = 0;
    Status s{};
    AcquireSRWLockShared(&status_lock); s=status; ReleaseSRWLockShared(&status_lock);
    if (s.queries != logged_queries) {
        logged_queries = s.queries;
        Log("[render-scale] query generation=%llu requested=%u%% output=%ux%u suggested=%ux%u decision=%u queries=%llu accepted=%llu; NOT proof of engine resize",
            s.generation,s.percent,s.output_w,s.output_h,s.render_w,s.render_h,
            static_cast<unsigned>(s.decision),s.queries,s.accepted);
    }
}

void SetRequest(unsigned percent) {
    if (nr_f8::IsActive() || (percent && (percent<50 || percent>100))) return;
    const auto old = request.load(std::memory_order_acquire);
    request.store((((old >> 32)+1) << 32) | percent, std::memory_order_release);
    wchar_t value[16]{}; swprintf_s(value, L"%u", percent);
    save_failed.store(!WritePrivateProfileStringW(L"NRBeforeSR", L"RenderScalePercent", value, g_ini_path));
    Log("[render-scale] requested=%u%%; only a later engine optimal-settings query can apply it; Evaluate/resources unchanged",percent);
}
} // namespace nr_render_control

void NrRenderControlPoll() { nr_render_control::Poll(); }

void DrawRenderControl() {
    using namespace nr_render_control;
    const bool zh=UseChineseUi();
    const auto ui=[zh](const char* en,const char* cn){return zh?cn:en;};
    ImGui::SeparatorText(ui("Game resolution request","游戏分辨率请求"));
    const auto packed=request.load(std::memory_order_acquire);
    const unsigned percent=static_cast<unsigned>(packed);
    static int edit=100;static unsigned long long shown=~0ull;
    // The editor is a request draft. External saved requests replace it only
    // while idle; neither the draft nor a successful query proves a resize.
    if(shown!=packed&&!ImGui::IsAnyItemActive()){edit=percent?static_cast<int>(percent):100;shown=packed;}
    ImGui::TextWrapped("%s",percent?ui("Request saved; waiting for an engine query / matching observed size.","已保存请求，等待引擎查询／对应尺寸观察。"):
        ui("No override; the game controls its resolution.","未覆盖：游戏自行控制分辨率。"));
    const bool rr=IsRayReconstructionRouteObserved();
    ImGui::BeginDisabled(rr);
    ImGui::TextUnformatted(ui("Request draft (not actual scale)","请求草稿（不是实际比例）"));ImGui::SetNextItemWidth(-1.f);
    ImGui::SliderInt("##game-render-scale",&edit,50,100,"%d%%");
    if(ImGui::Button(ui("Request on next engine query","下次引擎查询时请求")))SetRequest(static_cast<unsigned>(edit));
    ImGui::EndDisabled();
    if(rr)ImGui::TextWrapped("%s",ui("New requests are unavailable while RR protection is active.","RR 保护期间不接受新比例请求。"));
    if(ImGui::Button(ui("Return resolution control to game","恢复游戏控制分辨率")))SetRequest(0);
    Status observed{};AcquireSRWLockShared(&status_lock);observed=status;ReleaseSRWLockShared(&status_lock);
    if(percent)ImGui::Text(ui("Saved request: %u%%","已保存请求：%u%%"),percent);
    if(observed.queries&&observed.generation==(packed>>32))ImGui::TextWrapped(ui("Query observed: suggested %u x %u; this does not prove the game resized.","已观察查询：建议 %u × %u，尚不证明游戏已改变尺寸。"),observed.render_w,observed.render_h);
    const auto w=g_last_dlss_input_width.load(),h=g_last_dlss_input_height.load();
    const auto ow=g_last_dlss_output_width.load(),oh=g_last_dlss_output_height.load();
    if(w&&h&&ow&&oh)ImGui::TextWrapped(ui("Observed game size: %u x %u -> %u x %u","已观察游戏尺寸：%u × %u → %u × %u"),w,h,ow,oh);
    else ImGui::TextWrapped("%s",ui("No actual game size observed yet.","尚未观察到实际游戏尺寸。"));
    if(!callback_count.load())ImGui::TextWrapped("%s",ui("No compatible query callback observed; the draft is not an applied override.","尚未观察到可用查询回调；编辑框数值不代表覆盖已生效。"));
    if(save_failed.load())ImGui::TextWrapped("%s",ui("Save failed; request is session-only.","保存失败，请求仅用于当前运行。"));
}
