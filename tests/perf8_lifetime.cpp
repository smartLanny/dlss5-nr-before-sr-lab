// Actual frozen/current production state tables and token lookup bodies.
// The caller-owned lock stays unchanged; this fixture tests value/index semantics.
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <shared_mutex>
struct SRWLOCK{std::shared_mutex value;};
#define SRWLOCK_INIT {}
static void AcquireSRWLockShared(SRWLOCK* p){p->value.lock_shared();}
static void ReleaseSRWLockShared(SRWLOCK* p){p->value.unlock_shared();}
static void AcquireSRWLockExclusive(SRWLOCK* p){p->value.lock();}
static void ReleaseSRWLockExclusive(SRWLOCK* p){p->value.unlock();}
#endif
#include <algorithm>
#include <atomic>
#include <thread>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <random>
#include <string>
#include <vector>
#include "nr_source_frame.hpp"
using nr_source_frame::Id;using nr_source_frame::Recording;using nr_source_frame::Resource;
namespace baseline {
std::atomic<unsigned long long> work{0};
#include "old_state.inl"
struct CommandStateRecord {unsigned long long token=0;};CommandStateRecord g_command_states[2048]{};
#include "old_token.inl"
}
namespace candidate {
std::atomic<unsigned long long> work{0};
#include "current_state.inl"
struct CommandStateRecord {unsigned long long token=0;};CommandStateRecord g_command_states[2048]{};
#include "current_token.inl"
}
static void check(bool value,const char* message){if(!value){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}}
static Recording rec(unsigned i,unsigned serial=1){return {Id(0x10000+i*16),Id(1+i%7),serial,Id(1+i%3)};}
static Resource res(unsigned i){return {Id(0x20000+i*32),Id(1+i%5),Id(1+i%3),i%4};}
static void compare(const baseline::SourceStateTable& a,const candidate::SourceStateTable& b){
 for(unsigned i=0;i<320;++i)for(unsigned serial=1;serial<4;++serial){unsigned x=0xaabbccdd,y=x;
  bool ax=a.Get(rec(i,serial),res(i),x),bx=b.Get(rec(i,serial),res(i),y);check(ax==bx&&x==y,"exact-key state/absence changed");}
 unsigned x=19,y=x;check(a.Get({}, {},x)==b.Get({}, {},y)&&x==y,"empty-key legacy diagnostic changed");
}
static void semantics(){
 baseline::SourceStateTable a;candidate::SourceStateTable b;
 for(unsigned i=0;i<300;++i)check(a.Put(rec(i),res(i),i)==b.Put(rec(i),res(i),i),"capacity admission changed");
 compare(a,b);
 // Thousands of small Reset transactions with unrelated live state present.
 for(unsigned round=0;round<2000;++round){unsigned i=round%256;const auto r=rec(i);
  a.EraseListLifetime(r);b.EraseListLifetime(r);
  check(a.Put(r,res(i),round)==b.Put(r,res(i),round),"reset slot reuse");
  if(round%37==0)compare(a,b);
 }
 // Colliding list hashes, different reset/device generations and exact erasure.
 for(unsigned round=0;round<12000;++round){
  static std::mt19937 rng(0x800277);unsigned i=rng()%320,serial=1+rng()%3;auto r=rec(i,serial);auto v=res(i);
  switch(rng()%10){
   case 0:a.Erase(r,v);b.Erase(r,v);break;
   case 1:a.EraseListLifetime(r);b.EraseListLifetime(r);break;
   case 2:a.EraseRecording(r);b.EraseRecording(r);break;
   case 3:a.EraseResourceAddress(v.address);b.EraseResourceAddress(v.address);break;
   case 4:a.EraseListAddress(r.list);b.EraseListAddress(r.list);break;
   default:check(a.Put(r,v,round)==b.Put(r,v,round),"mixed publication/capacity");break;
  }
  if(round%17==0)compare(a,b);
 }
 compare(a,b);
 // Many resources of one list, plus other lifetimes sharing its address.
 a={};b={};
 for(unsigned i=0;i<256;++i){auto r=rec(0,1+i%3);r.lifetime=1+i%2;r.device=1+i%4;
  check(a.Put(r,res(i),i)==b.Put(r,res(i),i),"multi-resource insertion");}
 auto r=rec(0);r.lifetime=1;a.EraseListLifetime(r);b.EraseListLifetime(r);
 for(unsigned i=0;i<256;++i){auto q=rec(0,1+i%3);q.lifetime=1+i%2;q.device=1+i%4;
  unsigned x=0,y=0;check(a.Get(q,res(i),x)==b.Get(q,res(i),y)&&x==y,"list/lifetime unlink skipped collision");}
 // Copy assignment preserves relative membership, hash positions and first-free.
 auto c=b;auto oldcopy=a;
 for(unsigned i=0;i<256;++i){c.EraseResourceAddress(res(i).address);oldcopy.EraseResourceAddress(res(i).address);}
 for(unsigned i=0;i<300;++i)check(c.Put(rec(i),res(i),i)==oldcopy.Put(rec(i),res(i),i),"copied table capacity");
 for(unsigned i=0;i<2048;++i){baseline::g_command_states[i].token=candidate::g_command_states[i].token=10000+i;}
 for(unsigned round=0;round<4000;++round){unsigned slot=(round*37)%2048;auto token=candidate::g_command_states[slot].token;
  auto* x=baseline::FindCommandStateTokenLocked(token);auto* y=candidate::FindCommandStateTokenLocked(token);
  check(x&&y&&x-baseline::g_command_states==y-candidate::g_command_states,"token lookup differed");
  baseline::g_command_states[slot].token=candidate::g_command_states[slot].token=100000+round;
  check(!candidate::FindCommandStateTokenLocked(token),"retired token survived hint");
  auto fresh=100000+round;check(candidate::FindCommandStateTokenLocked(fresh)==&candidate::g_command_states[slot],"new token in reused slot");
 }
 check(!candidate::FindCommandStateTokenLocked(0),"zero token admitted");
 std::puts("PASS 12000 mixed lifetime operations, 2000 live-table resets, clustered erasure, copying, 4000 token slot generations");
}
template<class T> static unsigned long long reset_work(T& t,std::atomic<unsigned long long>& work){
 for(unsigned i=0;i<256;++i)t.Put(rec(i),res(i),i);
 work=0;
 for(unsigned i=0;i<1000;++i){auto r=rec(i%256);t.EraseListLifetime(r);check(t.Put(r,res(i%256),i),"reset lost capacity");}
 return work;
}
static int budget(){
 candidate::SourceStateTable t;auto count=reset_work(t,candidate::work);
 std::printf("live-table Reset work=%llu (budget 18000, hash/member/cluster tests)\n",count);
 for(unsigned i=0;i<32;++i)candidate::g_command_states[2016+i].token=7000+i;
 for(unsigned i=0;i<32;++i)(void)candidate::FindCommandStateTokenLocked(7000+i);
 candidate::work=0;
 for(unsigned round=0;round<1000;++round)for(unsigned i=0;i<32;++i)
  check(candidate::FindCommandStateTokenLocked(7000+i)==&candidate::g_command_states[2016+i],"warm token result");
 std::printf("warm token full-table comparisons=%llu (budget 0)\n",candidate::work.load());
 return count<=18000&&candidate::work==0?0:3;
}
static void bench(){
 for(unsigned mode=0;mode<3;++mode){
  baseline::SourceStateTable a;candidate::SourceStateTable b;
  for(unsigned i=0;i<256;++i){a.Put(rec(i),res(i),i);b.Put(rec(i),res(i),i);}
  auto run=[&](auto& table){auto begin=std::chrono::steady_clock::now();unsigned long long sum=0;
   for(unsigned round=0;round<20000;++round){unsigned i=round%256;
    if(mode==0){table.EraseListLifetime(rec(i));check(table.Put(rec(i),res(i),round),"timing capacity");unsigned state=0;check(table.Get(rec(i),res(i),state),"timing state");sum+=state;}
    else if(mode==1){table.EraseListLifetime(rec(512+round%512));sum+=round&1u;}
    else {unsigned state=0;table.Get(rec(i),res(i),state);sum+=state;}}
   std::printf(" checksum=%llu",sum);return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();};
  double before=run(a),after=run(b);std::printf(" BENCH mode=%u old_ms=%.4f new_ms=%.4f\n",mode,before,after);
 }
}
static void concurrent_tokens(){
 SRWLOCK lock=SRWLOCK_INIT;std::atomic<bool> good{true};
 candidate::g_command_states[1024].token=900000;
 std::vector<std::thread> readers;
 for(unsigned t=0;t<4;++t)readers.emplace_back([&]{for(unsigned i=0;i<10000;++i){
  AcquireSRWLockShared(&lock);auto token=candidate::g_command_states[1024].token;
  auto* found=candidate::FindCommandStateTokenLocked(token);
  if(token?(found!=&candidate::g_command_states[1024]):(found!=nullptr))good=false;
  ReleaseSRWLockShared(&lock);}});
 for(unsigned i=0;i<4000;++i){AcquireSRWLockExclusive(&lock);candidate::g_command_states[1024].token=i%7?900001+i:0;ReleaseSRWLockExclusive(&lock);}
 for(auto& thread:readers)thread.join();check(good.load(),"concurrent live token identity");
 std::puts("PASS 40000 locked token lookups across 4000 retire/republish transactions (native Windows SRW when built on Windows)");
}
int main(int argc,char**argv){std::string mode=argc>1?argv[1]:"semantics";if(mode=="--budget")return budget();if(mode=="--bench"){bench();return 0;}semantics();concurrent_tokens();return 0;}
