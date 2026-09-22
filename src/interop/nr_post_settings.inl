// Included after normal config helpers. Separate requests never overwrite
// g_config.mode; effective route selection is per evaluated feature.
#include "../nr_rr_depth_projection.h"
namespace nr_post_settings {
// New/missing preferences attempt the bounded missing-projection candidate.
// Explicit saved OFF and the independent scale/reset contract are preserved.
std::atomic<bool> missing_projection{true};
std::atomic<bool> missing_projection_save_failed{false};
struct Options { bool rr=true; unsigned percent=100; };
std::atomic<unsigned> bits{100u | 256u};
std::atomic<unsigned long long> revision{0};
std::atomic<bool> ready{false}, persistence_failed{false}, request_rejected{false};
std::atomic<unsigned> rejected_percent{0};
std::atomic<unsigned long long> rr_attempts{0}, rr_recorded{0}, rr_native_failures{0};
// Lifetime boundary observations, not mutually exclusive frame outcomes.
// Compare start/end deltas from one process; native failure can also follow
// a before-state rejection. None of these counters changes NR admission.
std::atomic<unsigned long long> rr_before_state_rejected{0}, rr_prepare_declined{0};
std::atomic<unsigned long long> rr_after_state_rejected{0}, rr_finish_declined{0};
std::atomic<unsigned long long> rr_finish_gate_rejected{0};
std::atomic<unsigned long long> last_rr_tick{0};
std::atomic<int> last_rr_stage{0}; // 0 untouched, 1 prepared, 2 recorded, 3 native failed, 4 not applied
std::atomic<int> last_rr_depth_type{-1}; // actual Create declaration; never infer from texture format
struct ProjectionReceipt {
    std::atomic<unsigned long long> sequence{0}, generation{0}, tick{0};
    std::atomic<int> state{0}; // 0 none, 1 absent, 2 unreadable, 3 nonfinite, 4 finite copy
    std::atomic<float> values[16]{};
};
ProjectionReceipt projection;
std::atomic<ULONGLONG> next_poll{0};
// RR selects its compatible post route automatically. The master NR switch
// and all resource/lifecycle admission checks remain authoritative.
constexpr bool RrPostAutomatic() { return true; }
Options Read() {
    const unsigned v=bits.load(std::memory_order_acquire);
    return {RrPostAutomatic(), nr_post_compat::Percent(v&255u)};
}
bool Reject(unsigned percent) {
    const bool was_rejected=request_rejected.exchange(true,std::memory_order_acq_rel);
    const unsigned previous=rejected_percent.exchange(percent,std::memory_order_acq_rel);
    if(!was_rejected || previous!=percent)
        Log("[post-compat] unsupported Npost/O request=%u%%; previous precision retained, NOT clamped. Choose 25-100 or 0/off. F8 TXT and upload BUG if unexpected.",percent);
    return false;
}
bool PrepareRequestLocked(Options& value) {
    value.rr=RrPostAutomatic();
    if(value.percent!=0 && !nr_post_compat::SupportedPercent(value.percent))
        return Reject(value.percent);
    if(value.percent==0) {
        // The control endpoint disables through the ordinary master/history/
        // persistence path. Never allocate zero-sized work or lose precision.
        value.percent=Read().percent;
        ChainConfig disabled=g_config;
        disabled.enabled=false;
        ApplyConfig(disabled,"Post NR precision 0");
        if(g_config.enabled) return false; // capture acquired the hold meanwhile
    }
    request_rejected.store(false,std::memory_order_release);
    return true;
}
void ApplyLocked(Options value) {
    // Only validated, nonzero work percentages may enter this packed state.
    if(!nr_post_compat::SupportedPercent(value.percent)) { Reject(value.percent); return; }
    value.rr=RrPostAutomatic();
    const unsigned encoded=value.percent|256u;
    if(bits.exchange(encoded,std::memory_order_acq_rel)!=encoded) {
        revision.fetch_add(1,std::memory_order_release);
        g_config_generation.fetch_add(1,std::memory_order_relaxed);
        g_history_generation.fetch_add(1,std::memory_order_relaxed);
        Log("[post-compat] requested RR-post=%d Npost/O=%u%%; user Mode=%d unchanged. Request is not proof of a recorded/presented frame.",
            value.rr?1:0,value.percent,g_config.mode);
    }
}
void Poll() {
    if(nr_f8::g_config_hold.load(std::memory_order_acquire) || !g_ini_path[0] ||
       persistence_failed.load(std::memory_order_acquire) ||
       missing_projection_save_failed.load(std::memory_order_acquire)) return;
    const auto now=GetTickCount64();
    auto next_tick=next_poll.load(std::memory_order_relaxed);
    if(now<next_tick || !next_poll.compare_exchange_strong(next_tick,now+500,
            std::memory_order_relaxed)) return;
    const auto observed_revision=revision.load(std::memory_order_acquire);
    Options next;
    // Ignore legacy RRPostEnabled=0: RR routing is automatic, not a second
    // enable switch. User F6/master state is never changed by this policy.
    next.rr=RrPostAutomatic();
    next.percent=GetPrivateProfileIntW(L"NRBeforeSR",L"PostWorkPercent",100,g_ini_path);
    const bool next_missing=GetPrivateProfileIntW(L"NRBeforeSR",L"RRMissingProjectionFallback",1,g_ini_path)!=0;
    const bool zero_request=next.percent==0;
    EnterCriticalSection(&g_chain_cs);
    if(!nr_f8::g_config_hold.load(std::memory_order_acquire) &&
       observed_revision==revision.load(std::memory_order_acquire) && PrepareRequestLocked(next)) {
        ApplyLocked(next);
        if(missing_projection.exchange(next_missing,std::memory_order_acq_rel)!=next_missing) {
            g_config_generation.fetch_add(1,std::memory_order_relaxed);
            g_history_generation.fetch_add(1,std::memory_order_relaxed);
        }
        if(!ready.exchange(true,std::memory_order_acq_rel))
            Log("[post-compat] initial RR-post=%d Npost/O=%u%%; requested Mode=%d retained.",
                next.rr?1:0,next.percent,g_config.mode);
        // Convert an INI zero control request back to its retained precision.
        // Otherwise every poll would disable an explicit later F6 re-enable.
        if(zero_request && !WriteIniInt(L"PostWorkPercent",static_cast<int>(next.percent)))
            persistence_failed.store(true,std::memory_order_release);
    }
    LeaveCriticalSection(&g_chain_cs);
}
bool Request(Options next) {
    if(nr_f8::g_config_hold.load(std::memory_order_acquire)) return false;
    EnterCriticalSection(&g_chain_cs);
    if(nr_f8::g_config_hold.load(std::memory_order_acquire) || !PrepareRequestLocked(next)) {
        LeaveCriticalSection(&g_chain_cs); return false;
    }
    // Explicit no-op requests also invalidate already-running stale INI reads.
    revision.fetch_add(1,std::memory_order_release);
    ApplyLocked(next);
    ready.store(true,std::memory_order_release);
    const bool rr_saved=WriteIniInt(L"RRPostEnabled",next.rr?1:0);
    const bool scale_saved=WriteIniInt(L"PostWorkPercent",static_cast<int>(next.percent));
    persistence_failed.store(!rr_saved||!scale_saved,std::memory_order_release);
    LeaveCriticalSection(&g_chain_cs);
    return true;
}
bool RequestMissingProjection(bool enabled) {
    if(nr_f8::g_config_hold.load(std::memory_order_acquire))return false;
    EnterCriticalSection(&g_chain_cs);
    if(nr_f8::g_config_hold.load(std::memory_order_acquire)){LeaveCriticalSection(&g_chain_cs);return false;}
    revision.fetch_add(1,std::memory_order_release); // Invalidate stale INI polls.
    if(missing_projection.exchange(enabled,std::memory_order_acq_rel)!=enabled){
        g_config_generation.fetch_add(1,std::memory_order_relaxed);
        g_history_generation.fetch_add(1,std::memory_order_relaxed);
    }
    const bool saved=WriteIniInt(L"RRMissingProjectionFallback",enabled?1:0);
    missing_projection_save_failed.store(!saved,std::memory_order_release);
    Log("[RR-depth] explicit missing-projection fallback=%d; placeholder depth, reset each frame, native RR unchanged; request is not NR success.",enabled?1:0);
    LeaveCriticalSection(&g_chain_cs);return saved;
}
// The existing capture session owns the configuration hold for its complete
// bounded sequence. Temporary work sizes and restoration must never write INI.
bool RequestForCapture(Options next) {
    if(!nr_f8::g_config_hold.load(std::memory_order_acquire) ||
       !nr_post_compat::SupportedPercent(next.percent)) return false;
    EnterCriticalSection(&g_chain_cs);
    if(!nr_f8::g_config_hold.load(std::memory_order_acquire)) {
        LeaveCriticalSection(&g_chain_cs); return false;
    }
    next.rr=RrPostAutomatic();
    ApplyLocked(next);
    ready.store(true,std::memory_order_release);
    LeaveCriticalSection(&g_chain_cs);
    return true;
}
} // namespace nr_post_settings
