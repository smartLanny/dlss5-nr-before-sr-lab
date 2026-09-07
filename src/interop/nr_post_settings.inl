// Included after normal config helpers. Separate requests never overwrite
// g_config.mode; effective route selection is per evaluated feature.
namespace nr_post_settings {
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
std::atomic<ULONGLONG> next_poll{0};
Options Read() {
    const bool loaded=ready.load(std::memory_order_acquire);
    const unsigned v=bits.load(std::memory_order_acquire);
    return {loaded && (v&256u)!=0, nr_post_compat::Percent(v&255u)};
}
bool Reject(unsigned percent) {
    const bool was_rejected=request_rejected.exchange(true,std::memory_order_acq_rel);
    const unsigned previous=rejected_percent.exchange(percent,std::memory_order_acq_rel);
    if(!was_rejected || previous!=percent)
        Log("[post-compat] unsupported Npost/O request=%u%%; previous precision retained, NOT clamped. Choose 50-100 or 0/off. F8 TXT and upload BUG if unexpected.",percent);
    return false;
}
bool PrepareRequestLocked(Options& value) {
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
    const unsigned encoded=value.percent|(value.rr?256u:0u);
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
       persistence_failed.load(std::memory_order_acquire)) return;
    const auto now=GetTickCount64();
    auto next_tick=next_poll.load(std::memory_order_relaxed);
    if(now<next_tick || !next_poll.compare_exchange_strong(next_tick,now+500,
            std::memory_order_relaxed)) return;
    const auto observed_revision=revision.load(std::memory_order_acquire);
    Options next;
    next.rr=GetPrivateProfileIntW(L"NRBeforeSR",L"RRPostEnabled",1,g_ini_path)!=0;
    next.percent=GetPrivateProfileIntW(L"NRBeforeSR",L"PostWorkPercent",100,g_ini_path);
    const bool zero_request=next.percent==0;
    EnterCriticalSection(&g_chain_cs);
    if(!nr_f8::g_config_hold.load(std::memory_order_acquire) &&
       observed_revision==revision.load(std::memory_order_acquire) && PrepareRequestLocked(next)) {
        ApplyLocked(next);
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
} // namespace nr_post_settings
