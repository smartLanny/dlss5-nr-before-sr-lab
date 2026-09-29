// Real production scheduling adapter; only OS/config/source services are host
// substitutes. No NR implementation or GPU result is simulated as validation.
#include "../src/nr_processing_route.h"
#include "../src/nr_fg_presentation.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <iostream>
#include <mutex>
#include <thread>

using SRWLOCK=std::mutex;
#define SRWLOCK_INIT {}
constexpr int FALSE=0;
std::atomic<unsigned long long> host_tick{100},clock_reads{0},lock_reads{0},ini_reads{0},ini_writes{0};
unsigned long long GetTickCount64(){++clock_reads;return host_tick.load();}
void AcquireSRWLockExclusive(SRWLOCK* lock){++lock_reads;lock->lock();}
void ReleaseSRWLockExclusive(SRWLOCK* lock){lock->unlock();}
bool TryAcquireSRWLockShared(SRWLOCK* lock){++lock_reads;return lock->try_lock();}
void ReleaseSRWLockShared(SRWLOCK* lock){lock->unlock();}
int saved_conservative=-1,saved_heuristic=-1;
unsigned GetPrivateProfileIntW(const wchar_t*,const wchar_t* key,int value,const wchar_t*){++ini_reads;const int saved=std::wcscmp(key,L"PauseNrOnFullscreenUi")==0?saved_conservative:saved_heuristic;return unsigned(saved<0?value:saved);}
int WritePrivateProfileStringW(const wchar_t*,const wchar_t*,const wchar_t*,const wchar_t*){++ini_writes;return 1;}
wchar_t g_ini_path[]=L"host-ini";
struct Config {bool enabled=true,identity_use_original=false;float transfer_strength=1;
    nr_processing::Start processing_start=nr_processing::Start::Before;
    unsigned long long processing_selection=1;}g_config;
std::atomic<unsigned> g_history_generation{1};
nr_fg_presentation::Policy g_fg_presentation_policy;
int g_chain_cs=0;
bool TryEnterCriticalSection(int*){return true;}
void LeaveCriticalSection(int*){}
void Log(const char*,...){}
namespace nr_source_runtime {
unsigned long long token=1;
unsigned long long CurrentRetryToken(){return token;}
bool CurrentEnergyPaused();
}
namespace nr_compat_present_runtime {
bool InlineDrainReady(){return true;}
bool NativeNrDrained(){return true;}
}
#include "../src/nr_processing_runtime.inl"
#include "energy-runtime-under-test.inl"
using ULONGLONG=unsigned long long;
namespace reshade {namespace api {struct effect_runtime;}}
#include "energy-feedback-under-test.inl"
namespace nr_source_runtime {
bool CurrentEnergyPaused(){return nr_processing_runtime::EnergyGateBlocks({1,10,2});}
}
static void Check(bool value){if(!value)std::abort();}
static std::string Feedback() {
    namespace R=nr_processing_runtime;
    const auto history=g_history_generation.load();const auto ini=ini_reads.load();
    const auto writes=ini_writes.load();
    const auto blocked=R::energy_gate_blocked_checks.load(),resets=R::energy_gate_resets.load();
    const auto loaded=R::energy_gate_loaded.load(),active=R::energy_gate_any_active.load();
    static FeedbackSession report;report={};
    FeedbackAppendEnergyGate(report,"sample");
    Check(!report.report_truncated&&g_history_generation==history&&ini_reads==ini&&ini_writes==writes);
    Check(R::energy_gate_blocked_checks==blocked&&R::energy_gate_resets==resets);
    Check(R::energy_gate_loaded==loaded&&R::energy_gate_any_active==active);
    return report.report;
}
static void Contains(const std::string& text,const char* expected){Check(text.find(expected)!=std::string::npos);}
int main() {
    namespace R=nr_processing_runtime;
    R::energy_current_query=nr_source_runtime::CurrentEnergyPaused;
    const nr_energy_gate::StreamKey a{1,10,2},b{1,11,2};
    const auto clocks=clock_reads.load(),locks=lock_reads.load();
    for(unsigned i=0;i<10000;++i)Check(!R::EnergyGateBlocks(a));
    Check(clock_reads==clocks&&lock_reads==locks&&ini_reads==0);
    const auto cold=Feedback();Contains(cold,"energyGate.loaded=0");
    Contains(cold,"state=not-loaded");Check(cold.find("energyGate.config=")==std::string::npos);
    R::EnsureEnergyGateLoaded();
    Check(!R::EnergyGateOptions().conservative&&!R::EnergyGateOptions().heuristic);
    Contains(Feedback(),"energyGate.state=off");
    R::ObserveEnergyPause(a,100,3,true,true,10);
    host_tick=120;R::ObserveEnergyPause(a,100,3,true,true,10);
    Check(!R::EnergyGateBlocks(a));
    saved_conservative=saved_heuristic=1;R::energy_gate_loaded=false;R::EnsureEnergyGateLoaded();
    Check(R::EnergyGateOptions().conservative&&R::EnergyGateOptions().heuristic);
    saved_conservative=saved_heuristic=0;R::energy_gate_loaded=false;R::EnsureEnergyGateLoaded();
    Check(!R::EnergyGateOptions().conservative&&!R::EnergyGateOptions().heuristic);
    Check(R::SetEnergyGateOption(true,true));host_tick=100;
    Contains(Feedback(),"energyGate.state=waiting-for-evidence");
    R::ObserveEnergyPause(a,100,3,true,false,1);
    auto observed=R::ReadEnergyGateStatus();
    Check(observed.last_signal==1&&observed.pause_false==1&&!observed.active);
    R::ObserveEnergyPause({},100,3,false,false,0);
    observed=R::ReadEnergyGateStatus();Check(observed.last_signal==4&&observed.unbound==1);
    Contains(Feedback(),"observations=total:2,pauseTrue:0,pauseFalse:1,unknown:1,unbound:1,lastSignal:4");
    Check(!R::EnergyGateBlocks(a)); // Diagnostic observations never fabricate a pause.
    R::ObserveEnergyPause(a,100,3,true,true,10);
    Contains(Feedback(),"energyGate.state=confirming");
    Check(R::ReadEnergyGateStatus().explicit_samples==1&&!R::ReadEnergyGateStatus().active);
    host_tick=120;R::ObserveEnergyPause(a,100,3,true,true,10);
    Check(R::EnergyGateBlocks(a)&&!R::EnergyGateBlocks(b));
    const auto paused=Feedback();Contains(paused,"energyGate.state=paused-recorded");
    Contains(paused,"energyGate.event.1=atMs:120,transition:1,history:");
    Check(R::energy_gate_event_count==1);
    const auto event_before_snapshot=R::energy_gate_event_count;
    (void)Feedback();Check(R::energy_gate_event_count==event_before_snapshot);
    Contains(paused,"recorded=active:1,streams:1,explicit:1,heuristic:0,confirming:0");
    Contains(paused,"blockedChecks counts rejected scheduling checks");
    const auto selection=g_config.processing_selection;
    {R::CarrierScope scope;
        Check(R::Recorded()&&R::source_blocked&&!R::private_recorded);
        Check(R::Effective()==nr_processing::Start::Before);
        Check(R::work_enabled&&g_config.enabled); // SR/FG evidence can continue.
        R::Reject(nr_processing::Start::Before,"host must not advance route");
        Check(R::session.Requested()==nr_processing::Start::Before);
    }
    Check(g_config.processing_selection==selection&&!R::source_blocked);
    const auto history=g_history_generation.load();
    R::ObserveEnergyPause(a,100,3,true,false,11);
    Check(!R::EnergyGateBlocks(a)&&g_history_generation==history+1);
    Contains(Feedback(),"energyGate.state=recently-resumed");
    Check(!R::ReadEnergyGateStatus().active&&R::ReadEnergyGateStatus().recently_resumed);

    // Real adapter locking: one paused stream must never block another. Resume
    // records a history generation before any admission can observe reopening.
    std::atomic<bool> stop{false};
    std::thread observer([&]{while(!stop)Check(!R::EnergyGateBlocks(b));});
    const auto before=g_history_generation.load();
    for(unsigned i=0;i<200;++i) {
        host_tick=1000+static_cast<unsigned long long>(i)*30;
        R::ObserveEnergyPause(a,100,3,true,true,20+i);
        host_tick.fetch_add(20);
        R::ObserveEnergyPause(a,100,3,true,true,20+i);
        Check(R::EnergyGateBlocks(a));
        R::ObserveEnergyPause(a,100,3,true,false,20+i);
        Check(!R::EnergyGateBlocks(a)&&g_history_generation==before+i+1);
    }
    stop=true;observer.join();
    R::EnergyGateSnapshot timeline{};
    Check(R::TrySnapshotEnergyGateStatus(timeline)&&timeline.event_count>timeline.events.size());
    const auto& latest=timeline.events[(timeline.event_count-1)%timeline.events.size()];
    Check(latest.sequence==timeline.event_count&&latest.transition==3&&latest.history==g_history_generation.load());
    for(unsigned long long i=timeline.event_count-timeline.events.size();i<timeline.event_count;++i)
        Check(timeline.events[i%timeline.events.size()].sequence==i+1);
    host_tick=10000;R::ObserveEnergyPause(a,100,3,true,true,400);
    host_tick=10020;R::ObserveEnergyPause(a,100,3,true,true,400);
    const auto retired=g_history_generation.load();
    R::RetireEnergyObserver(100,3);
    Check(!R::EnergyGateBlocks(a)&&g_history_generation==retired+1);
    const auto after_clocks=clock_reads.load(),after_locks=lock_reads.load();
    for(unsigned i=0;i<10000;++i)Check(!R::EnergyGateBlocks(a));
    Check(clock_reads==after_clocks&&lock_reads==after_locks);
    // F8 must leave an expired/reversed-clock lease recorded until the real
    // scheduling consumer polls it. It cannot fabricate a resumed history.
    host_tick=14000;R::ObserveEnergyPause(a,100,4,true,true,500);
    host_tick=14020;R::ObserveEnergyPause(a,100,4,true,true,500);
    host_tick=17000;
    const auto expired=Feedback();Contains(expired,"state=paused-recorded-expiry-pending");
    Contains(expired,"staleStreams:1,maxObservationAgeMs:2980,clockReversed:0");
    const auto expiry_history=g_history_generation.load();
    Check(!R::EnergyGateBlocks(a)&&g_history_generation==expiry_history+1);
    host_tick=18000;R::ObserveEnergyPause(a,100,4,true,true,501);
    host_tick=18020;R::ObserveEnergyPause(a,100,4,true,true,501);
    host_tick=17900;
    const auto reversed=Feedback();Contains(reversed,"clockReversed:1");
    Contains(reversed,"state=paused-recorded-expiry-pending");
    Check(!R::EnergyGateBlocks(a));
    R::SetEnergyGateOption(true,false);R::SetEnergyGateOption(false,true);
    for(unsigned i=0;i<4;++i){host_tick=21000+i*100;R::ObserveEnergyPause(a,100,4,false,false,700);}
    const auto guess=Feedback();Contains(guess,"recorded=active:1,streams:1,explicit:0,heuristic:1");
    Contains(guess,"config=conservative:0,heuristic:1");
    std::atomic<bool> holding{false},release{false};
    std::thread busy([&]{std::lock_guard lock(R::energy_gate_lock);holding=true;while(!release)std::this_thread::yield();});
    while(!holding)std::this_thread::yield();
    const auto omitted=Feedback();release=true;busy.join();
    Contains(omitted,"energyGate.available=0");Contains(omitted,"omitted=lock-busy; state unknown");
    Check(omitted.find("energyGate.recorded=")==std::string::npos&&omitted.find("energyGate.config=")==std::string::npos);
    R::SetEnergyGateOption(false,false);
    const auto disabled=Feedback();Contains(disabled,"config=conservative:0,heuristic:0");
    Contains(disabled,"recorded=active:0");
    std::cout<<"PASS production energy adapter: zero-evidence fast path, route preservation, source isolation, concurrent resume/reset and retirement\n";
    std::cout<<"PASS actual F8 writer: unloaded/off/waiting/confirmation/explicit/heuristic/resume/stale/reversed-clock/busy; read-only history and admission\n";
}
