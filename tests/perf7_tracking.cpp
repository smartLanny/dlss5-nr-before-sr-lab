// Real extracted OnBarrier/StateOf bodies, with CPU-only COM identity doubles.
// End-to-end native ABI/resource contracts remain covered by the WARP gates.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>
#include "nr_source_frame.hpp"
using nr_source_frame::Id;using nr_source_frame::Recording;using nr_source_frame::Resource;
struct ID3D12GraphicsCommandList{};struct ID3D12Resource{};
constexpr unsigned D3D12_RESOURCE_BARRIER_TYPE_TRANSITION=0,D3D12_RESOURCE_BARRIER_TYPE_ALIASING=1,
 D3D12_RESOURCE_BARRIER_TYPE_UAV=2,D3D12_RESOURCE_BARRIER_FLAG_NONE=0,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES=~0u;
struct D3D12_RESOURCE_BARRIER {unsigned Type=0,Flags=0;
 struct {ID3D12Resource* pResource=nullptr;unsigned Subresource=~0u,StateBefore=0,StateAfter=0;} Transition;};
namespace nr_processing_runtime{std::atomic<bool> work_enabled{true};}
namespace baseline {
#include "tracking_stubs.inl"
#include "tracking_old.inl"
}
namespace candidate {
#include "tracking_stubs.inl"
#include "tracking_current.inl"
}
static void check(bool b,const char* m){if(!b){std::fprintf(stderr,"FAIL %s\n",m);std::exit(1);}}
static ID3D12Resource* ptr(unsigned i){return reinterpret_cast<ID3D12Resource*>(Id(0x10000+i*64));}
template<class Table> void reset_table(Table& t){t={};}
template<class Entry,std::size_t N> void reset_table(Entry (&t)[N]){for(auto& e:t)e={};}
template<class Table> void retire(Table& t,unsigned op,Recording r,Resource v){
 if constexpr(requires{t.EraseListLifetime(r);}) {
  if(op==0)t.EraseListLifetime(r);else if(op==1)t.EraseResourceAddress(v.address);else t.EraseListAddress(r.list);
 }else for(auto& e:t)if(op==0?(e.recording.list==r.list&&e.recording.lifetime==r.lifetime):op==1?e.resource.address==v.address:e.recording.list==r.list)e={};
}
static void compare(){
 check(baseline::lineage.invalidations==candidate::lineage.invalidations,"alias/enhanced taint unchanged");
 for(unsigned n=1;n<280;n+=7) {
  for(Recording r: {baseline::recording,Recording{9,9,9,9},Recording{}}) {
   Resource res{Id(ptr(n)),1,1,0};unsigned a=0xabcdef,b=a;
   bool x=baseline::StateOf(r,res,a),y=candidate::StateOf(r,res,b);
   check(x==y&&a==b,"whole-resource known state differs");
  }
 }
}
static void semantic_tests(){
 reset_table(baseline::resource_states);reset_table(candidate::resource_states);
 ID3D12GraphicsCommandList list;std::mt19937 rng(277007);std::array<D3D12_RESOURCE_BARRIER,256> bs{};
 // Fill past capacity, change already-present keys, and fail closed on new keys.
 for(unsigned i=1;i<=300;++i){bs[0].Transition.pResource=ptr(i);bs[0].Transition.StateAfter=i;
  baseline::OnBarrier(&list,1,bs.data());candidate::OnBarrier(&list,1,bs.data());compare();}
 for(unsigned round=0;round<3500;++round){
  const unsigned count=rng()%65;baseline::recording=candidate::recording=Recording{1+round%3,1+round%2,1+(round/300),1};
  baseline::resource_generation=candidate::resource_generation=1+round/1000;
  for(unsigned i=0;i<count;++i){auto& b=bs[i];b.Type=rng()%40==0?1:rng()%3==0?2:0;
   b.Flags=rng()%12==0?1:0;b.Transition.pResource=(rng()%31==0)?nullptr:ptr(1+rng()%280);
   b.Transition.Subresource=rng()%13==0?3:~0u;b.Transition.StateAfter=rng()%128;}
  nr_processing_runtime::work_enabled.store(round%19!=0);
  baseline::OnBarrier(&list,count,bs.data());candidate::OnBarrier(&list,count,bs.data());
  if(round%73==0){baseline::OnEnhancedBarrier(&list);candidate::OnEnhancedBarrier(&list);}
  if(round%37==0){const unsigned op=round%3;Resource res{Id(ptr(1+round%280)),1,1,0};
   retire(baseline::resource_states,op,baseline::recording,res);retire(candidate::resource_states,op,candidate::recording,res);}
  compare();
 }
 nr_processing_runtime::work_enabled.store(true);
 baseline::OnBarrier(nullptr,1,bs.data());candidate::OnBarrier(nullptr,1,bs.data());
 baseline::OnBarrier(&list,257,bs.data());candidate::OnBarrier(&list,257,bs.data());compare();
 std::puts("PASS actual barrier bodies: capacity, mixed/UAV/alias, split/subresource, reset/lifetime/retirement and disabled phases");
}
static int work_budget(){
 ID3D12GraphicsCommandList list;std::array<D3D12_RESOURCE_BARRIER,256> bs{};
 candidate::recording={1,1,1,1};candidate::resource_generation=1;reset_table(candidate::resource_states);
 for(unsigned i=0;i<256;++i){bs[i].Transition.pResource=ptr(i+1);bs[i].Transition.StateAfter=i;}
 candidate::OnBarrier(&list,256,bs.data());
 for(unsigned i=0;i<256;++i)bs[i].Transition.pResource=ptr(253+i%4);
 candidate::state_probes=candidate::resource_queries=0;
 candidate::OnBarrier(&list,256,bs.data());
 std::printf("actual mixed-key state probes=%llu resource identity queries=%llu (budgets 2048/8)\n",
  candidate::state_probes,candidate::resource_queries);
 bool ok=candidate::state_probes<=2048&&candidate::resource_queries<=8;
 for(auto& b:bs)b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;
 candidate::lock_calls=candidate::recording_queries=0;
 for(unsigned i=0;i<1000;++i)candidate::OnBarrier(&list,256,bs.data());
 std::printf("pure UAV calls preserve identity publication: locks=%llu recording queries=%llu (required 1000/1000)\n",candidate::lock_calls,candidate::recording_queries);
 return ok&&candidate::lock_calls==1000&&candidate::recording_queries==1000?0:3;
}
static void bench(){
 ID3D12GraphicsCommandList list;std::array<D3D12_RESOURCE_BARRIER,256> bs{};
 reset_table(baseline::resource_states);reset_table(candidate::resource_states);
 baseline::recording=candidate::recording={1,1,1,1};baseline::resource_generation=candidate::resource_generation=1;
 for(unsigned i=0;i<256;++i){bs[i].Transition.pResource=ptr(i+1);bs[i].Transition.StateAfter=i;}
 baseline::OnBarrier(&list,256,bs.data());candidate::OnBarrier(&list,256,bs.data());
 for(unsigned mode=0;mode<4;++mode){
  const unsigned count=mode==3?1:256;
  for(unsigned i=0;i<256;++i){bs[i].Type=mode==2?2:0;bs[i].Transition.pResource=ptr(mode==0?253+i%4:i+1);}
  auto start=std::chrono::steady_clock::now();
  for(unsigned r=0;r<2000;++r)baseline::OnBarrier(&list,count,bs.data());
  double old_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  start=std::chrono::steady_clock::now();
  for(unsigned r=0;r<2000;++r)candidate::OnBarrier(&list,count,bs.data());
  double now_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  std::printf("BENCH actual barrier mode=%u old_ms=%.4f new_ms=%.4f\n",mode,old_ms,now_ms);
 }
}
int main(int argc,char**argv){std::string mode=argc>1?argv[1]:"semantics";
 if(mode=="--budget")return work_budget();if(mode=="--bench"){bench();return 0;}semantic_tests();}
