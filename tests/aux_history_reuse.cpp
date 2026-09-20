// Real FindOrCreateContext body plus the production AuxHistory policy. No GPU
// or NGX substitutes are interpreted as successful game execution.
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "nr_guide_contract.h"
#include "nr_dual_pass.hpp"
#include "nr_layer_config.h"
struct NgxHandle {unsigned id;};
enum class CodecColorMode:unsigned {Automatic,LinearSdr,ModelEncodedValueDomain};
enum class NrJitterMode:int{Legacy=0,GameInputPixel=1};
struct Config {float intensity=1,local_tone=1,local_structure=1,skin_structure=-1;bool auto_mask=false;unsigned style=1,nr_input_filter=0;bool ui_correction=false;int depth_mode=-1;NrJitterMode jitter_mode=NrJitterMode::GameInputPixel;nr_layers::Auxiliary nr_layers{};}g_config;
#include "tuning.inl"
struct NrContext {
 const NgxHandle* sr_handle=nullptr;unsigned long long feature_generation=0,instance_id=0,stage_capture_request_id=0;
 unsigned width=0,height=0,layer_index=0;int mode=0;
 unsigned long long parent_instance_id=0,parent_input_epoch=0;
 bool retired=false,codec_color_mode_initialized=false,tuning_signature_initialized=false,codec_peak_shoulder=false,reset_retry_pending=false;
 CodecColorMode codec_color_mode=CodecColorMode::Automatic;NrTuningSignature tuning_signature{};
 NrGuideSignature guide_storage_signature{};NrGuideHistory guide_history{};nr_dual::AuxHistory dual_history{};
};
NrContext g_contexts[16]{};std::atomic<bool>g_lifecycle_tracking_uncertain{false},g_nr_failed_create_process_latched{false};
void CollectRetiredContextsLocked(){} // No completed fence: retired objects cannot be recycled.
bool DeferNrCreationForReleaseLocked(){return false;} // This fixture has no ready/detached releases; integration is tested separately.
void Log(const char*,...){}
void PrepareStageCaptureContextLocked(const NgxHandle*,unsigned long long,int){}
unsigned long long PreparedStageCaptureRequestId(const NgxHandle*,unsigned long long,int){return 0;}
#include "lookup.inl"
static void check(bool b,const char* m){if(!b){std::fprintf(stderr,"FAIL %s\n",m);std::exit(1);}}
static void clear(){for(auto& c:g_contexts)c={};g_config={};g_lifecycle_tracking_uncertain=false;g_nr_failed_create_process_latched=false;}
static NgxHandle carrier{1};static NrGuideSignature guides{};
static NrContext* lookup(unsigned long long parent=11,unsigned long long epoch=10,unsigned layer=1,unsigned w=1720,unsigned h=720,unsigned long long feature=3,int mode=1){
 return FindOrCreateContext(&carrier,feature,mode,w,h,CodecColorMode::Automatic,guides,false,layer,parent,epoch);
}
static int reproduce(){
 clear();auto* first=lookup();check(first,"initial allocation");first->instance_id=22;
 nr_dual::LayerKey previous{1,11,10,22,{1720,720}};
 check(first->dual_history.CommitSuccess(previous,60),"old successful history");
 auto* reused=lookup(11,11);
 if(reused!=first||first->retired){std::puts("REPRODUCED: parent history-only change retires same-contract auxiliary");return 3;}
 check(reused->instance_id==22&&reused->parent_input_epoch==11,"reuse preserves instance and updates parent epoch");
 check(reused->reset_retry_pending,"reuse must request, not consume, Reset");
 auto next=previous;next.primary_input_epoch=11;
 check(reused->dual_history.NeedsReset(next,61,false),"new epoch must not replay old temporal state");
 check(reused->dual_history.NeedsReset(previous,61,false),"explicit invalidation still applies to old key");
 // A rejected recording/failed bind leaves both proofs pending; only success
 // permits continuity, exactly as the actual RunOne caller contract requires.
 auto* again=lookup(11,11);check(again==first&&again->reset_retry_pending,"lookup cannot consume failed reset");
 check(again->dual_history.NeedsReset(next,61,false),"failed evaluation did not commit history");
 check(again->dual_history.CommitSuccess(next,61),"commit on successful Evaluate");again->reset_retry_pending=false;
 check(!again->dual_history.NeedsReset(next,62,false),"stable next parent reuses reset history");
 check(again->dual_history.NeedsReset(next,63,false),"skipped auxiliary still resets");
 check(again->dual_history.NeedsReset(next,62,true),"game/camera reset remains mandatory");
 // Rapid ON/OFF cannot fill the 16-slot pool with fence-pending identical models.
 for(unsigned epoch=12;epoch<140;++epoch){auto* c=lookup(11,epoch);check(c==first&&!c->retired,"128 toggles must not retire stable allocation");check(c->reset_retry_pending,"every history epoch still requests Reset");}
 unsigned active=0;for(auto& c:g_contexts)active+=c.sr_handle!=nullptr;
 check(active==1,"stable history transitions consumed context capacity");
 std::puts("PASS: stable parent/history changes reuse one allocation; Reset survives rejection and is consumed only on success; 128 epochs stay bounded");return 0;
}
static void contracts(){
 clear();auto* a=lookup();auto* b=lookup(12,10);check(a&&b!=a&&a->retired,"different parent instance must retire");
 clear();a=lookup();b=lookup(11,11,1,1920,1080);check(b&&b!=a&&a->retired,"history plus extent change must rebuild");
 clear();a=lookup();b=lookup(11,11,1,1720,720,4);check(b&&b!=a&&a->retired,"carrier generation change must rebuild");
 clear();a=lookup();
#if UNIFORM_LAYERS
 g_config.nr_layers[0].intensity=2;
#else
 g_config.intensity=2;
#endif
 b=lookup(11,11);check(b&&b!=a&&a->retired,"history plus create-tuning change must rebuild");
 clear();a=lookup();guides.normalized_create_flags^=2;b=lookup(11,11);check(b&&b!=a&&a->retired,"guide-storage create flags must rebuild");guides={};
 clear();a=lookup();b=FindOrCreateContext(&carrier,3,1,1720,720,CodecColorMode::LinearSdr,guides,false,1,11,11);check(b&&b!=a&&a->retired,"codec contract must rebuild");
 clear();a=lookup();b=FindOrCreateContext(&carrier,3,1,1720,720,CodecColorMode::Automatic,guides,true,1,11,11);check(b&&b!=a&&a->retired,"peak contract must rebuild");
 clear();a=lookup();b=lookup(11,11,2);check(b&&b!=a&&!a->retired,"distinct layers stay independent");
 clear();a=lookup();b=lookup(11,11,1,1720,720,3,2);check(b&&b!=a&&!a->retired,"distinct route stays independent");
 clear();a=lookup(0,0,0);b=lookup(0,999,0);check(a==b&&!a->retired,"primary lookup unchanged");
 clear();a=lookup();a->retired=true;b=lookup();check(b&&b!=a,"retired allocation cannot be resurrected");
 clear();a=lookup();g_lifecycle_tracking_uncertain=true;check(!lookup(),"uncertain lifetime must decline");g_lifecycle_tracking_uncertain=false;g_nr_failed_create_process_latched=true;check(!lookup(),"opaque create failure remains latched");
 clear();for(unsigned i=0;i<16;++i)check(lookup(11,10,1,100+i,100)!=nullptr,"original capacity available");check(!lookup(11,10,1,300,100),"pending-fence full pool must fail closed");
 clear();a=lookup();b=lookup(11,10,1,1920,1080);auto* c=lookup();check(c&&c!=a&&c!=b&&a->retired&&b->retired,"A-B-A dimensions must not resurrect old allocation");
 std::puts("PASS 14 allocation/retirement contracts: parent identity, extent, carrier, tuning, formats, layers, routes, failures, capacity and A-B-A");
#if UNIFORM_LAYERS
 // Editing one auxiliary must neither recreate its upstream models nor borrow
 // their controls. Downstream input changes request Reset without a new model.
 for(unsigned edited=1;edited<nr_layers::Count;++edited){
  clear();std::array<NrContext*,nr_layers::Count> before{};
  for(unsigned i=0;i<nr_layers::Count;++i)before[i]=lookup(11,10,i);
  g_config.nr_layers[edited-1].intensity=1.8f;
  for(unsigned i=0;i<nr_layers::Count;++i){
   auto* after=lookup(11,i>edited?11:10,i);
   if(i==edited)check(after!=before[i]&&before[i]->retired,"only edited layer rebuilds");
   else {check(after==before[i]&&!after->retired,"unmodified layer retains independent model");
    if(i>edited)check(after->reset_retry_pending,"downstream history resets without recreation");}
  }
 }
 std::puts("PASS five-layer independent tuning, upstream reuse and downstream Reset");
#endif
}
int main(int argc,char** argv){if(argc>1&&std::string(argv[1])=="--reproduction")return reproduce();contracts();}
