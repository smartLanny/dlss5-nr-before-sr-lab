// Actual production collector and lifetime predicates; CPU service doubles.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
struct NgxHandle{};
struct ID3D12Fence{unsigned long long completed=0;};
unsigned long long ID3D12Fence_GetCompletedValue(ID3D12Fence* p){return p->completed;}
struct LifecycleQueue{ID3D12Fence* fence=nullptr;};
struct NrContext;
struct NrSlot{void* key=nullptr;NrContext* dual_owner=nullptr;unsigned long long dual_bytes=0,completion_fence_value=0;unsigned recording_command_lists=0;bool awaiting_fence_signal=false,bindings_ok=true;LifecycleQueue* last_submission_queue=nullptr;};
struct NrContext{
 const NgxHandle* sr_handle=nullptr;unsigned long long instance_id=0,parent_instance_id=0,feature_generation=0;
 unsigned layer_index=0,width=0,height=0,recording_command_lists=0;int mode=1;
 bool retired=false,create_dependencies_quarantined=false,lifecycle_dependencies_quarantined=false,post_ready=true;
 unsigned long long pending_recording_token=0,completion_fence_value=0;bool awaiting_fence_signal=false,bindings_ok=true;
 LifecycleQueue* last_submission_queue=nullptr;NrSlot slots[8]{};
};
NrContext g_contexts[16]{},g_pending_destroy_contexts[16]{};
std::atomic<bool> g_lifecycle_tracking_uncertain{false};
std::atomic<unsigned long long> g_aux_parent_retirements{0},g_dual_allocated_bytes{0};
unsigned temporal_maintains=0,post_maintains=0,spatial_sweeps=0,post_ready_checks=0;
// The isolated collector uses lifecycle service doubles. The full Core links
// the real modules; this fixture observes their calls and models a refusal.
namespace nr_temporal {void Maintain(NrContext&){++temporal_maintains;}}
namespace nr_post_product {
bool Ready(const NrContext& context){++post_ready_checks;return context.post_ready;}
void Maintain(NrContext&){++post_maintains;}
}
namespace nr_spatial_experiment {void SweepIdle(){++spatial_sweeps;}}
struct Lock{unsigned depth=0;}g_chain_cs;
void EnterCriticalSection(Lock* p){++p->depth;}
void LeaveCriticalSection(Lock* p){if(!p->depth)std::abort();--p->depth;}
void DrainNgxFeatureRetirementsLocked(){}
void CollectCompletedDeltaStatsLocked(){}
// No native COM objects in these parent/residency CPU services. Actual native
// destruction and its recording ownership are covered by the full-Core host.
void CollectDeadNativeRecordingsLocked(){}
void MarkLifecycleUncertain(const char*){g_lifecycle_tracking_uncertain=true;}
template<class...A>void Log(const char*,A...){}
namespace nr_dual{void RebindSlotOwners(NrContext& c){for(auto& s:c.slots)s.dual_owner=&c;}}
bool CanDestroyBindingsLocked(const NrContext& c){return c.bindings_ok;}
bool InvalidateSlotBindingsLocked(const NrSlot& s){return s.bindings_ok;}
#include "lifecycle.inl"
#include "collector.inl"
#if !BASELINE
#include "snapshot.inl"
#endif
NgxHandle carriers[3];
void check(bool b,const char* why){if(!b){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
void clear(){for(auto& c:g_contexts)c={};for(auto& c:g_pending_destroy_contexts)c={};g_lifecycle_tracking_uncertain=false;g_aux_parent_retirements=0;g_dual_allocated_bytes=0;temporal_maintains=post_maintains=spatial_sweeps=post_ready_checks=0;}
void setup(unsigned r=7,unsigned c=2){clear();auto& p=g_contexts[r];p.sr_handle=&carriers[0];p.instance_id=101;p.feature_generation=9;
 auto& a=g_contexts[c];a.sr_handle=p.sr_handle;a.instance_id=102;a.parent_instance_id=101;a.layer_index=1;a.feature_generation=9;
 a.slots[0].key=&a;a.slots[0].dual_owner=&a;a.slots[0].dual_bytes=100;g_dual_allocated_bytes=100;}
unsigned scheduled(unsigned long long id){unsigned n=0;for(auto& c:g_pending_destroy_contexts)n+=c.sr_handle&&c.instance_id==id;return n;}
int reproduce(){setup();g_contexts[7].retired=true;CollectRetiredContextsLocked();
 std::printf("root scheduled=%u auxiliary scheduled=%u orphan live=%u\n",scheduled(101),scheduled(102),g_contexts[2].sr_handle&&!g_contexts[2].retired);
 check(scheduled(101)==1,"root retirement changed");return scheduled(102)==1?0:3;}
void unchanged(){setup();CollectRetiredContextsLocked();check(g_contexts[2].sr_handle&&!g_contexts[2].retired,"live parent retired; F6 OFF must retain valid context");
 for(unsigned k=0;k<5;++k){setup();g_contexts[7].retired=true;auto& c=g_contexts[2];if(k==0)c.parent_instance_id=999;if(k==1)c.feature_generation=10;if(k==2)c.mode=2;if(k==3)c.sr_handle=&carriers[1];if(k==4)c.layer_index=0;CollectRetiredContextsLocked();check(c.sr_handle&&!c.retired,"unrelated scope retired");}
 setup();g_lifecycle_tracking_uncertain=true;g_contexts[7].retired=true;CollectRetiredContextsLocked();check(g_contexts[7].sr_handle&&g_contexts[2].sr_handle&&!scheduled(101),"uncertain lifetime discarded");}
#if !BASELINE
void safety(){
 for(unsigned k=0;k<11;++k){setup();g_contexts[7].retired=true;auto& c=g_contexts[2];ID3D12Fence f{4};LifecycleQueue q{&f};
  switch(k){case 0:c.recording_command_lists=1;break;case 1:c.pending_recording_token=123;break;case 2:c.awaiting_fence_signal=true;break;case 3:c.completion_fence_value=5;c.last_submission_queue=&q;break;case 4:c.completion_fence_value=5;break;case 5:c.slots[0].recording_command_lists=1;break;case 6:c.slots[0].awaiting_fence_signal=true;break;case 7:c.slots[0].completion_fence_value=5;c.slots[0].last_submission_queue=&q;break;case 8:c.bindings_ok=false;break;case 9:c.slots[0].bindings_ok=false;break;case 10:c.post_ready=false;break;}
  CollectRetiredContextsLocked();check(c.sr_handle&&c.retired&&!scheduled(102)&&scheduled(101)==1,"premature child release");
  check(temporal_maintains>0&&post_maintains>0&&spatial_sweeps>0,"collector skipped module maintenance");
  if(k==10)check(post_ready_checks>0,"post readiness was not consulted");
  auto count=g_aux_parent_retirements.load();CollectRetiredContextsLocked();check(g_aux_parent_retirements==count,"double counted retirement");
  f.completed=UINT64_MAX;CollectRetiredContextsLocked();check(c.sr_handle,"removed-device fence accepted");
  f.completed=5;c.recording_command_lists=0;c.pending_recording_token=0;c.awaiting_fence_signal=false;c.completion_fence_value=0;c.bindings_ok=true;c.post_ready=true;c.slots[0].recording_command_lists=0;c.slots[0].awaiting_fence_signal=false;c.slots[0].bindings_ok=true;
  CollectRetiredContextsLocked();check(!c.sr_handle&&scheduled(102)==1,"ready child stuck after parent detached");
 }
 for(unsigned k=0;k<2;++k){setup();g_contexts[7].retired=true;auto& c=g_contexts[2];if(k)c.lifecycle_dependencies_quarantined=true;else c.create_dependencies_quarantined=true;CollectRetiredContextsLocked();check(c.retired&&c.sr_handle&&!scheduled(102),"quarantine released");}
 for(unsigned r=0;r<16;++r)for(unsigned c=0;c<16;++c)if(r!=c){setup(r,c);g_contexts[r].retired=true;CollectRetiredContextsLocked();check(scheduled(101)==1&&scheduled(102)==1,"array-order leak");}
 setup();for(auto& p:g_pending_destroy_contexts)p.sr_handle=&carriers[2];g_contexts[7].retired=true;CollectRetiredContextsLocked();check(g_lifecycle_tracking_uncertain&&g_contexts[7].sr_handle&&g_contexts[2].sr_handle,"full pending queue discarded resources");
 for(unsigned n=0;n<200;++n){clear();auto& p=g_contexts[7];p.sr_handle=&carriers[0];p.instance_id=1000+n*10;p.feature_generation=9;p.retired=true;auto id=p.instance_id;
  for(unsigned i=0;i<4;++i){auto& c=g_contexts[i];c.sr_handle=p.sr_handle;c.instance_id=id+i+1;c.parent_instance_id=id;c.layer_index=i+1;c.feature_generation=9;c.slots[0].dual_bytes=106037248;}
  CollectRetiredContextsLocked();check(scheduled(id)==1&&g_aux_parent_retirements==4,"multilayer cascade lost");for(unsigned i=0;i<4;++i)check(scheduled(id+i+1)==1,"dependent layer unscheduled");
  for(auto& c:g_pending_destroy_contexts)if(c.layer_index)check(c.slots[0].dual_owner==&c,"owner not rebound after move");
 }
 setup();auto a=SnapshotNrContextMemory();check(a.live_primary==1&&a.live_auxiliary==1&&a.live_slots==1&&a.live_aux_bytes==100&&a.tracked_aux_bytes==100&&!g_chain_cs.depth,"live ledger");
 g_contexts[7].retired=true;g_contexts[2].recording_command_lists=1;CollectRetiredContextsLocked();auto b=SnapshotNrContextMemory();check(b.pending_primary==1&&b.retired_auxiliary==1&&b.retired_aux_bytes==100&&b.parent_retirements==1,"retired ledger");
 g_contexts[2].recording_command_lists=0;CollectRetiredContextsLocked();auto c=SnapshotNrContextMemory();check(c.pending_auxiliary==1&&c.pending_aux_bytes==100&&!c.live_slots&&!c.retired_slots,"pending ledger");
 g_dual_allocated_bytes=400;auto d=SnapshotNrContextMemory();check(d.tracked_aux_bytes==400&&d.pending_aux_bytes==100,"global/unlisted distinction");
 std::puts("PASS 11 own-release gates including post readiness, maintenance calls, quarantine, 240 slot orders, 200 four-child cascades, memory categories");
}
#endif
int main(int argc,char** argv){if(argc>1&&std::strcmp(argv[1],"--reproduction")==0)return reproduce();unchanged();
#if !BASELINE
 safety();
#endif
 std::puts("PASS unchanged live-parent, scope and uncertain-lifetime behavior");return 0;}
