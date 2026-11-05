// Reproduce SF6's missing ReShade owner using real D3D12 objects and hooks.
// No NGX/model call; this tests registration, state admission and destruction.
int RunNativeCommandRecordGate() {
    g_enabled_fast=true;
    IDXGIFactory4 *factory=nullptr; IDXGIAdapter *adapter=nullptr; ID3D12Device *device=nullptr;
    Hr(nr_test_warp::Prepare(),"Prepare pinned CI WARP");
    Hr(CreateDXGIFactory1(IID_IDXGIFactory4,reinterpret_cast<void **>(&factory)),"record factory");
    Hr(IDXGIFactory4_EnumWarpAdapter(factory,IID_IDXGIAdapter,reinterpret_cast<void **>(&adapter)),"record WARP");
    Hr(D3D12CreateDevice(reinterpret_cast<IUnknown *>(adapter),D3D_FEATURE_LEVEL_11_0,
        IID_ID3D12Device,reinterpret_cast<void **>(&device)),"record device");
    Hr(nr_test_warp::Verify("command-state-native-record"),"Verify actual CI WARP");
    reshade::api::device od{device};
    auto *root=MakeRoot(device,1); auto *pipeline=MakePipeline(device,root);
    ID3D12CommandAllocator *allocator=nullptr;
    Hr(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_ID3D12CommandAllocator,reinterpret_cast<void **>(&allocator)),"record allocator");
    ID3D12CommandAllocator *legacy_allocator=nullptr;
    Hr(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_ID3D12CommandAllocator,reinterpret_cast<void **>(&legacy_allocator)),
        "legacy record allocator");
    auto *snapshot=new NativeCommandState;

    // Create this list before the native creation observer is published.  The
    // ReShade wrapper therefore registers it first; a late creation receipt
    // must upgrade the same record instead of allocating a second token.
    ID3D12GraphicsCommandList *owner_first=nullptr;
    Hr(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,pipeline,
        IID_ID3D12GraphicsCommandList,reinterpret_cast<void **>(&owner_first)),
        "owner-first native create before observer");
    ID3D12GraphicsCommandList *legacy=nullptr;
    Hr(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,legacy_allocator,pipeline,
        IID_ID3D12GraphicsCommandList,reinterpret_cast<void **>(&legacy)),
        "legacy native create before observer");
    OnCommandStateInitDevice(&od);
    reshade::api::command_list legacy_wrapper{&od,legacy};
    OnCommandStateInitList(&legacy_wrapper);
    const auto legacy_token=ReadCommandStateToken(legacy);
    auto *legacy_record=FindCommandStateTokenLocked(legacy_token);
    Check(legacy_record && legacy_record->owner==&legacy_wrapper && !legacy_record->native_created,
        "pre-observer ReShade record remains an ordinary non-native-created record");
    const auto legacy_retire_before=g_native_command_retirements.load(std::memory_order_relaxed);
    OnCommandStateDestroyList(&legacy_wrapper);
    Check(!FindCommandStateTokenLocked(legacy_token) &&
        g_native_command_retirements.load(std::memory_order_relaxed)==legacy_retire_before,
        "ordinary non-native-created wrapper destroy still retires immediately");
    Hr(ID3D12GraphicsCommandList_Close(legacy),"legacy final close");
    Drop(legacy);
    Check(g_native_command_retirements.load(std::memory_order_relaxed)==legacy_retire_before,
        "ordinary record has no native lifetime callback after wrapper retirement");

    reshade::api::command_list owner_first_wrapper{&od,owner_first};
    OnCommandStateInitList(&owner_first_wrapper);
    const auto owner_first_token=ReadCommandStateToken(owner_first);
    auto *owner_first_record=FindCommandStateTokenLocked(owner_first_token);
    Check(owner_first_record && owner_first_record->native==owner_first &&
        owner_first_record->owner==&owner_first_wrapper && !owner_first_record->native_created &&
        owner_first_record->creation.serial==0,
        "owner-first ReShade registration starts without a native creation receipt");
    Check(!CaptureNativeCommandState(owner_first,*snapshot),
        "owner-first registration stays unknown before a successful Reset");
    owner_first_record=FindCommandStateTokenLocked(owner_first_token);
    NativeCommandState *owner_first_state=owner_first_record ? owner_first_record->state : nullptr;
    Check(owner_first_record && owner_first_state!=nullptr,
        "owner-first capture keeps one bounded state slot");

    CommandStateMetaContract owner_first_meta{};
    UINT owner_first_meta_bytes=sizeof(owner_first_meta);
    const HRESULT owner_first_meta_result=ID3D12Device_GetPrivateData(
        device,kCommandStateMetaContract,&owner_first_meta_bytes,&owner_first_meta);
    Check(SUCCEEDED(owner_first_meta_result) && owner_first_meta_bytes==sizeof(owner_first_meta) &&
        owner_first_meta.device_identity!=0,
        "owner-first upgrade reads the device meta identity");
    CommandCreationInfo late_creation{};
    late_creation.serial=g_command_creation_serial.fetch_add(1,std::memory_order_relaxed)+1;
    late_creation.tick=GetTickCount64();
    late_creation.device_identity=owner_first_meta.device_identity;
    late_creation.method=1; late_creation.initial_pso=true;
    Hr(ID3D12GraphicsCommandList_SetPrivateData(owner_first,kCommandCreationReceipt,
        sizeof(late_creation),&late_creation),"owner-first late creation receipt");
    Check(RegisterCreatedCommandStateList(owner_first,owner_first_meta,late_creation),
        "owner-first late creation receipt attaches native lifetime");
    auto *owner_first_upgraded=FindCommandStateTokenLocked(ReadCommandStateToken(owner_first));
    Check(owner_first_upgraded && owner_first_upgraded->native==owner_first &&
        ReadCommandStateToken(owner_first)==owner_first_token &&
        owner_first_upgraded->creation.serial==late_creation.serial &&
        owner_first_upgraded->native_created && owner_first_upgraded->lifetime_ready &&
        owner_first_upgraded->state==owner_first_state,
        "late creation receipt upgrades in place without a duplicate token or state");

    Hr(ID3D12GraphicsCommandList_Close(owner_first),"owner-first close");
    Hr(ID3D12GraphicsCommandList_Reset(owner_first,allocator,pipeline),"owner-first reset");
    ID3D12GraphicsCommandList_SetComputeRootSignature(owner_first,root);
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(owner_first,0,701,0);
    Check(CaptureNativeCommandState(owner_first,*snapshot) && snapshot->known,
        "owner-first upgraded record admits state only after a real Reset");
    owner_first_upgraded=FindCommandStateTokenLocked(owner_first_token);
    auto *owner_first_known_state=owner_first_upgraded ? owner_first_upgraded->state : nullptr;
    const auto owner_first_retire_before_detach=g_native_command_retirements.load(std::memory_order_relaxed);
    OnCommandStateDestroyList(&owner_first_wrapper);
    auto *owner_first_detached=FindCommandStateTokenLocked(owner_first_token);
    Check(owner_first_detached && owner_first_detached->owner==nullptr &&
        owner_first_detached->state==owner_first_known_state,
        "native-created wrapper destroy detaches owner but preserves observation state");
    Check(g_native_command_retirements.load(std::memory_order_relaxed)==owner_first_retire_before_detach,
        "wrapper destroy does not retire the native-created record");
    Check(CaptureNativeCommandState(owner_first,*snapshot) && snapshot->known,
        "native-created record remains capturable after wrapper destroy");
    const auto owner_first_retire_before_native_drop=g_native_command_retirements.load(std::memory_order_relaxed);
    Hr(ID3D12GraphicsCommandList_Close(owner_first),"owner-first final close");
    Drop(owner_first);
    Check(!FindCommandStateTokenLocked(owner_first_token) &&
        g_native_command_retirements.load(std::memory_order_relaxed)==owner_first_retire_before_native_drop+1,
        "true native destruction retires the detached record exactly once");

    for(unsigned cycle=0;cycle<3;++cycle) {
        ID3D12GraphicsCommandList *list=nullptr;
        Hr(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,pipeline,
            IID_ID3D12GraphicsCommandList,reinterpret_cast<void **>(&list)),
            "ownerless native create");
        const auto token=ReadCommandStateToken(list);
        auto *record=FindCommandStateTokenLocked(token);
        Check(record && record->native==list && !record->owner && record->native_created &&
            record->lifetime_ready,
            "native Create registers identity without a ReShade owner");
        if(!record) { Drop(list); continue; }
        Check(record->creation.method==1 && record->creation.serial && !record->state,
            "Create receipt gives identity, never invented state");
        Check(!CaptureNativeCommandState(list,*snapshot),
            "initial unobserved recording stays native");
        record=FindCommandStateTokenLocked(token);
        auto *state_before_owner=record ? record->state : nullptr;
        Check(record && record->hooks_ready && state_before_owner!=nullptr &&
            !state_before_owner->known,
            "native observation starts unknown and remains bounded");
        Hr(ID3D12GraphicsCommandList_Close(list),"ownerless close");
        Hr(ID3D12GraphicsCommandList_Reset(list,allocator,pipeline),"ownerless actual reset");
        ID3D12GraphicsCommandList_SetComputeRootSignature(list,root);
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(list,0,731+cycle,0);
        Check(CaptureNativeCommandState(list,*snapshot) && snapshot->known &&
              snapshot->arguments[0][0].constants[0]==731+cycle,
              "real Reset and native setters admit the exact ownerless list");
        if(cycle==1) {
            reshade::api::command_list old_owner{&od,list};
            reshade::api::command_list new_owner{&od,list};
            OnCommandStateInitList(&old_owner);
            record=FindCommandStateTokenLocked(token);
            Check(ReadCommandStateToken(list)==token && record && record->owner==&old_owner &&
                record->state!=nullptr && record->state==state_before_owner && record->state->known,
                "first ReShade wrapper attaches without replacing identity or state");
            OnCommandStateInitList(&new_owner);
            record=FindCommandStateTokenLocked(token);
            Check(ReadCommandStateToken(list)==token && record && record->owner==&new_owner &&
                record->state!=nullptr && record->state==state_before_owner && record->state->known,
                "second ReShade wrapper replaces only the current owner");
            const auto retire_before_stale_destroy=g_native_command_retirements.load(std::memory_order_relaxed);
            OnCommandStateDestroyList(&old_owner);
            auto *after_stale_destroy=FindCommandStateTokenLocked(token);
            Check(after_stale_destroy && after_stale_destroy->owner==&new_owner &&
                after_stale_destroy->state!=nullptr && after_stale_destroy->state==state_before_owner &&
                after_stale_destroy->state->known,
                "stale wrapper destroy preserves the current owner and state");
            Check(g_native_command_retirements.load(std::memory_order_relaxed)==retire_before_stale_destroy,
                "stale wrapper destroy does not retire the current native record");
            Hr(ID3D12GraphicsCommandList_Close(list),"stale owner close before reset");
            Hr(ID3D12GraphicsCommandList_Reset(list,allocator,pipeline),"stale owner reset after stale destroy");
            ID3D12GraphicsCommandList_SetComputeRootSignature(list,root);
            ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(list,0,913,0);
            record=FindCommandStateTokenLocked(token);
            Check(CaptureNativeCommandState(list,*snapshot) && snapshot->known &&
                snapshot->arguments[0][0].constants[0]==913 && record && record->owner==&new_owner &&
                record->state!=nullptr && record->state==state_before_owner && record->state->known,
                "current owner and native Reset state survive stale wrapper destroy");
            const auto retire_before_current_destroy=g_native_command_retirements.load(std::memory_order_relaxed);
            OnCommandStateDestroyList(&new_owner);
            auto *detached=FindCommandStateTokenLocked(token);
            Check(detached && detached->owner==nullptr && detached->state==state_before_owner,
                "current wrapper destroy detaches only after stale wrapper is ignored");
            Check(g_native_command_retirements.load(std::memory_order_relaxed)==retire_before_current_destroy,
                "current wrapper destroy leaves native-created record alive");
            Check(CaptureNativeCommandState(list,*snapshot) && snapshot->known,
                "native record remains capturable after current wrapper destroy");
        } else {
            OnCommandStateResetResult(list,E_FAIL,pipeline);
            Check(!CaptureNativeCommandState(list,*snapshot),
                "failed native Reset revokes admission");
        }
        const auto retire_before_native_drop=g_native_command_retirements.load(std::memory_order_relaxed);
        Hr(ID3D12GraphicsCommandList_Close(list),"native final close");
        Drop(list);
        Check(!FindCommandStateTokenLocked(token) &&
            g_native_command_retirements.load(std::memory_order_relaxed)==retire_before_native_drop+1,
            "native COM destruction retires exactly once");
    }

    ID3D12GraphicsCommandList *cleared=nullptr;
    Hr(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,pipeline,
        IID_ID3D12GraphicsCommandList,reinterpret_cast<void **>(&cleared)),
        "clear-notification native create");
    const auto cleared_token=ReadCommandStateToken(cleared);
    auto *cleared_record=FindCommandStateTokenLocked(cleared_token);
    Check(cleared_record && cleared_record->native_created && cleared_record->lifetime_ready,
        "clear-notification scenario starts with a native lifetime record");
    const auto retire_before_clear=g_native_command_retirements.load(std::memory_order_relaxed);
    Hr(ID3D12GraphicsCommandList_SetPrivateDataInterface(cleared,kNativeCommandLifetime,nullptr),
        "clear native lifetime notification");
    Check(!FindCommandStateTokenLocked(cleared_token) &&
        g_native_command_retirements.load(std::memory_order_relaxed)==retire_before_clear+1,
        "clearing the lifetime GUID revokes the record immediately");
    Check(!CaptureNativeCommandState(cleared,*snapshot),
        "cleared lifetime record refuses native capture");
    const auto retire_after_clear=g_native_command_retirements.load(std::memory_order_relaxed);
    Hr(ID3D12GraphicsCommandList_Close(cleared),"clear-notification final close");
    Drop(cleared);
    Check(g_native_command_retirements.load(std::memory_order_relaxed)==retire_after_clear,
        "cleared lifetime notification is not retired twice at COM destruction");

    ReleaseNativeCommandState(*snapshot);delete snapshot;
    Drop(legacy_allocator);Drop(allocator);Drop(pipeline);Drop(root);Drop(device);Drop(adapter);Drop(factory);
    std::printf("Native command records: %d checks / %d failures\n",checks,failures);
    return failures?1:0;
}
