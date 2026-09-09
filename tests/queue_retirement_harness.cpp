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
struct Fixture {
    IDXGIFactory4 *factory = nullptr;
    IDXGIAdapter *adapter = nullptr;
    ID3D12Device *device = nullptr;
    ID3D12CommandQueue *a = nullptr;
    ID3D12CommandQueue *b = nullptr;
    Fixture() {
        InitializeCriticalSection(&g_chain_cs);
        Hr(CreateDXGIFactory2(0, IID_IDXGIFactory4, reinterpret_cast<void **>(&factory)), "factory");
        Hr(IDXGIFactory4_EnumWarpAdapter(factory, IID_IDXGIAdapter, reinterpret_cast<void **>(&adapter)), "WARP");
        Hr(D3D12CreateDevice(reinterpret_cast<IUnknown *>(adapter), D3D_FEATURE_LEVEL_11_0,
            IID_ID3D12Device, reinterpret_cast<void **>(&device)), "device");
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
    explicit List(Fixture &f) {
        Hr(ID3D12Device_CreateCommandAllocator(f.device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_ID3D12CommandAllocator, reinterpret_cast<void **>(&allocator)), "allocator");
        Hr(ID3D12Device_CreateCommandList(f.device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator, nullptr, IID_ID3D12GraphicsCommandList, reinterpret_cast<void **>(&native)), "fresh list");
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

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string mode = argc == 2 ? argv[1] : "one-shot";
    if (mode == "one-shot") queue_test::OneShot();
    else if (mode == "guards") queue_test::Guards();
#ifndef NR_QUEUE_OLD_POLICY_NEGATIVE
    else if (mode == "submitted-retirement") queue_test::SubmittedRetirement();
    else if (mode == "retirement-timeout") queue_test::RetirementTimeout();
    else if (mode == "retirement-guards") queue_test::RetirementGuards();
    else if (mode == "post-output") post_output_test::Run();
#endif
    else queue_test::Negative(mode);
    std::printf("PASS queue retirement %s: %u checks; actual WARP, no NVIDIA NR\n",
                mode.c_str(), queue_test::checks);
    return 0;
}
