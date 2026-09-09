// Included after the complete production Core translation unit by the runner.
// Uses actual WARP queues/fences and the production admission/submit functions.
// No NGX/model is loaded and no ReShade host or game is launched.
#include <dxgi1_4.h>
#include <cstdlib>
#include <string>

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
} // namespace queue_test

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string mode = argc == 2 ? argv[1] : "one-shot";
    if (mode == "one-shot") queue_test::OneShot();
    else if (mode == "guards") queue_test::Guards();
    else queue_test::Negative(mode);
    std::printf("PASS queue retirement %s: %u checks; actual WARP, no NVIDIA NR\n",
                mode.c_str(), queue_test::checks);
    return 0;
}
