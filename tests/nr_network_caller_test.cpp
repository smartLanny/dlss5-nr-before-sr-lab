// Actual Create + admission bodies and actual caller Create branches, with
// observation stubs for external NGX/D3D services. Present Submit/RetireProof
// and Timeline are production code; fake fences advance only when requested.
// This exercises CPU failure classification, not Windows/GPU execution.
#define NR_NETWORK_CREATE_FIXTURE_ONLY
#include "nr_network_create_test.cpp"
#include "nr_layer_config.h"
#include "nr_compat_present_policy.hpp"
#include "nr_replay_format.h"
#include <climits>
#include <iterator>
#include <string>

struct Config {
    float intensity=1,local_tone=1,local_structure=1,skin_structure=0;
    bool auto_mask=false,ui_correction=false;
    unsigned style=0;
    int nr_network_preset=2,nr_primary_network_preset=-1;
    nr_layers::Auxiliary nr_layers{};
} g_config;
#include "network-admission.inl"

struct ID3D12Resource {};
struct ID3D12CommandList {};
struct ID3D12CommandQueue {};
struct ID3D12Fence { unsigned long long completed=0,signaled=0; };
using HRESULT = int;
constexpr bool FAILED(HRESULT r) { return r<0; }
enum D3D12_RESOURCE_STATES { D3D12_RESOURCE_STATE_COMMON,
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE };
unsigned gpu_commands=0,submissions=0,opaque_failures=0,restores=0,guide_restores=0;
unsigned state_restores=0,recovery_failures=0,resource_releases=0,retained_guides=0;
HRESULT signal_result=0;
unsigned long long GetTickCount64(){return 1000;}
int ID3D12GraphicsCommandList_Close(ID3D12GraphicsCommandList*){return 0;}
void ID3D12CommandQueue_ExecuteCommandLists(ID3D12CommandQueue*,unsigned,ID3D12CommandList**){++submissions;}
HRESULT ID3D12CommandQueue_Signal(ID3D12CommandQueue*,ID3D12Fence* fence,unsigned long long value){
    if(!FAILED(signal_result))fence->signaled=value;
    return signal_result;
}
unsigned long long ID3D12Fence_GetCompletedValue(ID3D12Fence* f){return f->completed;}
void ID3D12Resource_AddRef(void*){++retained_guides;}
template<class T> void ReleaseCom(T*&){++resource_releases;}
void Transition(ID3D12GraphicsCommandList*,void*,D3D12_RESOURCE_STATES& state,D3D12_RESOURCE_STATES next){state=next;++gpu_commands;}
void UavBarrier(ID3D12GraphicsCommandList*,void*){++gpu_commands;}
namespace nr_source_runtime {
unsigned fg_failures=0,present_completions=0;
void ReportFgFailure(unsigned){++fg_failures;}
void CompletePresent(ID3D12Fence*,unsigned long long){++present_completions;}
}
namespace nr_processing {enum class Start{Present};}
namespace nr_processing_runtime {void Adopted(nr_processing::Start){} }
namespace nr_present_capture {void Submit(unsigned long long,ID3D12Fence*,unsigned long long){} }
namespace nr_recovery {struct Token{};enum class Stage {NrCreate};}
struct RecoveryCarrier {nr_recovery::Token tokens[2];unsigned selected_role=0;};
RecoveryCarrier* g_nr_recovery_carrier=nullptr;
void NrRecoveryRecord(nr_recovery::Token,nr_recovery::Stage,bool,unsigned){++recovery_failures;}
const char* ResultName(unsigned){return "observed-result";}
bool DeferNrCreateForCommitPressure(NrContext&){return false;}
std::atomic<unsigned> g_last_nr_create_result{0};
std::atomic<bool> g_pre_sr_runtime_rejected{false},g_post_sr_runtime_rejected{false};
struct PreparedReceipt {bool create_admission_refused=false;};
struct Substitution {bool active=false;unsigned fatal_recording=0;};
struct PostSrSubstitution {bool opaque_failure_is_fatal=false;unsigned fatal_recording=0,guide_width=640,guide_height=360;PreparedReceipt dual_prepared{};};
void PreservePostSrOnFailure(ID3D12GraphicsCommandList*,PostSrSubstitution&){++restores;}
struct Timing {bool safe=true;};

Substitution BeforeCreate(ID3D12GraphicsCommandList* command_list,NrContext* context,
    const FeatureRecord& feature) {
    Substitution substitution;const bool first=true;unsigned width=640,height=360;
#include "network-before-create.inl"
    return substitution;
}
bool PostCreate(ID3D12GraphicsCommandList* command_list,NrContext& context,
    const FeatureRecord& feature,PostSrSubstitution& substitution,Timing& timing) {
    const bool first=true;unsigned work_width=640,work_height=360;
#include "network-post-create.inl"
    return true;
}
Substitution NoContextBefore(bool network_request_refused) {
    NrContext* context=nullptr;int* device=nullptr;Substitution substitution;
#include "network-before-no-context.inl"
    return substitution;
}
Substitution NoContextPost(bool network_request_refused) {
    NrContext* context=nullptr;int* device=nullptr;Substitution substitution;
#include "network-post-no-context.inl"
    return substitution;
}

struct NrSlot {
    void *proxy=nullptr,*neural=nullptr,*motion_proxy=nullptr,*rr_depth=nullptr,*dual_spatial=nullptr;
    D3D12_RESOURCE_STATES proxy_state=D3D12_RESOURCE_STATE_COMMON,neural_state=D3D12_RESOURCE_STATE_COMMON;
};
namespace nr_dual::dx12 {
bool RecordCreationInputs(void*,void*,ID3D12GraphicsCommandList*,unsigned& state){state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;++gpu_commands;return true;}
}
namespace auxiliary {
enum class Status {Preparing,CreatePending,Failed,ResourceBudget};Status status=Status::Preparing;
void SetStatus(Status value){status=value;}
std::atomic<unsigned> second_creates{0};
void* pipelines=nullptr;
struct Signature {bool resample_motion=false,linear_depth=false;};
struct Prepared {struct Guides {void *motion=nullptr,*depth=nullptr;} guides;bool full=false,create_admission_refused=false;};
NrContext g_contexts[2];
void ResolveLayerTiming(ID3D12GraphicsCommandList*,NrSlot&,bool){}
unsigned Fail(ID3D12GraphicsCommandList*,NrContext& primary,NrContext& aux,unsigned result,const char*){
    ++opaque_failures;primary.lifecycle_dependencies_quarantined=aux.lifecycle_dependencies_quarantined=true;
    aux.failed=true;status=Status::Failed;return result;
}
unsigned Create(ID3D12GraphicsCommandList* list,NrContext& primary,NrContext& aux,
    const FeatureRecord& feature,Prepared* receipt=nullptr) {
    NrSlot slot,source;Prepared prepared;const Signature g;unsigned state=0;
    struct Observe {Prepared* receipt;Prepared& prepared;~Observe(){if(receipt)*receipt=prepared;}} observe{receipt,prepared};
#include "network-aux-create.inl"
    return 0;
}
}

bool GetUInt(const NgxParams* p,const char* key,unsigned* value){
    if(std::strcmp(key,"DLSSNR.Hint.Render.Preset")||!p->preset_written)return false;
    *value=p->preset;return true;
}
bool GetInt(const NgxParams*,const char*,int*){return false;}
bool GetFloat(const NgxParams*,const char*,float*){return false;}
namespace nr_replay {
struct Recorder {static constexpr unsigned Invalid=UINT_MAX;};
unsigned stops=0,stop_reason=0,finishes=0;
void Stop(unsigned value){++stops;stop_reason=value;}
void Finish(ID3D12GraphicsCommandList*,NrContext&,NrSlot&,unsigned,bool){++finishes;}
#include "network-replay-attempt.inl"
#include "network-replay-scalars.inl"
}
unsigned terminal_adoptions=0,timing_resolves=0;
namespace nr_acceptance_gpu {void Finish(ID3D12GraphicsCommandList*,NrSlot&,bool visible){Check(!visible,"refused post source is not an adopted acceptance sample");}}
struct ColorContract{};
namespace nr_dual_runtime {
bool next_admission_refused=false;
unsigned Run(ID3D12GraphicsCommandList*,NrContext&,NrSlot&,PreparedReceipt& prepared,const FeatureRecord&,const ColorContract&){
    prepared.create_admission_refused=next_admission_refused;return 0;
}
void ResolveTiming(ID3D12GraphicsCommandList*,NrSlot&,bool visible){Check(!visible,"declined recording only resolves unadopted timing");++timing_resolves;}
}
Substitution BeforeConsumer(ID3D12GraphicsCommandList* command_list,NrContext* context,NrSlot* slot,const FeatureRecord& feature){
    Substitution substitution;PreparedReceipt dual_prepared;ColorContract color_contract;
    nr_replay::Attempt replay;replay.index=7;
#include "network-before-consumer.inl"
    ++terminal_adoptions;substitution.active=true;SetBypassReason(BypassReason::None);return substitution;
}
bool PostConsumer(ID3D12GraphicsCommandList* command_list,NrContext& context,NrSlot& slot,const FeatureRecord& feature){
    PostSrSubstitution substitution;ColorContract color_contract;
#include "network-post-timing.inl"
    nr_replay::Attempt replay;replay.index=7;
#include "network-post-consumer.inl"
    ++terminal_adoptions;SetBypassReason(BypassReason::None);return true;
}
struct FgSession {bool failed=false;};
struct RestoreGuides {bool enabled=true;~RestoreGuides(){if(enabled)++guide_restores;}};
struct NativeState {bool abandoned=false;void AbandonAfterOpaqueFailure(){abandoned=true;}
    ~NativeState(){if(!abandoned)++state_restores;}};
bool FgCreate(ID3D12GraphicsCommandList* list,NrContext* context,const FeatureRecord& feature,FgSession& s) {
    RestoreGuides restore_guides;NativeState state;
#include "network-fg-create.inl"
    return true;
}

namespace present {
using nr_compat_present::Status;using nr_compat_present::Extent;
struct Layer {NrContext context;NrSlot binding;void *spatial=nullptr,*init_input=nullptr,*init_output=nullptr;};
struct Flight {ID3D12GraphicsCommandList* list=nullptr;unsigned long long fence=0,source=0;bool adopted=false;};
struct Context {
    Layer layers[5];Flight flights[3];NrSlot image;
    void *unknown_depth=nullptr,*unknown_motion=nullptr,*unknown_guide_table=nullptr,*init_guides=nullptr,*init_color=nullptr,*spatial_pipelines=nullptr;
    D3D12_RESOURCE_STATES unknown_depth_state=D3D12_RESOURCE_STATE_COMMON,unknown_motion_state=D3D12_RESOURCE_STATE_COMMON;
    bool uses_native_guides=false,opaque_called=false,recorded=false;
    ID3D12CommandQueue* queue=nullptr;ID3D12Fence* fence=nullptr;
    unsigned passes=2;Extent work{640,360};unsigned long long create_fence=0;
    nr_compat_present::Timeline timeline;
};
nr_compat_present::Observation observation;
std::atomic<unsigned long long> last_recorded_tick{0};
void SetStatus(Status s){observation.status=s;}
void Poison(Context& c,unsigned,const char*){
    ++opaque_failures;c.timeline.Poison();c.recorded=true;
    for(auto& l:c.layers)l.context.failed=l.context.lifecycle_dependencies_quarantined=true;
}
bool Bind(Context&,Layer&,NgxParams*){return true;}
FeatureRecord SyntheticFeature(Context&){return {};}
void Utility(Context&,ID3D12GraphicsCommandList*,void*,void*,Extent){++gpu_commands;}
#include "network-present-submit.inl"
#include "network-present-retire.inl"
#include "network-present-layer.inl"
#include "network-present-bootstrap.inl"
bool LaterLayer(Context& c,unsigned slot,unsigned long long source) {
    // Frame's earlier primary Evaluate was already successful. Only its actual
    // later-model creation branch is extracted here; no GPU work is simulated
    // as an NGX success receipt. Its legal prior packet must still be submitted.
    auto* list=c.flights[slot].list;unsigned i=1;auto& l=c.layers[i];
#include "network-present-later.inl"
    return true;
}
}

void ResetBranches() {
    Reset();g_config={};++g_config_generation;
    gpu_commands=submissions=opaque_failures=restores=guide_restores=state_restores=0;
    recovery_failures=resource_releases=retained_guides=0;signal_result=0;
    nr_source_runtime::fg_failures=nr_source_runtime::present_completions=0;
    g_last_nr_create_result=0xFEED1234u;g_pre_sr_runtime_rejected=g_post_sr_runtime_rejected=false;
    auxiliary::second_creates=0;auxiliary::status=auxiliary::Status::Preparing;
    present::observation={};
    terminal_adoptions=timing_resolves=0;nr_dual_runtime::next_admission_refused=false;
    nr_replay::stops=nr_replay::stop_reason=nr_replay::finishes=0;
}
void ArmLateReceipt() {
    runtime_family=nr_network_policy::Family::Unknown;runtime_evidence=nr_network_policy::Evidence::Unknown;
    Check(ValidateNrActiveNetworkRequests(),"production frame admission accepts unknown runtime without an invented fallback guarantee");
    // Represents the already pinned worker publishing its known-old loaded-file
    // receipt after frame admission, before the real Create body observes it.
    publish_old_hash_on_create=true;
}
void CheckDeclined(const NrContext& c) {
    Check(!bridge_calls&&!mark_recorded&&!c.nr_handle&&!c.failed&&!c.create_dependencies_quarantined&&!c.lifecycle_dependencies_quarantined,
        "late refusal makes no opaque call, no false model handle and no quarantine");
    Check(!c.create_failures&&!c.next_create_retry_frame&&!opaque_failures&&!recovery_failures,
        "host admission is not counted as NGX failure or permanent/retry error");
    Check(bypass==BypassReason::NetworkUnavailable&&g_last_nr_create_result==0xFEED1234u,
        "native network reason survives and last actual NGX result is not fabricated");
    Check(!g_pre_sr_runtime_rejected&&!g_post_sr_runtime_rejected&&!g_nr_failed_create_process_latched,
        "late file evidence cannot latch either native route or the process");
}
int main(int argc,char** argv) {
    const std::string only=argc>1?argv[1]:"all";
    const auto selected=[&](const char* route){return only=="all"||only==route;};
    ID3D12GraphicsCommandList list;FeatureRecord feature;
    if(selected("before")) {
        ResetBranches();NgxParams params;auto c=Context(params,2);ArmLateReceipt();
        const auto result=BeforeCreate(&list,&c,feature);Check(!result.active,"pre route keeps native output");CheckDeclined(c);
        Check(!resource_releases,"recorded dependencies remain pinned for normal retirement");
        Check(!ValidateNrActiveNetworkRequests(),"next primary boundary sees newly known unavailable network");
        NoContextBefore(true);NoContextPost(true);ValidateNrActiveNetworkRequests();
        Check(network_rejection_logs==1&&!generic_context_logs,"persistent refusal emits one correct network transition, no per-frame capacity error");
        ResetBranches();params={};c=Context(params,1);next_result=0xBAD00005u;
        BeforeCreate(&list,&c,feature);Check(bridge_calls==1&&c.create_failures==1&&c.next_create_retry_frame>c.frames_seen,"real native Create errors retain retry/error handling");
    }
    if(selected("post"))for(bool fatal:{false,true}) {
        ResetBranches();NgxParams params;auto c=Context(params,2);ArmLateReceipt();
        PostSrSubstitution substitution{fatal};Timing timing;
        Check(!PostCreate(&list,c,feature,substitution,timing),"post route does not adopt a refused model");CheckDeclined(c);
        Check(restores==1&&timing.safe&&!substitution.fatal_recording&&!resource_releases,"native and private post restore output with original lifetime pins");
        ResetBranches();params={};c=Context(params,1);next_result=0xBAD00005u;substitution={fatal};timing={};
        PostCreate(&list,c,feature,substitution,timing);
        Check(bridge_calls==1&&(fatal?substitution.fatal_recording&&!timing.safe&&c.create_dependencies_quarantined:c.create_failures==1&&restores==1),"real post opaque failure retains its caller's original safety contract");
    }
    if(selected("aux")) {
        ResetBranches();NgxParams params;auto primary=Context(params,1),c=Context(params,2);c.layer_index=1;ArmLateReceipt();
        auxiliary::Prepared receipt;
        Check(auxiliary::Create(&list,primary,c,feature,&receipt)==0&&receipt.create_admission_refused,"auxiliary refusal preserves already successful parent recording and reports unavailable whole chain");CheckDeclined(c);
        Check(!primary.lifecycle_dependencies_quarantined&&!auxiliary::second_creates&&retained_guides==2&&!resource_releases&&gpu_commands,
            "auxiliary does not miscount Create and leaves recording/guide dependencies retained");
        ResetBranches();params={};c=Context(params,1);primary={};next_result=0xBAD00005u;
        auxiliary::Create(&list,primary,c,feature);
        Check(opaque_failures==1&&c.failed&&primary.lifecycle_dependencies_quarantined&&auxiliary::second_creates==1,"real auxiliary opaque failure still stops and quarantines the affected chain");
    }
    for(bool post:{false,true})if(selected(post?"post-consumer":"before-consumer")) {
        ResetBranches();NgxParams params;auto primary=Context(params,1),c=Context(params,2);ArmLateReceipt();
        auxiliary::Prepared receipt;auxiliary::Create(&list,primary,c,feature,&receipt);
        nr_dual_runtime::next_admission_refused=receipt.create_admission_refused;NrSlot slot;
        Check(post?!PostConsumer(&list,primary,slot,feature):!BeforeConsumer(&list,&primary,&slot,feature).active,
            "actual native consumer returns original after a late auxiliary refusal");
        Check(!terminal_adoptions&&!opaque_failures&&bypass==BypassReason::NetworkUnavailable&&timing_resolves==1,
            "declined chain cannot adopt N1, clear its network reason or skip safe timing completion");
        Check(nr_replay::stops==1&&nr_replay::stop_reason==12&&!nr_replay::finishes&&!resource_releases,
            "actual replay Attempt stops incomplete; no read of an unwritten immutable Composite view or premature release");
        Check(restores==unsigned(post),"post consumer restores native output exactly once");
    }
    if(selected("replay"))for(int request=0;request<=2;++request){
        ResetBranches();NgxParams params;auto c=Context(params,request);void* handle=nullptr;
        Check(CreateSignedNrFeature(&list,c,feature,&handle)==kNgxSuccess,"replay scalar comes from successful actual owned Create binding");
        nr_replay::Header header;nr_replay::SnapshotScalars(header,&params);unsigned found=0;
        for(unsigned i=0;i<header.parameter_count;++i)if(!std::strcmp(header.parameters[i].name,"DLSSNR.Hint.Render.Preset")){
            ++found;Check(header.parameters[i].type==unsigned(nr_replay::ScalarType::UInt32)&&header.parameters[i].present==1&&header.parameters[i].value==unsigned(request),
                "production replay snapshots the exact UInt creation-network request");
        }
        Check(found==1&&header.parameter_count<=nr_replay::kParameters&&sizeof(header)==6080,
            "network scalar is unique and fits the unchanged fixed replay ABI");
        if(request==2&&argc>2){auto* file=std::fopen(argv[2],"wb");Check(file,"open metadata-only replay fixture");
            const bool saved=std::fwrite(&header,1,sizeof(header),file)==sizeof(header);std::fclose(file);Check(saved,"write actual C++ Header for independent Python scalar reader");}
    }
    if(selected("fg")) {
        ResetBranches();NgxParams params;auto c=Context(params,2);ArmLateReceipt();FgSession s;
        Check(FgCreate(&list,&c,feature,s),"FG caller retains its legal prior recording");CheckDeclined(c);
        Check(!s.failed&&!nr_source_runtime::fg_failures&&guide_restores==1&&state_restores==1&&!resource_releases,"FG ownership restores guides/state and avoids a false permanent failure");
        ResetBranches();params={};c=Context(params,1);next_result=0xBAD00005u;s={};FgCreate(&list,&c,feature,s);
        Check(s.failed&&c.create_dependencies_quarantined&&nr_source_runtime::fg_failures==1&&!guide_restores&&!state_restores,"real FG Create failure retains opaque-failure abandonment");
    }
    for(bool later:{false,true})if(selected(later?"present-later":"present-primary")) {
        ResetBranches();NgxParams params;ID3D12Fence fence;ID3D12CommandQueue queue;present::Context c;
        c.queue=&queue;c.fence=&fence;c.flights[0].list=&list;
        c.layers[later?1:0].context=Context(params,2);
        if(later){c.layers[0].context.nr_handle=&returned_handle;c.opaque_called=true;}
        ArmLateReceipt();
        const bool result=later?present::LaterLayer(c,0,7):present::Bootstrap(c,0,7);
        Check(result&&!c.timeline.Poisoned()&&!opaque_failures,"Present refusal safely submits its prior valid packet");
        CheckDeclined(c.layers[later?1:0].context);
        Check(c.opaque_called==later,"Present marks opaque calls only when one actually occurred");
        Check(submissions==1&&fence.signaled&&c.flights[0].fence==fence.signaled&&!c.create_fence&&!c.flights[0].adopted&&!present::observation.submitted,
            "refused Present packet gets a real submission receipt, no Create fence and no adopted image claim");
        Check(!present::RetireProof(&c),"recorded Present resources cannot retire before exact fence completion");
        fence.completed=fence.signaled;Check(present::RetireProof(&c),"exact completed fence permits ordinary retirement without poison");
        ResetBranches();params={};c={};fence={};c.queue=&queue;c.fence=&fence;c.flights[0].list=&list;
        c.layers[later?1:0].context=Context(params,1);next_result=0xBAD00005u;
        Check(!(later?present::LaterLayer(c,0,8):present::Bootstrap(c,0,8))&&c.timeline.Poisoned()&&opaque_failures==1&&!submissions,
            "real Present Create failure still poisons and forbids further Submit");
        ResetBranches();params={};c={};fence={};c.queue=&queue;c.fence=&fence;c.flights[0].list=&list;
        c.layers[later?1:0].context=Context(params,2);ArmLateReceipt();signal_result=-1;
        Check(!(later?present::LaterLayer(c,0,9):present::Bootstrap(c,0,9))&&c.timeline.Poisoned()&&submissions==1,
            "failed Signal after legal Execute still retains unsafe dependencies even when Create was declined");
    }
    std::printf("PASS %u actual network admission/Create caller checks (%s); CPU/fake-fence boundary only\n",checks,only.c_str());
}
