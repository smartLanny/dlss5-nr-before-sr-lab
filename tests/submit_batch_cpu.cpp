// Actual extracted production submission/affinity/capture/completion functions.
// COM queues/lists and observer callbacks are explicit CPU seams; no GPU exists.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define CINTERFACE
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include "nr_runtime_timing.h"
#include "nr_runtime_timing.inl"
struct FeedbackSession{std::string text;};
__attribute__((format(printf,2,3)))
void FeedbackAppend(FeedbackSession& session,const char* format,...){
    char text[4096];va_list args;va_start(args,format);std::vsnprintf(text,sizeof(text),format,args);va_end(args);session.text+=text;
}
#include "nr_timing_feedback.inl"
unsigned checks=0;
void Check(bool value,const char* label) {
    ++checks;
    if(!value){std::printf("FAIL %s\n",label);std::exit(41);}
}
void Log(const char*,...){}
namespace nr_resource_recovery { struct Token{unsigned value=0;}; }
namespace nr_binding { constexpr unsigned kPackets=2; }
struct LifecycleQueue;
struct NrContext {
    unsigned long long pending_recording_token=0,completion_fence_value=0;
    unsigned recording_command_lists=1;
    bool awaiting_fence_signal=false,lifecycle_dependencies_quarantined=false,complete=true;
    LifecycleQueue* last_submission_queue=nullptr;
};
struct NrSlot {
    unsigned long long completion_fence_value=0,dual_time_token=0,dual_time_fence=0;
    unsigned recording_command_lists=1;
    bool awaiting_fence_signal=false,dual_time_pending=false,complete=true;
    LifecycleQueue* last_submission_queue=nullptr;
    LifecycleQueue* dual_time_queue=nullptr;
};
#include "production_types.inl"
CRITICAL_SECTION g_chain_cs;
std::atomic<unsigned> g_live_graphics_queue_count{0};
std::atomic<unsigned long long> g_queue_affinity_marker_failures{0},g_queue_affinity_actual_mismatches{0};
std::atomic<unsigned long long> g_duplicate_execute_list_detections{0},g_command_list_replay_detections{0};
std::atomic<unsigned long long> g_pipeline_stats_epoch{1},g_pipeline_stats_generation{1};
std::atomic<int> g_pipeline_stats_mode{1};
std::atomic<unsigned long long> g_nr_transfer_submitted_frames{0},g_pipeline_transfer_submitted{0};
void MarkLifecycleUncertain(const char*){g_lifecycle_tracking_uncertain=true;}
template<class T> void ReleaseCom(T*& p){if(p){p->lpVtbl->Release(p);p=nullptr;}}
bool IsContextFenceCompleteLocked(const NrContext& c){return c.complete;}
bool IsSlotFenceCompleteLocked(const NrSlot& s){return s.complete;}
struct CaptureObserver{void OnSubmit(unsigned long long,void*,unsigned long long){};
    void Submit(unsigned long long,void*,unsigned long long){};} g_f8_gpu;
namespace nr_replay { CaptureObserver recorder; }
namespace nr_dual_capture {void Submit(unsigned long long,void*,unsigned long long){}}
namespace nr_fg_capture {void Submit(unsigned long long,void*,unsigned long long){}}
namespace nr_face_runtime {void Submit(unsigned long long,void*,unsigned long long){}}
namespace nr_recovery {enum class Stage{Submitted};}
void NrRecoveryReceiptOverflow(nr_resource_recovery::Token){}
void NrRecoveryRecord(nr_resource_recovery::Token,nr_recovery::Stage,bool){}
std::string order;
unsigned native_calls=0,source_calls=0,boundary_before=0,boundary_after=0,signals=0;
bool marker_failure=false,signal_failure=false,reset_between_locks=false;
void (*before_capture)()=nullptr;
GraphicsQueueRecord* TrackGraphicsQueueLocked(ID3D12CommandQueue*);
LifecycleQueue* FindOrCreateLifecycleQueueLocked(ID3D12CommandQueue* queue) {
    auto* tracked=TrackGraphicsQueueLocked(queue);
    if(!tracked)return nullptr;
    auto& state=g_lifecycle_queues[tracked->identity-1];
    state.queue=queue;state.identity=tracked->identity;
    if(before_capture)before_capture();
    return &state;
}
using ExecuteCommandListsFn=void(*)(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*);
ExecuteCommandListsFn g_original_queue_execute[4]{};
struct NrPostBoundarySubmitScope {
    NrPostBoundarySubmitScope(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*){++boundary_before;order+='B';}
    ~NrPostBoundarySubmitScope(){++boundary_after;order+='Z';}
};
namespace nr_source_runtime {void OnExecute(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*,bool succeeded){
    Check(succeeded,"source true native-return receipt");++source_calls;order+='S';}}
struct List {
    struct Base {ID3D12CommandList api{};List* owner=nullptr;} base;
    struct Graphics {ID3D12GraphicsCommandList api{};List* owner=nullptr;} graphics;
    CommandListQueueMarker marker{};
    unsigned long long token=0;
    HRESULT base_result=S_OK;
    bool qi_fail=false,malformed=false;
    D3D12_COMMAND_LIST_TYPE type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    unsigned qi_calls=0,releases=0,base_reads=0,graphics_reads=0,marker_writes=0;
    static List& Of(ID3D12CommandList* p){return *reinterpret_cast<Base*>(p)->owner;}
    static List& Of(ID3D12GraphicsCommandList* p){return *reinterpret_cast<Graphics*>(p)->owner;}
    template<class T> static HRESULT Read(T* api,REFGUID key,UINT* size,void* data){
        auto& l=Of(api);
        if constexpr(std::is_same_v<T,ID3D12CommandList>)++l.base_reads;else ++l.graphics_reads;
        if(key==kLifecycleCommandListGuid){
            if constexpr(std::is_same_v<T,ID3D12CommandList>) if(FAILED(l.base_result))return l.base_result;
            if(!l.token)return DXGI_ERROR_NOT_FOUND;
            if(l.malformed){*size=1;return S_OK;}
            if(*size<sizeof(l.token))return E_INVALIDARG;
            *size=sizeof(l.token);std::memcpy(data,&l.token,*size);return S_OK;
        }
        if(!l.marker.identity)return DXGI_ERROR_NOT_FOUND;
        *size=sizeof(l.marker);std::memcpy(data,&l.marker,*size);return S_OK;
    }
    template<class T> static HRESULT Write(T* api,REFGUID,UINT size,const void* data){
        auto& l=Of(api);++l.marker_writes;
        if(!size){l.marker={};return S_OK;}
        if(marker_failure)return E_OUTOFMEMORY;
        std::memcpy(&l.marker,data,sizeof(l.marker));return S_OK;
    }
    static HRESULT STDMETHODCALLTYPE Query(ID3D12CommandList* api,REFIID,void** out){
        auto& l=Of(api);++l.qi_calls;
        *out=l.qi_fail?nullptr:&l.graphics.api;return l.qi_fail?E_NOINTERFACE:S_OK;
    }
    static ULONG STDMETHODCALLTYPE Release(ID3D12GraphicsCommandList* api){++Of(api).releases;return 1;}
    static D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE Type(ID3D12CommandList* api){return Of(api).type;}
    List(){
        static ID3D12CommandListVtbl bv=[] {ID3D12CommandListVtbl v{};v.QueryInterface=Query;
            v.GetPrivateData=Read<ID3D12CommandList>;v.SetPrivateData=Write<ID3D12CommandList>;v.GetType=Type;return v;}();
        static ID3D12GraphicsCommandListVtbl gv=[] {ID3D12GraphicsCommandListVtbl v{};v.Release=Release;
            v.GetPrivateData=Read<ID3D12GraphicsCommandList>;v.SetPrivateData=Write<ID3D12GraphicsCommandList>;return v;}();
        base.api.lpVtbl=&bv;base.owner=this;graphics.api.lpVtbl=&gv;graphics.owner=this;
    }
};
struct Queue {
    ID3D12CommandQueue api{};
    D3D12_COMMAND_LIST_TYPE type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    static D3D12_COMMAND_QUEUE_DESC* STDMETHODCALLTYPE Desc(ID3D12CommandQueue* p,D3D12_COMMAND_QUEUE_DESC* out){
        *out={};out->Type=reinterpret_cast<Queue*>(p)->type;return out;
    }
    static HRESULT STDMETHODCALLTYPE Signal(ID3D12CommandQueue*,ID3D12Fence*,UINT64 value){
        Check(order.size()>=2&&order.substr(order.size()-2)=="ES","execute/source before exact Signal");
        Check(value!=0,"Signal has reserved value");++signals;order+='F';return signal_failure?E_FAIL:S_OK;
    }
    Queue(){static ID3D12CommandQueueVtbl v=[] {ID3D12CommandQueueVtbl t{};t.GetDesc=Desc;t.Signal=Signal;return t;}();api.lpVtbl=&v;}
};
void NativeExecute(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*){
    ++native_calls;order+='E';
    // A Reset can detach the record after capture but before completion. The
    // actual production Complete must not drop its in-flight ownership early.
    if(reset_between_locks){auto& r=g_lifecycle_command_lists[0];Check(r.in_flight_submissions==1,"capture pins before native");r.detached=true;}
}
#include "production_submit.inl"
void Reset(){
    Check(!g_external_training_pending.load()&&!g_external_training_list&&!g_external_training_queue,
          "batch fixture has no external training target; actual inactive path retained");
    g_nr_timing={};g_nr_timing_recent_next=0;g_nr_timing_sequence=0;
    for(auto& r:g_lifecycle_command_lists)r={};for(auto& q:g_lifecycle_queues)q={};for(auto& q:g_graphics_queues)q={};
    g_graphics_queue_identity=1;g_lifecycle_tracking_uncertain=false;g_lifecycle_active_command_lists=1;
    g_live_graphics_queue_count=0;g_queue_affinity_marker_failures=0;g_queue_affinity_actual_mismatches=0;
    g_duplicate_execute_list_detections=0;g_command_list_replay_detections=0;
    g_original_queue_execute[0]=NativeExecute;marker_failure=signal_failure=reset_between_locks=false;
    before_capture=nullptr;
    order.clear();native_calls=source_calls=boundary_before=boundary_after=signals=0;
}
void Submit(Queue& q,List& l){ID3D12CommandList* list=&l.base.api;ProcessSubmittedCommandLists(0,&q.api,1,&list);}
void Own(List& l,NrContext& c,NrSlot& s){
    l.token=17;c.pending_recording_token=l.token;
    auto& r=g_lifecycle_command_lists[0];r.token=l.token;r.queue_proof=RecordingQueueProof::CompletedDependencies;
    r.contexts[0]=&c;r.context_count=1;r.slots[0]=&s;r.slot_count=1;
}
const nr_timing::SubmitBatch& Latest(){const auto& s=g_nr_timing.submissions;return s.recent[(s.count-1)%nr_timing::kSubmitRecentCount];}
int main(){
    InitializeCriticalSection(&g_chain_cs);
    Reset();Queue q,q2;List plain;
    Submit(q,plain);
    Check(native_calls==1&&source_calls==1&&boundary_before==1&&boundary_after==1&&order=="BESZ","INDEPENDENT_OBSERVERS_ORACLE");
    Check(plain.qi_calls==0&&plain.marker.identity==1&&plain.marker.stable_submissions==1,"UNRELATED_QI_BUDGET_ORACLE");
    Check(Latest().direct_absent==1&&Latest().qi==0&&Latest().scanned==1&&Latest().chain_sections==1,"batch exact light metrics");
    Submit(q,plain);Submit(q2,plain);
    Check(plain.marker.identity==2&&plain.marker.flags==kQueueMarkerMigrated,"AFFINITY_MIGRATION_ORACLE");
    for(unsigned i=0;i<80;++i)Submit(q,plain);
    Check(g_nr_timing.submissions.count==83&&g_nr_timing.submissions.recent_count==32,"bounded independent batch ring");
    Check(g_nr_timing.stages[static_cast<unsigned>(nr_timing::Stage::ChainLock)].count==83,"existing acquisition metric retained");
    Check(g_nr_timing.recent_count==0,"fast batch ring does not evict existing carrier records");
    FeedbackSession feedback;FeedbackAppendTiming(feedback,"cpu");
    Check(feedback.text.find("cpu.submitBatch.recent.0=epoch:1,batch:52,")!=std::string::npos&&
          feedback.text.find("cpu.submitBatch.recent.31=epoch:1,batch:83,")!=std::string::npos&&
          feedback.text.find("cpu.timing.chain-lock=count:83")!=std::string::npos,"actual feedback exports bounded ordered batches and existing metrics");
    Reset();List unknown;unknown.base_result=E_FAIL;Submit(q,unknown);
    Check(unknown.qi_calls==1&&unknown.releases==1&&unknown.marker.identity==1&&(Latest().reasons&nr_timing::UnknownList),"UNKNOWN_FALLBACK_ORACLE");
    Reset();List malformed;malformed.token=9;malformed.malformed=true;Submit(q,malformed);
    Check(malformed.qi_calls==1&&signals==0&&(Latest().reasons&nr_timing::UnknownList),"malformed token not absence proof");
    Reset();List other;other.type=D3D12_COMMAND_LIST_TYPE_COMPUTE;Submit(q,other);
    Check(other.qi_calls==1&&other.marker.identity==1,"non Direct list original fallback");
    Reset();List failed;failed.token=9;failed.qi_fail=true;Submit(q,failed);
    Check(native_calls==1&&source_calls==1&&signals==0&&(Latest().reasons&nr_timing::UnknownList),"QI failure keeps native observers");
    Reset();List lost;lost.marker.identity=5;marker_failure=true;Submit(q,lost);
    Check(g_lifecycle_tracking_uncertain&&lost.marker.identity==0&&native_calls==1&&source_calls==1,"marker failure clears stale history and latches");
    Reset();List reuse;Submit(q,reuse);NrContext reuse_context;NrSlot reuse_slot;Own(reuse,reuse_context,reuse_slot);Submit(q,reuse);
    Check(signals==1&&reuse.qi_calls==2&&g_lifecycle_command_lists[0].submission_count==1,"RESET_REUSE_ORACLE");
    Reset();List swap;NrContext swap_context;NrSlot swap_slot;Own(swap,swap_context,swap_slot);
    static List* changed=nullptr;changed=&swap;
    before_capture=[] {changed->token=101;};Submit(q,swap);
    Check(signals==0&&g_lifecycle_tracking_uncertain&&g_lifecycle_command_lists[0].submission_count==0,"CAPTURE_REVALIDATES_TOKEN_ORACLE");
    Reset();List clear;NrContext clear_context;NrSlot clear_slot;Own(clear,clear_context,clear_slot);
    changed=&clear;before_capture=[] {changed->token=0;};Submit(q,clear);
    Check(signals==0&&!g_lifecycle_tracking_uncertain&&g_lifecycle_command_lists[0].submission_count==0,"capture does not use stale token snapshot");
    Reset();List fallback_nr;NrContext fallback_context;NrSlot fallback_slot;Own(fallback_nr,fallback_context,fallback_slot);
    fallback_nr.base_result=E_FAIL;Submit(q,fallback_nr);
    Check(signals==1&&!g_lifecycle_tracking_uncertain&&fallback_nr.qi_calls==2,"unknown base result preserves graphics token authority");
    Reset();List nr,empty;NrContext c;NrSlot s;Own(nr,c,s);
    ID3D12CommandList* mixed[]={&empty.base.api,&nr.base.api};
    ProcessSubmittedCommandLists(0,&q.api,2,mixed);
    Check(signals==1&&order=="BESFZ"&&!c.awaiting_fence_signal&&!s.awaiting_fence_signal&&c.pending_recording_token==0,"MIXED_BATCH_SIGNAL_ORACLE");
    Check(g_lifecycle_command_lists[0].submission_count==1&&g_lifecycle_command_lists[0].in_flight_submissions==0,"real capture/completion lifecycle");
    Check(nr.qi_calls==2&&nr.releases==2&&empty.qi_calls==1&&Latest().qi==3&&Latest().scanned==4,"all actual initial/capture scans counted");
    Submit(q,nr);
    Check(g_command_list_replay_detections==1&&g_lifecycle_tracking_uncertain&&c.lifecycle_dependencies_quarantined&&signals==2,"REPLAY_ORACLE");
    Reset();List dup;NrContext dc;NrSlot ds;Own(dup,dc,ds);ID3D12CommandList* duplicates[]={&dup.base.api,&dup.base.api};
    ProcessSubmittedCommandLists(0,&q.api,2,duplicates);
    Check(g_duplicate_execute_list_detections==1&&dc.lifecycle_dependencies_quarantined&&native_calls==1&&signals==1,"duplicate preserved quarantine");
    Reset();List pending;NrContext pc;NrSlot ps;Own(pending,pc,ps);pc.complete=false;Submit(q,pending);
    Check(pc.lifecycle_dependencies_quarantined&&g_lifecycle_tracking_uncertain&&signals==1,"completed dependency must still be complete");
    Reset();List missing;missing.token=99;Submit(q,missing);
    Check(g_lifecycle_tracking_uncertain&&native_calls==1&&signals==0&&(Latest().reasons&nr_timing::CaptureFailed)&&Latest().qi==3,"unknown token capture/quarantine fallback");
    Reset();List reset;NrContext rc;NrSlot rs;Own(reset,rc,rs);reset_between_locks=true;Submit(q,reset);
    Check(g_lifecycle_command_lists[0].token==0&&!rc.awaiting_fence_signal&&signals==1,"detached record released only after Signal");
    Reset();List sf;NrContext sc;NrSlot ss;Own(sf,sc,ss);signal_failure=true;Submit(q,sf);
    Check(g_lifecycle_tracking_uncertain&&sc.awaiting_fence_signal&&ss.awaiting_fence_signal&&g_lifecycle_command_lists[0].in_flight_submissions==1,"failed Signal retains lifetime pins");
    Reset();List moved;NrContext mc;NrSlot ms;Own(moved,mc,ms);
    g_lifecycle_command_lists[0].queue_proof=RecordingQueueProof::StableHistory;
    g_lifecycle_command_lists[0].expected_queue_identity=2;Submit(q,moved);
    Check(g_lifecycle_tracking_uncertain&&mc.lifecycle_dependencies_quarantined&&native_calls==1&&signals==1,"immutable queue prediction remains authoritative");
    Reset();List many[300];ID3D12CommandList* large[300];
    for(unsigned i=0;i<300;++i)large[i]=&many[i].base.api;
    ProcessSubmittedCommandLists(0,&q.api,300,large);
    Check(Latest().scanned==300&&Latest().direct_absent==300&&Latest().qi==0&&native_calls==1,"large batch does not truncate classification or native Execute");
    Reset();List concurrent;
    auto learn=[&](unsigned long long identity){for(unsigned i=0;i<500;++i){EnterCriticalSection(&g_chain_cs);
        RememberLastSubmissionQueue(&concurrent.base.api,identity);LeaveCriticalSection(&g_chain_cs);}};
    std::thread one(learn,1),two(learn,2);one.join();two.join();
    Check(concurrent.marker.flags==kQueueMarkerMigrated&&concurrent.marker.stable_submissions>0&&concurrent.marker_writes==1000,"concurrent base affinity cannot lose migration bit");
    Reset();List off;g_lifecycle_active_command_lists=0;Submit(q,off);
    Check(order=="BESZ"&&off.base_reads==0&&g_nr_timing.submissions.count==0,"global zero does not skip source or boundary");
    Reset();q.type=D3D12_COMMAND_LIST_TYPE_COMPUTE;List compute;Submit(q,compute);
    Check(order=="BESZ"&&compute.base_reads==0,"non Direct queue keeps all observers");q.type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    Reset();{NrSubmitTiming observation(1);NrTimingSetProbePolicy(1,2);}
    Check(g_nr_timing.submissions.count==0&&g_nr_timing.excluded_old_epoch==1,"old batch excluded across timing phase reset");
    DeleteCriticalSection(&g_chain_cs);
    std::printf("SUBMIT_BATCH_CPU PASS checks=%u GPU=0 actualProcessCaptureComplete=1\n",checks);
}
