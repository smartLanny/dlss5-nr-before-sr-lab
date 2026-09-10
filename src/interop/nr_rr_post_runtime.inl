// Included immediately before ProcessEvaluate. This is a real execution path
// for observed Feature13, not Feature1 spoofing or a global Mode rewrite.
NgxResult ProcessRrPost(EvaluateFeatureFn original,
                        ID3D12GraphicsCommandList* command_list,
                        const NgxHandle* handle, const NgxParams* params,
                        void* callback, const FeatureRecord& feature) {
    g_last_carrier_feature.store(13,std::memory_order_relaxed);
    g_rr_guard_was_active.store(true,std::memory_order_relaxed);
    ++g_rr_passthrough_evaluations;
    if(!nr_post_settings::Read().rr || !params || !command_list || !feature.generation) {
        SetBypassReason(BypassReason::RayReconstructionGuard);
        return original(command_list,handle,params,callback);
    }
    // RR calls must also train the existing real queue-affinity observer.
    // Without this counter, an RR-only game never reaches NR's queue proof.
    const auto carrier=++g_carrier_evaluations;
    TrackDlssResolution(params,feature);
    FeedbackObserveEvaluate(params,feature,carrier);
    ScopedRrOwnershipGate ownership(true,false,true);
    if(!ownership.acquired() || ownership.is_nested()) {
        if(!ownership.acquired() &&
           g_rr_ownership_gate_owner.load(std::memory_order_acquire)==1)
            LatchConcurrentRrOwnershipChange("Feature 13 Evaluate");
        ownership.Release();
        SetBypassReason(BypassReason::Feature1TransactionBusy);
        return original(command_list,handle,params,callback);
    }
    if(!TryEnterCriticalSection(&g_chain_cs)) {
        ownership.Release();
        SetBypassReason(BypassReason::Feature1TransactionBusy);
        return original(command_list,handle,params,callback);
    }
    const bool eligible=g_config.enabled && nr_post_settings::Read().rr &&
        !OtherNrOwnerActive() && !g_feature_tracking_uncertain.load(std::memory_order_acquire) &&
        !g_lifecycle_tracking_uncertain.load(std::memory_order_acquire) &&
        (g_config.identity_use_original || g_config.post_transfer_strength>0.000001f);
    if(!eligible) {
        LeaveCriticalSection(&g_chain_cs); ownership.Release();
        return original(command_list,handle,params,callback);
    }
    BeginPipelineStatsScopeLocked(feature.generation,1);
    ObserveRrDepthProjection(params, feature);
    nr_post_settings::last_rr_depth_type.store(feature.rr_depth_type,std::memory_order_relaxed);
    g_pipeline_eligible_carriers.fetch_add(1,std::memory_order_relaxed);
    ++nr_post_settings::rr_attempts;
    nr_post_settings::last_rr_tick.store(GetTickCount64(),std::memory_order_relaxed);
    nr_post_settings::last_rr_stage.store(4,std::memory_order_relaxed);
    NativeCommandStateScope before(command_list);
    PostSrSubstitution prepared{};
    if(before.active) {
        // No native Output/resource parameters are replaced. Capture guides and
        // exposure before the opaque native call using the ordinary post path.
        prepared=PrepareAfterSr(before.command_list,handle,params,feature);
        before.Restore();
        if(!prepared.active)
            nr_post_settings::rr_prepare_declined.fetch_add(1,std::memory_order_relaxed);
    } else nr_post_settings::rr_before_state_rejected.fetch_add(1,std::memory_order_relaxed);
    if(prepared.active) nr_post_settings::last_rr_stage.store(1,std::memory_order_relaxed);
    // Exactly one original RR invocation on EVERY branch. Its HRESULT/status
    // is retained on ordinary declines. An opaque NR2 failure returns its fatal
    // status after quarantine; it never retries an already run denoiser.
    if(prepared.active){nr_dual_runtime::Stamp(before.command_list,*prepared.slot,10);nr_dual_runtime::Stamp(before.command_list,*prepared.slot,8);}
    const NgxResult native_result=original(command_list,handle,params,callback);
    if(prepared.active)nr_dual_runtime::Stamp(before.command_list,*prepared.slot,9);
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
