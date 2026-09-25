// Included inside the existing F8/F7 FeedbackAppendActivity function.
// Keep one named TXT and the established bounded FeedbackAppend writer.
{
    const auto post = nr_post_settings::Read();
#if defined(NR_HAS_ROUTE_COMPATIBILITY)
    const bool compatibility_attempt_recent=IsCompatibilityPostAttemptRecent();
#else
    const bool compatibility_attempt_recent=IsCompatibilityPostObserved();
#endif
    FeedbackAppend(s,"%s.settings.colorProtection=%d\n",prefix,g_config.color_protection?1:0);
    FeedbackAppend(s,"%s.post.rr.missingProjection.saveFailed=%d\n",prefix,
        nr_post_settings::missing_projection_save_failed.load(std::memory_order_acquire)?1:0);
    FeedbackAppend(s,"%s.post.rr.depthAdapter.currentInput=%s\n%s.post.rr.depthAdapter.projectionAdmitted=%llu\n%s.post.rr.missingProjection.requested=%d\n%s.post.rr.missingProjection.admitted=%llu\n%s.post.rr.missingProjection.recorded=%llu\n%s.post.rr.missingProjection.note=explicit unknown-depth post candidate; Reset every Evaluate; normal creation/transition warmup retained; counts are layer dispatches, not converted depth, completed GPU work or NR adoption\n",
        prefix,nr_rr_depth::Name(nr_rr_depth::observations.input.load(std::memory_order_relaxed)),
        prefix,nr_rr_depth::observations.admitted.load(std::memory_order_relaxed),
        prefix,nr_post_settings::missing_projection.load(std::memory_order_acquire)?1:0,
        prefix,nr_rr_depth::observations.placeholder_admitted.load(std::memory_order_relaxed),
        prefix,nr_rr_depth::observations.placeholder_recorded.load(std::memory_order_relaxed),prefix);
    FeedbackAppend(s,"%s.post.rr.depthAdapter.lastReject=%s\n%s.post.rr.depthAdapter.proxyDeviceMatches=%llu\n",
        prefix,RrDepthRejectName(g_rr_depth_last_reject.load(std::memory_order_relaxed)),
        prefix,g_rr_depth_proxy_matches.load(std::memory_order_relaxed));
    FeedbackAppend(s,"%s.compat.requestedMode=%d\n%s.compat.effectiveMode=%d\n%s.compat.preInputFallback.recent=%d\n%s.compat.preInputFallback.reason=%d\n%s.compat.preInputFallback.attempts=%llu\n%s.compat.preInputFallback.recorded=%llu\n%s.compat.note=Evaluate only; post admission remains required; recording is not GPU completion; no Present NR provider\n",
        prefix,g_config.mode,prefix,EffectivePictureMode(g_config.mode),
        prefix,compatibility_attempt_recent?1:0,prefix,g_compat_fallback_reason.load(),
        prefix,g_compat_fallback_attempts.load(),prefix,g_compat_fallback_recorded.load(),prefix);
    FeedbackAppend(s,"%s.compat.preInputFallback.cause=%u\n%s.compat.preInputFallback.causeLegend=0:none,1:input-storage,2:input-encoding,3:r8-exposure,4:pq-exposure,5:shader-load\n",
        prefix,g_compat_fallback_cause.load(std::memory_order_relaxed),prefix);
#if defined(NR_HAS_ROUTE_COMPATIBILITY)
    const auto route=SnapshotNrRoute();
    const auto u=[](std::uint64_t value){return static_cast<unsigned long long>(value);};
    FeedbackAppend(s,"%s.compat.route=state:%s,current:%d,locked:%d,requested:%u,selected:%u,frame:%llu,scopeFirstFrame:%llu,device:%llu,feature:%llu,config:%llu,color:%llu,output:%llu,lifetimeKnown:%d,attempted:%d,admitted:%d,recorded:%d,submitted:%d,verified:%d,preDefinitive:%d,pre:%s,post:%s,invalidated:%s\n",
        prefix,nr_route::Name(route.state),route.current,route.locked,route.scope.requested,
        route.locked?1u:route.scope.requested,u(route.frame),u(route.first_frame),u(route.scope.device),
        u(route.scope.feature),u(route.scope.config),u(route.scope.color),u(route.scope.output),route.scope.lifetime_known,
        route.post_attempted,route.post_admitted,route.post_recorded,route.post_submitted,route.post_verified,
        route.pre_definitive,route.pre_reason,route.post_reason,route.invalidation);
    FeedbackAppend(s,"%s.compat.routeProof=attempt:%llu,tokenFrame:%llu,proofAttempt:%llu,proofFrame:%llu,window:%llu/%llu,descriptor:%d,storage:%d; submitted is this token, verified is its exact scope window; final visuals unverified\n",
        prefix,u(route.token.attempt_id),u(route.token.carrier.frame_id),u(route.proof.proof_attempt_id),
        u(route.proof.proof_frame_id),u(route.proof.window_first_frame_id),u(route.proof.window_last_frame_id),
        route.proof.descriptor_verified,route.proof.writeback_verified);
#endif
    FeedbackAppend(s,"%s.commandState.nativeCreated=%llu\n%s.commandState.nativeRetired=%llu\n%s.commandState.nativeRecordFailures=%llu\n",
        prefix,g_native_command_records.load(),prefix,g_native_command_retirements.load(),prefix,g_native_command_record_failures.load());
    FeedbackAppend(s, "%s.post.rr.depthAdapter.recorded=%llu\n%s.post.rr.depthAdapter.declined=%llu\n%s.post.rr.depthAdapter.note=standard positive view-Z projection; dispatch is not GPU completion\n",
        prefix,g_rr_depth_conversions.load(std::memory_order_relaxed),
        prefix,g_rr_depth_declines.load(std::memory_order_relaxed),prefix);
    const auto now = GetTickCount64();
    const auto attempted = nr_post_settings::last_rr_tick.load(std::memory_order_acquire);
    const bool recent = attempted != 0 && now >= attempted && now - attempted <= 2000;
    FeedbackAppend(s, "%s.post.rr.depthType=%d\n",
        prefix, nr_post_settings::last_rr_depth_type.load(std::memory_order_acquire));
    {
        auto &receipt = nr_post_settings::projection;
        const auto sequence = receipt.sequence.load(std::memory_order_seq_cst);
        const auto generation = receipt.generation.load(std::memory_order_seq_cst);
        const auto tick = receipt.tick.load(std::memory_order_seq_cst);
        const int state = receipt.state.load(std::memory_order_seq_cst);
        float matrix[16]{};
        for (unsigned i = 0; i != 16; ++i)
            matrix[i] = receipt.values[i].load(std::memory_order_seq_cst);
        const bool coherent = sequence != 0 && !(sequence & 1) &&
            sequence == receipt.sequence.load(std::memory_order_seq_cst);
        FeedbackAppend(s, "%s.post.rr.projection.coherent=%d\n", prefix, coherent ? 1 : 0);
        if (coherent) {
            FeedbackAppend(s, "%s.post.rr.projection.state=%d\n%s.post.rr.projection.generation=%llu\n"
                "%s.post.rr.projection.ageMs=%lld\n%s.post.rr.projection.note=CPU copy only; adapter admission and recordings reported separately; game depth semantics require runtime validation\n",
                prefix, state, prefix, generation,
                prefix, now >= tick ? static_cast<long long>(now-tick) : -1LL, prefix);
            if (state == 4) for (unsigned i = 0; i != 16; ++i)
                FeedbackAppend(s, "%s.post.rr.projection.m%u=%.9g\n", prefix, i, static_cast<double>(matrix[i]));
        }
    }
    FeedbackAppend(s,
        "%s.post.request.rr=%d\n%s.post.request.percent=%u\n"
        "%s.post.request.revision=%llu\n%s.post.settingsReady=%d\n"
        "%s.post.saveFailed=%d\n%s.post.requestRejected=%d\n%s.post.rejectedPercent=%u\n",
        prefix, post.rr ? 1 : 0, prefix, post.percent,
        prefix, nr_post_settings::revision.load(std::memory_order_acquire),
        prefix, nr_post_settings::ready.load(std::memory_order_acquire) ? 1 : 0,
        prefix, nr_post_settings::persistence_failed.load(std::memory_order_acquire) ? 1 : 0,
        prefix, nr_post_settings::request_rejected.load(std::memory_order_acquire) ? 1 : 0,
        prefix, nr_post_settings::rejected_percent.load(std::memory_order_acquire));
    FeedbackAppend(s,
        "%s.post.rr.attempts=%llu\n%s.post.rr.nrRecordings=%llu\n"
        "%s.post.rr.nativeFailures=%llu\n%s.post.rr.lastStage=%d\n"
        "%s.post.rr.recentAttempt=%d\n%s.post.rr.attemptAgeMs=%lld\n"
        "%s.post.note=sampled atomic observations; request/recording is not GPU completion, presentation or game verification\n",
        prefix, nr_post_settings::rr_attempts.load(std::memory_order_acquire),
        prefix, nr_post_settings::rr_recorded.load(std::memory_order_acquire),
        prefix, nr_post_settings::rr_native_failures.load(std::memory_order_acquire),
        prefix, nr_post_settings::last_rr_stage.load(std::memory_order_acquire),
        prefix, recent ? 1 : 0,
        prefix, attempted != 0 && now >= attempted ? static_cast<long long>(now - attempted) : -1LL,
        prefix);
    FeedbackAppend(s,
        "%s.post.evaluateEntries=%llu\n%s.post.rr.srGuarded=%llu\n"
        "%s.post.entryNote=existing counters after early bypass; SR guarded by a live RR owner does not increment carrier/post attempts\n",
        prefix, g_all_evaluations.load(std::memory_order_acquire),
        prefix, g_rr_guarded_sr_evaluations.load(std::memory_order_acquire),
        prefix);
    FeedbackAppend(s,
        "%s.post.rr.beforeStateRejected=%llu\n%s.post.rr.prepareDeclined=%llu\n"
        "%s.post.rr.afterStateRejected=%llu\n%s.post.rr.finishDeclined=%llu\n"
        "%s.post.rr.finishGateRejected=%llu\n"
        "%s.post.rr.boundaryNote=lifetime sampled counters; compare same-process start/end deltas; not disjoint outcomes or GPU completion; commandState is a separate latest sample\n",
        prefix, nr_post_settings::rr_before_state_rejected.load(std::memory_order_acquire),
        prefix, nr_post_settings::rr_prepare_declined.load(std::memory_order_acquire),
        prefix, nr_post_settings::rr_after_state_rejected.load(std::memory_order_acquire),
        prefix, nr_post_settings::rr_finish_declined.load(std::memory_order_acquire),
        prefix, nr_post_settings::rr_finish_gate_rejected.load(std::memory_order_acquire),
        prefix);
}

    FeedbackAppend(s,"%s.post.rr.depthAdapter.projectionSource=%s\n%s.post.rr.depthAdapter.slScopeMatches=%llu\n%s.post.rr.depthAdapter.slCommandMismatches=%llu\n%s.post.rr.depthAdapter.slProjectionSelected=%llu\n",
        prefix,nr_rr_projection::last_origin.load()==1?"same-frame-streamline":"current-ngx-or-unavailable",
        prefix,nr_rr_projection::scope_matches.load(),prefix,nr_rr_projection::scope_mismatches.load(),prefix,nr_rr_projection::used.load());
