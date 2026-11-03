// Real D3D12 WARP objects and installed native hooks; no NGX/model/GPU submit.
// Reproduces #205: fresh initially-null list, real PSO setters, no native Reset.
int RunFirstRecordingGate() {
    g_enabled_fast=true;g_allow_experimental=false;
    IDXGIFactory4 *factory=nullptr;IDXGIAdapter *adapter=nullptr;ID3D12Device *device=nullptr;
    Hr(nr_test_warp::Prepare(),"Prepare pinned CI WARP");
    Hr(CreateDXGIFactory1(IID_IDXGIFactory4,reinterpret_cast<void **>(&factory)),"first factory");
    Hr(IDXGIFactory4_EnumWarpAdapter(factory,IID_IDXGIAdapter,reinterpret_cast<void **>(&adapter)),"first WARP");
    Hr(D3D12CreateDevice(reinterpret_cast<IUnknown *>(adapter),D3D_FEATURE_LEVEL_11_0,
        IID_ID3D12Device,reinterpret_cast<void **>(&device)),"first device");
    Hr(nr_test_warp::Verify("command-state-first-recording"),"Verify actual CI WARP");
    reshade::api::device od{device};OnCommandStateInitDevice(&od);
    auto *root=MakeRoot(device,1);auto *pso=MakePipeline(device,root);
    ID3D12DescriptorHeap *heap=nullptr,*other_heap=nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors=1;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Hr(ID3D12Device_CreateDescriptorHeap(device,&hd,IID_ID3D12DescriptorHeap,reinterpret_cast<void **>(&heap)),"first heap");
    Hr(ID3D12Device_CreateDescriptorHeap(device,&hd,IID_ID3D12DescriptorHeap,reinterpret_cast<void **>(&other_heap)),"first other heap");
    ID3D12CommandAllocator *allocator=nullptr;ID3D12GraphicsCommandList *warm=nullptr;
    Hr(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_ID3D12CommandAllocator,reinterpret_cast<void **>(&allocator)),"first allocator");
    Hr(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,
        IID_ID3D12GraphicsCommandList,reinterpret_cast<void **>(&warm)),"first warm list");
    reshade::api::command_list warm_owner{&od,warm};OnCommandStateInitList(&warm_owner);
    auto *snapshot=new NativeCommandState;
    Check(!CaptureNativeCommandState(warm,*snapshot),"unobserved first family remains native");
    Check(FindCommandStateTokenLocked(ReadCommandStateToken(warm))->hooks_ready,
        "actual carrier publishes complete setter family");
    Hr(ID3D12GraphicsCommandList_Close(warm),"warm close");
    OnCommandStateDestroyList(&warm_owner);Drop(warm);
    const auto resets_before=g_command_native_reset_events.load();
    for(unsigned i=0;i<3;++i) {
        ID3D12GraphicsCommandList *list=nullptr;
        Hr(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,
            IID_ID3D12GraphicsCommandList,reinterpret_cast<void **>(&list)),"fresh null list");
        const auto token=ReadCommandStateToken(list);
        auto *record=FindCommandStateTokenLocked(token);
        const bool early=record && record->state && record->state->first_recording_observed;
        Check(early,"FIRST_RECORDING_NATIVE_ORACLE");
        if(!early)return 41;
        Check(!record->state->known&&!record->state->reset_serial&&!record->native_resets,
            "Create observation alone has neither admission nor Reset proof");
        reshade::api::command_list owner{&od,list};OnCommandStateInitList(&owner);
        OnCommandStateBeforeReset(&owner); // ReShade creation notification, not native Reset.
        Check(!CaptureNativeCommandState(list,*snapshot),"initial dummy PSO remains refused");
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,&heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(list,root);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(list,root);
        const UINT values[]={3,5,8};
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,0,3,values,2);
        ID3D12GraphicsCommandList_SetPipelineState(list,pso);
        Check(CaptureNativeCommandState(list,*snapshot),"fresh real-PSO carrier admitted without Reset");
        Check(snapshot->pipeline==pso&&snapshot->roots[0]==root&&snapshot->roots[1]==root&&snapshot->heaps[0]==heap&&
            snapshot->arguments[0][0].constants[2]==3&&snapshot->arguments[0][0].constants[4]==8,
            "first recording preserves bindings observed before its first PSO");
        Check(!snapshot->reset_serial&&!record->native_resets&&!record->reset_successes&&
            !snapshot->null_guard_reset_observed&&!std::strcmp(snapshot->baseline_origin,"Create-first-recording"),
            "first-recording origin never fabricates Reset counters or guard freshness");
        NativeCommandStateScope scope(list);Check(scope.active,"real state scope takes first-recording snapshot");
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,&other_heap);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(list,nullptr);
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(list,0,99,2);
        scope.Restore();
        Check(CaptureNativeCommandState(list,*snapshot)&&snapshot->arguments[0][0].constants[2]==3&&
            snapshot->heaps[0]==heap&&snapshot->roots[1]==root,
            "real native restore restores first-recording constants, heaps and both roots");
        if(i==0) {
            OnCommandStateResetResult(list,E_FAIL,pso); // Failure adapter, no fake native Reset.
            ID3D12GraphicsCommandList_SetPipelineState(list,pso);
            Check(!CaptureNativeCommandState(list,*snapshot),"failed Reset cannot revive via later PSO");
        }
        if(i==1) {
            Hr(ID3D12GraphicsCommandList_SetPrivateDataInterface(list,kNativeCommandLifetime,nullptr),"revoke lifetime receipt");
            Check(!FindCommandStateTokenLocked(token)&&!CaptureNativeCommandState(list,*snapshot),
                "revoked native lifetime cannot retain first-recording admission");
        }
        OnCommandStateDestroyList(&owner);
        Hr(ID3D12GraphicsCommandList_Close(list),"fresh final close");Drop(list);
        Check(!FindCommandStateTokenLocked(token),"native destruction removes first-recording state");
    }
    Check(g_command_native_reset_events.load()==resets_before+1,
        "only the explicit failed-Reset adapter changes lifetime Reset counters");
    ID3D12GraphicsCommandList *initial_pso_list=nullptr;
    Hr(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,pso,
        IID_ID3D12GraphicsCommandList,reinterpret_cast<void **>(&initial_pso_list)),"unsupported initial PSO list");
    ID3D12GraphicsCommandList_SetPipelineState(initial_pso_list,pso);
    Check(!CaptureNativeCommandState(initial_pso_list,*snapshot),"initial non-null Create stays outside narrow first-recording policy");
    Hr(ID3D12GraphicsCommandList_Close(initial_pso_list),"initial PSO close");
    Hr(ID3D12GraphicsCommandList_Reset(initial_pso_list,allocator,pso),"actual native Reset");
    Check(CaptureNativeCommandState(initial_pso_list,*snapshot)&&snapshot->reset_serial==1&&
        !snapshot->first_recording_observed&&!std::strcmp(snapshot->baseline_origin,"Reset"),
        "genuine Reset remains a distinct recovery path");
    Hr(ID3D12GraphicsCommandList_Close(initial_pso_list),"initial PSO final close");Drop(initial_pso_list);
    ReleaseNativeCommandState(*snapshot);delete snapshot;
    Drop(allocator);Drop(heap);Drop(other_heap);Drop(pso);Drop(root);Drop(device);Drop(adapter);Drop(factory);
    std::printf("First-recording native: %d checks / %d failures\n",checks,failures);
    return failures?1:0;
}
