#include "src/nr_rr_projection_source.h"
// Executes the actual ProcessRrPost implementation using a CPU API seam.
// No Windows/NGX or game-runtime claim. Checks ordering, exact native result,
// no retry, retained feature identity, and requested Mode2 never overwritten.
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#if defined(NR_RR_TIMING_TEST)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <climits>
#include "src/nr_runtime_timing.h"
#include "src/nr_runtime_timing.inl"
// Existing RR boundary doubles remain explicit. The timing implementation
// above uses real Win32 locks and QPC, and never creates a D3D device.
#define GetTickCount64 RrTestGetTickCount64
#define TryEnterCriticalSection RrTestTryEnterCriticalSection
#define LeaveCriticalSection RrTestLeaveCriticalSection
#endif
#include "src/interop/nr_post_compat.h"
#include "src/nr_processing_route.h"
#include "src/nr_fg_presentation.h"
using NgxResult=std::int32_t;
struct ID3D12GraphicsCommandList{};struct NgxHandle{};struct NgxParams{};
#if defined(NR_RR_TIMING_TEST)
struct ID3D12Device { unsigned references=1; } timing_device;
constexpr int IID_ID3D12Device=1;
unsigned timing_device_queries=0;
bool timing_device_available=true;
std::atomic<unsigned long long> g_config_generation{39};
HRESULT ID3D12GraphicsCommandList_GetDevice(ID3D12GraphicsCommandList*,int,void** out){
 ++timing_device_queries;
 if(!timing_device_available){*out=nullptr;return E_NOINTERFACE;}
 ++timing_device.references;*out=&timing_device;return S_OK;
}
unsigned ID3D12Device_Release(ID3D12Device* device){return --device->references;}
unsigned long long NrFormatDeviceIdentity(ID3D12Device*){return 19;}
#endif
struct FeatureRecord{const NgxHandle* handle=nullptr;int feature_id=-1;unsigned long long generation=0;int rr_depth_type=1;};
void ObserveRrDepthProjection(const NgxParams*, const FeatureRecord&) {}
using EvaluateFeatureFn=NgxResult(*)(ID3D12GraphicsCommandList*,const NgxHandle*,const NgxParams*,void*);
constexpr NgxResult kNgxSuccess=1;
struct Config{
 bool enabled=true,identity_use_original=false;int mode=2;float post_transfer_strength=1,transfer_strength=1;
 nr_processing::Start processing_start=nr_processing::Start::Before;
 unsigned long long processing_selection=1;
} g_config;
std::atomic<unsigned long long> g_history_generation{0};
nr_fg_presentation::Policy g_fg_presentation_policy;
std::atomic<int> g_last_carrier_feature{-1};std::atomic<bool> g_rr_guard_was_active{false};
std::atomic<unsigned long long> g_rr_passthrough_evaluations{0},g_carrier_evaluations{0},g_pipeline_eligible_carriers{0};
std::atomic<bool> g_feature_tracking_uncertain{false},g_lifecycle_tracking_uncertain{false};
namespace nr_post_settings{
 struct Options{bool rr=true;}; Options options;
 Options Read(){return options;}
 std::atomic<unsigned long long> rr_attempts{0},rr_recorded{0},rr_native_failures{0},last_rr_tick{0};
 std::atomic<unsigned long long> rr_before_state_rejected{0},rr_prepare_declined{0};
 std::atomic<unsigned long long> rr_after_state_rejected{0},rr_finish_declined{0},rr_finish_gate_rejected{0};
 std::atomic<int> last_rr_stage{0}, last_rr_depth_type{-1};
}
enum class BypassReason{RayReconstructionGuard,Feature1TransactionBusy};
void SetBypassReason(BypassReason reason){
#if defined(NR_RR_TIMING_TEST)
 NrTimingBypass(static_cast<unsigned>(reason));
#else
 (void)reason;
#endif
}
unsigned long long GetTickCount64(){return 10;}
bool owner=false,gate=true,nested=false,chain=true,prepare_ok=true,finish_ok=true,before_ok=true,after_ok=true;
std::atomic<int> g_rr_ownership_gate_owner{0};
unsigned concurrent_aborts=0;
void LatchConcurrentRrOwnershipChange(const char*) { ++concurrent_aborts;g_feature_tracking_uncertain=true; }
int g_chain_cs=0,chain_depth=0,gate_depth=0,native_calls=0,finishes=0,prepares=0,quarantines=0,scopes=0,restores=0,stage=0;
int abandons=0;
bool change_generation=false,change_identity=false,change_uncertain=false,finish_uncertain=false,opaque_failure=false;
NgxResult opaque_result=-77;
NgxResult result=1;FeatureRecord current;
#if defined(NR_RR_TIMING_TEST)
NgxResult nr_result=1;
unsigned nr_calls=0;
#endif
ID3D12GraphicsCommandList command;NgxHandle handle;NgxParams params;int callback;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);std::exit(1);}}while(0)
bool OtherNrOwnerActive(){return owner;}
void TrackDlssResolution(const NgxParams*,const FeatureRecord& f){CHECK(f.feature_id==13);}
void FeedbackObserveEvaluate(const NgxParams*,const FeatureRecord&,unsigned long long){}
struct ScopedRrOwnershipGate{
 bool acquired_=false;
 ScopedRrOwnershipGate(bool enabled,bool feature1,bool try_only){CHECK(enabled&&!feature1&&try_only);acquired_=gate;if(acquired_)++gate_depth;}
 bool acquired(){return acquired_;}bool is_nested(){return nested;}
 void Release(){if(acquired_){--gate_depth;acquired_=false;}}
 ~ScopedRrOwnershipGate(){Release();}
};
bool TryEnterCriticalSection(int*){if(chain){++chain_depth;return true;}return false;}
void LeaveCriticalSection(int*){--chain_depth;}
void BeginPipelineStatsScopeLocked(unsigned long long generation,int mode){
 CHECK(generation==7&&mode==1);
#if defined(NR_RR_TIMING_TEST)
 NrTimingSetRoute(static_cast<unsigned>(mode),g_config_generation.load());
#endif
}
struct NrSlot {} timing_slot;
namespace nr_dual_runtime {
unsigned stamp_calls=0;
void Stamp(ID3D12GraphicsCommandList*,NrSlot&,unsigned){++stamp_calls;}
}
struct NativeCommandStateScope{
 bool active=false;ID3D12GraphicsCommandList* command_list;
 NativeCommandStateScope(ID3D12GraphicsCommandList* c):command_list(c){active=(scopes++==0)?before_ok:after_ok;}
 void Restore(){++restores;}
 void AbandonAfterOpaqueFailure(){++abandons;active=false;}
};
struct PostSrSubstitution{bool active=false;NgxResult fatal_recording=0;NrSlot* slot=nullptr;};
PostSrSubstitution PrepareAfterSr(ID3D12GraphicsCommandList*,const NgxHandle*,const NgxParams*,const FeatureRecord& f){
 ++prepares;CHECK(stage==0&&f.feature_id==13&&g_config.mode==2);
 PostSrSubstitution prepared{};prepared.active=prepare_ok;prepared.slot=&timing_slot;return prepared;
}
bool FinishAfterSr(ID3D12GraphicsCommandList*,const NgxParams*,const FeatureRecord& f,PostSrSubstitution& p){
 ++finishes;CHECK(stage==1&&result==1&&p.active&&f.feature_id==13&&g_config.mode==2);
 if(finish_uncertain) g_lifecycle_tracking_uncertain=true;
#if defined(NR_RR_TIMING_TEST)
 // The opaque NR callback is the only scripted model boundary. The production
 // timing wrapper must inherit the real RR carrier without a recovery scope.
 const auto actual_result=NrTimingCallNr([]{++nr_calls;return nr_result;});
 if(actual_result!=1){p.fatal_recording=actual_result;return false;}
#endif
 if(opaque_failure){p.fatal_recording=opaque_result;return false;}
 return finish_ok;
}
FeatureRecord FindFeature(const NgxHandle*){return current;}
void QuarantineAbortedRrTransactionLocked(ID3D12GraphicsCommandList*){++quarantines;}
void Log(const char*,...){}
// Use the actual sticky route implementation. Only the source identity input
// is a fixture value; this test does not observe native frame ownership.
namespace nr_source_runtime {unsigned long long CurrentRetryToken(){return 1;}}
// CPU route fixture has no Present worker, native NR flight, fence, or GPU.
// These only state that its deliberately empty retirement seams are drained.
namespace nr_compat_present_runtime {
bool InlineDrainReady(){return true;}
bool NativeNrDrained(){return true;}
}
#include "src/nr_processing_runtime.inl"
#include "src/interop/nr_rr_post_runtime.inl"
NgxResult Native(ID3D12GraphicsCommandList* c,const NgxHandle* h,const NgxParams* p,void* cb){
 ++native_calls;CHECK(c==&command&&h==&handle&&p==&params&&cb==&callback);
 CHECK(g_config.mode==2);stage=1;
 if(change_generation)++current.generation;
 if(change_identity)current.feature_id=1;
 if(change_uncertain)g_feature_tracking_uncertain=true;
 return result;
}
void Reset(){
#if defined(NR_RR_TIMING_TEST)
 static unsigned long long timing_phase=100;
 NrTimingSetWaitPolicy(0,++timing_phase);
 timing_device_queries=0;timing_device_available=true;
 nr_result=1;nr_calls=0;
#endif
 owner=false;gate=chain=prepare_ok=finish_ok=before_ok=after_ok=true;nested=false;
 change_generation=change_identity=change_uncertain=finish_uncertain=opaque_failure=false;
 opaque_result=-77;abandons=0;
 nr_dual_runtime::stamp_calls=0;
 native_calls=finishes=prepares=quarantines=scopes=restores=stage=chain_depth=gate_depth=0;
 current={&handle,13,7};result=1;g_config={};g_config.mode=2;
 // Reset separates independent cases through the public selection contract.
 g_config.processing_selection=nr_processing_runtime::next_selection.fetch_add(1);
 g_feature_tracking_uncertain=false;g_lifecycle_tracking_uncertain=false;
 g_rr_ownership_gate_owner=0;concurrent_aborts=0;
 nr_post_settings::options={};nr_post_settings::rr_recorded=0;
 nr_post_settings::rr_before_state_rejected=0;nr_post_settings::rr_prepare_declined=0;
 nr_post_settings::rr_after_state_rejected=0;nr_post_settings::rr_finish_declined=0;
 nr_post_settings::rr_finish_gate_rejected=0;
}
void Run(){
 const auto feature=current;
#if defined(NR_RR_TIMING_TEST)
 const auto rr_frame=g_rr_passthrough_evaluations.load()+1;
 const bool timing_expected=nr_post_settings::options.rr&&feature.generation;
 const bool chain_expected=timing_expected&&gate&&!nested;
#endif
 auto returned=ProcessRrPost(&Native,&command,&handle,&params,&callback,feature);
 CHECK(returned==(opaque_failure?opaque_result:result)&&native_calls==1&&g_config.mode==2);
 CHECK(chain_depth==0&&gate_depth==0);
#if defined(NR_RR_TIMING_TEST)
 nr_timing::Snapshot timing;SnapshotNrTiming(timing);
 CHECK(timing.stages[static_cast<unsigned>(nr_timing::Stage::Carrier)].count==(timing_expected?1u:0u));
 CHECK(timing.stages[static_cast<unsigned>(nr_timing::Stage::NativeEvaluate)].count==(timing_expected?1u:0u));
 CHECK(timing.stages[static_cast<unsigned>(nr_timing::Stage::ChainLock)].count==(chain_expected?1u:0u));
 CHECK(nr_dual_runtime::stamp_calls==((prepares&&prepare_ok)?3u:0u));
 CHECK(timing_device_queries==(timing_expected?1u:0u)&&timing_device.references==1);
 if(timing_expected){
  const auto& carrier=timing.recent[timing.recent_count-1];
  CHECK(carrier.stage==nr_timing::Stage::Carrier);
  CHECK(carrier.identity.frame==rr_frame&&carrier.identity.feature==feature.generation&&
        carrier.identity.config==39&&carrier.identity.mode==1);
  CHECK(carrier.identity.device==(timing_device_available?19u:0u));
  CHECK(carrier.detail.native_calls==1&&carrier.detail.native_result==result&&carrier.detail.first_native_result==result);
  CHECK(carrier.detail.nr_calls==nr_calls&&timing.stages[static_cast<unsigned>(nr_timing::Stage::NrEvaluate)].count==nr_calls);
  if(nr_calls)CHECK(carrier.detail.nr_result==nr_result&&carrier.detail.first_nr_result==nr_result);
  CHECK(!timing.mixed_scopes&&!timing.route_unknown_records);
  if(timing_device_available)CHECK(timing.aggregate_scope_available&&timing.scoped_records==timing.observed&&!timing.unscoped_records);
  else CHECK(!timing.aggregate_scope_available&&!timing.scoped_records&&timing.unscoped_records==timing.observed);
 }else CHECK(!timing.observed&&!timing.recent_count);
#endif
}
int main(){
 Reset();Run();CHECK(prepares==1&&finishes==1&&nr_post_settings::rr_recorded==1&&restores==2);
 Reset();result=-91;Run();CHECK(prepares==1&&finishes==0);
 Reset();result=0;Run();CHECK(finishes==0);
 Reset();result=2;Run();CHECK(finishes==0);
 Reset();finish_ok=false;Run();CHECK(finishes==1&&nr_post_settings::rr_recorded==0);
 Reset();prepare_ok=false;Run();CHECK(finishes==0);
 Reset();before_ok=false;Run();CHECK(prepares==0&&finishes==0);
 Reset();after_ok=false;Run();CHECK(prepares==1&&finishes==0);
 Reset();gate=false;Run();CHECK(prepares==0&&finishes==0);
 Reset();gate=false;g_rr_ownership_gate_owner=1;Run();
 CHECK(prepares==0&&finishes==0&&concurrent_aborts==1&&g_feature_tracking_uncertain);
 Reset();gate=false;g_rr_ownership_gate_owner=2;Run();
 CHECK(prepares==0&&finishes==0&&concurrent_aborts==0&&!g_feature_tracking_uncertain);
 Reset();nested=true;Run();CHECK(prepares==0&&finishes==0);
 Reset();chain=false;Run();CHECK(prepares==0&&finishes==0);
 Reset();owner=true;Run();CHECK(prepares==0&&finishes==0);
 Reset();g_config.enabled=false;Run();CHECK(prepares==0&&finishes==0);
 Reset();g_config.post_transfer_strength=0;Run();CHECK(prepares==0&&finishes==0);
 Reset();nr_post_settings::options.rr=false;Run();CHECK(prepares==0&&finishes==0);
 Reset();current.generation=0;Run();CHECK(prepares==0&&finishes==0);
 Reset();change_generation=true;Run();CHECK(finishes==0);
 Reset();change_identity=true;Run();CHECK(finishes==0);
 Reset();change_uncertain=true;Run();CHECK(finishes==0&&quarantines==1);
 Reset();finish_uncertain=true;Run();CHECK(finishes==1&&quarantines==1);
#if defined(NR_RR_TIMING_TEST)
 Reset();nr_result=-77;opaque_failure=true;Run();
 CHECK(finishes==1&&nr_calls==1&&nr_post_settings::rr_recorded==0&&native_calls==1&&
       restores==1&&abandons==2&&nr_dual_runtime::stamp_calls==3);
 Reset();timing_device_available=false;Run();
 // The production RR dispatch and actual timing module are both included.
 // There is deliberately no Feature1 recovery type/global in this fixture.
 std::printf("RR_TIMING_CPU PASS checks=%u; actual ProcessRrPost and timing modules, real Win32 QPC, scripted API/model boundaries, no GPU/device/NGX\n",checks);
#endif
 std::printf("%u actual RR-post execution seam checks passed (no GPU)\n",checks);
}
