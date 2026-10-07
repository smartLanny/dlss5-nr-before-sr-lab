// Actual production predicate, collector, binding retirement and POD publisher.
// Only OS/owner-maintenance services are controlled; no D3D/NGX/game execution.
#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include "nr_retirement_evidence.h"
#include "nr_feedback_limits.h"
#define NR_HAS_RETIREMENT_EVIDENCE 1
using Id=nr_retirement::Id;
struct NgxHandle{} handle;
struct ID3D12GraphicsCommandList{} list;
struct ID3D12Fence{Id completed=9;};
unsigned fence_reads=0,ready_calls=0,invalidations=0,drops=0,releases=0,logs=0,checks=0;
Id now=100;
Id GetTickCount64(){return now;}
Id ID3D12Fence_GetCompletedValue(ID3D12Fence* f){++fence_reads;return f->completed;}
struct Lock{unsigned depth=0;}g_chain_cs;
void EnterCriticalSection(Lock* l){++l->depth;}
void LeaveCriticalSection(Lock* l){if(!l->depth)std::abort();--l->depth;}
struct LifecycleQueue{void* queue=nullptr;ID3D12Fence* fence=nullptr;Id identity=7;};
struct NrContext;
struct NrSlot{void* key=nullptr;NrContext* dual_owner=nullptr;unsigned recording_command_lists=0;
    bool awaiting_fence_signal=false;LifecycleQueue* last_submission_queue=nullptr;Id completion_fence_value=0;};
struct NrBindingPacket{NrSlot lease{};struct{const void* primary_owner=nullptr;const void* secondary_owner=nullptr;}plan{};bool valid=false;};
struct NrBindingPool{NrBindingPacket packets[8]{};};
namespace nr::final_residual {
#include "temporal_types.inl"
struct Ptr{ID3D12Fence* p=nullptr;ID3D12Fence* Get()const{return p;}explicit operator bool()const{return p!=nullptr;}};
struct RecordedTemporal{
    struct Impl{struct Slot{Id recording=0,value=0;Ptr fence;};std::array<Slot,3> slots{};
        bool fault=false,recording_unsafe=false;int pending_slot=-1;void* amd=nullptr;};
    std::unique_ptr<Impl> p_=std::make_unique<Impl>();
    RetirementInspection inspect_retirement()const noexcept;
};
#include "temporal_inspector.inl"
}
namespace nr_post_product {
struct Recording{Id lifetime=0,list=0,reset=0,device=0,queue=0;explicit operator bool()const{return lifetime!=0;}};
struct Flight{Recording recording{};ID3D12Fence* fence=nullptr;Id fence_value=0;bool submitted=false;};
struct Owner{void* backend=nullptr;void* normalizer=nullptr;bool poisoned=false;std::array<Flight,3> flights{};};
}
struct NrContext{
    const NgxHandle* sr_handle=nullptr;Id instance_id=0,parent_instance_id=0,feature_generation=13;
    unsigned config_generation=12,layer_index=0,width=1920,height=1080,evaluations=4498;
    int mode=2;bool retired=false,create_dependencies_quarantined=false,lifecycle_dependencies_quarantined=false;
    unsigned recording_command_lists=0;Id pending_recording_token=0,completion_fence_value=0,temporal_token=0,temporal_queue=0;
    bool awaiting_fence_signal=false,post_ready=true;
    LifecycleQueue* last_submission_queue=nullptr;NrSlot slots[8]{};NrBindingPool* binding_pool=nullptr;
    nr_post_product::Owner* private_sr=nullptr;nr::final_residual::RecordedTemporal* temporal=nullptr;
};
NrContext g_contexts[16]{},g_pending_destroy_contexts[16]{};
constexpr unsigned kLifecycleQueueCount=8;
struct LifecycleCommandList{Id token=0,expected_queue_identity=0,submission_count=0;
    NrContext* contexts[32]{};NrSlot* slots[32]{};unsigned context_count=0,slot_count=0,in_flight_submissions=0;
    bool detached=false,execution_attempted_without_proof=false;};
LifecycleCommandList g_lifecycle_command_lists[128]{};
std::atomic<bool> g_lifecycle_tracking_uncertain{false};
std::atomic<unsigned> g_nr_context_releases_in_progress{0};
std::atomic<Id> g_aux_parent_retirements{0};
struct{void* device=reinterpret_cast<void*>(0xD);}g_codec;
void Log(const char*,...){++logs;}
void MarkLifecycleUncertain(const char*){g_lifecycle_tracking_uncertain=true;}
void DrainNgxFeatureRetirementsLocked(){}
void CollectCompletedDeltaStatsLocked(){}
// This diagnostic-only fixture has no native COM lifetime events. The actual
// native death consumer is covered by the complete-Core WARP queue host.
void CollectDeadNativeRecordingsLocked(){}
namespace nr_temporal{void Maintain(NrContext&) {}}
namespace nr_spatial_experiment{void SweepIdle(){}}
namespace nr_dual{void RebindSlotOwners(NrContext& c){for(auto& s:c.slots)s.dual_owner=&c;}}
namespace nr_post_product{
bool Ready(const NrContext& c){++ready_calls;return c.post_ready;}
void Maintain(NrContext&){}
#include "private_inspector.inl"
}
void DropBindingPacket(NrBindingPacket& p){++drops;p={};}
void DestroyContext(NrContext& c){if(g_chain_cs.depth)std::abort();++releases;c={};}
void NrRetirementBeginDecisionLocked(const NrContext&);
void NrRetirementDecisionLocked(const NrContext&,Id,bool=false,bool=false);
void NrRetirementEndDecisionLocked();
void NrRetirementFenceObservedLocked(ID3D12Fence*,unsigned long long);
void NrRetirementContextFenceObservedLocked(const NrContext&,unsigned long long);
void NrRetirementPublishLocked(bool=false);
bool CanDestroyBindingsLocked(const NrContext&);
bool InvalidateSlotBindingsLocked(const NrSlot&);
#include "predicate.inl"
#include "bindings.inl"
#include "runtime.inl"
#include "collector.inl"
struct FeedbackSession{std::unique_ptr<nr_retirement::Snapshot> retirement_scratch{new(std::nothrow) nr_retirement::Snapshot};
    char report[nr_feedback_limits::kMetadataBytes]{};std::size_t report_used=0;bool report_truncated=false;};
#include "append.inl"
#include "formatter.inl"
void Check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(3);}}
void Clear(){for(auto& c:g_contexts)c={};for(auto& c:g_pending_destroy_contexts)c={};
    for(auto& r:g_lifecycle_command_lists)r={};for(auto& e:g_nr_retirement_observed)e={};
    for(auto& f:g_nr_retirement_fences)f={};std::memset(&g_nr_retirement_published,0,sizeof(g_nr_retirement_published));g_nr_retirement_dirty=false;
    g_nr_retirement_deciding=nullptr;g_nr_retirement_observations=g_nr_retirement_suppressed=g_nr_retirement_publication=0;
    g_lifecycle_tracking_uncertain=false;g_nr_context_releases_in_progress=0;
    fence_reads=ready_calls=invalidations=drops=releases=logs=0;now+=2000;
}
NrContext& Retired(){auto& c=g_contexts[0];c.sr_handle=&handle;c.instance_id=73;c.retired=true;return c;}
void Collect(){EnterCriticalSection(&g_chain_cs);CollectRetiredContextsLocked();LeaveCriticalSection(&g_chain_cs);}
bool Pending(){for(const auto& c:g_pending_destroy_contexts)if(c.sr_handle)return true;return false;}
void MeasureDiagnosticCpu(){
    constexpr unsigned iterations=50000;
    Clear();const auto empty_start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<iterations;++i)NrRetirementPublishLocked(false);
    const auto empty_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-empty_start).count();
    Check(!g_nr_retirement_published.sequence&&!fence_reads&&!ready_calls,"no-retired diagnostic loop does not scan owners or poll fences");
    auto& c=Retired();c.recording_command_lists=1;NrBindingPool pool;c.binding_pool=&pool;
    for(unsigned i=0;i<8;++i){c.slots[i].key=&list;c.slots[i].recording_command_lists=1;
        pool.packets[i].valid=true;pool.packets[i].lease.recording_command_lists=1;}
    Collect();
    const auto sequence=g_nr_retirement_published.sequence;const auto reads=fence_reads,ready=ready_calls;
    const auto stable_start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<iterations;++i){NrRetirementBeginDecisionLocked(c);
        NrRetirementDecisionLocked(c,nr_retirement::ContextRecording);NrRetirementEndDecisionLocked();NrRetirementPublishLocked(false);}
    const auto stable_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-stable_start).count();
    Check(g_nr_retirement_published.sequence==sequence&&fence_reads==reads&&ready_calls==ready,"stable retired diagnostic loop stays on bounded fingerprint and throttle path");
    std::printf("CPU rough new-functions only: iterations=%u no-retired-total-ns=%lld per-call-ns=%.1f stable-retired-total-ns=%lld per-call-ns=%.1f (one retired context, 8 slots/8 valid packets); controlled types/clock/services, not game or FPS evidence\n",
        iterations,static_cast<long long>(empty_ns),double(empty_ns)/iterations,static_cast<long long>(stable_ns),double(stable_ns)/iterations);
}
int main(){using namespace nr_retirement;
#if defined(NR_RETIREMENT_SMALL_BINDING_DETAIL)
    // The runner lowers only the extracted detail guard to one entry, so the
    // real production overflow branch can run with legal, small owner tables.
    Clear();auto& c=Retired();c.recording_command_lists=1;c.slots[0].key=&list;c.slots[0].recording_command_lists=1;
    NrBindingPool own,other;c.binding_pool=&own;own.packets[0].valid=true;own.packets[0].lease.recording_command_lists=1;
    auto& external=g_contexts[1];external.sr_handle=&handle;external.instance_id=74;external.binding_pool=&other;
    other.packets[0].valid=true;other.packets[0].lease.recording_command_lists=1;other.packets[0].plan.primary_owner=&c.slots[0];
    auto& indirect=g_lifecycle_command_lists[0];indirect.token=46;indirect.contexts[0]=&external;indirect.context_count=1;
    indirect.slots[0]=&other.packets[0].lease;indirect.slot_count=1;NrRetirementRecordListLocked(indirect,&list);
    Collect();const auto& row=g_nr_retirement_published.context[0];
    Check(g_nr_retirement_published.bindings==1&&g_nr_retirement_published.binding_overflow==1,"bounded binding detail overflow is counted");
    Check((row.blocked&(OtherContextBinding|SlotRecording))==(OtherContextBinding|SlotRecording)&&
        (row.evaluated&OtherContextBinding)&&!(row.unknown&OtherContextBinding),"binding detail overflow retains complete known lease and slot blockers");
    Check(g_nr_retirement_published.recordings==1&&g_nr_retirement_published.recording[0].token==46&&
        !g_nr_retirement_published.recording[0].direct_contexts&&g_nr_retirement_published.recording[0].indirect_contexts==1,
        "binding detail overflow retains actual indirect live-context list holder");
    Check(row.first_block==ContextRecording&&c.recording_command_lists==1&&!Pending()&&!ready_calls&&!invalidations,"binding detail overflow does not alter original release gate");
    std::printf("PASS binding detail overflow: %u checks; only extracted diagnostic capacity guard reduced\n",checks);return 0;
#else
    Clear();Collect();Check(!g_nr_retirement_published.sequence&&!fence_reads&&!ready_calls,"no retired path creates no snapshot or fence/Ready work");
    auto& c=Retired();ID3D12Fence fence;LifecycleQueue queue{nullptr,&fence,7};
    c.last_submission_queue=&queue;c.completion_fence_value=9;c.recording_command_lists=1;c.pending_recording_token=45;c.awaiting_fence_signal=true;
    c.slots[0].key=&list;c.slots[0].recording_command_lists=1;c.slots[0].last_submission_queue=&queue;c.slots[0].completion_fence_value=9;
    NrBindingPool own,other;c.binding_pool=&own;own.packets[0].valid=true;own.packets[0].lease.recording_command_lists=1;
    own.packets[0].lease.awaiting_fence_signal=true;own.packets[0].plan.primary_owner=&c.slots[0];
    auto& external=g_contexts[1];external.sr_handle=&handle;external.instance_id=74;external.binding_pool=&other;
    other.packets[0].valid=true;other.packets[0].lease.recording_command_lists=1;other.packets[0].plan.primary_owner=&c.slots[0];
    nr_post_product::Owner private_owner;c.private_sr=&private_owner;private_owner.backend=&list;
    private_owner.flights[0].recording={45,0x123,8,0xD,7};private_owner.flights[0].fence=&fence;private_owner.flights[0].fence_value=9;private_owner.flights[0].submitted=true;
    nr::final_residual::RecordedTemporal temporal;c.temporal=&temporal;c.temporal_queue=7;temporal.p_->slots[0].recording=45;temporal.p_->slots[0].fence.p=&fence;temporal.p_->slots[0].value=9;
    auto& record=g_lifecycle_command_lists[0];record.token=45;record.contexts[0]=&c;record.context_count=1;record.slots[0]=&c.slots[0];record.slot_count=1;record.submission_count=1;
    NrRetirementRecordListLocked(record,&list);NrRetirementEvaluationLocked(c,4498);NrRetirementSubmittedLocked(c,queue,9);
    auto& indirect=g_lifecycle_command_lists[1];indirect.token=46;indirect.contexts[0]=&external;indirect.context_count=1;
    indirect.slots[0]=&other.packets[0].lease;indirect.slot_count=1;NrRetirementRecordListLocked(indirect,&list);
    const auto before_inspect=fence_reads;(void)nr_post_product::InspectRetirement(c);(void)temporal.inspect_retirement();
    Check(fence_reads==before_inspect,"const inspectors copy stored state without polling");
    auto evidence_heap=std::make_unique<Snapshot>();auto& evidence=*evidence_heap;
    Collect();SnapshotNrRetirementEvidence(evidence);const auto& row=evidence.context[0];
    Check(row.first_block==ContextRecording,"firstBlock comes from original recording branch");
    const Id expected=ContextRecording|ContextPending|ContextAwaitSignal|SlotRecording|Bindings|BindingRecording|BindingAwaitSignal|OtherContextBinding|PrivateRecording|TemporalRecording;
    Check((row.blocked&expected)==expected,"full mask preserves simultaneous pending/slot/binding/private/temporal blockers");
    Check(!row.post_evaluated&&(row.unknown&PostReady)&&!ready_calls,"earlier branch never calls Ready or claims unobserved post-ready");
    Check(row.lease.fence.sampled&&row.lease.fence.completed==9&&!(row.blocked&ContextFence),"exact fence complete does not clear command-list recording blocker");
    Check(evidence.recordings==2&&evidence.recording[0].token==45&&evidence.recording[0].address==reinterpret_cast<Id>(&list)&&evidence.recording[0].direct_contexts==1,"actual lifecycle holder identity reaches POD");
    Check(evidence.recording[1].token==46&&evidence.recording[1].direct_contexts==0&&evidence.recording[1].indirect_contexts==1,"live other-context packet lease holder is included indirectly");
    Check(!evidence.recording[0].native_identity_known,"unobserved native lifetime/reset is explicit unknown");
    Check(!Pending()&&!releases&&!drops&&c.recording_command_lists==1&&c.slots[0].recording_command_lists==1,"snapshot does not change release permission or lease references");
    const auto reads=fence_reads,ready_before=ready_calls,drops_before=drops;for(unsigned i=0;i<8;++i)SnapshotNrRetirementEvidence(evidence);
    Check(fence_reads==reads&&ready_calls==ready_before&&drops==drops_before,"F8 reads do not poll or call Ready/Drop");
    const auto sequence=evidence.sequence;for(unsigned i=0;i<8;++i)Collect();
    Check(g_nr_retirement_published.sequence==sequence&&fence_reads==reads,"stable state does not repeat global detailed scans");
    now+=1000;Collect();Check(g_nr_retirement_published.sequence>sequence&&logs==1,"stable periodic refresh is bounded and does not log every refresh");
    c.pending_recording_token=0;c.awaiting_fence_signal=false;Collect();Check(!Pending()&&g_nr_retirement_published.context[0].first_block==ContextRecording,"submitted final-release reproducer stays blocked without successful Reset");
    c.recording_command_lists=0;Collect();Check(g_nr_retirement_published.context[0].first_block==SlotRecording&&!ready_calls,"slot recording branch retains original short circuit");
    c.slots[0].recording_command_lists=0;Collect();Check(g_nr_retirement_published.context[0].first_block==Bindings&&!ready_calls,"binding failure does not call post Ready");
    own.packets[0].lease.recording_command_lists=0;own.packets[0].lease.awaiting_fence_signal=false;
    c.private_sr=nullptr;c.temporal=nullptr;Collect();
    Check(g_nr_retirement_published.context[0].first_block==OtherContextBinding&&ready_calls==1&&invalidations==1&&!drops&&!Pending(),"actual external binding invalidation blocks once after one original Ready");
    other.packets[0].lease.recording_command_lists=0;Collect();
    Check(Pending()&&ready_calls==2&&invalidations==9&&drops==2&&!releases,"successful original invalidation only queues destruction");
    DrainPendingContextDestructions();Check(releases==1&&!Pending(),"vendor release remains outside original chain lock");
    Clear();auto& removed=Retired();removed.last_submission_queue=&queue;removed.completion_fence_value=9;fence.completed=UINT64_MAX;
    Collect();SnapshotNrRetirementEvidence(evidence);Check(evidence.context[0].first_block==ContextFence&&evidence.context[0].lease.fence.completed==UINT64_MAX&&!Pending(),"UINT64_MAX retained raw and never treated as completion");
    Check(!evidence.context[0].last_completed_value,"removed device does not publish a completed identity");
    Clear();auto& cached=Retired();fence.completed=9;cached.last_submission_queue=&queue;cached.completion_fence_value=9;
    cached.slots[0].key=&list;cached.slots[0].recording_command_lists=1;Collect();
    Check(g_nr_retirement_published.context[0].lease.fence.reused,"publication reuses original decision fence read");
    fence.completed=8;g_nr_retirement_dirty=true;NrRetirementPublishLocked(false);
    Check(!g_nr_retirement_published.context[0].lease.fence.reused&&g_nr_retirement_published.context[0].lease.fence.completed==8,"same-millisecond later publication cannot reuse stale original fence read");
    Clear();auto& quarantined=Retired();quarantined.create_dependencies_quarantined=true;quarantined.lifecycle_dependencies_quarantined=true;
    quarantined.last_submission_queue=&queue;quarantined.completion_fence_value=9;Collect();SnapshotNrRetirementEvidence(evidence);
    Check(evidence.context[0].first_block==CreateQuarantine&&(evidence.context[0].blocked&(CreateQuarantine|LifecycleQuarantine))==(CreateQuarantine|LifecycleQuarantine),"quarantine first branch and complete mask agree");
    Check(!fence_reads&&!ready_calls&&(evidence.context[0].unknown&ContextFence),"quarantine diagnostic does not dereference unproven fence");
    Clear();auto& uncertain=Retired();uncertain.recording_command_lists=1;g_lifecycle_tracking_uncertain=true;Collect();
    Check(g_nr_retirement_published.context[0].first_block==TrackingUncertain&&!ready_calls,"collector uncertainty early return is observed without predicate calls");
    Clear();auto& releasing=Retired();releasing.recording_command_lists=1;g_nr_context_releases_in_progress=1;Collect();
    Check(g_nr_retirement_published.releasing==1&&(g_nr_retirement_published.context[0].blocked&ReleaseInProgress),"outside-lock release window is separate evidence");
    const auto prior_sequence=g_nr_retirement_published.sequence;g_lifecycle_tracking_uncertain=true;Collect();
    Check(g_nr_retirement_published.sequence>prior_sequence&&g_nr_retirement_published.context[0].first_block==TrackingUncertain,"uncertain transition refreshes immediately even on collector early return");
    private_owner={};private_owner.flights[0].fence=&fence;private_owner.flights[0].fence_value=9;releasing.private_sr=&private_owner;
    temporal.p_->slots[0].recording=0;releasing.temporal=&temporal;g_nr_retirement_dirty=true;Collect();
    Check((g_nr_retirement_published.context[0].unknown&(PrivateRecording|TemporalRecording))==(PrivateRecording|TemporalRecording)&&
        !(g_nr_retirement_published.context[0].blocked&(PrivateRecording|TemporalRecording)),"unsampled owner fence is unknown, not certified busy");
    Clear();Retired();for(unsigned i=0;i<16;++i){auto& p=g_pending_destroy_contexts[i];p.sr_handle=&handle;p.instance_id=100+i;p.retired=true;}
    Collect();Check(g_lifecycle_tracking_uncertain&&g_nr_retirement_published.pending_used==16&&g_nr_retirement_published.context[0].first_block==PendingCapacity,"original full pending queue records its own branch before uncertainty");
    // Exercise every existing table capacity with full-width values through the
    // actual F8 formatter, independently of the production ownership fixture.
    auto& full=g_nr_retirement_published;std::memset(&full,0,sizeof(full));full.contexts=kContexts;full.slots=kSlots;full.bindings=kBindings;full.recordings=kRecordings;full.private_flights=full.temporal_flights=kOwnerFlights;
    const Fence wide{UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,true,true};
    const Lease lease{wide,UINT_MAX,true};
    for(auto& v:full.context){v.instance=v.parent=v.feature=v.global_codec_device=v.handle=v.pending_token=v.last_eval=v.last_eval_frame=UINT64_MAX;v.blocked=v.evaluated=v.unknown=UINT64_MAX;v.first_block=ContextRecording;
        v.last_submit_queue=v.last_submit_value=v.last_completed_queue=v.last_completed_value=v.retired_tick=UINT64_MAX;v.lease=lease;v.configuration=v.layer=v.width=v.height=v.retire_reason=UINT_MAX;v.mode=INT_MIN;v.pending_destroy=v.post_evaluated=v.post_ready=true;}
    for(auto& v:full.slot){v.lease=lease;v.key=UINT64_MAX;v.context=v.index=UINT_MAX;}
    for(auto& v:full.binding){v.lease=lease;v.instance=v.feature=v.primary_instance=v.secondary_instance=UINT64_MAX;v.configuration=v.index=v.primary_slot=v.secondary_slot=UINT_MAX;v.primary_known=v.secondary_known=true;}
    for(auto& v:full.recording){v.token=v.address=v.expected_queue=v.submissions=v.contexts=v.direct_contexts=v.indirect_contexts=v.native_lifetime=v.native_reset=v.device=UINT64_MAX;v.in_flight=v.context_count=v.slot_count=UINT_MAX;v.detached=v.unproven_execution=v.native_identity_known=true;}
    const auto wide_flight=[&](OwnerFlight& v){v.instance=v.token=v.list=v.reset=v.device=v.queue=UINT64_MAX;v.fence=wide;v.index=UINT_MAX;v.submitted=v.poisoned=v.unsafe=true;};
    for(auto& v:full.private_flight)wide_flight(v);for(auto& v:full.temporal_flight)wide_flight(v);
    auto feedback=std::make_unique<FeedbackSession>();const auto before_reads=fence_reads,before_ready=ready_calls;FeedbackAppendRetirement(*feedback,"end");
    std::printf("retirement worst-width text=%zu reserved=%u truncated=%d POD=%zu\n",feedback->report_used,nr_feedback_limits::kRetirementTextBytes,feedback->report_truncated?1:0,sizeof(Snapshot));
    Check(!feedback->report_truncated&&feedback->report_used<=nr_feedback_limits::kRetirementTextBytes,"full-width retirement text fits separately reserved F8 tail");
    Check(std::strstr(feedback->report,"retirement.context.31=")&&std::strstr(feedback->report,"textOmitted=contexts:0,contextDetails:"),"all full-width context masks precede accurately omitted optional detail");
    Check(fence_reads==before_reads&&ready_calls==before_ready,"actual formatter only copies POD");
    Check(std::strstr(feedback->report,"nativeKnown:1")&&std::strstr(feedback->report,"18446744073709551615"),"formatter retains identity and raw removed fence values");
    auto allocation_failed=std::make_unique<FeedbackSession>();allocation_failed->retirement_scratch.reset();FeedbackAppendRetirement(*allocation_failed,"end");
    FeedbackAppend(*allocation_failed,"final.status=partial\n");Check(std::strstr(allocation_failed->report,"cpu-snapshot-allocation")&&std::strstr(allocation_failed->report,"final.status=partial"),"scratch allocation failure remains explicit and preserves final status");
    MeasureDiagnosticCpu();
    std::printf("PASS retirement evidence: %u checks; POD=%zu bytes; worst-width end text=%zu bytes; CPU controlled services, no GPU/NGX/game\n",checks,sizeof(Snapshot),feedback->report_used);
#endif
}
