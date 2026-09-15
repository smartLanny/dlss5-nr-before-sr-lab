// Included after ChainConfig, atomics and chain lock; no GPU work here.
#include "nr_energy_gate.h"
namespace nr_compat_present_runtime {bool InlineDrainReady();bool NativeNrDrained();}
namespace nr_processing_runtime {
using Start=nr_processing::Start;
nr_processing::Session session;
std::atomic<unsigned> effective{0};
std::atomic<unsigned> requested{0};
std::atomic<bool> work_enabled{false};
std::atomic<unsigned long long> next_selection{1};
thread_local bool private_recorded=false,source_blocked=false;
thread_local unsigned long long carrier_serial=0;
thread_local bool native_reconstruction_carrier=false;
thread_local bool energy_gate_blocked=false;
bool fg_redirect_previous=false;
nr_processing::Failure fg_after_failure{};

SRWLOCK energy_gate_lock=SRWLOCK_INIT;
nr_energy_gate::State energy_gate_state{};
std::atomic<bool> energy_gate_loaded{false};
std::atomic<bool> energy_gate_conservative{true};
std::atomic<bool> energy_gate_heuristic{false};
std::atomic<bool> energy_gate_active{false};
std::atomic<bool> energy_gate_persistence_failed{false};
std::atomic<unsigned long long> energy_gate_explicit_pauses{0};
std::atomic<unsigned long long> energy_gate_guess_pauses{0};
std::atomic<unsigned long long> energy_gate_resumes{0};
std::atomic<unsigned long long> energy_gate_stale{0};
std::atomic<unsigned long long> energy_gate_skipped_sources{0};

struct EnergyGateStatus {
    bool conservative=true,heuristic=false,active=false,persistence_failed=false;
    nr_energy_gate::Cause cause=nr_energy_gate::Cause::None;
    unsigned explicit_samples=0,guess_samples=0;
    unsigned long long explicit_age_ms=0,guess_age_ms=0,guess_frame_id=0;
    unsigned long long explicit_pauses=0,guess_pauses=0,resumes=0,stale=0,skipped_sources=0;
};

nr_energy_gate::Options EnergyGateOptions() {
    return {energy_gate_conservative.load(std::memory_order_acquire),
            energy_gate_heuristic.load(std::memory_order_acquire)};
}

void EnsureEnergyGateLoaded() {
    if(energy_gate_loaded.load(std::memory_order_acquire)||!g_ini_path[0])return;
    AcquireSRWLockExclusive(&energy_gate_lock);
    if(!energy_gate_loaded.load(std::memory_order_relaxed)&&g_ini_path[0]){
        energy_gate_state={};
        energy_gate_conservative.store(
            GetPrivateProfileIntW(L"NRBeforeSR",L"PauseNrOnFullscreenUi",1,g_ini_path)!=0,
            std::memory_order_relaxed);
        energy_gate_heuristic.store(
            GetPrivateProfileIntW(L"NRBeforeSR",L"GuessFullscreenUiPause",0,g_ini_path)!=0,
            std::memory_order_relaxed);
        energy_gate_active.store(false,std::memory_order_relaxed);
        energy_gate_loaded.store(true,std::memory_order_release);
    }
    ReleaseSRWLockExclusive(&energy_gate_lock);
}

void ApplyEnergyGateTransition(nr_energy_gate::Transition transition) {
    using nr_energy_gate::Transition;
    switch(transition){
    case Transition::None:break;
    case Transition::PausedExplicit:
        energy_gate_explicit_pauses.fetch_add(1,std::memory_order_relaxed);
        Log("[energy-gate] NR auto-paused by explicit DLSSG.NotRenderingGameFrames; private Feature 18/resources retained.");
        break;
    case Transition::PausedHeuristic:
        energy_gate_guess_pauses.fetch_add(1,std::memory_order_relaxed);
        Log("[energy-gate] NR auto-paused by EXPERIMENTAL FG source-stall guess; private Feature 18/resources retained. If this is real gameplay, press F8 and submit logs.");
        break;
    case Transition::Resumed:
        energy_gate_resumes.fetch_add(1,std::memory_order_relaxed);
        g_history_generation.fetch_add(1,std::memory_order_relaxed);
        Log("[energy-gate] NR resumed; temporal history reset requested.");
        break;
    case Transition::Stale:
        energy_gate_stale.fetch_add(1,std::memory_order_relaxed);
        g_history_generation.fetch_add(1,std::memory_order_relaxed);
        Log("[energy-gate] pause evidence expired; fail-open resumed NR and requested a history reset. If a menu should still be paused, press F8 and submit logs.");
        break;
    case Transition::Disabled:
        energy_gate_resumes.fetch_add(1,std::memory_order_relaxed);
        g_history_generation.fetch_add(1,std::memory_order_relaxed);
        Log("[energy-gate] active power gate was disabled; NR resumed and temporal history reset requested.");
        break;
    }
}

void ObserveEnergyPause(bool paused_known,bool paused,unsigned long long frame_id=0) {
    EnsureEnergyGateLoaded();
    if(!energy_gate_loaded.load(std::memory_order_acquire))return;
    const auto now=GetTickCount64();
    nr_energy_gate::Transition transition=nr_energy_gate::Transition::None;
    AcquireSRWLockExclusive(&energy_gate_lock);
    const auto result=nr_energy_gate::Observe(energy_gate_state,EnergyGateOptions(),
        paused_known,paused,frame_id,now);
    energy_gate_state=result.state;transition=result.transition;
    energy_gate_active.store(result.state.Active(),std::memory_order_release);
    ReleaseSRWLockExclusive(&energy_gate_lock);
    ApplyEnergyGateTransition(transition);
}

bool EnergyGateActive() {
    EnsureEnergyGateLoaded();
    if(!energy_gate_loaded.load(std::memory_order_acquire))return false;
    const auto now=GetTickCount64();
    nr_energy_gate::Transition transition=nr_energy_gate::Transition::None;
    bool active=false;
    AcquireSRWLockExclusive(&energy_gate_lock);
    const auto result=nr_energy_gate::Poll(energy_gate_state,EnergyGateOptions(),now);
    energy_gate_state=result.state;transition=result.transition;active=result.state.Active();
    energy_gate_active.store(active,std::memory_order_release);
    ReleaseSRWLockExclusive(&energy_gate_lock);
    ApplyEnergyGateTransition(transition);
    return active;
}

bool SetEnergyGateOption(bool conservative,bool enabled) {
    EnsureEnergyGateLoaded();
    if(!energy_gate_loaded.load(std::memory_order_acquire)){
        AcquireSRWLockExclusive(&energy_gate_lock);
        energy_gate_state={};energy_gate_loaded.store(true,std::memory_order_release);
        ReleaseSRWLockExclusive(&energy_gate_lock);
    }
    if(conservative)energy_gate_conservative.store(enabled,std::memory_order_release);
    else energy_gate_heuristic.store(enabled,std::memory_order_release);
    nr_energy_gate::Transition transition=nr_energy_gate::Transition::None;
    AcquireSRWLockExclusive(&energy_gate_lock);
    const auto result=nr_energy_gate::Poll(energy_gate_state,EnergyGateOptions(),GetTickCount64());
    energy_gate_state=result.state;transition=result.transition;
    energy_gate_active.store(result.state.Active(),std::memory_order_release);
    ReleaseSRWLockExclusive(&energy_gate_lock);
    ApplyEnergyGateTransition(transition);
    const wchar_t* key=conservative?L"PauseNrOnFullscreenUi":L"GuessFullscreenUiPause";
    const bool persisted=g_ini_path[0]&&WritePrivateProfileStringW(L"NRBeforeSR",key,
        enabled?L"1":L"0",g_ini_path)!=FALSE;
    energy_gate_persistence_failed.store(!persisted,std::memory_order_release);
    if(!persisted)Log("[energy-gate] power-saver option changed for this session but could not be saved to nr_before_sr.ini.");
    return persisted;
}

EnergyGateStatus ReadEnergyGateStatus() {
    (void)EnergyGateActive();
    EnergyGateStatus status{};const auto now=GetTickCount64();
    status.conservative=energy_gate_conservative.load(std::memory_order_acquire);
    status.heuristic=energy_gate_heuristic.load(std::memory_order_acquire);
    AcquireSRWLockShared(&energy_gate_lock);
    status.active=energy_gate_state.Active();status.cause=energy_gate_state.ActiveCause();
    status.explicit_samples=energy_gate_state.explicit_samples;status.guess_samples=energy_gate_state.guess_samples;
    status.guess_frame_id=energy_gate_state.guess_frame_id;
    if(energy_gate_state.explicit_last_ms&&now>=energy_gate_state.explicit_last_ms)
        status.explicit_age_ms=now-energy_gate_state.explicit_last_ms;
    if(energy_gate_state.guess_last_ms&&now>=energy_gate_state.guess_last_ms)
        status.guess_age_ms=now-energy_gate_state.guess_last_ms;
    ReleaseSRWLockShared(&energy_gate_lock);
    status.persistence_failed=energy_gate_persistence_failed.load(std::memory_order_acquire);
    status.explicit_pauses=energy_gate_explicit_pauses.load(std::memory_order_relaxed);
    status.guess_pauses=energy_gate_guess_pauses.load(std::memory_order_relaxed);
    status.resumes=energy_gate_resumes.load(std::memory_order_relaxed);
    status.stale=energy_gate_stale.load(std::memory_order_relaxed);
    status.skipped_sources=energy_gate_skipped_sources.load(std::memory_order_relaxed);
    return status;
}

bool FgRedirectRequested() {
    return static_cast<Start>(requested.load(std::memory_order_acquire))==Start::Present&&
        static_cast<Start>(effective.load(std::memory_order_acquire))==Start::Present &&
        g_fg_presentation_policy.Read().inline_reconstruction;
}
bool FgInlineRedirect() {
    return native_reconstruction_carrier&&carrier_serial&&FgRedirectRequested()&&
        carrier_serial>=fg_after_failure.retry_source;
}
void Reconcile() {
    work_enabled.store(g_config.enabled && !g_config.identity_use_original && std::isfinite(g_config.transfer_strength) && g_config.transfer_strength>0.000001f,std::memory_order_release);
    if(session.Select(g_config.processing_start,g_config.processing_selection)) {
        effective.store(static_cast<unsigned>(session.Current()),std::memory_order_release);
        g_history_generation.fetch_add(1,std::memory_order_relaxed);
    }
    requested.store(static_cast<unsigned>(session.Requested()),std::memory_order_release);
    const bool fg_redirect=FgRedirectRequested();
    if(fg_redirect!=fg_redirect_previous){
        fg_redirect_previous=fg_redirect;
        fg_after_failure={};
        g_history_generation.fetch_add(1,std::memory_order_relaxed);
        const auto fg=g_fg_presentation_policy.Read();
        Log("[processing-FG] requested=%s route=%s reason=%s; saved selection and game FG settings unchanged.",
            nr_processing::Name(session.Requested()),fg_redirect?"native-SR-After-before-FG":"selected-boundary",
            nr_fg_presentation::Name(fg.reason));
    }
}
Start Effective(){
    // Per-carrier pause is a scheduling gate, never a route migration. Returning
    // After while source_blocked is true prevents the Before prepare path from
    // recording private work; the user's real route remains unchanged.
    if(energy_gate_blocked)return Start::After;
    return FgInlineRedirect()?Start::After:static_cast<Start>(effective.load(std::memory_order_acquire));
}
bool TrySnapshot(nr_processing::Snapshot& out) {
    out={};if(!TryEnterCriticalSection(&g_chain_cs))return false;
    out.requested=session.Requested();out.current=FgRedirectRequested()?Start::After:Effective();out.retained=session.Retained();
    std::strncpy(out.reason,FgRedirectRequested()?"FG-present-owned; native-SR-After-before-FG":session.Reason(),sizeof(out.reason)-1);
    LeaveCriticalSection(&g_chain_cs);return true;
}
bool PresentSelected(){return !EnergyGateActive()&&Effective()==Start::Present;}
void MarkRecorded(){private_recorded=true;if(!FgInlineRedirect())session.Hold(Effective());}
void BlockSource(){source_blocked=true;}
bool Recorded(){return private_recorded||source_blocked;}
void Reject(Start route,const char* reason,bool structural=false) {
    if(Recorded())return;
    if(route==Start::After&&FgInlineRedirect()){
        if(fg_after_failure.last_source>=carrier_serial)return;
        fg_after_failure.last_source=carrier_serial;
        if(fg_after_failure.count!=UINT32_MAX)++fg_after_failure.count;
        const unsigned delay=structural&&fg_after_failure.count<3?4u:
            fg_after_failure.count>=3?(8u<<std::min(fg_after_failure.count-3,4u)):0u;
        fg_after_failure.retry_source=carrier_serial>UINT64_MAX-delay?UINT64_MAX:carrier_serial+delay;
        Log("[processing-FG] native-SR After unavailable reason=%s; source=%llu retrySource=%llu. Present surface remains FG-owned.",
            reason?reason:"unknown",carrier_serial,fg_after_failure.retry_source);
        return;
    }
    char prior[128]{};std::strncpy(prior,session.Reason(),sizeof(prior)-1);
    const bool moved=session.Reject(route,carrier_serial,reason,structural);
    if(moved) {
        effective.store(static_cast<unsigned>(session.Current()),std::memory_order_release);
        g_history_generation.fetch_add(1,std::memory_order_relaxed);
    }
    if(moved||std::strcmp(prior,session.Reason())!=0) {
        Log("[processing-route] requested=%s actual=%s reason=%s; possible compatibility bug: press F8 and submit logs. Saved preference retained.",
            nr_processing::Name(session.Requested()),nr_processing::Name(session.Current()),session.Reason());
    }
}
void RejectBeforePresentWork(unsigned long long source,const char* reason){
    const auto old_serial=carrier_serial;const bool old_recorded=private_recorded,old_blocked=source_blocked;
    carrier_serial=source;private_recorded=source_blocked=false;
    Reject(Start::Present,reason,true);
    carrier_serial=old_serial;private_recorded=old_recorded;source_blocked=old_blocked;
}
void Adopted(Start route){
    if(route==Start::After&&FgInlineRedirect()){fg_after_failure={};return;}
    session.Adopted(route);
}
void BeginSource(unsigned long long source) {
    if(session.BeginSource(source)) {
        effective.store(static_cast<unsigned>(session.Current()),std::memory_order_release);
        g_history_generation.fetch_add(1,std::memory_order_relaxed);
    }
}
void MissingInlineAtPresent(unsigned long long source) {
    const auto old_serial=carrier_serial;const bool old_recorded=private_recorded,old_blocked=source_blocked;
    carrier_serial=source;private_recorded=source_blocked=false;
    BeginSource(source);
    if(Effective()==Start::Before)Reject(Start::Before,"native-reconstruction-entry-unavailable");
    if(Effective()==Start::After)Reject(Start::After,"native-reconstruction-entry-unavailable");
    carrier_serial=old_serial;private_recorded=old_recorded;source_blocked=old_blocked;
}
struct CarrierScope {
    bool previous_recorded,previous_blocked;
    bool previous_native_reconstruction,previous_energy_gate;
    unsigned long long previous_serial;
    CarrierScope():previous_recorded(private_recorded),previous_blocked(source_blocked),
        previous_native_reconstruction(native_reconstruction_carrier),previous_energy_gate(energy_gate_blocked),
        previous_serial(carrier_serial) {
        Reconcile();private_recorded=false;source_blocked=false;energy_gate_blocked=false;
        carrier_serial=nr_source_runtime::CurrentRetryToken();
        // ProcessEvaluate constructs this scope only for a real Feature1
        // carrier. RR and Feature11 take their own pass-through/consumer paths.
        native_reconstruction_carrier=true;
        // Never flip work_enabled here: Feature11 must keep running so it can
        // publish the resume evidence. Only private NR scheduling is suppressed.
        if(EnergyGateActive()){
            energy_gate_blocked=true;source_blocked=true;
            energy_gate_skipped_sources.fetch_add(1,std::memory_order_relaxed);
            return;
        }
        // Only a real native carrier is a preferred-route probe opportunity.
        // Present-only callbacks cannot promote a route or fabricate a source.
        // Once armed, Session::Ready stops new fallback work so its last exact
        // fence can drain. No Flush, CPU wait, claim reset or forced retirement.
        if(session.ObserveNativeSource(carrier_serial) &&
           session.ReprobeRequested(nr_compat_present_runtime::InlineDrainReady() &&
                                    nr_compat_present_runtime::NativeNrDrained())) {
            effective.store(static_cast<unsigned>(session.Current()),std::memory_order_release);
            g_history_generation.fetch_add(1,std::memory_order_relaxed);
            Log("[processing-route] requested=%s actual=%s retained=%s reason=%s; native preflight still required.",
                nr_processing::Name(session.Requested()),nr_processing::Name(session.Current()),
                nr_processing::Name(session.Retained()),session.Reason());
        }
        BeginSource(carrier_serial);
    }
    ~CarrierScope(){private_recorded=previous_recorded;source_blocked=previous_blocked;
        native_reconstruction_carrier=previous_native_reconstruction;energy_gate_blocked=previous_energy_gate;
        carrier_serial=previous_serial;}
};
}
