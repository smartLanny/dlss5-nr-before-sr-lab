// Actual snapshot function under real Windows SRW or portable shared_mutex.
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <shared_mutex>
struct SRWLOCK{std::shared_mutex mutex;};
#define SRWLOCK_INIT {}
static void AcquireSRWLockShared(SRWLOCK* p){p->mutex.lock_shared();}
static void ReleaseSRWLockShared(SRWLOCK* p){p->mutex.unlock_shared();}
static void AcquireSRWLockExclusive(SRWLOCK* p){p->mutex.lock();}
static void ReleaseSRWLockExclusive(SRWLOCK* p){p->mutex.unlock();}
#endif
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>
using Id=std::uint64_t;
struct ID3D12GraphicsCommandList{};
namespace nr_post_boundary{struct List{Id address=0,token=0,reset=0,device=0;bool known=false;};}
#define NR_HAS_DESCRIPTOR_PROVENANCE 1
#define ENVIRONMENT \
SRWLOCK g_command_state_lock=SRWLOCK_INIT; \
std::atomic<Id> g_command_state_epoch{1},snapshot_probes{0}; \
struct CommandStateRecord{ID3D12GraphicsCommandList* native=nullptr;Id token=0,native_resets=0;bool lifetime_ready=false;struct{Id device_identity=0;}meta_contract;}; \
CommandStateRecord g_command_states[2048]{}; \
struct NrPostBoundaryAliasScope{NrPostBoundaryAliasScope* parent=nullptr;Id original=0,epoch=0;nr_post_boundary::List identity;}; \
thread_local NrPostBoundaryAliasScope* g_nr_post_boundary_alias_scope=nullptr; \
CommandStateRecord* FindCommandStateTokenLocked(Id t){if(!t)return nullptr;for(auto& r:g_command_states)if(r.token==t)return &r;return nullptr;}
namespace baseline{ENVIRONMENT
#include "snapshot_old.inl"
}
namespace candidate{ENVIRONMENT
#include "snapshot_current.inl"
}
#undef ENVIRONMENT
static void check(bool b,const char* what){if(!b){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
static auto pointers(){std::array<ID3D12GraphicsCommandList*,8> p{};unsigned n=0;
 for(Id a=0x100000;n<p.size();a+=16)if((((a>>4)^(a>>12)^(a>>20))&15u)==0)p[n++]=reinterpret_cast<ID3D12GraphicsCommandList*>(a);return p;}
static const auto p=pointers();
static bool same(nr_post_boundary::List a,nr_post_boundary::List b){return a.address==b.address&&a.token==b.token&&a.reset==b.reset&&a.device==b.device&&a.known==b.known;}
static void setup(){for(unsigned i=0;i<8;++i){baseline::g_command_states[2040+i]={p[i],100+i,200+i,true,{9}};candidate::g_command_states[2040+i]={p[i],100+i,200+i,true,{9}};}++baseline::g_command_state_epoch;++candidate::g_command_state_epoch;}
static void compare(){for(auto* ptr:p)check(same(baseline::SnapshotPostBoundaryList(ptr),candidate::SnapshotPostBoundaryList(ptr)),"current snapshot differs");}
static void semantics(){setup();compare();
 for(unsigned i=0;i<8;++i){++baseline::g_command_states[2040+i].native_resets;++candidate::g_command_states[2040+i].native_resets;compare();
  baseline::g_command_states[2040+i].lifetime_ready=candidate::g_command_states[2040+i].lifetime_ready=false;compare();
  baseline::g_command_states[2040+i].lifetime_ready=candidate::g_command_states[2040+i].lifetime_ready=true;compare();}
 // More colliders than ways falls back; no eviction of actual records/state.
 for(unsigned n=0;n<200;++n)compare();
 // Negative lookup may gain an actual call-bounded alias at the SAME epoch.
 auto* alias=reinterpret_cast<ID3D12GraphicsCommandList*>(Id(0x990000));
 check(!candidate::SnapshotPostBoundaryList(alias).known,"native miss");
 auto id=baseline::SnapshotPostBoundaryList(p[2]);
 baseline::NrPostBoundaryAliasScope a{nullptr,Id(alias),baseline::g_command_state_epoch.load(),id};
 candidate::NrPostBoundaryAliasScope b{nullptr,Id(alias),candidate::g_command_state_epoch.load(),id};
 baseline::g_nr_post_boundary_alias_scope=&a;candidate::g_nr_post_boundary_alias_scope=&b;
 check(same(baseline::SnapshotPostBoundaryList(alias),candidate::SnapshotPostBoundaryList(alias)),"alias after negative");
 a.identity=baseline::SnapshotPostBoundaryList(p[3]);b.identity=a.identity;
 check(same(baseline::SnapshotPostBoundaryList(alias),candidate::SnapshotPostBoundaryList(alias)),"alias rebind");
 ++baseline::g_command_state_epoch;++candidate::g_command_state_epoch;
 check(!candidate::SnapshotPostBoundaryList(alias).known,"alias expiry on epoch");
 baseline::g_nr_post_boundary_alias_scope=nullptr;candidate::g_nr_post_boundary_alias_scope=nullptr;
 check(!candidate::SnapshotPostBoundaryList(alias).known,"alias not retained");
 for(unsigned n=0;n<2000;++n){unsigned i=n%8;baseline::g_command_states[2040+i]={};candidate::g_command_states[2040+i]={};++baseline::g_command_state_epoch;++candidate::g_command_state_epoch;compare();
  baseline::g_command_states[2040+i]={p[i],10000+n,n*2,true,{1+n%3}};candidate::g_command_states[2040+i]={p[i],10000+n,n*2,true,{1+n%3}};
  ++baseline::g_command_state_epoch;++candidate::g_command_state_epoch;compare();}
 std::puts("PASS collision/overflow, live Reset/readiness, scoped alias rebind/expiry, 2000 retired/native-address generations");
}
static void concurrency(){setup();std::atomic<bool> ok{true};std::vector<std::thread> readers;
 candidate::g_command_states[2040].token=1;candidate::g_command_states[2040].native_resets=2;++candidate::g_command_state_epoch;
 for(unsigned t=0;t<4;++t)readers.emplace_back([&]{for(unsigned n=0;n<5000;++n){auto r=candidate::SnapshotPostBoundaryList(p[0]);if(!r.known||r.reset!=r.token*2)ok=false;}});
 for(unsigned i=2;i<2000;++i){AcquireSRWLockExclusive(&candidate::g_command_state_lock);candidate::g_command_states[2040].token=i;candidate::g_command_states[2040].native_resets=i*2;++candidate::g_command_state_epoch;ReleaseSRWLockExclusive(&candidate::g_command_state_lock);}
 for(auto& t:readers)t.join();check(ok,"SRW snapshot transaction");std::puts("PASS 20000 locked snapshots across 1998 epoch changes; native Windows SRW on Windows");}
static int budget(){setup();for(unsigned i=0;i<4;++i)(void)candidate::SnapshotPostBoundaryList(p[i]);candidate::snapshot_probes=0;
 for(unsigned n=0;n<1000;++n)for(unsigned i=0;i<4;++i)check(candidate::SnapshotPostBoundaryList(p[i]).token==100+i,"hot collision token");
 auto scans=candidate::snapshot_probes.load();std::printf("four colliding lists, 4000 hot calls: registry predicates=%llu\n",static_cast<unsigned long long>(scans));return scans==0?0:3;}
static void bench(){setup();for(unsigned count:{1u,4u,8u}){Id sums[2]{};double times[2]{};
 for(unsigned v=0;v<2;++v){auto start=std::chrono::steady_clock::now();for(unsigned n=0;n<12000;++n){auto r=v?candidate::SnapshotPostBoundaryList(p[n%count]):baseline::SnapshotPostBoundaryList(p[n%count]);sums[v]+=r.token;}
 times[v]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
 check(sums[0]==sums[1],"timing identity checksum");std::printf("snapshot lists=%u old_ms=%.3f new_ms=%.3f checksum=%llu\n",count,times[0],times[1],static_cast<unsigned long long>(sums[0]));}}
int main(int argc,char** argv){std::string mode=argc>1?argv[1]:"";if(mode=="--budget")return budget();if(mode=="--bench"){bench();return 0;}semantics();concurrency();return 0;}
