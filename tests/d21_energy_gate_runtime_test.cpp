// Real production scheduling adapter; only OS/config/source services are host
// substitutes. No NR implementation or GPU result is simulated as validation.
#include "../src/nr_processing_route.h"
#include "../src/nr_fg_presentation.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>

using SRWLOCK=std::mutex;
#define SRWLOCK_INIT {}
constexpr int FALSE=0;
std::atomic<unsigned long long> host_tick{100},clock_reads{0},lock_reads{0},ini_reads{0};
unsigned long long GetTickCount64(){++clock_reads;return host_tick.load();}
void AcquireSRWLockExclusive(SRWLOCK* lock){++lock_reads;lock->lock();}
void ReleaseSRWLockExclusive(SRWLOCK* lock){lock->unlock();}
int saved_conservative=-1,saved_heuristic=-1;
unsigned GetPrivateProfileIntW(const wchar_t*,const wchar_t* key,int value,const wchar_t*){++ini_reads;const int saved=std::wcscmp(key,L"PauseNrOnFullscreenUi")==0?saved_conservative:saved_heuristic;return unsigned(saved<0?value:saved);}
int WritePrivateProfileStringW(const wchar_t*,const wchar_t*,const wchar_t*,const wchar_t*){return 1;}
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
namespace nr_source_runtime {
bool CurrentEnergyPaused(){return nr_processing_runtime::EnergyGateBlocks({1,10,2});}
}
static void Check(bool value){if(!value)std::abort();}
int main() {
    namespace R=nr_processing_runtime;
    R::energy_current_query=nr_source_runtime::CurrentEnergyPaused;
    const nr_energy_gate::StreamKey a{1,10,2},b{1,11,2};
    const auto clocks=clock_reads.load(),locks=lock_reads.load();
    for(unsigned i=0;i<10000;++i)Check(!R::EnergyGateBlocks(a));
    Check(clock_reads==clocks&&lock_reads==locks&&ini_reads==0);
    R::EnsureEnergyGateLoaded();
    Check(!R::EnergyGateOptions().conservative&&!R::EnergyGateOptions().heuristic);
    R::ObserveEnergyPause(a,100,3,true,true,10);
    host_tick=120;R::ObserveEnergyPause(a,100,3,true,true,10);
    Check(!R::EnergyGateBlocks(a));
    saved_conservative=saved_heuristic=1;R::energy_gate_loaded=false;R::EnsureEnergyGateLoaded();
    Check(R::EnergyGateOptions().conservative&&R::EnergyGateOptions().heuristic);
    saved_conservative=saved_heuristic=0;R::energy_gate_loaded=false;R::EnsureEnergyGateLoaded();
    Check(!R::EnergyGateOptions().conservative&&!R::EnergyGateOptions().heuristic);
    Check(R::SetEnergyGateOption(true,true));host_tick=100;
    R::ObserveEnergyPause(a,100,3,true,true,10);
    Check(R::ReadEnergyGateStatus().explicit_samples==1&&!R::ReadEnergyGateStatus().active);
    host_tick=120;R::ObserveEnergyPause(a,100,3,true,true,10);
    Check(R::EnergyGateBlocks(a)&&!R::EnergyGateBlocks(b));
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
    host_tick=10000;R::ObserveEnergyPause(a,100,3,true,true,400);
    host_tick=10020;R::ObserveEnergyPause(a,100,3,true,true,400);
    const auto retired=g_history_generation.load();
    R::RetireEnergyObserver(100,3);
    Check(!R::EnergyGateBlocks(a)&&g_history_generation==retired+1);
    const auto after_clocks=clock_reads.load(),after_locks=lock_reads.load();
    for(unsigned i=0;i<10000;++i)Check(!R::EnergyGateBlocks(a));
    Check(clock_reads==after_clocks&&lock_reads==after_locks);
    std::cout<<"PASS production energy adapter: zero-evidence fast path, route preservation, source isolation, concurrent resume/reset and retirement\n";
}
