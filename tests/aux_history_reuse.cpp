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
struct Config {float intensity=1,local_tone=1,local_structure=1,skin_structure=-1;bool auto_mask=false;unsigned style=1,nr_input_filter=0;bool ui_correction=false;int depth_mode=-1,nr_network_preset=0,nr_primary_network_preset=-1;NrJitterMode jitter_mode=NrJitterMode::GameInputPixel;nr_layers::Auxiliary nr_layers{};}g_config;
#if defined(NR_HAS_NETWORK_MODEL_SELECTION)
struct NrNetworkCreationIdentity { unsigned long long generation=1,module_address=0x12340000; };
NrNetworkCreationIdentity runtime_identity{};
NrNetworkCreationIdentity CurrentNrNetworkCreationIdentity(){return runtime_identity;}
#endif
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
bool DeferNrCreationForReleaseLocked(const NgxHandle* = nullptr){return false;} // This history-only fixture has no retirement; real scoped admission/collector/drain is tested separately.
void Log(const char*,...){}
void PrepareStageCaptureContextLocked(const NgxHandle*,unsigned long long,int){}
unsigned long long PreparedStageCaptureRequestId(const NgxHandle*,unsigned long long,int){return 0;}
#if defined(NR_HAS_NETWORK_MODEL_SELECTION)
// Admission itself is covered by the production Create fixture. The context
// fixture retains non-GPU retired objects to observe signature separation.
bool network_admission = true;
unsigned network_admission_checks = 0;
bool ValidateNrActiveNetworkRequests(){++network_admission_checks;return network_admission;}
void RetireContextForHandle(const NgxHandle* h){for(auto& c:g_contexts)if(c.sr_handle==h)c.retired=true;}
#endif
#include "lookup.inl"
static void check(bool b,const char* m){if(!b){std::fprintf(stderr,"FAIL %s\n",m);std::exit(1);}}
static void clear(){for(auto& c:g_contexts)c={};g_config={};g_lifecycle_tracking_uncertain=false;g_nr_failed_create_process_latched=false;
#if defined(NR_HAS_NETWORK_MODEL_SELECTION)
 runtime_identity={};
 network_admission=true;network_admission_checks=0;
#endif
}
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
#if defined(NR_HAS_NETWORK_MODEL_SELECTION)
static void networks(){
 // A worker receipt becoming available while the primary is being prepared
 // cannot let a nested auxiliary lookup retire that same primary. The next
 // primary boundary rejects the whole chain; its existing pins remain owned.
 clear();auto* primary=lookup(0,0,0);check(primary&&network_admission_checks==1,"primary validates whole requested chain once");
 network_admission=false;auto* auxiliary=lookup();
 check(auxiliary&&!primary->retired&&network_admission_checks==1,"late refusal does not retire primary from nested auxiliary lookup");
 bool refused=false;
 check(!FindOrCreateContext(&carrier,3,1,1720,720,CodecColorMode::Automatic,guides,false,0,0,0,&refused)&&refused,
  "next primary reports explicit network refusal independently of context capacity");
 check(primary->retired&&auxiliary->retired,"refused chain uses normal retirement for all model dependencies");
 // Each native request is a Create identity, independently of Style/intensity.
 for(unsigned edited=0;edited<nr_layers::Count;++edited){
  clear();std::array<NrContext*,nr_layers::Count> before{};
  for(unsigned i=0;i<nr_layers::Count;++i)before[i]=lookup(11,10,i);
  if(edited==0)g_config.nr_primary_network_preset=2;
  else g_config.nr_layers[edited-1].network_preset=2;
  for(unsigned i=0;i<nr_layers::Count;++i){
   auto* after=lookup(11,i>edited?11:10,i);
   check(after&&after->tuning_signature.network_preset==(i==edited?2:0),"each layer receives only its own resolved request");
   if(i==edited)check(after!=before[i]&&before[i]->retired,"edited network recreates its own model");
   else check(after==before[i]&&!after->retired,"other networks keep their own allocation");
  }
 }
 clear();g_config.nr_primary_network_preset=1;g_config.nr_layers[1].network_preset=0;
 std::array<NrContext*,nr_layers::Count> before{};
 for(unsigned i=0;i<nr_layers::Count;++i)before[i]=lookup(11,10,i);
 g_config.nr_network_preset=2;
 for(unsigned i=0;i<nr_layers::Count;++i){
  auto* after=lookup(11,10,i);
  check(after,"global inheritance lookup");
  if(i==0||i==2)check(after==before[i]&&!after->retired,"explicit Original and runtime-default ignore a global change");
  else check(after!=before[i]&&before[i]->retired&&after->tuning_signature.network_preset==2,"only inherited network follows global change");
 }
 clear();g_config.nr_network_preset=1;auto* old=lookup();
 g_config.nr_layers[0].network_preset=1;auto* same=lookup();
 check(same==old&&!old->retired,"inherit-to-equivalent-explicit changes persistence, not native model identity");
 ++runtime_identity.generation;auto* generation=lookup();
 check(generation&&generation!=old&&old->retired,"new module generation cannot reuse old model");
 ++runtime_identity.module_address;auto* module=lookup();
 check(module&&module!=generation&&generation->retired,"different module address cannot reuse old model");
 clear();auto* automatic=lookup();g_config.nr_layers[0].network_preset=2;auto* optimized=lookup();
 g_config.nr_layers[0].network_preset=0;auto* automatic_again=lookup();
 check(automatic_again&&automatic_again!=automatic&&automatic_again!=optimized&&automatic->retired&&optimized->retired,
  "network A-B-A never resurrects retired temporal allocation");
 std::puts("PASS production lookup: five independent networks, inherited/explicit-default semantics, runtime generation/module identity and network A-B-A retirement");
}
#endif

int main(int argc,char** argv){if(argc>1&&std::string(argv[1])=="--reproduction")return reproduce();contracts();
#if defined(NR_HAS_NETWORK_MODEL_SELECTION)
 networks();
#endif
}
