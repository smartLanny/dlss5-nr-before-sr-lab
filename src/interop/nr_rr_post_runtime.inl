#if defined(NR_HAS_ACCEPTANCE)
// Acceptance timestamp implementation follows this interop include in the generated Core.
namespace nr_acceptance_gpu { void Stamp(ID3D12GraphicsCommandList*,NrSlot&,unsigned); void Abort(NrSlot&,const char*); }
#endif
// Included immediately before ProcessEvaluate. This is a real execution path
// for observed Feature13, not Feature1 spoofing or a global Mode rewrite.
NgxResult ProcessRrPost(EvaluateFeatureFn original,
                        ID3D12GraphicsCommandList* command_list,
                        const NgxHandle* handle, const NgxParams* params,
                        void* callback, const FeatureRecord& feature) {
#if defined(NR_HAS_ROUTE_COMPATIBILITY)
    NrRouteInvalidate("rr-route-active");
#endif
    g_last_carrier_feature.store(13,std::memory_order_relaxed);
    g_rr_guard_was_active.store(true,std::memory_order_relaxed);
    [[maybe_unused]] const auto rr_frame=++g_rr_passthrough_evaluations;
#if defined(NR_HAS_ACCEPTANCE)
    nr_acceptance_gpu::EntryScope acceptance_rr_entry(nr_acceptance_gpu::EntryRoute::Rr);
#endif
    if(!nr_post_settings::Read().rr || !params || !command_list || !feature.generation) {
#if defined(NR_HAS_ACCEPTANCE)
        acceptance_rr_entry.Decline(nr_acceptance_gpu::PreReason::Route);
#endif
        SetBypassReason(BypassReason::RayReconstructionGuard);
        return original(command_list,handle,params,callback);
    }
#if defined(NR_HAS_TIMING)
    // RR frame numbers use the RR Evaluate counter, paired with this Feature's
    // lifecycle generation. They are not Feature1 recovery frame numbers.
    NrTimingCarrierScope timing_carrier;
    ID3D12Device* timing_device=nullptr;
    unsigned long long timing_device_identity=0;
    if(SUCCEEDED(ID3D12GraphicsCommandList_GetDevice(command_list,IID_ID3D12Device,
            reinterpret_cast<void**>(&timing_device))) && timing_device) {
        timing_device_identity=NrFormatDeviceIdentity(timing_device);
        ID3D12Device_Release(timing_device);
    }
    timing_carrier.Begin(rr_frame,feature.generation,
        g_config_generation.load(std::memory_order_acquire),1,timing_device_identity);
#endif
    const auto call_native=[&]() {
#if defined(NR_HAS_TIMING)
        NrTimingScope native_timing(nr_timing::Stage::NativeEvaluate);
#endif
        const NgxResult result=original(command_list,handle,params,callback);
#if defined(NR_HAS_TIMING)
        NrTimingNativeResult(static_cast<std::int32_t>(result));
        native_timing.Stop();
#endif
        return result;
    };
    // RR calls must also train the existing real queue-affinity observer.
    // Without this counter, an RR-only game never reaches NR's queue proof.
    const auto carrier=++g_carrier_evaluations;
    TrackDlssResolution(params,feature);
    FeedbackObserveEvaluate(params,feature,carrier);
    ScopedRrOwnershipGate ownership(true,false,true);
    if(!ownership.acquired() || ownership.is_nested()) {
#if defined(NR_HAS_ACCEPTANCE)
        acceptance_rr_entry.Decline(nr_acceptance_gpu::PreReason::Owner);
#endif
        if(!ownership.acquired() &&
           g_rr_ownership_gate_owner.load(std::memory_order_acquire)==1)
            LatchConcurrentRrOwnershipChange("Feature 13 Evaluate");
        ownership.Release();
        SetBypassReason(BypassReason::Feature1TransactionBusy);
        return call_native();
    }
#if defined(NR_HAS_TIMING)
    NrTimingScope chain_timing(nr_timing::Stage::ChainLock);
#endif
    const bool chain_acquired=TryEnterCriticalSection(&g_chain_cs)!=0;
#if defined(NR_HAS_TIMING)
    chain_timing.Stop();
#endif
    if(!chain_acquired) {
#if defined(NR_HAS_ACCEPTANCE)
        acceptance_rr_entry.Decline(nr_acceptance_gpu::PreReason::Lifecycle);
#endif
        ownership.Release();
        SetBypassReason(BypassReason::Feature1TransactionBusy);
        return call_native();
    }
    const bool eligible=g_config.enabled && nr_post_settings::Read().rr &&
        !OtherNrOwnerActive() && !g_feature_tracking_uncertain.load(std::memory_order_acquire) &&
        !g_lifecycle_tracking_uncertain.load(std::memory_order_acquire) &&
        (g_config.identity_use_original || g_config.post_transfer_strength>0.000001f);
    if(!eligible) {
#if defined(NR_HAS_ACCEPTANCE)
        acceptance_rr_entry.Decline(OtherNrOwnerActive()?nr_acceptance_gpu::PreReason::Owner:
            (g_feature_tracking_uncertain.load(std::memory_order_acquire)||
             g_lifecycle_tracking_uncertain.load(std::memory_order_acquire))?
                nr_acceptance_gpu::PreReason::Lifecycle:nr_acceptance_gpu::PreReason::Route);
#endif
        LeaveCriticalSection(&g_chain_cs); ownership.Release();
        return call_native();
    }
    nr_processing_runtime::CarrierScope processing_carrier;
    if(nr_processing_runtime::Effective()==nr_processing::Start::Before)
        nr_processing_runtime::Reject(nr_processing::Start::Before,"RR-requires-post-reconstruction");
    if(nr_processing_runtime::PresentSelected()) {
#if defined(NR_HAS_ACCEPTANCE)
        acceptance_rr_entry.Decline(nr_acceptance_gpu::PreReason::Route);
#endif
        LeaveCriticalSection(&g_chain_cs);ownership.Release();return call_native();
    }
    BeginPipelineStatsScopeLocked(feature.generation,1);
    ObserveRrDepthProjection(params, feature);
    nr_post_settings::last_rr_depth_type.store(feature.rr_depth_type,std::memory_order_relaxed);
    g_pipeline_eligible_carriers.fetch_add(1,std::memory_order_relaxed);
    ++nr_post_settings::rr_attempts;
    nr_post_settings::last_rr_tick.store(GetTickCount64(),std::memory_order_relaxed);
    nr_post_settings::last_rr_stage.store(4,std::memory_order_relaxed);
    nr_rr_projection::NativeScope projection_scope(command_list,params,feature.generation);
    NativeCommandStateScope before(command_list);
    PostSrSubstitution prepared{};
    if(before.active) {
#if defined(NR_HAS_ACCEPTANCE)
        acceptance_rr_entry.Decline(nr_acceptance_gpu::PreReason::Unavailable);
#endif
        // No native Output/resource parameters are replaced. Capture guides and
        // exposure before the opaque native call using the ordinary post path.
        prepared=PrepareAfterSr(before.command_list,handle,params,feature);
        before.Restore();
        if(!prepared.active)
            nr_post_settings::rr_prepare_declined.fetch_add(1,std::memory_order_relaxed);
    } else {
#if defined(NR_HAS_ACCEPTANCE)
        acceptance_rr_entry.Decline(nr_acceptance_gpu::PreReason::Lifecycle);
#endif
        nr_post_settings::rr_before_state_rejected.fetch_add(1,std::memory_order_relaxed);
    }
    if(!prepared.active&&!nr_processing_runtime::Recorded())
        nr_processing_runtime::Reject(nr_processing::Start::After,"RR-post-input-or-command-state-unavailable");
    if(prepared.active) nr_post_settings::last_rr_stage.store(1,std::memory_order_relaxed);
    // Exactly one original RR invocation on EVERY branch. Its HRESULT/status
    // is retained on ordinary declines. An opaque NR2 failure returns its fatal
    // status after quarantine; it never retries an already run denoiser.
    if(prepared.active){
#if defined(NR_HAS_ACCEPTANCE)
        nr_acceptance_gpu::Stamp(before.command_list,*prepared.slot,1);
#endif
        nr_dual_runtime::Stamp(before.command_list,*prepared.slot,10);nr_dual_runtime::Stamp(before.command_list,*prepared.slot,8);}
    const NgxResult native_result=call_native();
    if(prepared.active){
#if defined(NR_HAS_ACCEPTANCE)
        nr_acceptance_gpu::Stamp(before.command_list,*prepared.slot,2);
#endif
        nr_dual_runtime::Stamp(before.command_list,*prepared.slot,9);}
    const auto after_feature=FindFeature(handle);
    const bool uncertain=g_feature_tracking_uncertain.load(std::memory_order_acquire) ||
        g_lifecycle_tracking_uncertain.load(std::memory_order_acquire) ||
        OtherNrOwnerActive();
    if(nr_post_compat::CanFinish(native_result,feature.generation,after_feature.generation,
            after_feature.handle==handle && after_feature.feature_id==13,uncertain,prepared.active)) {
        NativeCommandStateScope after(command_list);
        if(after.active) {
            if(FinishAfterSr(after.command_list,params,feature,prepared)) {
                ++nr_post_settings::rr_recorded;
                nr_post_settings::last_rr_stage.store(2,std::memory_order_relaxed);
            } else {
                nr_post_settings::rr_finish_declined.fetch_add(1,std::memory_order_relaxed);
                nr_post_settings::last_rr_stage.store(4,std::memory_order_relaxed);
            }
            if(prepared.fatal_recording){
#if defined(NR_HAS_ACCEPTANCE)
                nr_acceptance_gpu::Abort(*prepared.slot,"post-NR-fatal-before-finish");
#endif
                after.AbandonAfterOpaqueFailure();before.AbandonAfterOpaqueFailure();
                LeaveCriticalSection(&g_chain_cs);ownership.Release();return prepared.fatal_recording;
            }
            after.Restore();
        } else {
            nr_post_settings::rr_after_state_rejected.fetch_add(1,std::memory_order_relaxed);
            nr_post_settings::last_rr_stage.store(4,std::memory_order_relaxed);
        }
    } else if(native_result!=kNgxSuccess) {
        ++nr_post_settings::rr_native_failures;
        nr_post_settings::last_rr_stage.store(3,std::memory_order_relaxed);
    } else {
        // Only count this gate after successful preparation AND native RR.
        // Otherwise a before/prepare decline would be misleadingly counted twice.
        if(prepared.active)
            nr_post_settings::rr_finish_gate_rejected.fetch_add(1,std::memory_order_relaxed);
        nr_post_settings::last_rr_stage.store(4,std::memory_order_relaxed);
    }
    #if defined(NR_HAS_ACCEPTANCE)
    if(prepared.active)nr_acceptance_gpu::Abort(*prepared.slot,
        native_result!=kNgxSuccess?"native-RR-failed-before-finish":"RR-finish-unavailable");
    #endif
    if((uncertain || g_feature_tracking_uncertain.load(std::memory_order_acquire) ||
        g_lifecycle_tracking_uncertain.load(std::memory_order_acquire)) && prepared.active)
        QuarantineAbortedRrTransactionLocked(command_list);
    const auto attempts=nr_post_settings::rr_attempts.load(std::memory_order_relaxed);
    if(attempts<=3 || (attempts%600)==0)
        Log("[RR-post] native=0x%08X requestedMode=%d effectiveMode=1 stage=%d recorded=%llu; native RR Output retained on decline. F8 TXT for BUG feedback.",
            native_result,g_config.mode,nr_post_settings::last_rr_stage.load(),
            nr_post_settings::rr_recorded.load());
    LeaveCriticalSection(&g_chain_cs); ownership.Release();
    return native_result;
}
