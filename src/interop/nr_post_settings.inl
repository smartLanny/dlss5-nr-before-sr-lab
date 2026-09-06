// Included after normal config helpers. Separate requests never overwrite
// g_config.mode; all effective route selection is per evaluated feature.
namespace nr_post_settings {
struct Options { bool rr=true; unsigned percent=100; };
std::atomic<unsigned> bits{100u | 256u};
std::atomic<unsigned long long> revision{0};
std::atomic<bool> ready{false};
std::atomic<bool> persistence_failed{false};
std::atomic<unsigned long long> rr_attempts{0}, rr_recorded{0}, rr_native_failures{0};
std::atomic<unsigned long long> last_rr_tick{0};
std::atomic<int> last_rr_stage{0}; // 0 untouched, 1 prepared, 2 recorded, 3 native failed, 4 not applied
ULONGLONG next_poll=0;
Options Read() {
    // Read readiness first: acquiring its initial publication must precede
    // reading the settings it publishes (including a saved RR opt-out).
    const bool loaded=ready.load(std::memory_order_acquire);
    const unsigned v=bits.load(std::memory_order_acquire);
    return {loaded && (v&256u)!=0, nr_post_compat::Percent(v&255u)};
}
void ApplyLocked(Options value) {
    const unsigned encoded=nr_post_compat::Percent(value.percent)|(value.rr?256u:0u);
    if(bits.exchange(encoded,std::memory_order_acq_rel)!=encoded) {
        // Same model-size edits retire through ordinary context/fence rules.
        // A disabled/re-enabled route also gets fresh temporal history.
        revision.fetch_add(1,std::memory_order_release);
        g_config_generation.fetch_add(1,std::memory_order_relaxed);
        g_history_generation.fetch_add(1,std::memory_order_relaxed);
        Log("[post-compat] requested RR-post=%d Npost/O=%u%%; user Mode=%d unchanged. Request is not proof of a recorded/presented frame.",
            value.rr?1:0,nr_post_compat::Percent(value.percent),g_config.mode);
    }
}
void Poll() {
    if(nr_f8::g_config_hold.load(std::memory_order_acquire) || !g_ini_path[0]) return;
    const auto now=GetTickCount64();
    if(now<next_poll || persistence_failed.load(std::memory_order_acquire)) return;
    next_poll=now+500;
    const auto observed_revision=revision.load(std::memory_order_acquire);
    Options next;
    next.rr=GetPrivateProfileIntW(L"NRBeforeSR",L"RRPostEnabled",1,g_ini_path)!=0;
    next.percent=nr_post_compat::Percent(GetPrivateProfileIntW(
        L"NRBeforeSR",L"PostWorkPercent",100,g_ini_path));
    EnterCriticalSection(&g_chain_cs);
    if(!nr_f8::g_config_hold.load(std::memory_order_acquire) &&
       observed_revision==revision.load(std::memory_order_acquire)) {
        ApplyLocked(next);
        if(!ready.exchange(true,std::memory_order_acq_rel))
            Log("[post-compat] initial RR-post=%d Npost/O=%u%%; requested Mode=%d retained.",
                next.rr?1:0,next.percent,g_config.mode);
    }
    LeaveCriticalSection(&g_chain_cs);
}
void Request(Options next) {
    if(nr_f8::g_config_hold.load(std::memory_order_acquire)) return;
    EnterCriticalSection(&g_chain_cs);
    if(nr_f8::g_config_hold.load(std::memory_order_acquire)) {
        LeaveCriticalSection(&g_chain_cs); return;
    }
    next.percent=nr_post_compat::Percent(next.percent);
    // Even an explicit no-op request invalidates an INI read already in
    // progress. Otherwise a stale disk value can overwrite this user action.
    revision.fetch_add(1,std::memory_order_release);
    ApplyLocked(next);
    ready.store(true,std::memory_order_release);
    const bool rr_saved=WriteIniInt(L"RRPostEnabled",next.rr?1:0);
    const bool scale_saved=WriteIniInt(L"PostWorkPercent",static_cast<int>(next.percent));
    persistence_failed.store(!rr_saved||!scale_saved,std::memory_order_release);
    LeaveCriticalSection(&g_chain_cs);
}
} // namespace nr_post_settings
