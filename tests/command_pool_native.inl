// Native WARP regression; each list must acquire its own successful baseline.
void CheckResetNotificationGap(ID3D12Device *, reshade::api::device &);
void CheckCommandPoolPriming(ID3D12Device *device, reshade::api::device &od) {
    const unsigned before=g_primed_command_state_count;
    auto *lists=new ID3D12GraphicsCommandList *[kMaxPrimedCommandStates+3]{};
    auto *owners=new reshade::api::command_list[kMaxPrimedCommandStates+3]{};
    ID3D12CommandAllocator *allocator=nullptr;
    Hr(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_ID3D12CommandAllocator,reinterpret_cast<void **>(&allocator)),"pool allocator");
    auto *snapshot=new NativeCommandState;
    for (unsigned i=0;i<kMaxPrimedCommandStates+3;++i) {
        // Exercise cold Reset priming, not the separately tested enabled Create
        // boundary. Real creation/registration still runs; no receipt is forged.
        g_enabled_fast.store(false);
        Hr(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,
            IID_ID3D12GraphicsCommandList,reinterpret_cast<void **>(&lists[i])),"pool list");
        owners[i]={&od,lists[i]}; OnCommandStateInitList(&owners[i]);
        g_enabled_fast.store(true);
        Hr(ID3D12GraphicsCommandList_Close(lists[i]),"pool initial close");
        auto *record=FindCommandStateTokenLocked(ReadCommandStateToken(lists[i]));
        Check(record && !record->state,"registration does not invent recorded state");
        if (!record) ExitProcess(3);
        if (i==0) {
            g_enabled_fast.store(false); OnCommandStateBeforeReset(&owners[i]);
            Check(!record->state,"F6 off never starts speculative observation");
            g_enabled_fast.store(true);
            auto identity=record->meta_contract.device_identity;
            record->meta_contract.device_identity=~identity;
            OnCommandStateBeforeReset(&owners[i]);
            Check(!record->state,"different device cannot inherit family proof");
            record->meta_contract.device_identity=identity;
            for (auto &family:g_observed_command_families) {
                if(family.device_identity==identity && family.vtable==lists[i]->lpVtbl) {
                    const unsigned mask=family.interface_mask;family.interface_mask^=128;
                    OnCommandStateBeforeReset(&owners[i]);
                    Check(!record->state,"different extended interface coverage rejected");
                    family.interface_mask=mask;
                }
            }
            ID3D12GraphicsCommandListVtbl copied=*lists[i]->lpVtbl;
            ID3D12GraphicsCommandList different{&copied};
            Check(!FindObservedCommandFamilyLocked(*record,&different),"different native vtable rejected");
        }
        OnCommandStateBeforeReset(&owners[i]);
        Check(record->state && record->hooks_ready && !record->state->known,"observation begins before real Reset");
        if (!record->state) { std::fputs("POOL_FIRST_CAPTURE_ORACLE\n",stderr); ExitProcess(41); }
        OnCommandStateResetResult(lists[i],E_FAIL,nullptr);
        Check(!record->state->known,"failed Reset never certifies primed state");
        Hr(ID3D12GraphicsCommandList_Reset(lists[i],allocator,nullptr),"pool actual Reset");
        Check(record->state->known && record->state->null_guard_reset_observed,"native success establishes own baseline");
        if (i==0) {
            ID3D12RootSignature *root=MakeRoot(device,1);
            ID3D12GraphicsCommandList_SetComputeRootSignature(lists[i],root);
            ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(lists[i],0,731,0);
            Check(CaptureNativeCommandState(lists[i],*snapshot),"POOL_FIRST_CAPTURE_ORACLE: first capture ready");
            Check(snapshot->arguments[0][0].constants[0]==731,"own root arguments observed");
            Check(record->prime_serial==0,"actual carrier never speculatively evicted");
            Drop(root);
        }
        Hr(ID3D12GraphicsCommandList_Close(lists[i]),"pool close");
        Check(g_primed_command_state_count<=kMaxPrimedCommandStates,"bounded speculative state");
    }
    auto *first=FindCommandStateTokenLocked(ReadCommandStateToken(lists[0]));
    Check(first && first->state && first->prime_serial==0,"actual carrier survives cache pressure");
    auto *evicted=FindCommandStateTokenLocked(ReadCommandStateToken(lists[1]));
    Check(evicted && !evicted->state,"old non-carrier recycled");
    for(unsigned i=0;i<kMaxPrimedCommandStates+3;++i) {OnCommandStateDestroyList(&owners[i]);Drop(lists[i]);}
    Check(g_primed_command_state_count==before,"destroy returns budget");
    ReleaseNativeCommandState(*snapshot);delete snapshot;delete[] owners;delete[] lists;Drop(allocator);
    CheckResetNotificationGap(device, od);
}

// Real native hook: no ReShade pre-reset callback. A creation receipt alone is
// never admission, and the later successful Reset must not be silently lost.
void CheckResetNotificationGap(ID3D12Device *device, reshade::api::device &od) {
    ID3D12CommandAllocator *allocator=nullptr; ID3D12GraphicsCommandList *list=nullptr;
    Hr(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_ID3D12CommandAllocator,reinterpret_cast<void **>(&allocator)),"gap allocator");
    // Preserve a genuinely cold observer until the actual native Reset. Merely
    // checking !known on an already allocated Create observer would lose this
    // test's missing-pre-reset-notification regression oracle.
    g_enabled_fast.store(false);
    Hr(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,
        IID_ID3D12GraphicsCommandList,reinterpret_cast<void **>(&list)),"gap initial list");
    reshade::api::command_list owner{&od,list}; OnCommandStateInitList(&owner);
    g_enabled_fast.store(true);
    auto *record=FindCommandStateTokenLocked(ReadCommandStateToken(list));
    Check(record && !record->state,"native creation receipt alone never invents state");
    if (!record) ExitProcess(3);
    Check(record->creation.method==1 && record->creation.serial && record->genuine_init_event,
        "real CreateCommandList receipt joins the actual init event");
    const auto reset_count=g_command_native_reset_events.load();
    Hr(ID3D12GraphicsCommandList_Close(list),"gap initial close");
    // Deliberately omit OnCommandStateBeforeReset. Hooks for this family have
    // already been published by the preceding real carrier tests.
    Hr(ID3D12GraphicsCommandList_Reset(list,allocator,nullptr),"gap real Reset");
    auto *snapshot=new NativeCommandState;
    Check(CaptureNativeCommandState(list,*snapshot),"RESET_NOTIFICATION_GAP_ORACLE: actual Reset restores cold observation");
    Check(record->native_resets==1 && record->reset_notifications==0 && record->reset_successes==1,
        "native and ReShade reset observations are distinct");
    Check(g_command_native_reset_events.load()==reset_count+1,"native result counted exactly once");
    Check(snapshot->known && snapshot->null_guard_reset_observed && snapshot->reset_serial==1,
        "successful native Reset, not creation, proves baseline");
    OnCommandStateResetResult(list,E_FAIL,nullptr);
    Check(!CaptureNativeCommandState(list,*snapshot),"later failed Reset still invalidates baseline");
    OnCommandStateDestroyList(&owner);ReleaseNativeCommandState(*snapshot);delete snapshot;
    Drop(list);Drop(allocator);
}
