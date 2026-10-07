// Included after the complete production Core translation unit by the runner.
// Uses actual WARP queues/fences and the production admission/submit functions.
// No NGX/model is loaded and no ReShade host or game is launched.
#include <dxgi1_4.h>
#include <cstdlib>
#include <string>
#include <memory>

namespace queue_test {
unsigned checks = 0;
unsigned native_executes = 0;
void Check(bool condition, const char *name) {
    ++checks;
    if (!condition) {
        std::printf("FAIL %s\n", name);
        std::fflush(stdout);
        std::exit(41);
    }
}
void Hr(HRESULT result, const char *name) {
    if (FAILED(result)) {
        std::printf("API_FAIL %s %08lx\n", name, static_cast<unsigned long>(result));
        std::exit(2);
    }
}
void STDMETHODCALLTYPE NativeExecute(ID3D12CommandQueue *queue, UINT count,
                                     ID3D12CommandList *const *lists) {
    ++native_executes;
    ID3D12CommandQueue_ExecuteCommandLists(queue, count, lists);
}
void Wait(ID3D12Fence *fence, UINT64 value) {
    Check(fence != nullptr && value != 0, "wait has exact fence");
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Check(event != nullptr, "fence event created");
    Hr(ID3D12Fence_SetEventOnCompletion(fence, value, event), "fence event");
    Check(WaitForSingleObject(event, 5000) == WAIT_OBJECT_0, "exact fence completed");
    CloseHandle(event);
    Check(ID3D12Fence_GetCompletedValue(fence) != UINT64_MAX, "device not removed");
}
enum class NativeNoticeFailure { None, QueryInterface, RegisterCallback };
ID3D12GraphicsCommandList* notice_failure_native=nullptr;
ID3DDestructionNotifier* notice_failure_notifier=nullptr;
unsigned notice_failure_hits=0;
using NoticeQueryFn=HRESULT (STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*,REFIID,void**);
using NoticeRegisterFn=HRESULT (STDMETHODCALLTYPE *)(ID3DDestructionNotifier*,PFN_DESTRUCTION_CALLBACK,void*,UINT*);
NoticeQueryFn notice_original_query=nullptr;
NoticeRegisterFn notice_original_register=nullptr;
HRESULT STDMETHODCALLTYPE UnavailableNoticeQuery(ID3D12GraphicsCommandList* native,REFIID iid,void** output) {
    if(native==notice_failure_native && IsEqualGUID(iid,IID_ID3DDestructionNotifier)) {
        ++notice_failure_hits;
        if(output)*output=nullptr;
        return E_NOINTERFACE;
    }
    return notice_original_query(native,iid,output);
}
HRESULT STDMETHODCALLTYPE UnavailableNoticeRegister(ID3DDestructionNotifier* notifier,
        PFN_DESTRUCTION_CALLBACK callback,void* data,UINT* id) {
    if(notifier==notice_failure_notifier) {
        ++notice_failure_hits;
        if(id)*id=0;
        return E_FAIL;
    }
    return notice_original_register(notifier,callback,data,id);
}
bool RegisterTestNativeList(ID3D12GraphicsCommandList* native,const CommandStateMetaContract& meta,
        const CommandCreationInfo& created,NativeNoticeFailure failure) {
    if(failure==NativeNoticeFailure::None)return RegisterCreatedCommandStateList(native,meta,created);
    // Only the real registration boundary is faulted, for this exact native
    // object/IID or notifier. No death bit, ledger or production branch is set.
    const auto initialized=MH_Initialize();
    Check(initialized==MH_OK||initialized==MH_ERROR_ALREADY_INITIALIZED,"notice injection hook initialization");
    void* target=nullptr;
    if(failure==NativeNoticeFailure::QueryInterface) {
        notice_failure_native=native;target=reinterpret_cast<void*>(native->lpVtbl->QueryInterface);
        Check(MH_CreateHook(target,reinterpret_cast<void*>(&UnavailableNoticeQuery),
              reinterpret_cast<void**>(&notice_original_query))==MH_OK,"exact notifier QI failure hook created");
    } else {
        Hr(ID3D12GraphicsCommandList_QueryInterface(native,IID_ID3DDestructionNotifier,
            reinterpret_cast<void**>(&notice_failure_notifier)),"actual notifier queried before registration-failure injection");
        target=reinterpret_cast<void*>(notice_failure_notifier->lpVtbl->RegisterDestructionCallback);
        Check(MH_CreateHook(target,reinterpret_cast<void*>(&UnavailableNoticeRegister),
              reinterpret_cast<void**>(&notice_original_register))==MH_OK,"exact notifier callback failure hook created");
    }
    Check(MH_EnableHook(target)==MH_OK,"notice registration failure hook enabled");
    const bool registered=RegisterCreatedCommandStateList(native,meta,created);
    Check(MH_DisableHook(target)==MH_OK,"notice registration failure hook disabled");
    Check(MH_RemoveHook(target)==MH_OK,"notice registration failure hook removed");
    ReleaseCom(notice_failure_notifier);notice_failure_native=nullptr;
    Check(notice_failure_hits==1,"notice injection faulted exactly one target registration call");
    std::printf("NOTICE_FAILURE_INJECTION kind=%s targetCalls=%u hookRemoved=1; no native-death or NR-counter injection\n",
        failure==NativeNoticeFailure::QueryInterface?"exact-native-IID-QI":"exact-notifier-RegisterDestructionCallback",notice_failure_hits);
    return registered;
}
struct Fixture {
    IDXGIFactory4 *factory = nullptr;
    IDXGIAdapter *adapter = nullptr;
    ID3D12Device *device = nullptr;
    ID3D12CommandQueue *a = nullptr;
    ID3D12CommandQueue *b = nullptr;
    CommandStateMetaContract command_meta{};
    UINT64 next_creation_serial = 0;
    Fixture() {
        InitializeCriticalSection(&g_chain_cs);
        Hr(CreateDXGIFactory2(0, IID_IDXGIFactory4, reinterpret_cast<void **>(&factory)), "factory");
        Hr(IDXGIFactory4_EnumWarpAdapter(factory, IID_IDXGIAdapter, reinterpret_cast<void **>(&adapter)), "WARP");
        Hr(D3D12CreateDevice(reinterpret_cast<IUnknown *>(adapter), D3D_FEATURE_LEVEL_11_0,
            IID_ID3D12Device, reinterpret_cast<void **>(&device)), "device");
        command_meta.device_identity = g_next_command_device_identity.fetch_add(1);
        Hr(ID3D12Device_SetPrivateData(device, kCommandStateMetaContract,
            sizeof(command_meta), &command_meta), "controlled device lifetime metadata");
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        Hr(ID3D12Device_CreateCommandQueue(device, &desc, IID_ID3D12CommandQueue, reinterpret_cast<void **>(&a)), "queue A");
        Hr(ID3D12Device_CreateCommandQueue(device, &desc, IID_ID3D12CommandQueue, reinterpret_cast<void **>(&b)), "queue B");
        g_original_queue_execute[0] = NativeExecute;
        g_enabled_fast.store(true);
        g_carrier_evaluations.store(1);
        g_pipeline_stats_epoch.store(1);
        g_pipeline_stats_generation.store(1);
        g_pipeline_stats_mode.store(1);
    }
    ID3D12Fence *Fence(UINT64 value = 0) {
        ID3D12Fence *fence = nullptr;
        Hr(ID3D12Device_CreateFence(device, value, D3D12_FENCE_FLAG_NONE,
            IID_ID3D12Fence, reinterpret_cast<void **>(&fence)), "fence");
        return fence;
    }
    void Drain(ID3D12CommandQueue *queue) {
        ID3D12Fence *fence = Fence();
        Hr(ID3D12CommandQueue_Signal(queue, fence, 1), "drain signal");
        Wait(fence, 1);
        ReleaseCom(fence);
    }
    ~Fixture() {
        for (auto &queue : g_lifecycle_queues) {
#if defined(NR_HAS_SUBMITTED_FENCE_EVENT)
            if (queue.completion_event.registered_fence && queue.completion_event.event &&
                WaitForSingleObject(queue.completion_event.event, 1000) == WAIT_OBJECT_0)
                NrFenceEventNotification(queue.completion_event, true);
            Check(ReleaseNrSubmittedFenceEvent(queue.completion_event),
                  "fixture retires deferred event notifications before releasing the fence");
#endif
            ReleaseCom(queue.fence);
            ReleaseCom(queue.queue);
        }
        ReleaseCom(a); ReleaseCom(b); ReleaseCom(device);
        ReleaseCom(adapter); ReleaseCom(factory);
        DeleteCriticalSection(&g_chain_cs);
    }
};
struct List {
    ID3D12CommandAllocator *allocator = nullptr;
    ID3D12CommandAllocator *replacement = nullptr;
    ID3D12GraphicsCommandList *native = nullptr;
    explicit List(Fixture &f,NativeNoticeFailure failure=NativeNoticeFailure::None) {
        Hr(ID3D12Device_CreateCommandAllocator(f.device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_ID3D12CommandAllocator, reinterpret_cast<void **>(&allocator)), "allocator");
        Hr(ID3D12Device_CreateCommandList(f.device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator, nullptr, IID_ID3D12GraphicsCommandList, reinterpret_cast<void **>(&native)), "fresh list");
        // The real Create observer normally supplies these receipts. This host
        // owns real WARP lists but does not install the process-wide observer.
        CommandCreationInfo created{};
        created.serial = ++f.next_creation_serial;
        created.device_identity = f.command_meta.device_identity;
        created.method = 1;
        Hr(ID3D12GraphicsCommandList_SetPrivateData(native, kCommandCreationReceipt,
            sizeof(created), &created), "controlled native creation receipt");
        Check(RegisterTestNativeList(native, f.command_meta, created, failure),
              "actual native lifetime registration after successful Create");
    }
    void Close() { Hr(ID3D12GraphicsCommandList_Close(native), "close"); }
    void ResetWithNewAllocator(Fixture &f) {
        Hr(ID3D12Device_CreateCommandAllocator(f.device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_ID3D12CommandAllocator, reinterpret_cast<void **>(&replacement)), "replacement allocator");
        Hr(ID3D12GraphicsCommandList_Reset(native, replacement, nullptr), "reset with independent allocator");
    }
    ID3D12CommandList *Base() { return reinterpret_cast<ID3D12CommandList *>(native); }
    void Submit(ID3D12CommandQueue *queue) {
        ID3D12CommandList *list = Base();
        ProcessSubmittedCommandLists(0, queue, 1, &list);
    }
    void Prime(ID3D12CommandQueue *queue) {
        // Same truthful pre-submit queue observation as the ReShade callback,
        // followed unconditionally by an actual native submission.
        GraphicsQueueRecord *record = TrackGraphicsQueueLocked(queue);
        Check(record != nullptr, "queue observed");
        Check(RememberLastSubmissionQueue(native, record->identity), "queue history saved");
        ID3D12CommandList *list = Base();
        NativeExecute(queue, 1, &list);
    }
    void Detach() { DetachLifecycleCommandListLocked(native, "fixture destroy"); }
    ~List() { ReleaseCom(native); ReleaseCom(replacement); ReleaseCom(allocator); }
};
// These large production objects never live on the game-style worker stack.
NrContext context;

void OneShot() {
    Fixture f;
    List first(f), unknown(f), stable(f);
    ID3D12Fence *gate = f.Fence();
    Check(CanBeginContextUseLocked(first.native, context), "ONE_SHOT_RETIRED_ADMISSION");
    Check(RecordNrUseLocked(first.native, context, context.slots[0]), "fresh use pinned");
    Check(RecordVisibleTransferLocked(first.native, 1, 1), "fresh transfer bound");
    Check(!CanBeginContextUseLocked(unknown.native, context), "other pending recording rejected");
    first.Close();
    Hr(ID3D12CommandQueue_Wait(f.a, gate, 1), "fixture hold queue A");
    first.Submit(f.a);
    Check(!g_lifecycle_tracking_uncertain.load(), "fresh submission remains verified");
    Check(context.last_submission_queue && context.last_submission_queue->queue == f.a,
          "actual first queue bound at submit");
    Check(!context.awaiting_fence_signal && context.completion_fence_value != 0,
          "exact retirement signal published");
    Check(g_nr_transfer_submitted_frames.load() == 1, "fresh visible transfer submitted");
    Check(!IsContextFenceCompleteLocked(context), "fixture holds actual prior GPU proof");
    Check(!CanBeginContextUseLocked(unknown.native, context), "unknown list waits for retirement");
    stable.Close(); stable.Prime(f.a); stable.Prime(f.a);
    stable.ResetWithNewAllocator(f);
    Check(CanBeginContextUseLocked(stable.native, context), "stable same queue overlap retained");
    Check(RecordNrUseLocked(stable.native, context, context.slots[1]), "stable overlap pinned");
    Check(RecordVisibleTransferLocked(stable.native, 1, 1), "stable transfer bound");
    stable.Close(); stable.Submit(f.a);
    Check(!g_lifecycle_tracking_uncertain.load(), "stable overlap remains verified");
    Check(g_nr_transfer_submitted_frames.load() == 2, "stable visible transfer submitted");
    Check(!CanBeginContextUseLocked(unknown.native, context), "fresh list cannot overlap pending work");
    Hr(ID3D12Fence_Signal(gate, 1), "release fixture hold");
    Wait(context.last_submission_queue->fence, context.completion_fence_value);
    first.Detach(); stable.Detach();
    Check(CanBeginContextUseLocked(unknown.native, context), "next one-shot begins after retirement");
    Check(RecordNrUseLocked(unknown.native, context, context.slots[0]), "next fresh use pinned");
    Check(RecordVisibleTransferLocked(unknown.native, 1, 1), "next fresh transfer bound");
    unknown.Close(); unknown.Submit(f.b);
    Check(!g_lifecycle_tracking_uncertain.load(), "retired history permits actual queue B");
    Check(context.last_submission_queue && context.last_submission_queue->queue == f.b,
          "fresh queue B known only from actual submit");
    Wait(context.last_submission_queue->fence, context.completion_fence_value);
    unknown.Detach();
    Check(g_nr_transfer_submitted_frames.load() == 3, "three NR transfers reached real submit");
    Check(g_queue_affinity_actual_mismatches.load() == 0, "no guessed queue mismatch");
    Check(g_lifecycle_active_command_lists.load() == 0 && context.recording_command_lists == 0,
          "all one-shot owners detached");
    ReleaseCom(gate);
}

void Guards() {
    Fixture f;
    List list(f);
    Check(CanBeginContextUseLocked(list.native, context), "fresh complete context admitted");
    context.awaiting_fence_signal = true;
    Check(!CanBeginContextUseLocked(list.native, context), "unsignaled submission rejected");
    context.awaiting_fence_signal = false;
    context.pending_recording_token = 991;
    Check(!CanBeginContextUseLocked(list.native, context), "foreign pending owner rejected");
    context.pending_recording_token = 0;
    LifecycleQueue pending = {};
    pending.fence = f.Fence();
    context.last_submission_queue = &pending;
    context.completion_fence_value = 7;
    Check(!CanBeginContextUseLocked(list.native, context), "unfinished exact context fence rejected");
    Hr(ID3D12Fence_Signal(pending.fence, UINT64_MAX), "removed-value control");
    Check(!CanBeginContextUseLocked(list.native, context), "removed-value fence not completion");
    context.last_submission_queue = nullptr;
    context.completion_fence_value = 0;
    ReleaseCom(pending.fence);
    CommandListQueueMarker marker = {11, 100, kQueueMarkerMigrated};
    Hr(ID3D12GraphicsCommandList_SetPrivateData(list.native, kLastSubmissionQueueGuid,
        sizeof(marker), &marker), "migrated marker");
    Check(!CanBeginContextUseLocked(list.native, context), "migration stays permanently rejected");
    Hr(ID3D12GraphicsCommandList_SetPrivateData(list.native, kLastSubmissionQueueGuid,
        sizeof(marker) - 1, &marker), "malformed marker");
    Check(!CanBeginContextUseLocked(list.native, context), "malformed marker is not fresh evidence");
    Hr(ID3D12GraphicsCommandList_SetPrivateData(list.native, kLastSubmissionQueueGuid,
        0, nullptr), "clear fixture marker");
    context.slots[0].awaiting_fence_signal = true;
    Check(!RecordNrUseLocked(list.native, context, context.slots[0]), "unsignaled slot rejected before pin");
    unsigned long long token = 0;
    Check(!ReadLifecycleToken(list.native, token) && g_lifecycle_active_command_lists.load() == 0,
          "failed slot admission creates no owner");
    context.slots[0].awaiting_fence_signal = false;
    pending.fence = f.Fence();
    context.slots[0].last_submission_queue = &pending;
    context.slots[0].completion_fence_value = 9;
    Check(!RecordNrUseLocked(list.native, context, context.slots[0]), "unfinished slot rejected");
    context.slots[0].last_submission_queue = nullptr;
    context.slots[0].completion_fence_value = 0;
    ReleaseCom(pending.fence);
    g_lifecycle_tracking_uncertain.store(true);
    Check(!CanBeginContextUseLocked(list.native, context), "global uncertainty stays rejected");
}

void Negative(const std::string &mode) {
    Fixture f;
    List list(f);
    const bool migration = mode == "migration" || mode == "one-observation-migration";
    if (migration) {
        list.Close(); list.Prime(f.a);
        if (mode == "migration") list.Prime(f.a);
        f.Drain(f.a); list.ResetWithNewAllocator(f);
    }
    Check(RecordNrUseLocked(list.native, context, context.slots[0]), "negative setup genuinely admitted");
    unsigned long long token = 0;
    Check(ReadLifecycleToken(list.native, token), "negative setup owned token");
    LifecycleQueue pending = {};
    if (mode == "pending-context" || mode == "pending-slot") {
        pending.fence = f.Fence();
        if (mode == "pending-context") {
            context.last_submission_queue = &pending;
            context.completion_fence_value = 1;
        } else {
            context.slots[0].last_submission_queue = &pending;
            context.slots[0].completion_fence_value = 1;
        }
    }
    if (mode == "discard") {
        Check(QuarantineUnsubmittedLifecycleRecordLocked(token, "fixture discard"),
              "unsubmitted work cannot be discarded as completed");
        Check(g_lifecycle_tracking_uncertain.load() && context.lifecycle_dependencies_quarantined,
              "discard retains entire dependency closure");
        return;
    }
    list.Close();
    unsigned before = native_executes;
    list.Submit(migration ? f.b : f.a);
    Check(native_executes == before + 1, "native game submission always forwarded");
    if (mode == "replay") {
        Check(!g_lifecycle_tracking_uncertain.load(), "first replay-control submit succeeds");
        Wait(context.last_submission_queue->fence, context.completion_fence_value);
        list.Submit(f.a);
        Check(native_executes == before + 2, "replay still forwarded");
    }
    Check(g_lifecycle_tracking_uncertain.load(), "invalid proof quarantines subsequent NR");
    Check(context.lifecycle_dependencies_quarantined, "invalid proof retains dependencies");
    f.Drain(migration ? f.b : f.a);
    ReleaseCom(pending.fence);
}
#ifndef NR_QUEUE_OLD_POLICY_NEGATIVE
struct RetirementRelease {
    ID3D12Fence *gate=nullptr;
    unsigned long long after_wait=0;
};
DWORD WINAPI ReleaseAfterWaitStarts(void *opaque) {
    auto &release=*static_cast<RetirementRelease *>(opaque);
    const auto started=GetTickCount64();
    while(g_queue_retirement_waits.load()<=release.after_wait) {
        if(GetTickCount64()-started>3000)return 2;
        Sleep(1);
    }
    Sleep(1);
    return FAILED(ID3D12Fence_Signal(release.gate,1))?3:0;
}
void SubmittedRetirement() {
    Fixture f;
    std::unique_ptr<List> previous;
    ID3D12Fence *gate=nullptr;
    for(unsigned frame=0;frame<8;++frame) {
        auto current=std::make_unique<List>(f);
        RetirementRelease release{gate,g_queue_retirement_waits.load()};
        HANDLE worker=nullptr;
        if(previous) {
            Check(!IsContextFenceCompleteLocked(context),"prior real GPU work is unfinished");
            Check(!CanBeginContextUseLocked(current->native,context,false),
                  "old immediate admission skips this consecutive carrier");
            worker=CreateThread(nullptr,0,ReleaseAfterWaitStarts,&release,0,nullptr);
            Check(worker!=nullptr,"completion producer started");
        }
        Check(PrepareContextUseLocked(current->native,context),"SUBMITTED_RETIREMENT_CONTINUITY");
        Check(IsContextFenceCompleteLocked(context),"completion proved before any new recording");
        if(worker) {
            Check(WaitForSingleObject(worker,4000)==WAIT_OBJECT_0,"completion producer finished");
            DWORD exit=99;Check(GetExitCodeThread(worker,&exit)&&exit==0,"real fence released by producer");
            CloseHandle(worker);ReleaseCom(gate);previous->Detach();previous.reset();
        }
        Check(RecordNrUseLocked(current->native,context,context.slots[0]),"continuous fresh list pinned");
        Check(RecordVisibleTransferLocked(current->native,1,1),"continuous transfer bound");
        current->Close();gate=f.Fence();auto *queue=frame%2?f.b:f.a;
        Hr(ID3D12CommandQueue_Wait(queue,gate,1),"hold this submitted test batch");
        current->Submit(queue);
        Check(!g_lifecycle_tracking_uncertain.load(),"alternating actual queues stay safe after retirement");
        previous=std::move(current);
    }
    Hr(ID3D12Fence_Signal(gate,1),"release last batch");
    Wait(context.last_submission_queue->fence,context.completion_fence_value);
    previous->Detach();previous.reset();ReleaseCom(gate);
    Check(g_nr_transfer_submitted_frames.load()==8,"eight consecutive carriers submitted");
    Check(g_queue_retirement_waits.load()==7 && g_queue_retirement_wait_completed.load()==7,
          "seven actual pending-to-complete waits");
    Check(g_queue_retirement_wait_timeouts.load()==0 && g_queue_affinity_actual_mismatches.load()==0,
          "no timeout or invented queue affinity");
    Check(g_lifecycle_active_command_lists.load()==0 && context.recording_command_lists==0,
          "continuous owners all detached");
#if defined(NR_HAS_TIMING) && defined(NR_HAS_SUBMITTED_FENCE_EVENT)
    unsigned event_handles = 0;
    for (const auto& queue : g_lifecycle_queues) event_handles += queue.completion_event.event != nullptr;
    const auto policy = NrTimingPolicySnapshot();
    auto timing = std::make_unique<nr_timing::Snapshot>(); SnapshotNrTiming(*timing);
    Check(timing->stages[static_cast<unsigned>(nr_timing::Stage::FenceWait)].count == 7,
          "actual production preparation records one timing per exact submitted wait");
    Check(policy.wait.mode == 0 ? event_handles > 0 && event_handles <= 2 : event_handles == 0,
          "selected event or legacy polling strategy reaches actual production WARP fences");
    std::printf("SUBMITTED_WAIT_STRATEGY mode=%s phase=%llu event_handles=%u waits=%llu\n",
        nr_timing::WaitModeName(policy.wait.mode), static_cast<unsigned long long>(policy.wait.phase), event_handles,
        static_cast<unsigned long long>(timing->stages[static_cast<unsigned>(nr_timing::Stage::FenceWait)].count));
#endif
}
void RetirementTimeout() {
    Fixture f;List first(f),declined(f);
    Check(PrepareContextUseLocked(first.native,context),"timeout setup admitted");
    Check(RecordNrUseLocked(first.native,context,context.slots[0]),"timeout setup pinned");
    Check(RecordVisibleTransferLocked(first.native,1,1),"timeout setup transfer");
    first.Close();auto *gate=f.Fence();Hr(ID3D12CommandQueue_Wait(f.a,gate,1),"never complete within budget");
    first.Submit(f.a);
    Check(!PrepareContextUseLocked(declined.native,context),"unfinished work times out without admission");
    Check(g_queue_retirement_wait_timeouts.load()==1 && g_queue_retirement_waits.load()==1,
          "one bounded wait timeout");
    Check(!g_lifecycle_tracking_uncertain.load(),"timeout does not invent device or lifetime corruption");
    unsigned long long token=0;
    Check(!ReadLifecycleToken(declined.native,token),"timed out carrier owns no injected work");
    const auto count=native_executes;declined.Close();declined.Submit(f.a);
    Check(native_executes==count+1,"timed out native game batch still executes");
    Hr(ID3D12Fence_Signal(gate,1),"release after timeout");f.Drain(f.a);first.Detach();ReleaseCom(gate);
    List retry(f);
    Check(IsContextFenceCompleteLocked(context),"timeout dependency subsequently completed");
    Check(!PrepareContextUseLocked(retry.native,context),"same config does not resume intermittent retry");
    Check(g_queue_retirement_waits.load()==1,"no repeated stall");
    g_config_generation.fetch_add(1);
    Check(PrepareContextUseLocked(retry.native,context),"new committed config can retry");
    Check(RecordNrUseLocked(retry.native,context,context.slots[0]),"resumed fresh work pinned");
    retry.Close();retry.Submit(f.b);f.Drain(f.b);retry.Detach();
}
void RetirementGuards() {
    Fixture f;List list(f);LifecycleQueue pending{};pending.fence=f.Fence();
    context.last_submission_queue=&pending;context.completion_fence_value=9;
    context.awaiting_fence_signal=true;
    Check(!PrepareContextUseLocked(list.native,context),"unpublished Signal cannot be waited on");
    context.awaiting_fence_signal=false;context.pending_recording_token=99;
    Check(!PrepareContextUseLocked(list.native,context),"unsubmitted recording cannot be waited on");
    context.pending_recording_token=0;context.failed=true;
    Check(!PrepareContextUseLocked(list.native,context),"failed context not waited on");context.failed=false;
    CommandListQueueMarker marker{1,1,kQueueMarkerMigrated};
    Hr(ID3D12GraphicsCommandList_SetPrivateData(list.native,kLastSubmissionQueueGuid,sizeof(marker),&marker),"migrated history");
    Check(!PrepareContextUseLocked(list.native,context),"migration cannot be cured by waiting");
    Hr(ID3D12GraphicsCommandList_SetPrivateData(list.native,kLastSubmissionQueueGuid,sizeof(marker)-1,&marker),"corrupt history");
    Check(!PrepareContextUseLocked(list.native,context),"corruption cannot be cured by waiting");
    Hr(ID3D12GraphicsCommandList_SetPrivateData(list.native,kLastSubmissionQueueGuid,0,nullptr),"remove test marker");
    Check(g_queue_retirement_waits.load()==0,"all unproven/invalid paths avoided CPU wait");
    Hr(ID3D12Fence_Signal(pending.fence,UINT64_MAX),"removed device sentinel");
    Check(!PrepareContextUseLocked(list.native,context),"removed value is never successful retirement");
    Check(g_lifecycle_tracking_uncertain.load() && g_queue_retirement_wait_completed.load()==0,
          "removed value stops future mutations");
    context.last_submission_queue=nullptr;context.completion_fence_value=0;ReleaseCom(pending.fence);
}
#endif
} // namespace queue_test

#ifndef NR_QUEUE_OLD_POLICY_NEGATIVE
#include "post_output_harness.inl"
#endif

#if defined(NR_HAS_POST_BOUNDARY_TRACE)
void BoundaryCallbacks() {
    using queue_test::Check;
    g_enabled_fast.store(true);g_config_generation.store(9);
    auto *resource=reinterpret_cast<ID3D12Resource *>(0x1010);
    auto *list=reinterpret_cast<ID3D12GraphicsCommandList *>(0x2020);
    NrNativeOutputTraceScope outer(resource,list);
    ObserveNativeOutputBarrier(0x2020,0x9999,8,128);
    ObserveNativeOutputBarrier(0x3030,0x1010,8,128);
    Check(outer.evidence.matches==0 && outer.evidence.other_lists==1,"wrong object/list cannot supply native exit");
    {
        NrNativeOutputTraceScope nested(reinterpret_cast<ID3D12Resource *>(0x4040),list);
        ObserveNativeOutputBarrier(0x2020,0x4040,8,64);
        Check(nested.evidence.matches==1 && outer.evidence.matches==0,"nested native observations isolated");
    }
    ObserveNativeOutputBarrier(0x2020,0x1010,8,64);
    ObserveNativeOutputBarrier(0x2020,0x1010,64,128);
    const auto native=outer.Finish();
    Check(!g_native_output_trace && native.matches==2 && native.first_before==8 && native.last_after==128,"exact native window ends and preserves observed order");
    NrDiagnosticIdentity identity{};identity.frame_id=71;identity.config_generation=9;
    const auto trace=PublishPostBoundary(resource,native,identity,3,0x4444);
    Check(!SnapshotPostBoundary().frame,"no Present-closed window fabricated");
    using R=reshade::api::resource;using U=reshade::api::resource_usage;
    const R targets[]={{0x1010},{0x9999}};const U before[]={U::unordered_access,U::unordered_access};const U after[]={U::shader_resource_pixel,U::copy_dest};
    OnPostBoundaryBarrier(nullptr,2,targets,before,after);
    Check(!OnPostBoundaryCopy(nullptr,R{0x1010},R{0x9090}),"read observation never suppresses game copy");
    Check(!OnPostBoundaryCopyRegion(nullptr,R{0x9090},0,nullptr,R{0x1010},0,nullptr,reshade::api::filter_mode::min_mag_mip_point),"write observation never suppresses region copy");
    g_inside_ngx_evaluate=true;
    OnPostBoundaryCopy(nullptr,R{0x1010},R{0x1010});
    OnPostBoundaryBarrier(nullptr,2,targets,before,after);
    g_inside_ngx_evaluate=false;
    ClosePostBoundaryAtPresent(0x5555);
    Check(!SnapshotPostBoundary().frame,"other-device Present cannot close this recording");
    ClosePostBoundaryAtPresent();auto closed=SnapshotPostBoundary();
    Check(closed.frame==71&&closed.config==9&&closed.feature==3,"coherent post identity carried to closed window");
    Check(closed.barriers==1&&closed.first_before==8&&closed.first_after==128,"unrelated and internal barriers excluded");
    Check(closed.copy_reads==1&&closed.copy_writes==1,"native/model internal copies excluded");
    Check(closed.native.matches==2&&closed.native.last_after==128,"native and following boundaries remain distinct");
    Check(g_post_boundary_resource.load()==0,"first matching Present disarms live watch");
    OnPostBoundaryBarrier(nullptr,2,targets,before,after);ClosePostBoundaryAtPresent();
    Check(SnapshotPostBoundary().barriers==1,"repeated Present/later events cannot grow historical counters");
    Check(SnapshotPostBoundary(trace,71,9,3).trace==trace,"probe joins exact trace/frame/config/feature only");
    Check(!SnapshotPostBoundary(trace,72,9,3).trace && !SnapshotPostBoundary(trace,71,10,3).trace &&
          !SnapshotPostBoundary(trace,71,9,4).trace,"a nearby frame or different generation is not a probe match");
    identity.frame_id=72;PublishPostBoundary(resource,native,identity,3);
    ForgetPostBoundary(0x9999);Check(g_post_boundary_resource.load()==0x1010,"unrelated destroy retains target");
    OnPostBoundaryDestroyResource(nullptr,R{0x1010});
    Check(g_post_boundary_resource.load()==0,"real destroy retires watched identity");
    OnPostBoundaryCopy(nullptr,R{0x1010},R{0x1010});ClosePostBoundaryAtPresent();
    Check(SnapshotPostBoundary().frame==72 && SnapshotPostBoundary().copy_reads==0 &&
          SnapshotPostBoundary().close==nr_post_boundary::Close::ResourceRetired,"destroy closes identity without later reuse contamination");
    identity.frame_id=73;const auto old_scope=PublishPostBoundary(resource,native,identity,3);
    g_config_generation.store(10);OnPostBoundaryBarrier(nullptr,2,targets,before,after);ClosePostBoundaryAtPresent();
    Check(SnapshotPostBoundary(old_scope,73,9,3).close==nr_post_boundary::Close::ScopeChanged &&
          SnapshotPostBoundary(old_scope,73,9,3).barriers==0,"config switch closes old post window before counting a new route");
    g_config_generation.store(9);identity.frame_id=74;PublishPostBoundary(resource,native,identity,3);
    RetirePostBoundaryBeforeEvaluate();OnPostBoundaryBarrier(nullptr,2,targets,before,after);
    Check(SnapshotPostBoundary().close==nr_post_boundary::Close::Superseded && SnapshotPostBoundary().barriers==0,
          "next Evaluate cannot append its commands to previous sampled frame");
    identity.frame_id=75;PublishPostBoundary(resource,native,identity,3);
    g_enabled_fast.store(false);OnPostBoundaryBarrier(nullptr,2,targets,before,after);
    Check(SnapshotPostBoundary().close==nr_post_boundary::Close::ScopeChanged,"master disable retires observer scope");
    g_enabled_fast.store(true);identity.frame_id=76;PublishPostBoundary(resource,native,identity,3);
    g_post_boundary_current.tick=GetTickCount64()-kPostBoundaryMaxAgeMs-1;
    OnPostBoundaryBarrier(nullptr,2,targets,before,after);
    Check(SnapshotPostBoundary().close==nr_post_boundary::Close::Expired,"missing Present cannot leave an unbounded watch");
    std::printf("Boundary callbacks: %u checks; production read-only callbacks, CPU only, no state permission\n",queue_test::checks);
}
#include "post_boundary_native.inl"
#include "post_boundary_readonly.inl"
#endif
#ifndef NR_QUEUE_OLD_POLICY_NEGATIVE
#include "native_nr_retirement.inl"
#endif
int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string mode = argc == 2 ? argv[1] : "one-shot";
#if defined(NR_HAS_POST_BOUNDARY_TRACE)
    if(mode=="boundary-callbacks"){BoundaryCallbacks();return 0;}
    if(mode=="boundary-native"){BoundaryNativeHooks();return 0;}
    if(mode=="boundary-readonly"){boundary_readonly_test::Run();return 0;}
#endif
    if (mode == "one-shot") queue_test::OneShot();
    else if (mode == "guards") queue_test::Guards();
#ifndef NR_QUEUE_OLD_POLICY_NEGATIVE
    else if (mode == "submitted-retirement") queue_test::SubmittedRetirement();
    else if (mode == "native-final-destruction") queue_test::NativeNrRetirement(false);
    else if (mode == "native-marker-replacement") queue_test::NativeNrRetirement(true);
    else if (mode == "native-wrapper-retention") queue_test::NativeWrapperRetention();
    else if (mode == "native-marker-wrapper-retention") queue_test::NativeWrapperRetention(true);
    else if (mode == "native-reset-success") queue_test::NativeResetRetirement(false);
    else if (mode == "native-reset-failure") queue_test::NativeResetRetirement(true);
    else if (mode == "native-death-pending-fence") queue_test::NativeDeathPendingFence();
    else if (mode == "native-unsubmitted-death") queue_test::NativeUnsubmittedDeath();
    else if (mode == "native-notifier-unavailable") queue_test::NativeNoticeUnavailable(queue_test::NativeNoticeFailure::QueryInterface);
    else if (mode == "native-notifier-register-failure") queue_test::NativeNoticeUnavailable(queue_test::NativeNoticeFailure::RegisterCallback);
#if defined(NR_HAS_TIMING)
    else if (mode == "submitted-retirement-polling") {
        NrTimingSetWaitPolicy(1, 2); queue_test::SubmittedRetirement();
    }
#endif
    else if (mode == "retirement-timeout") queue_test::RetirementTimeout();
    else if (mode == "retirement-guards") queue_test::RetirementGuards();
    else if (mode == "post-output") post_output_test::Run();
#endif
    else queue_test::Negative(mode);
    std::printf("PASS queue retirement %s: %u checks; actual WARP, no NVIDIA NR\n",
                mode.c_str(), queue_test::checks);
    return 0;
}
