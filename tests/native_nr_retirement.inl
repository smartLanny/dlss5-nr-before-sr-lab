// Included in the existing whole-Core queue host. Only the native command-state
// registration metadata is supplied by this fixture; the lifetime marker, NR
// recording producer, Execute/Signal, retirement predicates and collector are
// production code. WARP submits an empty list; no model, shader or game runs.
namespace queue_test {
void NativeNrRetirement(bool replace_marker) {
    Fixture f;
    auto list=std::make_unique<List>(f);
    CommandStateMetaContract meta{};
    meta.device_identity=339;
    CommandCreationInfo created{};
    created.serial=339;created.device_identity=339;created.method=1;
    Check(RegisterCreatedCommandStateList(list->native,meta,created),
          "native-created list lifetime registered without a ReShade wrapper");
    const auto state_token=ReadCommandStateToken(list->native);
    Check(state_token && FindCommandStateTokenLocked(state_token),
          "native command-state registration exists");
    auto& nr=g_contexts[0];
    // A carrier identity, not an NGX model handle. DestroyContext owns no model
    // in this lifetime-only case; the actual outside-lock drain still runs.
    nr.sr_handle=reinterpret_cast<const NgxHandle*>(static_cast<uintptr_t>(339));
    nr.instance_id=339;
    Check(RecordNrUseLocked(list->native,nr,nr.slots[0]),
          "actual NR recording producer pins the native list");
    unsigned long long token=0;
    Check(ReadLifecycleToken(list->native,token),"NR token captured while list is alive");
    list->Close();list->Submit(f.a);
    Check(!g_lifecycle_tracking_uncertain.load(),"one-shot native submit has exact proof");
    Wait(nr.last_submission_queue->fence,nr.completion_fence_value);
    nr.retired=true;
    Check(IsContextFenceCompleteLocked(nr),"exact submitted context fence completed");
    Check(!CanDestroyContextLocked(nr),"completed fence does not retire a replayable native list");

    auto* retained=list->native;
    ID3D12GraphicsCommandList_AddRef(retained);
    ReleaseCom(list->native);
    list.reset();
    EnterCriticalSection(&g_chain_cs);CollectRetiredContextsLocked();LeaveCriticalSection(&g_chain_cs);
    Check(FindCommandStateTokenLocked(state_token) && nr.recording_command_lists==1 &&
          nr.slots[0].recording_command_lists==1 && !g_pending_destroy_contexts[0].sr_handle,
          "extra raw reference retains replayable recording after first owner exits");
    if(replace_marker) {
        Hr(ID3D12GraphicsCommandList_SetPrivateDataInterface(retained,kNativeCommandLifetime,nullptr),
           "replace native observation marker without destroying the list");
        EnterCriticalSection(&g_chain_cs);CollectRetiredContextsLocked();LeaveCriticalSection(&g_chain_cs);
        Check(!FindCommandStateTokenLocked(state_token),"marker replacement retires observation state");
        Check(nr.recording_command_lists==1 && nr.slots[0].recording_command_lists==1 &&
              !g_pending_destroy_contexts[0].sr_handle && !CanDestroyContextLocked(nr),
              "private-data marker replacement is not native destruction proof");
    }
    std::printf("NATIVE_NR_BEFORE_FINAL stateToken=%llu token=%llu recording=%u slot=%u pending=%llu fence=%llu completed=%llu markerReplaced=%u\n",
        state_token,token,nr.recording_command_lists,nr.slots[0].recording_command_lists,
        nr.pending_recording_token,nr.completion_fence_value,
        ID3D12Fence_GetCompletedValue(nr.last_submission_queue->fence),replace_marker);
    ReleaseCom(retained);
    Check(!FindCommandStateTokenLocked(state_token),"final native destruction retires observation state");
    EnterCriticalSection(&g_chain_cs);CollectRetiredContextsLocked();LeaveCriticalSection(&g_chain_cs);
    std::printf("NATIVE_NR_AFTER_FINAL stateGone=1 ledgerPresent=%u recording=%u slot=%u retiredLive=%u pendingDestroy=%u\n",
        FindLifecycleCommandListLocked(token)!=nullptr,nr.recording_command_lists,
        nr.slots[0].recording_command_lists,nr.sr_handle!=nullptr,
        g_pending_destroy_contexts[0].sr_handle!=nullptr);
    Check(!FindLifecycleCommandListLocked(token) && !nr.recording_command_lists &&
          !nr.slots[0].recording_command_lists,"NATIVE_FINAL_DESTROY_MUST_RETIRE_NR_RECORDING");
    DrainPendingContextDestructions();
    Check(!g_contexts[0].sr_handle&&!g_pending_destroy_contexts[0].sr_handle,
          "existing collector and outside-lock drain retire a native-destroyed owner");
}
}
