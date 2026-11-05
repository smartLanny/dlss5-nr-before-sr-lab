// Included into the generated native command-state harness after production code.
// No NGX runtime or game is involved. Negative control must produce exactly 999.
int RunPristineNullGate(bool negative) {
    setvbuf(stdout,nullptr,_IONBF,0); // Preserve exact checkpoints if a future native regression terminates.
    g_allow_experimental=false;
    g_enabled_fast.store(true);
    IDXGIFactory4 *factory=nullptr; IDXGIAdapter *adapter=nullptr; ID3D12Device *device=nullptr;
    Hr(nr_test_warp::Prepare(),"Prepare pinned CI WARP");
    Hr(CreateDXGIFactory1(IID_IDXGIFactory4,reinterpret_cast<void **>(&factory)),"pristine factory");
    Hr(IDXGIFactory4_EnumWarpAdapter(factory,IID_IDXGIAdapter,reinterpret_cast<void **>(&adapter)),"pristine WARP");
    Hr(D3D12CreateDevice(reinterpret_cast<IUnknown *>(adapter),D3D_FEATURE_LEVEL_11_0,
        IID_ID3D12Device,reinterpret_cast<void **>(&device)),"pristine device");
    Hr(nr_test_warp::Verify("command-state-pristine-null"),"Verify actual CI WARP");
    reshade::api::device od{device};
    ID3D12CommandAllocator *allocator=nullptr; ID3D12GraphicsCommandList *list=nullptr;
    Hr(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_ID3D12CommandAllocator,reinterpret_cast<void **>(&allocator)),"pristine allocator");
    Hr(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,
        IID_ID3D12GraphicsCommandList,reinterpret_cast<void **>(&list)),"pristine list");
    // Keep this legacy recovery case genuinely unobserved: its native Create
    // predates hook publication. Post-publication native records are tested
    // separately and must not be made artificially unregistered here.
    // Both lists in this missed-init fixture genuinely predate native Create
    // observation. The separate first-recording gate starts enabled and proves
    // new-list coverage; it must not pre-publish full guards for this legacy case.
    ID3D12CommandAllocator *other_allocator=nullptr;
    Hr(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_ID3D12CommandAllocator,reinterpret_cast<void **>(&other_allocator)),"unrelated allocator");
    ID3D12GraphicsCommandList *other=nullptr;
    Hr(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,other_allocator,nullptr,
        IID_ID3D12GraphicsCommandList,reinterpret_cast<void **>(&other)),"unrelated known list");
    OnCommandStateInitDevice(&od);
    Check(g_state_RSSetViewports.count==0 && g_state_OMSetRenderTargets.count==0,
          "default device startup does not install wide graphics hooks");
    reshade::api::command_list owner{&od,list};
    auto *snapshot=new NativeCommandState;
    Check(!CaptureNativeCommandState(list,*snapshot),"missed init is not inferred from native pointer");
    Check(std::strcmp(SnapshotCommandStateEvidence().reason,"unregistered-list")==0,"unregistered reason retained");
    reshade::api::command_list other_owner{&od,other}; OnCommandStateInitList(&other_owner);
    Check(!CaptureNativeCommandState(other,*snapshot),"unrelated known list still awaits its own baseline");
    Check(g_command_state_registration_repair_needed.load(),"known carrier does not cancel missing-list recovery");
    OnCommandStateObserveSubmission(&owner);
    OnCommandStateDestroyList(&other_owner);Drop(other);Drop(other_allocator);
    const auto token=ReadCommandStateToken(list);
    auto *record=FindCommandStateTokenLocked(token);
    Check(record && token && !record->state,"real ReShade submission repairs mapping only");
    if (!record) ExitProcess(3);
    Check(!CaptureNativeCommandState(list,*snapshot),"first late capture installs but does not invent a baseline");
    Check(record->hooks_ready && !record->null_guard_requested,"ordinary envelope initially supports only observed typed PSO");
    Check(g_state_RSSetViewports.count==0,"no wide hook at first unknown carrier");
    const auto reset=[&] {
        Hr(ID3D12GraphicsCommandList_Close(list),"pristine close");
        OnCommandStateBeforeReset(&owner);
        Hr(ID3D12GraphicsCommandList_Reset(list,allocator,nullptr),"pristine successful Reset");
    };
    reset();
    Check(record->state->known && !record->state->null_guard_covered,"first Reset has no retrospective graphics coverage");
    Check(!CaptureNativeCommandState(list,*snapshot),"known null requests guard upgrade, never uses old recording");
    Check(record->hooks_ready && record->state->null_guard_covered && !record->state->null_guard_reset_observed,
          "guard publication requires a subsequent successful Reset");
    ID3D12GraphicsCommandList_ClearState(list,nullptr);
    Check(!CaptureNativeCommandState(list,*snapshot),"ClearState cannot certify pre-publication recording");
    OnCommandStateResetResult(list,E_FAIL,nullptr);
    Check(!record->state->known && !CaptureNativeCommandState(list,*snapshot),"failed Reset never admits");
    reset();
    Check(CaptureNativeCommandState(list,*snapshot),"default admits freshly guarded pristine null");
    Check(std::strcmp(SnapshotCommandStateEvidence().route,"pristine-null")==0,"actual default route recorded");
    Check(!SnapshotCommandStateEvidence().blend_observed,"pristine defaults are not observed blend values");
    Check(!CanRestoreNativeCommandState(*snapshot),"raw predicate still rejects null without explicit evidence");
    D3D12_VIEWPORT vp{0,0,2,2,0,1};
    ID3D12GraphicsCommandList_RSSetViewports(list,1,&vp);
    Check(CaptureNativeCommandState(list,*snapshot),"fully observed bounded viewport is admitted without experimental replay");
    Check(std::strcmp(SnapshotCommandStateEvidence().route,"guarded-null-raster")==0,"bounded raster route is explicit");
    D3D12_RECT scissor{0,0,2,2};ID3D12GraphicsCommandList_RSSetScissorRects(list,1,&scissor);
    Check(CaptureNativeCommandState(list,*snapshot),"viewport and scissor compose within the safe subset");
    {
        NativeCommandStateScope restore_raster(list);
        Check(restore_raster.active,"actual raster scope can be acquired");
        D3D12_VIEWPORT changed{0,0,1,1,0,1};
        ID3D12GraphicsCommandList_RSSetViewports(list,1,&changed);
        restore_raster.Restore();
        Check(record->state->dynamic.viewports[0].Width==2,"native scope restores observed viewport bytes");
    }
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Check(!CaptureNativeCommandState(list,*snapshot),"other graphics domains remain outside automatic subset");
    Check(SnapshotCommandStateEvidence().dynamic==(kDynamicViewport|kDynamicScissor|kDynamicTopology),"dynamic field survives evidence snapshot");
    ID3D12GraphicsCommandList_ClearState(list,nullptr);
    Check(CaptureNativeCommandState(list,*snapshot),"tracked ClearState after fresh Reset erases graphics state");
    const float blend[4]={0.25f,0.5f,0.75f,1.0f};
    ID3D12GraphicsCommandList_OMSetBlendFactor(list,blend);
    Check(CaptureNativeCommandState(list,*snapshot),"blend-only game state now admitted");
    const auto blend_evidence=SnapshotCommandStateEvidence();
    Check(blend_evidence.blend_observed && memcmp(blend_evidence.blend,blend,sizeof(blend))==0,
          "carrier snapshot retains all four actual blend values from the captured state");
    Check(g_command_history.count && g_command_history.entries[0].blend_observed &&
          memcmp(g_command_history.entries[0].blend,blend,sizeof(blend))==0,"history retains the same blend observation");
    {
        NativeCommandStateScope scope(list);
        Check(scope.active,"blend-only native scope acquired");
        const float wrong[4]={0,0,0,0};
        ID3D12GraphicsCommandList_OMSetBlendFactor(list,wrong);
        scope.Restore();
        Check(memcmp(record->state->dynamic.blend,blend,sizeof(blend))==0,"exact native blend values restored");
    }
    ID3D12GraphicsCommandList_OMSetBlendFactor(list,nullptr);
    Check(CaptureNativeCommandState(list,*snapshot),"legal NULL blend resets all channels to one");
    for(float v:record->state->dynamic.blend) Check(v==1.0f,"NULL blend follows D3D12 default");
    Check(SnapshotCommandStateEvidence().blend_observed,"actual NULL blend setter remains an observed write");
    ID3D12GraphicsCommandList_ClearState(list,nullptr);
    Check(CaptureNativeCommandState(list,*snapshot) && !SnapshotCommandStateEvidence().blend_observed,
          "ClearState removes prior blend evidence instead of leaking stale values");
    auto saved=g_state_BeginRenderPass.originals[3];
    g_state_BeginRenderPass.originals[3]=reinterpret_cast<void *>(&RenderPassTrampoline);
    StateHookBeginRenderPass<3>(list,0,nullptr,nullptr,D3D12_RENDER_PASS_FLAG_NONE);
    g_state_BeginRenderPass.originals[3]=saved;
    ID3D12GraphicsCommandList_ClearState(list,nullptr);
    Check(!CaptureNativeCommandState(list,*snapshot) && record->state->null_guard_tainted,
          "render-pass history is sticky across ClearState");
    reset();
    Check(CaptureNativeCommandState(list,*snapshot),"fresh Reset recovers recording taint");
    saved=g_state_SetProtectedResourceSession.originals[3];
    g_state_SetProtectedResourceSession.originals[3]=reinterpret_cast<void *>(&ProtectedTrampoline);
    StateHookSetProtectedResourceSession<3>(list,nullptr);
    g_state_SetProtectedResourceSession.originals[3]=saved;
    ID3D12GraphicsCommandList_ClearState(list,nullptr);
    Check(!CaptureNativeCommandState(list,*snapshot),"protected-session setter cannot be laundered by ClearState");
    reset();
    record->meta_contract.execution_dirty=D3D12_GRAPHICS_STATE_IA_VERTEX_BUFFERS;
    Check(!CaptureNativeCommandState(list,*snapshot),"uncovered meta contract stays rejected");
    record->meta_contract.execution_dirty=0;
    AcquireSRWLockExclusive(&g_command_state_lock);
    Check(!SnapshotCommandStateEvidence().available,"F8 never waits on contended state snapshot lock");
    ReleaseSRWLockExclusive(&g_command_state_lock);

    D3D12_ROOT_PARAMETER args[2]{};
    args[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; args[0].Constants.Num32BitValues=1;
    args[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=2;rd.pParameters=args;
    ID3DBlob *blob=nullptr,*error=nullptr;ID3D12RootSignature *root=nullptr;ID3D12PipelineState *pso=nullptr;
    Hr(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"probe root serialization");
    Hr(ID3D12Device_CreateRootSignature(device,0,ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob),
        IID_ID3D12RootSignature,reinterpret_cast<void **>(&root)),"probe root");Drop(blob);Drop(error);
    const char shader[]="cbuffer C:register(b0){uint value;} RWStructuredBuffer<uint> output:register(u0); [numthreads(1,1,1)] void main(){output[0]=value;}";
    Hr(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&blob,&error),"probe shader");
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;
    pd.CS={ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob)};
    Hr(ID3D12Device_CreateComputePipelineState(device,&pd,IID_ID3D12PipelineState,reinterpret_cast<void **>(&pso)),"probe PSO");Drop(blob);Drop(error);
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=4;bd.Height=1;
    bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource *output=nullptr,*readback=nullptr;
    Hr(ID3D12Device_CreateCommittedResource(device,&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,IID_ID3D12Resource,reinterpret_cast<void **>(&output)),"probe output");
    hp.Type=D3D12_HEAP_TYPE_READBACK;bd.Flags=D3D12_RESOURCE_FLAG_NONE;
    Hr(ID3D12Device_CreateCommittedResource(device,&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,IID_ID3D12Resource,reinterpret_cast<void **>(&readback)),"probe readback");
    ID3D12GraphicsCommandList_SetComputeRootSignature(list,root);
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(list,0,73,0);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list,1,ID3D12Resource_GetGPUVirtualAddress(output));
    {
        NativeCommandStateScope scope(list);
        Check(scope.active && scope.saved->null_pipeline_baseline,"real default null scope before GPU probe");
        if (!scope.active) ExitProcess(3);
        ID3D12GraphicsCommandList_SetPipelineState(scope.command_list,pso);
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(scope.command_list,0,999,0);
        ID3D12GraphicsCommandList_Dispatch(scope.command_list,1,1,1);
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=output;
        ID3D12GraphicsCommandList_ResourceBarrier(scope.command_list,1,&b);
        if (negative) scope.active=false;
    }
    ID3D12GraphicsCommandList_SetPipelineState(list,pso);
    ID3D12GraphicsCommandList_Dispatch(list,1,1,1);
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=output;
    b.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    ID3D12GraphicsCommandList_ResourceBarrier(list,1,&b);
    ID3D12GraphicsCommandList_CopyResource(list,readback,output);
    Hr(ID3D12GraphicsCommandList_Close(list),"probe close");
    ID3D12CommandQueue *queue=nullptr;ID3D12Fence *fence=nullptr;D3D12_COMMAND_QUEUE_DESC qd{};
    Hr(ID3D12Device_CreateCommandQueue(device,&qd,IID_ID3D12CommandQueue,reinterpret_cast<void **>(&queue)),"probe queue");
    Hr(ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,reinterpret_cast<void **>(&fence)),"probe fence");
    ID3D12CommandList *lists[]={reinterpret_cast<ID3D12CommandList *>(list)};
    ID3D12CommandQueue_ExecuteCommandLists(queue,1,lists);Hr(ID3D12CommandQueue_Signal(queue,fence,1),"probe signal");
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if (!event) ExitProcess(3);
    Hr(ID3D12Fence_SetEventOnCompletion(fence,1,event),"probe completion");
    if (WaitForSingleObject(event,10000)!=WAIT_OBJECT_0) ExitProcess(3);
    CloseHandle(event);Hr(ID3D12Device_GetDeviceRemovedReason(device),"probe device");
    void *data=nullptr;D3D12_RANGE range{0,4};Hr(ID3D12Resource_Map(readback,0,&range,&data),"probe map");
    const UINT actual=*static_cast<const UINT *>(data);range={0,0};ID3D12Resource_Unmap(readback,0,&range);
    Check(actual==73,"GPU result must equal game constant 73, not plugin constant 999");
    if (!negative) CheckCommandPoolPriming(device,od);
    const auto old_token=ReadCommandStateToken(list);
    OnCommandStateDestroyList(&owner);OnCommandStateInitList(&owner);
    Check(ReadCommandStateToken(list)!=old_token && !CaptureNativeCommandState(list,*snapshot),"legacy wrapper re-registration requires a fresh observed baseline");
    OnCommandStateDestroyList(&owner);ReleaseNativeCommandState(*snapshot);delete snapshot;
    Drop(queue);Drop(fence);Drop(readback);Drop(output);Drop(list);Drop(allocator);Drop(pso);Drop(root);Drop(device);Drop(adapter);Drop(factory);
    printf("default pristine-null native+GPU: %d checks, %d failures; GPU=%u, negative=%d\n",checks,failures,actual,negative?1:0);
    if (negative && failures==1 && actual==999) return 42;
    return failures==0 ? 0 : 1;
}
