// Calls the actual Core owned-boundary implementation with an injected provider.
// This validates dispatch, failure propagation and lifetime ownership, not NGX.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include "../src/nr_gpu_timing.h"
#include "../src/nr_nvfp4_failure.h"
#include "../src/nvfp4/backend.h"
struct ID3D12Device { int refs=1; };
struct ID3D12GraphicsCommandList { ID3D12Device *device=nullptr;unsigned long long token=1;bool fail_device=false; };
constexpr int MAX_PATH=260,IID_ID3D12Device=1;
using DWORD=unsigned long;
static DWORD GetModuleFileNameW(void *,wchar_t *out,unsigned){out[0]=L'x';out[1]=0;return 1;}
static struct { void *signed_module=reinterpret_cast<void *>(2); } g_nr_api;
using NgxResult=std::uint32_t;constexpr NgxResult kNgxSuccess=1;
#define FAILED(value) ((value)<0)
static int ID3D12GraphicsCommandList_GetDevice(ID3D12GraphicsCommandList *list,int,void **out){
    if(list->fail_device)return -1;*out=list->device;++list->device->refs;return 0;
}
template<class T>void ReleaseCom(T *&value){if(value){--value->refs;value=nullptr;}}
static bool ReadLifecycleToken(ID3D12GraphicsCommandList *list,unsigned long long &token){token=list->token;return token!=0;}
static void Log(const char *,...){ }
static std::atomic<void *> g_addon_module{nullptr};
static void SetSiblingPath(void *,const wchar_t *,wchar_t (&path)[MAX_PATH]){path[0]=L'x';path[1]=0;}
struct Config { unsigned nr_precision_mode=0;bool nr_precision_diagnostic=false,gpu_timing=false,suppress_reset_frame=true; } g_config;
static std::atomic<unsigned> g_config_generation{1};
struct NrContext {
    int mode=2;
    struct { unsigned precision_mode=1;bool precision_diagnostic=false; } tuning_signature;
    unsigned long long nvfp4_context_id=7,feature_generation=42,nvfp4_evaluation_serial=0;
    unsigned width=640,height=360;
    bool failed=false,lifecycle_dependencies_quarantined=false;
    bool nvfp4_effective_known=false,nvfp4_last_effective=false,nvfp4_transition_blocked=false,nvfp4_transition_pending=false;
    bool reset_retry_pending=false,diagnostic_reset_requested=false;
    void *params=nullptr;
};
struct NrSlot { struct { bool reset=false; } capture_params; bool suppress_transfer_this_frame=false; };
static unsigned reset_writes=0;
static void SetInt(void *,const char *key,int value){if(std::string_view(key)=="DLSSNR.Reset"&&value==1)++reset_writes;}
static nr_nvfp4_core::FailureLatch g_nvfp4_failure_latch;
static unsigned gpu_quarantines=0,backend_quarantines=0,scope_entries=0,scope_depth=0,init_calls=0,start_calls=0;
static unsigned long long last_quarantined_token=0;
struct Timing {
    nr_gpu_timing::Ticket Begin(ID3D12Device *,ID3D12GraphicsCommandList *,std::uint64_t,const nr_gpu_timing::Key &){return 1;}
    void BeforeSr(nr_gpu_timing::Ticket,ID3D12GraphicsCommandList *){}
    void End(nr_gpu_timing::Ticket,ID3D12GraphicsCommandList *,bool){}
    void Quarantine(std::uint64_t token){++gpu_quarantines;last_quarantined_token=token;}
} g_gpu_timing;
static bool provider_may_have_recorded=false;
static bool provider_ready=false;
static unsigned provider_actual_pairs=0;
namespace nr::nvfp4 {
bool Init(const Config &)noexcept{++init_calls;return true;}
bool EnsureStarted(ID3D12Device *)noexcept{++start_calls;return true;}
OwnedScope::OwnedScope(const OwnedCall &info)noexcept{if(info.feature_id!=18||!info.pre_sr||!info.recording_token)std::abort();implementation_=info.allow_replacement?reinterpret_cast<void *>(1):nullptr;provider_actual_pairs=0;++scope_entries;++scope_depth;}
OwnedScope::~OwnedScope(){(void)storage_;(void)previous_;--scope_depth;}
bool OwnedScope::MayHaveRecordedFailure()const noexcept{return provider_may_have_recorded;}
bool OwnedScope::WillReplace()const noexcept{return provider_ready&&implementation_!=nullptr;}
Snapshot OwnedScope::Status()const noexcept{Snapshot s{};s.replaced_pairs=provider_actual_pairs;return s;}
void OnQuarantine(std::uint64_t token,const char *)noexcept{++backend_quarantines;last_quarantined_token=token;}
}
#include "../src/nr_nvfp4_core.inl"
static void Check(bool ok,const char *message){if(!ok){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}}
int main(){
    ID3D12Device device;ID3D12GraphicsCommandList list{&device,91,false};NrContext context;
    unsigned calls=0;
    auto provider=[&](){++calls;Check(scope_depth==1,"provider inside exactly one owned scope");return kNgxSuccess;};
    Check(CallOwnedNrPrecision(context,&list,nr::nvfp4::Phase::Create,provider)==kNgxSuccess,"owned Create success");
    Check(context.nvfp4_evaluation_serial==0&&scope_depth==0&&device.refs==1,"Create scope and temporary device reference balanced");
    Check(CallOwnedNrPrecision(context,&list,nr::nvfp4::Phase::Evaluate,provider)==kNgxSuccess,"owned Evaluate success");
    Check(calls==2&&context.nvfp4_evaluation_serial==1&&!context.failed,"one provider call per successful Evaluate");
    const auto before=scope_entries;
    auto native=[&](){++calls;Check(scope_depth==0,"native provider never inherits owned scope");return NgxResult(77);};
    context.mode=1;Check(CallOwnedNrPrecision(context,&list,nr::nvfp4::Phase::Evaluate,native)==77,"post-SR never opts in");
    context.mode=2;context.tuning_signature.precision_mode=0;
    Check(CallOwnedNrPrecision(context,&list,nr::nvfp4::Phase::Evaluate,native)==77,"Off exactly preserves native result");
    context.tuning_signature.precision_mode=1;list.token=0;
    Check(CallOwnedNrPrecision(context,&list,nr::nvfp4::Phase::Evaluate,native)==77,"unknown recording declines before scope");
    list.token=91;list.fail_device=true;
    Check(CallOwnedNrPrecision(context,&list,nr::nvfp4::Phase::Evaluate,native)==77,"unavailable device declines before scope");
    Check(scope_entries==before&&device.refs==1,"declines do not fabricate ownership or leak device");list.fail_device=false;
    auto rejected=[&](){++calls;return NgxResult(0xBAD00001u);};
    Check(CallOwnedNrPrecision(context,&list,nr::nvfp4::Phase::Evaluate,rejected)==0xBAD00001u&&!context.failed,"pre-record provider rejection preserves returned status");
    const auto before_failure=calls;provider_may_have_recorded=true;
    Check(CallOwnedNrPrecision(context,&list,nr::nvfp4::Phase::Evaluate,provider)!=kNgxSuccess,"partial-record failure overrides even successful outer status");
    Check(calls==before_failure+1,"partial recording never retries original provider");
    Check(context.failed&&context.lifecycle_dependencies_quarantined&&g_nvfp4_failure_latch.Contains(42),"failure pins whole owned temporal generation");
    Check(!g_nvfp4_failure_latch.Contains(43),"unrelated feature generation remains independent");
    context.nvfp4_context_id=8;context.tuning_signature.precision_mode=0;
    Check(g_nvfp4_failure_latch.Contains(context.feature_generation),"setting and instance changes cannot escape poisoned generation");
    Check(gpu_quarantines==1&&backend_quarantines==1&&last_quarantined_token==91,"only exact recording token quarantined");
    Check(scope_depth==0&&device.refs==1&&g_nvfp4_recording_failures==1,"failure balances scope and temporary references");
    provider_may_have_recorded=false;NrContext transition;transition.feature_generation=100;NrSlot slot;
    auto original_eval=[&](){return kNgxSuccess;};
    CallOwnedNrPrecision(transition,&list,nr::nvfp4::Phase::Evaluate,original_eval,&slot);
    Check(transition.nvfp4_effective_known&&!transition.nvfp4_last_effective&&reset_writes==0,"initial original strategy observed");
    provider_ready=true;
    auto replaced=[&](){provider_actual_pairs=1;return kNgxSuccess;};
    CallOwnedNrPrecision(transition,&list,nr::nvfp4::Phase::Evaluate,replaced,&slot);
    Check(reset_writes==1&&transition.reset_retry_pending&&transition.diagnostic_reset_requested&&slot.capture_params.reset&&slot.suppress_transfer_this_frame,"async readiness resets actual NR parameters and transfer warmup");
    Check(transition.nvfp4_last_effective&&!transition.nvfp4_transition_pending,"actual pair commits effective NVFP4 strategy");
    transition.reset_retry_pending=false;provider_ready=false;
    CallOwnedNrPrecision(transition,&list,nr::nvfp4::Phase::Evaluate,original_eval,&slot);
    Check(reset_writes==2&&!transition.nvfp4_last_effective,"return to original precision also resets history");
    NrContext unconfirmed;unconfirmed.feature_generation=101;unconfirmed.nvfp4_effective_known=true;provider_ready=true;
    CallOwnedNrPrecision(unconfirmed,&list,nr::nvfp4::Phase::Evaluate,original_eval,&slot);
    Check(reset_writes==3&&unconfirmed.nvfp4_transition_blocked&&unconfirmed.nvfp4_transition_pending&&!unconfirmed.nvfp4_last_effective,"zero actual pairs cannot commit requested transition");
    unconfirmed.reset_retry_pending=false; // Successful NGX consumes only its ordinary Reset.
    CallOwnedNrPrecision(unconfirmed,&list,nr::nvfp4::Phase::Evaluate,original_eval,&slot);
    Check(reset_writes==3&&unconfirmed.nvfp4_transition_pending,"unconfirmed context freezes native without per-frame Reset");
    NrContext retry;retry.feature_generation=102;retry.nvfp4_effective_known=true;
    CallOwnedNrPrecision(retry,&list,nr::nvfp4::Phase::Evaluate,rejected,&slot);
    Check(retry.reset_retry_pending&&!retry.nvfp4_transition_blocked,"returned Evaluate error keeps precision Reset retry pending");
    CallOwnedNrPrecision(retry,&list,nr::nvfp4::Phase::Evaluate,replaced,&slot);
    Check(retry.nvfp4_last_effective&&!retry.nvfp4_transition_pending,"successful retry commits only actual replaced pair");
    g_config={};PrepareNrPrecision(&device);Check(init_calls==1&&start_calls==0,"default Off performs no startup");
    PrepareNrPrecision(&device);Check(init_calls==1,"unchanged configuration is not reinitialized");
    g_config.nr_precision_mode=1;PrepareNrPrecision(&device);Check(init_calls==2&&start_calls==1,"Auto starts only after explicit configuration");
    std::puts("PASS actual Core dispatch, partial-record isolation, generation latch, async precision transitions and Reset retry");
}
