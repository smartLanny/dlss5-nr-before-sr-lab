// Executes the actual ProcessRrPost implementation using a CPU API seam.
// No Windows/NGX or game-runtime claim. Checks ordering, exact native result,
// no retry, retained feature identity, and requested Mode2 never overwritten.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "src/interop/nr_post_compat.h"
using NgxResult=std::int32_t;
struct ID3D12GraphicsCommandList{};struct NgxHandle{};struct NgxParams{};
struct FeatureRecord{const NgxHandle* handle=nullptr;int feature_id=-1;unsigned long long generation=0;};
using EvaluateFeatureFn=NgxResult(*)(ID3D12GraphicsCommandList*,const NgxHandle*,const NgxParams*,void*);
constexpr NgxResult kNgxSuccess=1;
struct Config{bool enabled=true,identity_use_original=false;int mode=2;float post_transfer_strength=1;} g_config;
std::atomic<int> g_last_carrier_feature{-1};std::atomic<bool> g_rr_guard_was_active{false};
std::atomic<unsigned long long> g_rr_passthrough_evaluations{0},g_carrier_evaluations{0},g_pipeline_eligible_carriers{0};
std::atomic<bool> g_feature_tracking_uncertain{false},g_lifecycle_tracking_uncertain{false};
namespace nr_post_settings{
 struct Options{bool rr=true;}; Options options;
 Options Read(){return options;}
 std::atomic<unsigned long long> rr_attempts{0},rr_recorded{0},rr_native_failures{0},last_rr_tick{0};
 std::atomic<int> last_rr_stage{0};
}
enum class BypassReason{RayReconstructionGuard,Feature1TransactionBusy};
void SetBypassReason(BypassReason){}
unsigned long long GetTickCount64(){return 10;}
bool owner=false,gate=true,nested=false,chain=true,prepare_ok=true,finish_ok=true,before_ok=true,after_ok=true;
int g_chain_cs=0,chain_depth=0,gate_depth=0,native_calls=0,finishes=0,prepares=0,quarantines=0,scopes=0,restores=0,stage=0;
bool change_generation=false,change_identity=false,change_uncertain=false,finish_uncertain=false;
NgxResult result=1;FeatureRecord current;
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
void BeginPipelineStatsScopeLocked(unsigned long long generation,int mode){CHECK(generation==7&&mode==1);}
struct NativeCommandStateScope{
 bool active=false;ID3D12GraphicsCommandList* command_list;
 NativeCommandStateScope(ID3D12GraphicsCommandList* c):command_list(c){active=(scopes++==0)?before_ok:after_ok;}
 void Restore(){++restores;}
};
struct PostSrSubstitution{bool active=false;};
PostSrSubstitution PrepareAfterSr(ID3D12GraphicsCommandList*,const NgxHandle*,const NgxParams*,const FeatureRecord& f){
 ++prepares;CHECK(stage==0&&f.feature_id==13&&g_config.mode==2);return {prepare_ok};
}
bool FinishAfterSr(ID3D12GraphicsCommandList*,const NgxParams*,const FeatureRecord& f,PostSrSubstitution& p){
 ++finishes;CHECK(stage==1&&result==1&&p.active&&f.feature_id==13&&g_config.mode==2);
 if(finish_uncertain) g_lifecycle_tracking_uncertain=true;
 return finish_ok;
}
FeatureRecord FindFeature(const NgxHandle*){return current;}
void QuarantineAbortedRrTransactionLocked(ID3D12GraphicsCommandList*){++quarantines;}
void Log(const char*,...){}
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
 owner=false;gate=chain=prepare_ok=finish_ok=before_ok=after_ok=true;nested=false;
 change_generation=change_identity=change_uncertain=finish_uncertain=false;
 native_calls=finishes=prepares=quarantines=scopes=restores=stage=chain_depth=gate_depth=0;
 current={&handle,13,7};result=1;g_config={};g_config.mode=2;
 g_feature_tracking_uncertain=false;g_lifecycle_tracking_uncertain=false;
 nr_post_settings::options={};nr_post_settings::rr_recorded=0;
}
void Run(){
 const auto feature=current;
 auto returned=ProcessRrPost(&Native,&command,&handle,&params,&callback,feature);
 CHECK(returned==result&&native_calls==1&&g_config.mode==2);
 CHECK(chain_depth==0&&gate_depth==0);
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
 std::printf("%u actual RR-post execution seam checks passed (no GPU)\n",checks);
}
