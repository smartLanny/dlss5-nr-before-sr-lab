// Actual extracted Core/source identity functions with real Windows SRW locks.
// The COM objects are doubles; alias/record lifecycle and query budgets are real
// function logic, not a second implementation of the candidate lookup.
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <mutex>
#include <shared_mutex>
using ULONGLONG=unsigned long long;
struct SRWLOCK{std::shared_mutex mutex;};
#define SRWLOCK_INIT {}
static void AcquireSRWLockShared(SRWLOCK* p){p->mutex.lock_shared();}
static void ReleaseSRWLockShared(SRWLOCK* p){p->mutex.unlock_shared();}
static void AcquireSRWLockExclusive(SRWLOCK* p){p->mutex.lock();}
static void ReleaseSRWLockExclusive(SRWLOCK* p){p->mutex.unlock();}
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>
#include <thread>
#include <vector>
#include "nr_source_frame.hpp"
using nr_source_frame::Id;using nr_source_frame::Recording;
struct ID3D12GraphicsCommandList{};struct ID3D12DeviceChild{};
namespace nr_post_boundary{struct List{Id address=0,token=0,reset=0,device=0;bool known=false;};}
#define NR_HAS_DESCRIPTOR_PROVENANCE 1
#define IDENTITY_STUBS \
unsigned long long observer_probes=0,device_probes=0,device_calls=0;std::atomic<unsigned long long> snapshot_probes{0}; \
SRWLOCK source_lock=SRWLOCK_INIT,g_command_state_lock=SRWLOCK_INIT; \
std::atomic<Id> g_command_state_epoch{1}; \
struct CommandStateRecord{ID3D12GraphicsCommandList* native=nullptr;Id token=0,native_resets=0;bool lifetime_ready=false;struct{Id device_identity=0;}meta_contract;}; \
CommandStateRecord g_command_states[2048]{}; \
struct NrPostBoundaryAliasScope{NrPostBoundaryAliasScope* parent=nullptr;Id original=0,epoch=0;nr_post_boundary::List identity;}; \
thread_local NrPostBoundaryAliasScope* g_nr_post_boundary_alias_scope=nullptr; \
CommandStateRecord* FindCommandStateTokenLocked(Id token){for(auto& r:g_command_states)if(r.token==token)return &r;return nullptr;} \
struct Lock{Lock(){AcquireSRWLockExclusive(&source_lock);}~Lock(){ReleaseSRWLockExclusive(&source_lock);}}; \
Id DeviceOf(ID3D12DeviceChild*){++device_calls;return 77;}
namespace baseline{
IDENTITY_STUBS
#include "old_snapshot.inl"
#include "old_observer.inl"
#include "old_recording.inl"
}
namespace candidate{
IDENTITY_STUBS
#include "current_snapshot.inl"
#include "current_observer.inl"
#include "current_recording.inl"
}
#undef IDENTITY_STUBS
static void check(bool b,const char* m){if(!b){std::fprintf(stderr,"FAIL %s\n",m);std::exit(1);}}
static ID3D12GraphicsCommandList* ptr(unsigned i){return reinterpret_cast<ID3D12GraphicsCommandList*>(Id(0x100000)+Id(i)*16);}
#define SETUP(ns) \
for(unsigned i=0;i<8;++i){auto& r=ns::g_command_states[2040+i];r.native=ptr(i);r.token=100+i;r.native_resets=200+i*2;r.lifetime_ready=true;r.meta_contract.device_identity=9; \
 auto& s=ns::observed_lists[248+i];s.native=ptr(i);s.lifetime=1000+i;s.device=77;s.reset=1;s.ready=true;}
static bool equal(nr_post_boundary::List a,nr_post_boundary::List b){return a.address==b.address&&a.token==b.token&&a.reset==b.reset&&a.device==b.device&&a.known==b.known;}
static void compare(unsigned i){
 check(equal(baseline::SnapshotPostBoundaryList(ptr(i)),candidate::SnapshotPostBoundaryList(ptr(i))),"snapshot identity");
 baseline::Lock a;candidate::Lock b;
 check(baseline::RecordingOf(ptr(i))==candidate::RecordingOf(ptr(i)),"recording canonical/observer identity");
}
static int budget(){
 SETUP(candidate)
 {candidate::Lock lock;for(unsigned i=0;i<8;++i)(void)candidate::RecordingOf(ptr(i));}
 candidate::observer_probes=candidate::device_probes=candidate::snapshot_probes=0;
 for(unsigned round=0;round<2000;++round){candidate::Lock lock;
  for(unsigned i=0;i<8;++i){auto r=candidate::RecordingOf(ptr(i));check(r.list==Id(ptr(i))&&r.lifetime==100+i&&r.device==77,"budget identity");}}
 std::printf("identity probes: snapshot=%llu observer=%llu device=%llu (budgets 1024/1024/64000)\n",
  candidate::snapshot_probes.load(),candidate::observer_probes,candidate::device_probes);
 candidate::device_calls=0;
 for(unsigned i=0;i<1000;++i)candidate::SeedCoreRecordingDevice(ptr(i%8));
 std::printf("warm Evaluate device COM queries=%llu (budget 0)\n",candidate::device_calls);
 return candidate::snapshot_probes<=1024&&candidate::observer_probes<=1024&&candidate::device_probes<=64000&&candidate::device_calls==0?0:3;
}
static void semantics(){
 SETUP(baseline) SETUP(candidate)
 for(unsigned i=0;i<8;++i)compare(i);
 // Reset/readiness changes without registry epoch changes must be read freshly.
 for(unsigned i=0;i<8;++i){
  ++baseline::g_command_states[2040+i].native_resets;++candidate::g_command_states[2040+i].native_resets;compare(i);
  baseline::g_command_states[2040+i].lifetime_ready=candidate::g_command_states[2040+i].lifetime_ready=false;compare(i);
  baseline::g_command_states[2040+i].lifetime_ready=candidate::g_command_states[2040+i].lifetime_ready=true;
  baseline::observed_lists[248+i].ready=candidate::observed_lists[248+i].ready=false;compare(i);
  baseline::AdvanceObservedList(ptr(i));candidate::AdvanceObservedList(ptr(i));compare(i);
  baseline::ForgetObservedList(Id(ptr(i)));candidate::ForgetObservedList(Id(ptr(i)));compare(i);
  baseline::observed_lists[248+i]={ptr(i),3000+i,77,8,0,true,false};
  candidate::observed_lists[248+i]={ptr(i),3000+i,77,8,0,true,false};compare(i);
 }
 // TLS caches must not retain alias scopes even when the native miss is cached.
 auto alias=ptr(50);auto identity=baseline::SnapshotPostBoundaryList(ptr(2));
 baseline::NrPostBoundaryAliasScope a{nullptr,Id(alias),baseline::g_command_state_epoch.load(),identity};
 candidate::NrPostBoundaryAliasScope b{nullptr,Id(alias),candidate::g_command_state_epoch.load(),identity};
 baseline::g_nr_post_boundary_alias_scope=&a;candidate::g_nr_post_boundary_alias_scope=&b;
 check(equal(baseline::SnapshotPostBoundaryList(alias),candidate::SnapshotPostBoundaryList(alias)),"current alias hit");
 a.identity=baseline::SnapshotPostBoundaryList(ptr(3));b.identity=a.identity;
 check(equal(baseline::SnapshotPostBoundaryList(alias),candidate::SnapshotPostBoundaryList(alias)),"alias rebinding after negative hint");
 baseline::g_nr_post_boundary_alias_scope=nullptr;candidate::g_nr_post_boundary_alias_scope=nullptr;
 check(!candidate::SnapshotPostBoundaryList(alias).known,"alias not retained beyond scope");
 // Same address is a new generation after record retirement/republishing.
 baseline::g_command_states[2042]={};candidate::g_command_states[2042]={};
 ++baseline::g_command_state_epoch;++candidate::g_command_state_epoch;compare(2);
 baseline::g_command_states[123]={ptr(2),9000,0,true,{99}};candidate::g_command_states[123]={ptr(2),9000,0,true,{99}};
 ++baseline::g_command_state_epoch;++candidate::g_command_state_epoch;compare(2);
 // The source device cookie is queried only while seeding an unseen lifetime.
 baseline::ForgetObservedList(Id(ptr(2)));candidate::ForgetObservedList(Id(ptr(2)));
 baseline::SeedCoreRecordingDevice(ptr(2));candidate::SeedCoreRecordingDevice(ptr(2));
 candidate::device_calls=0;for(unsigned i=0;i<1000;++i)candidate::SeedCoreRecordingDevice(ptr(2));
 std::printf("repeated cold seed device queries=%llu (optimized target 0)\n",candidate::device_calls);
 std::puts("PASS native/alias identity, observer ready/reset, retirement/address reuse and cold device seeding");
}
static void concurrent_snapshot(){
 // Exact production snapshot under native SRW locks while records are rewritten.
 // Checksummed fields must always come from the same writer transaction.
 candidate::g_command_states[2040].native=ptr(0);candidate::g_command_states[2040].lifetime_ready=true;
 candidate::g_command_states[2040].token=1;candidate::g_command_states[2040].native_resets=2;
 candidate::g_command_states[2040].meta_contract.device_identity=9;++candidate::g_command_state_epoch;
 std::atomic<bool> good{true};std::vector<std::thread> threads;
 for(unsigned t=0;t<4;++t)threads.emplace_back([&]{for(unsigned n=0;n<10000;++n){const auto r=candidate::SnapshotPostBoundaryList(ptr(0));if(!r.known||r.reset!=r.token*2)good=false;}});
 for(unsigned n=2;n<5000;++n){AcquireSRWLockExclusive(&candidate::g_command_state_lock);
  candidate::g_command_states[2040].token=n;candidate::g_command_states[2040].native_resets=n*2;++candidate::g_command_state_epoch;
  ReleaseSRWLockExclusive(&candidate::g_command_state_lock);}
 for(auto& t:threads)t.join();check(good.load(),"shared/exclusive lifetime consistency");
#if defined(_WIN32)
 std::puts("PASS real Windows SRW: 40000 snapshots across 4998 record generation changes");
#else
 std::puts("PASS portable lock double: 40000 snapshots across 4998 generation changes; not native SRW");
#endif
}
int main(int argc,char**argv){if(argc>1&&std::string(argv[1])=="--budget")return budget();semantics();concurrent_snapshot();}
