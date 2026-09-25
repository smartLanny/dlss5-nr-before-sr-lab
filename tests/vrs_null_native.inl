// Actual native hooks/capture/scope on a WARP command list. No submission or NGX.
// The ABI probe independently observes replay; registry Copy cannot satisfy it.
struct VrsReplayProbe { void **vtable; unsigned calls=0; D3D12_SHADING_RATE rate{};
    bool has=false; D3D12_SHADING_RATE_COMBINER combiners[2]{}; };
void STDMETHODCALLTYPE VrsReplaySetter(ID3D12GraphicsCommandList *list,
    D3D12_SHADING_RATE rate, const D3D12_SHADING_RATE_COMBINER *combiners) {
    auto &p=*reinterpret_cast<VrsReplayProbe *>(list); ++p.calls;
    p.rate=rate; p.has=combiners!=nullptr;
    if(combiners) memcpy(p.combiners,combiners,sizeof(p.combiners));
}
struct VrsQueryProbe { ID3D12GraphicsCommandList iface; HRESULT result=E_NOINTERFACE;
    IUnknown *answer=nullptr; };
HRESULT STDMETHODCALLTYPE VrsQuery(ID3D12GraphicsCommandList *list, REFIID, void **out) {
    auto &p=*reinterpret_cast<VrsQueryProbe *>(list); *out=p.answer; return p.result;
}
ULONG STDMETHODCALLTYPE VrsRelease(IUnknown *) { return 1; }
void STDMETHODCALLTYPE VrsImageTrampoline(ID3D12GraphicsCommandList *,ID3D12Resource *) {}
unsigned vrs_forwarded=0;
D3D12_SHADING_RATE vrs_forwarded_rate{};
D3D12_SHADING_RATE_COMBINER vrs_forwarded_combiners[2]{};
void STDMETHODCALLTYPE VrsObserveTrampoline(ID3D12GraphicsCommandList *,D3D12_SHADING_RATE rate,
    const D3D12_SHADING_RATE_COMBINER *combiners) {
    ++vrs_forwarded; vrs_forwarded_rate=rate;
    if(combiners) memcpy(vrs_forwarded_combiners,combiners,sizeof(vrs_forwarded_combiners));
}
int RunVrsNullGate() {
    g_allow_experimental=false; g_enabled_fast.store(true);
    // Real installation helper: unsupported does not invent slots; unexpected
    // QI errors and different tear-off interfaces remain fail-closed.
    ID3D12GraphicsCommandListVtbl qtable{};
    qtable.QueryInterface=&VrsQuery;
    qtable.Release=reinterpret_cast<decltype(qtable.Release)>(&VrsRelease);
    VrsQueryProbe query{{&qtable}};
    CommandStateHookRequest requests[48]{}; unsigned count=0;
    const CommandStateExtendedHookRequest extra[]={
        {&g_state_RSSetShadingRate,77,g_state_detours_RSSetShadingRate,"RSSetShadingRate"}};
    Check(AppendCommandStateInterfaceHooks(&query.iface,IID_ID3D12GraphicsCommandList5,
        requests,count,extra,1,"CL5") && count==0,"unsupported CL5 installs no replay hook");
    query.result=E_FAIL;
    Check(!AppendCommandStateInterfaceHooks(&query.iface,IID_ID3D12GraphicsCommandList5,
        requests,count,extra,1,"CL5") && count==0,"CL5 query failure stays native");
    VrsQueryProbe tearoff{{&qtable}};
    query.result=S_OK; query.answer=reinterpret_cast<IUnknown *>(&tearoff.iface);
    Check(!AppendCommandStateInterfaceHooks(&query.iface,IID_ID3D12GraphicsCommandList5,
        requests,count,extra,1,"CL5") && count==0,"CL5 tear-off cannot license slot77");

    IDXGIFactory4 *factory=nullptr; IDXGIAdapter *adapter=nullptr; ID3D12Device *device=nullptr;
    Hr(CreateDXGIFactory1(IID_IDXGIFactory4,reinterpret_cast<void **>(&factory)),"VRS factory");
    Hr(IDXGIFactory4_EnumWarpAdapter(factory,IID_IDXGIAdapter,reinterpret_cast<void **>(&adapter)),"VRS WARP");
    Hr(D3D12CreateDevice(reinterpret_cast<IUnknown *>(adapter),D3D_FEATURE_LEVEL_11_0,
        IID_ID3D12Device,reinterpret_cast<void **>(&device)),"VRS device");
    ID3D12CommandAllocator *allocator=nullptr; ID3D12GraphicsCommandList *list=nullptr;
    Hr(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_ID3D12CommandAllocator,reinterpret_cast<void **>(&allocator)),"VRS allocator");
    Hr(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,
        IID_ID3D12GraphicsCommandList,reinterpret_cast<void **>(&list)),"VRS list");
    ID3D12GraphicsCommandList5 *cl5=nullptr;
    Hr(ID3D12GraphicsCommandList_QueryInterface(list,IID_ID3D12GraphicsCommandList5,
        reinterpret_cast<void **>(&cl5)),"VRS CL5 required (no skipped pass)");
    Check(reinterpret_cast<void *>(cl5)==list,"CL5 is exactly the installed native interface");
    reshade::api::device od{device}; reshade::api::command_list owner{&od,list};
    OnCommandStateInitDevice(&od); OnCommandStateInitList(&owner);
    auto *snapshot=new NativeCommandState;
    Check(!CaptureNativeCommandState(list,*snapshot),"unobserved initial recording refused");
    const auto reset=[&] {
        Hr(ID3D12GraphicsCommandList_Close(list),"VRS close");
        OnCommandStateBeforeReset(&owner);
        Hr(ID3D12GraphicsCommandList_Reset(list,allocator,nullptr),"VRS Reset");
    };
    reset();
    Check(!CaptureNativeCommandState(list,*snapshot),"guard upgrade still needs fresh Reset");
    ID3D12GraphicsCommandList5_RSSetShadingRate(cl5,D3D12_SHADING_RATE_1X1,nullptr);
    Check(!CaptureNativeCommandState(list,*snapshot),"observed VRS cannot replace missing Reset");
    reset();
    const auto token=ReadCommandStateToken(list);
    auto *record=FindCommandStateTokenLocked(token);
    Check(record && record->native==list && record->hooks_ready,"live token owns hooked native list");
    const D3D12_SHADING_RATE_COMBINER defaults[]={D3D12_SHADING_RATE_COMBINER_PASSTHROUGH,
        D3D12_SHADING_RATE_COMBINER_PASSTHROUGH};
    // Distinct lanes catch dropped/swapped combiner observations, without
    // issuing optional coarse VRS to this device. The actual hook is used.
    const D3D12_SHADING_RATE_COMBINER distinct[]={D3D12_SHADING_RATE_COMBINER_MIN,
        D3D12_SHADING_RATE_COMBINER_MAX};
    auto *scalar_original=g_state_RSSetShadingRate.originals[3];
    g_state_RSSetShadingRate.originals[3]=reinterpret_cast<void *>(&VrsObserveTrampoline);
    StateHookRSSetShadingRate<3>(list,D3D12_SHADING_RATE_2X2,distinct);
    g_state_RSSetShadingRate.originals[3]=scalar_original;
    Check(vrs_forwarded==1 && vrs_forwarded_rate==D3D12_SHADING_RATE_2X2 &&
        memcmp(vrs_forwarded_combiners,distinct,sizeof(distinct))==0 &&
        record->state->dynamic.shading_rate==D3D12_SHADING_RATE_2X2 &&
        record->state->dynamic.has_combiners &&
        memcmp(record->state->dynamic.combiners,distinct,sizeof(distinct))==0,
        "actual hook forwards and preserves rate plus distinct combiner lanes");
    Check(!CaptureNativeCommandState(list,*snapshot),"observed nondefault scalar stays native");
    Check(std::strcmp(SnapshotCommandStateEvidence().reason,"null-guard-shading-rate-not-default")==0,
        "nondefault VRS publishes its precise admission boundary");
    const auto rejected=SnapshotCommandStateEvidence();
    Check(rejected.available && !rejected.captured && rejected.shading_rate_observed &&
        rejected.shading_rate==static_cast<UINT>(D3D12_SHADING_RATE_2X2) &&
        rejected.shading_combiners_explicit && rejected.shading_combiners[0]==static_cast<UINT>(distinct[0]) &&
        rejected.shading_combiners[1]==static_cast<UINT>(distinct[1]),
        "rejected actual setter publishes both scalar lanes without claiming admission");
    for(bool explicit_combiners:{false,true}) {
        ID3D12GraphicsCommandList5_RSSetShadingRate(cl5,D3D12_SHADING_RATE_1X1,
            explicit_combiners?defaults:nullptr);
        const bool admitted=CaptureNativeCommandState(list,*snapshot);
        Check(admitted,"VRS_DEFAULT_ADMISSION_ORACLE");
        if(!admitted) continue; // old policy negative control; never fabricate an admitted state
        Check(snapshot->dynamic.recorded==kDynamicShadingRate &&
            snapshot->dynamic.shading_rate==D3D12_SHADING_RATE_1X1 &&
            snapshot->dynamic.has_combiners==explicit_combiners &&
            (!explicit_combiners || memcmp(snapshot->dynamic.combiners,defaults,sizeof(defaults))==0),
            "actual setter saves rate and both combiners without inventing a NULL payload");
        Check(std::strcmp(SnapshotCommandStateEvidence().route,"guarded-null-raster")==0,
            "default VRS uses bounded automatic route");
        const auto evidence=SnapshotCommandStateEvidence();
        Check(evidence.shading_rate_observed && evidence.shading_rate==0 &&
            evidence.shading_combiners_explicit==explicit_combiners &&
            evidence.shading_combiners[0]==0 && evidence.shading_combiners[1]==0 &&
            g_command_history.count && g_command_history.entries[0].shading_rate_observed &&
            g_command_history.entries[0].shading_combiners_explicit==explicit_combiners,
            "live and historical evidence preserve observed NULL versus explicit combiners");
        void *table[79]{}; table[77]=reinterpret_cast<void *>(&VrsReplaySetter);
        VrsReplayProbe probe{table};
        RestoreCommandDynamicState(reinterpret_cast<ID3D12GraphicsCommandList *>(&probe),*snapshot);
        Check(probe.calls==1 && probe.rate==D3D12_SHADING_RATE_1X1 && probe.has==explicit_combiners &&
            (!probe.has || memcmp(probe.combiners,defaults,sizeof(defaults))==0),"VRS_SLOT77_REPLAY_ORACLE");
        const auto serial=snapshot->reset_serial;
        { NativeCommandStateScope scope(list); Check(scope.active,"VRS scope acquired");
          ID3D12GraphicsCommandList_ClearState(list,nullptr);
          Check(record->state->dynamic.recorded==0,"injected ClearState really erases VRS observation");
          scope.Restore();
          Check(CaptureNativeCommandState(list,*snapshot) && snapshot->reset_serial==serial &&
              snapshot->dynamic.has_combiners==explicit_combiners &&
              snapshot->dynamic.recorded==kDynamicShadingRate,"scope restores same recording payload"); }
        // Other legal/invalid payloads are predicate negatives, never issued to
        // a device without proven optional coarse-rate support.
        for(unsigned rate:{1u,4u,5u,6u,9u,10u,0xffffffffu}) {
            snapshot->dynamic.shading_rate=static_cast<D3D12_SHADING_RATE>(rate);
            Check(!CanAutoRestoreRasterNull(*snapshot),"nondefault or invalid rate remains experimental");
        }
        snapshot->dynamic.shading_rate=D3D12_SHADING_RATE_1X1;
        snapshot->dynamic.has_combiners=true;
        for(unsigned lane=0;lane<2;++lane) for(unsigned value:{1u,2u,3u,4u,5u,0xffffffffu}) {
            memcpy(snapshot->dynamic.combiners,defaults,sizeof(defaults));
            snapshot->dynamic.combiners[lane]=static_cast<D3D12_SHADING_RATE_COMBINER>(value);
            Check(!CanAutoRestoreRasterNull(*snapshot),"either nondefault combiner stays rejected");
        }
    }
    D3D12_VIEWPORT viewport{0,0,2,2,0,1}; D3D12_RECT scissor{0,0,2,2};
    ID3D12GraphicsCommandList_RSSetViewports(list,1,&viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(list,1,&scissor);
    ID3D12GraphicsCommandList_OMSetRenderTargets(list,0,nullptr,FALSE,nullptr);
    Check(CaptureNativeCommandState(list,*snapshot) && snapshot->dynamic.recorded==0x846,
        "observed 0x846 composes with existing bounded raster contract (empty target fixture)");
    { NativeCommandStateScope scope(list); Check(scope.active,"0x846 scope acquired");
      ID3D12GraphicsCommandList_ClearState(list,nullptr); scope.Restore();
      Check(CaptureNativeCommandState(list,*snapshot) && snapshot->dynamic.recorded==0x846 &&
          snapshot->dynamic.viewports[0].Width==2 && snapshot->dynamic.scissors[0].right==2,
          "0x846 actual scope preserves independent scalar and raster observations"); }
    // WARP exposes CL5 but not image VRS. Exercise the actual observation hook
    // with a no-op native trampoline; this forbidden setter is never replayed.
    auto *image_original=g_state_RSSetShadingRateImage.originals[3];
    g_state_RSSetShadingRateImage.originals[3]=reinterpret_cast<void *>(&VrsImageTrampoline);
    StateHookRSSetShadingRateImage<3>(list,nullptr);
    g_state_RSSetShadingRateImage.originals[3]=image_original;
    Check(!CaptureNativeCommandState(list,*snapshot),"even explicit NULL shading-rate image is outside subset");
    reset();
    Check(CaptureNativeCommandState(list,*snapshot) && snapshot->dynamic.recorded==0,
        "Reset erases old VRS and image observations, restoring pristine route");
    Check(!SnapshotCommandStateEvidence().shading_rate_observed,
        "Reset does not publish stale VRS defaults as observed");
    ID3D12GraphicsCommandList5_RSSetShadingRate(cl5,D3D12_SHADING_RATE_1X1,nullptr);
    OnCommandStateResetResult(list,E_FAIL,nullptr);
    Check(!CaptureNativeCommandState(list,*snapshot),"failed Reset cannot retain VRS admission");
    reset();
    ID3D12GraphicsCommandList5_RSSetShadingRate(cl5,D3D12_SHADING_RATE_1X1,nullptr);
    OnCommandStateDestroyList(&owner);
    Check(!CaptureNativeCommandState(list,*snapshot),"retired list token cannot reuse saved VRS");
    ReleaseNativeCommandState(*snapshot); delete snapshot;
    Drop(cl5); Drop(list); Drop(allocator);
    Drop(device); Drop(adapter); Drop(factory);
    printf("VRS default native: %d checks, %d failures; no GPU submission or game claim\n",checks,failures);
    return failures?41:0;
}
