// Production Find + collector + drain + fence predicates. Only OS/NGX services
// are doubles; the new release-admission helper is never stubbed here.
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "nr_guide_contract.h"
#include "nr_dual_pass.hpp"
#include "nr_layer_config.h"
#include "nr_processing_route.h"
struct NgxHandle{unsigned id;};
struct ID3D12Fence{unsigned long long completed=0;};
unsigned long long ID3D12Fence_GetCompletedValue(ID3D12Fence* p){return p->completed;}
struct LifecycleQueue{ID3D12Fence* fence=nullptr;};
enum class CodecColorMode:unsigned{Automatic,LinearSdr,ModelEncodedValueDomain};
enum class NrJitterMode:int{Legacy=0,GameInputPixel=1};
struct Config{float intensity=1.2f,local_tone=1,local_structure=1,skin_structure=.4f;bool auto_mask=true,ui_correction=true,enabled=true;unsigned style=1,nr_input_filter=0;int depth_mode=-1;NrJitterMode jitter_mode=NrJitterMode::GameInputPixel;nr_layers::Auxiliary nr_layers{};}g_config;
#include "tuning.inl"
struct NrContext;
struct NrSlot{void* key=nullptr;NrContext* dual_owner=nullptr;unsigned recording_command_lists=0;unsigned long long completion_fence_value=0;bool awaiting_fence_signal=false,bindings_ok=true;LifecycleQueue* last_submission_queue=nullptr;};
struct NrContext{
 const NgxHandle* sr_handle=nullptr;unsigned long long instance_id=0,parent_instance_id=0,feature_generation=0,parent_input_epoch=0,stage_capture_request_id=0;
 unsigned layer_index=0,width=0,height=0,recording_command_lists=0;int mode=0;
 bool retired=false,create_dependencies_quarantined=false,lifecycle_dependencies_quarantined=false;
 unsigned long long pending_recording_token=0,completion_fence_value=0;bool awaiting_fence_signal=false,bindings_ok=true;
 LifecycleQueue* last_submission_queue=nullptr;NrSlot slots[8]{};
 bool codec_color_mode_initialized=false,tuning_signature_initialized=false,codec_peak_shoulder=false,reset_retry_pending=false;
 CodecColorMode codec_color_mode=CodecColorMode::Automatic;NrTuningSignature tuning_signature{};
 NrGuideSignature guide_storage_signature{};NrGuideHistory guide_history{};nr_dual::AuxHistory dual_history{};
};
NrContext g_contexts[16]{},g_pending_destroy_contexts[16]{};
std::atomic<bool>g_lifecycle_tracking_uncertain{false},g_nr_failed_create_process_latched{false};
std::atomic<unsigned>g_nr_context_releases_in_progress{0},g_history_generation{0};
bool g_nr_residency_drain_pending=false; // Explicit compaction is independently tested.
std::atomic<unsigned long long>g_nr_create_release_deferrals{0},g_aux_parent_retirements{0};
struct Lock{unsigned depth=0;}g_chain_cs;
void EnterCriticalSection(Lock* p){++p->depth;}
void LeaveCriticalSection(Lock* p){assert(p->depth);--p->depth;}
void DrainNgxFeatureRetirementsLocked(){}
void CollectCompletedDeltaStatsLocked(){}
void MarkLifecycleUncertain(const char*){g_lifecycle_tracking_uncertain=true;}
void Log(const char*,...){}
void PrepareStageCaptureContextLocked(const NgxHandle*,unsigned long long,int){}
unsigned long long PreparedStageCaptureRequestId(const NgxHandle*,unsigned long long,int){return 0;}
namespace nr_dual{void RebindSlotOwners(NrContext& c){for(auto& s:c.slots)s.dual_owner=&c;}}
bool CanDestroyBindingsLocked(const NrContext& c){return c.bindings_ok;}
bool InvalidateSlotBindingsLocked(const NrSlot& s){return s.bindings_ok;}
enum class BypassReason{None,LifecycleIsolation,ContextReleasePending};
BypassReason reason=BypassReason::None;
void SetBypassReason(BypassReason r){reason=r;}
namespace nr_processing_runtime{
using Start=nr_processing::Start;nr_processing::Session session;std::atomic<unsigned>effective{1};
bool private_recorded=false,source_blocked=false;unsigned long long carrier_serial=1;nr_processing::Failure fg_after_failure{};
bool FgInlineRedirect(){return false;}Start Effective(){return static_cast<Start>(effective.load());}
#include "route.inl"
}
void DestroyContext(NrContext&);
#include "lifecycle.inl"
#include "collector.inl"
#include "lookup.inl"
static void check(bool b,const char* why){if(!b){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
NgxHandle carriers[]={{1},{2}};NrGuideSignature guides{};std::vector<unsigned long long>released;
bool reenter=false;NrContext* matching_during_release=nullptr;
NrContext* lookup(unsigned w=3840,unsigned h=2160,unsigned layer=0,unsigned long long parent=0,const NgxHandle* handle=&carriers[0]){
 EnterCriticalSection(&g_chain_cs);auto* result=FindOrCreateContext(handle,3,1,w,h,CodecColorMode::Automatic,guides,false,layer,parent,10);LeaveCriticalSection(&g_chain_cs);return result;
}
void DestroyContext(NrContext& c){
 check(!g_chain_cs.depth,"real drain releases outside chain lock");
 if(reenter){
  check(g_nr_context_releases_in_progress==1,"detached release remains counted");
  check(!lookup(3776,2120),"synchronous callback cannot allocate during detached Release");
  if(matching_during_release)check(lookup(640,360,0,0,&carriers[1])==matching_during_release,"matching context bypasses unrelated Release");
 }
 for(auto& s:c.slots)check(s.dual_owner==&c,"owners rebound to detached storage before Release");
 released.push_back(c.instance_id);c={};
}
void clear(){for(auto& c:g_contexts)c={};for(auto& c:g_pending_destroy_contexts)c={};g_config={};released.clear();reenter=false;matching_during_release=nullptr;g_lifecycle_tracking_uncertain=false;g_nr_failed_create_process_latched=false;g_nr_context_releases_in_progress=0;g_nr_residency_drain_pending=false;g_nr_create_release_deferrals=0;g_aux_parent_retirements=0;reason=BypassReason::None;nr_processing_runtime::session={};nr_processing_runtime::session.Select(nr_processing::Start::After,1);nr_processing_runtime::effective=1;nr_processing_runtime::source_blocked=false;}
void collect(){EnterCriticalSection(&g_chain_cs);CollectRetiredContextsLocked();LeaveCriticalSection(&g_chain_cs);}
int main(){
 clear();auto* old=lookup();check(old,"initial context");old->instance_id=100;
 auto* replacement=lookup(3776,2120);
 if(replacement){std::puts("REPRODUCED: ready 3840 model remains unreleased when replacement 3776 context is admitted");return 3;}
 check(released.empty()&&g_pending_destroy_contexts[0].instance_id==100,"ready old model queued, not destroyed under Find lock");
 nr_processing_runtime::Reject(nr_processing::Start::After,"missing-context");
 check(nr_processing_runtime::Effective()==nr_processing::Start::After&&nr_processing_runtime::session.Requested()==nr_processing::Start::After,"deferred source does not fall back to Present or change selection");
 check(reason==BypassReason::ContextReleasePending,"observable deferred reason");
 matching_during_release=lookup(640,360,0,0,&carriers[1]);check(!matching_during_release,"pending queue blocks another allocation");
 // Latest configuration wins after drain; no saved snapshot of the old request.
 g_config.intensity=1.7f;DrainPendingContextDestructions();check(released==std::vector<unsigned long long>{100},"one release before replacement");
 replacement=lookup(2720,1528);check(replacement&&replacement->width==2720&&replacement->tuning_signature.intensity==1.7f,"latest scale/tuning allocated after release");

 clear();old=lookup();old->instance_id=101;matching_during_release=lookup(640,360,0,0,&carriers[1]);check(matching_during_release,"independent valid context");
 check(!lookup(3776,2120),"ready replacement deferred");reenter=true;DrainPendingContextDestructions();reenter=false;
 check(g_nr_context_releases_in_progress==0&&lookup(3776,2120),"release counter returns to zero and allows next admission");

 clear();old=lookup();old->instance_id=102;ID3D12Fence fence{4};LifecycleQueue queue{&fence};
 old->completion_fence_value=5;old->last_submission_queue=&queue;
 replacement=lookup(3776,2120);
 if(replacement){std::puts("REPRODUCED: in-flight retired model still occupies memory when replacement context is admitted");return 3;}
 check(old->retired&&released.empty(),"unfinished fence retains old ownership without CPU wait");
 check(nr_processing_runtime::source_blocked&&reason==BypassReason::ContextReleasePending,"deferred in-flight replacement has an explicit native-only reason");
 check(lookup(640,360,0,0,&carriers[1]),"an unrelated carrier need not wait for an unqueued old model");
 // Rapid request changes must not reserve several replacement models. Only the
 // latest call may create after the exact old GPU/recording lifetime completes.
 for(unsigned request=0;request<32;++request){g_config.intensity=1.2f+.01f*request;check(!lookup(2720+request*2,1528),"in-flight toggles admitted replacement allocations");}
 DrainPendingContextDestructions();check(released.empty(),"unfinished context cannot reach Release");
 fence.completed=UINT64_MAX;check(!lookup(2720,1528),"removed-device fence cannot admit a replacement");collect();DrainPendingContextDestructions();check(released.empty(),"device removed is not completion");
 fence.completed=5;check(!lookup(2720,1528),"completed old model must finish outside-lock Release before replacement");
 DrainPendingContextDestructions();check(released==std::vector<unsigned long long>{102},"own completed fence permits release");
 replacement=lookup(2720,1528);check(replacement&&replacement->tuning_signature.intensity==g_config.intensity,"only latest request admitted after old model release");

 clear();old=lookup();old->instance_id=110;g_config.enabled=false;collect();DrainPendingContextDestructions();
 check(old->sr_handle&&!old->retired&&released.empty(),"F6 OFF alone preserves valid cached model");g_config.enabled=true;check(lookup()==old,"F6 ON reuses matching cache");

 clear();old=lookup();old->instance_id=200;ID3D12Fence child_fence{8};LifecycleQueue child_queue{&child_fence};
 for(unsigned layer=1;layer<5;++layer){auto* c=lookup(3840,2160,layer,200);check(c,"five independent layer contexts");c->instance_id=200+layer;
  if(layer==4){c->completion_fence_value=9;c->last_submission_queue=&child_queue;}}
 check(!lookup(3776,2120),"root resize defers while ready layers await release");DrainPendingContextDestructions();
 check(released.size()==4&&std::find(released.begin(),released.end(),204)==released.end(),"root and three ready children release, pending child survives");
 check(!lookup(3776,2120),"retired child still blocks a replacement after parent was removed");
 child_fence.completed=9;collect();DrainPendingContextDestructions();check(released.size()==5&&released.back()==204,"last child releases only at own fence");
 check(lookup(3776,2120),"replacement becomes possible after last own child lifetime");

 for(unsigned gate=0;gate<10;++gate){clear();old=lookup();old->instance_id=300;old->slots[0].key=old;
  ID3D12Fence own_fence{1};LifecycleQueue own_queue{&own_fence};
  if(gate==0)old->recording_command_lists=1;if(gate==1)old->pending_recording_token=1;if(gate==2)old->awaiting_fence_signal=true;
  if(gate==3)old->bindings_ok=false;if(gate==4)old->slots[0].recording_command_lists=1;
  if(gate==5)old->slots[0].awaiting_fence_signal=true;
  if(gate==6){old->slots[0].completion_fence_value=2;old->slots[0].last_submission_queue=&own_queue;}
  if(gate==7)old->slots[0].bindings_ok=false;
  if(gate==8)old->create_dependencies_quarantined=true;
  if(gate==9)old->lifecycle_dependencies_quarantined=true;
  check(!lookup(3776,2120),"blocked or quarantined retirement must not overlap a replacement");collect();DrainPendingContextDestructions();check(released.empty(),"existing recording/signal/binding gate retained");
  old->recording_command_lists=0;old->pending_recording_token=0;old->awaiting_fence_signal=false;old->bindings_ok=true;
  old->slots[0].recording_command_lists=0;old->slots[0].awaiting_fence_signal=false;old->slots[0].bindings_ok=true;own_fence.completed=2;
  if(gate>=8){check(!lookup(3776,2120),"opaque quarantine must never be cleared by admission");continue;}
  check(!lookup(3776,2120),"ready release is still deferred outside the selector lock");DrainPendingContextDestructions();check(lookup(3776,2120),"completed lifecycle recovers without timed retry");
 }
 clear();old=lookup();old->instance_id=400;auto* matching=lookup(640,360,0,0,&carriers[1]);check(matching,"matching independent carrier");
 old->recording_command_lists=1;check(!lookup(3776,2120),"old source is blocked");check(lookup(640,360,0,0,&carriers[1])==matching,"unrelated matching model remains usable");
 // Same carrier across feature generation must not bypass the old allocation.
 clear();old=lookup();old->instance_id=401;old->pending_recording_token=1;
 EnterCriticalSection(&g_chain_cs);replacement=FindOrCreateContext(&carriers[0],4,1,3840,2160,CodecColorMode::Automatic,guides);LeaveCriticalSection(&g_chain_cs);
 check(!replacement&&old->retired,"carrier generation change bypassed retirement pressure");
 // A valid cached route for the SAME carrier may still be reused: this guard
 // limits new allocations, it is not a policy that destroys inactive routes.
 clear();old=lookup();old->instance_id=450;
 auto other_mode=[](){EnterCriticalSection(&g_chain_cs);auto* c=FindOrCreateContext(&carriers[0],3,2,640,360,CodecColorMode::Automatic,guides);LeaveCriticalSection(&g_chain_cs);return c;};
 auto* cached_route=other_mode();check(cached_route,"second route initially admitted");cached_route->instance_id=451;
 old->pending_recording_token=1;check(!lookup(3776,2120),"old route replacement deferred");check(other_mode()==cached_route,"already allocated same-carrier route must remain reusable");
 clear();g_nr_failed_create_process_latched=true;check(!lookup(),"failed Create quarantine is unchanged");
 clear();g_lifecycle_tracking_uncertain=true;check(!lookup(),"unknown lifecycle still rejects admission");
 std::puts("PASS real Find/collector/drain: ready and in-flight release ordering, detached reentry, matching reuse, ten own lifetime blockers, F6 cache, five layers, native route retention and latest settings");
}
