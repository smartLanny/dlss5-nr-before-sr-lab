// A snapshot joins value receipts from the current carrier/interpretation.
// Process totals and previous configurations cannot satisfy any proof gate.
const char* NrRecoveryFailureReason(const nr_recovery::Failure& failure) {
    switch (failure.stage) {
    case nr_recovery::Stage::NrCreate: return "nr-create-failed";
    case nr_recovery::Stage::ParameterBinding: return "nr-parameter-binding-failed";
    case nr_recovery::Stage::ResourceAllocation: return "resource-allocation-failed";
    case nr_recovery::Stage::Descriptor: return "descriptor-verification-failed";
    case nr_recovery::Stage::NrEvaluate: return "nr-evaluate-failed";
    case nr_recovery::Stage::NativeSrEvaluate: return "native-evaluate-failed";
    case nr_recovery::Stage::TransferRecorded: return "submission-failed";
    case nr_recovery::Stage::Submitted: return "submission-failed";
    case nr_recovery::Stage::ActualStorageRead: return "writeback-failed";
    default: return "recovery-stage-unknown";
    }
}
nr_compatibility_verdict::Input SnapshotNrCompatibilityVerdict() {
    using namespace nr_compatibility_verdict;
    AcquireSRWLockShared(&g_nr_recovery_lock);
    const auto snapshot = g_nr_recovery_snapshot;
    const auto proof = g_nr_recovery.Read(snapshot.key, snapshot.signature);
    ReleaseSRWLockShared(&g_nr_recovery_lock);
    Input input = snapshot.input;
    const auto config = g_config_generation.load(std::memory_order_acquire);
    const auto requested = static_cast<unsigned>(g_requested_mode_fast.load(std::memory_order_acquire));
    if (!input.current.available || input.current.config != config || input.current.requested_mode != requested) {
        input = {}; input.current = {true, config, 0, requested, requested};
        input.first_block = {true, true, input.current,
            g_enabled_fast.load(std::memory_order_acquire) ? "waiting-feature1" : "master-disabled", true};
        return input;
    }
    if (g_feature_tracking_uncertain.load(std::memory_order_acquire))
        input.observer = {FactState::Incomplete, input.current, "module-observer-uncertain", true};
    if (g_lifecycle_tracking_uncertain.load(std::memory_order_acquire))
        input.queue = {FactState::Incomplete, input.current, "lifecycle-isolation", true};
    else if (proof.submitted_count >= 3 && proof.correlated_window)
        input.queue = {FactState::Complete, input.current, "three-actual-matching-queue-submissions", true};
    auto& recovery = input.recovery;
    recovery.available = snapshot.key.resource != 0;
    recovery.scope = input.current;
    recovery.attempted = proof.attempt_id != 0;
    recovery.attempt_id = proof.attempt_id;
    recovery.proof_attempt_id = proof.proof_attempt_id;
    recovery.correlated_window = proof.correlated_window;
    recovery.descriptor_verified = proof.descriptor_verified;
    recovery.nr_success_streak = proof.nr_success_streak;
    recovery.native_success_streak = proof.native_success_streak;
    recovery.submitted_count = proof.submitted_count;
    recovery.writeback_verified = proof.writeback_verified;
    recovery.state = proof.state == nr_recovery::State::RecoverySucceeded ? RecoveryState::Succeeded :
        proof.state == nr_recovery::State::Latched ? RecoveryState::Failed :
        proof.attempt_id ? RecoveryState::Pending : RecoveryState::Unavailable;
    if (proof.first_failure.present) {
        recovery.first_failure = NrRecoveryFailureReason(proof.first_failure);
        recovery.first_failure_definitive = true;
        if (!proof.first_failure_historical) {
            recovery.failure_reason = recovery.first_failure;
            recovery.failure_definitive = true;
        }
    }
    if (snapshot.receipt_overflow || proof.capacity_unknown || proof.frame_evidence_lost) {
        recovery.correlated_window = false;
        recovery.state = recovery.state == RecoveryState::Failed ? RecoveryState::Failed : RecoveryState::Unavailable;
    }
#if defined(NR_HAS_EXPOSURE_SEMANTIC_EVIDENCE)
    if (input.first_block.reason == "exposure-texture-unverified") {
        const auto exposure = SnapshotNrExposureSemantic();
        if (exposure.available && exposure.config == config &&
            exposure.feature == input.current.feature_generation &&
            static_cast<unsigned>(exposure.mode) == input.current.effective_mode) {
            auto& resource = input.resource;
            resource = {}; resource.available = true; resource.scope = input.current;
            resource.admitted = static_cast<bool>(exposure.semantic);
            resource.definitive = exposure.semantic.definitive;
            resource.state = resource.admitted ? ResourceState::Ready :
                !exposure.access.query_attempted ? ResourceState::QueryNotAttempted :
                FAILED(exposure.access.query_result) ? ResourceState::QueryFailed : ResourceState::Rejected;
            resource.reason = exposure.semantic.reason;
            resource.missing_evidence = std::string_view(exposure.semantic.missing_evidence) == "none" ? "" : exposure.semantic.missing_evidence;
            resource.next_action = NextAction::NeedsCode;
        }
    }
#endif
    // Sparse storage proof does not establish what the game's later passes
    // presented. Only a separately correlated visual capture can prove that.
    input.visual = {FactState::Unobserved, input.current, "final-present-visual-unobserved", false};
    return input;
}
