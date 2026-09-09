// Adapter from the one production semantic decision to the value-only journal.
nr_recovery::ViewSignature NrRecoveryViewSignature(const NrResolvedColorView& result) {
    // Identity follows the actual read/write interpretation. Source strength
    // and observer-cache call numbers are diagnostics, not a new calculation:
    // losing/re-observing the same native view must never unlock its failure.
    return {result.plan.srv, result.plan.uav,
        static_cast<unsigned>(result.semantic.contract), 1};
}
NrResolvedColorView NrRecoveryResolve(NrResolvedColorView result, bool post_sr) {
    auto* frame = g_nr_recovery_carrier;
    if (!frame) return result;
    using namespace nr_resource_semantics;
    using namespace nr_compatibility_verdict;
    const unsigned role = post_sr ? 1u : 0u;
    if (frame->selected_role != role) {
        frame->selected_role = role;
        frame->snapshot.input.first_block = {};
    }
    auto& snapshot = frame->snapshot;
    auto& input = snapshot.input;
    input.current.effective_mode = post_sr ? 1u : 2u;
    input.observer.scope = input.current;
    if (frame->command_observed)
        input.command = {FactState::Complete, input.current, "actual-native-command-state-captured", true};
    snapshot.key = {frame->scopes[role], result.key.resource, result.key.resource, 0, role};
    snapshot.signature = NrRecoveryViewSignature(result);
    const auto semantic = result.semantic ?
        (result.semantic.source == Source::UniqueContract ? nr_recovery::SemanticState::Unique : nr_recovery::SemanticState::Exact) :
        (result.semantic.definitive ? nr_recovery::SemanticState::Rejected : nr_recovery::SemanticState::Waiting);
    AcquireSRWLockExclusive(&g_nr_recovery_lock);
    NrRecoveryForgetPreviousWaitingLocked(snapshot.key);
    const auto resolution = g_nr_recovery.Resolve(snapshot.key, snapshot.signature, semantic);
    ReleaseSRWLockExclusive(&g_nr_recovery_lock);
    if (result.semantic && resolution.state == nr_recovery::State::Latched) {
        Reject(result.semantic, nr_resource_semantics::Verdict::Unsupported, "prior-interpretation-validation-failed",
            "new-view-evidence-or-new-Feature-config-lifetime");
    }
    const auto& decision = result.semantic;
    auto& evidence = input.resource;
    evidence = {};
    evidence.available = true; evidence.scope = input.current;
    evidence.admitted = static_cast<bool>(decision); evidence.definitive = decision.definitive;
    evidence.state = evidence.admitted ? ResourceState::Ready :
        decision.verdict == nr_resource_semantics::Verdict::CapabilityUnknown ? ResourceState::QueryFailed :
        decision.definitive ? ResourceState::Rejected : ResourceState::UnknownSemantic;
    evidence.reason = decision.reason;
    evidence.missing_evidence = std::string_view(decision.missing_evidence) == "none" ? "" : decision.missing_evidence;
    evidence.next_action = std::string_view(decision.next_action) == "auto-retry" ? NextAction::AutoRetry : NextAction::NeedsCode;
    return result;
}
nr_recovery::Token NrRecoveryBegin(const NrResolvedColorView& view, bool post_sr) {
    auto* frame = g_nr_recovery_carrier;
    if (!frame || !view) return {};
    const unsigned role = post_sr ? 1u : 0u;
    AcquireSRWLockExclusive(&g_nr_recovery_lock);
    const auto token = g_nr_recovery.Begin(frame->snapshot.key, frame->snapshot.signature,
        frame->carriers[role], post_sr ? nr_recovery::WritebackTargetPolicy::SameResource : nr_recovery::WritebackTargetPolicy::Explicit);
    ReleaseSRWLockExclusive(&g_nr_recovery_lock);
    frame->tokens[role] = token;
    return token;
}
void NrRecoveryAdoptSlot(NrSlot& slot, const nr_recovery::Token& token, bool post_sr) {
    const bool same = slot.recovery_token.key == token.key && slot.recovery_token.signature == token.signature;
    if (!same) slot.recovery_probe_count = 0;
    slot.recovery_token = token;
    const auto target = NrFormatResourceIdentity(slot.result);
    slot.recovery_target = {target, target, 0};
    const bool programmed = slot.heap && slot.source && slot.result &&
        slot.descriptor_receipt.heap == slot.heap && slot.descriptor_receipt.source == slot.source &&
        slot.descriptor_receipt.result == slot.result &&
        slot.descriptor_receipt.source_srv == slot.source_srv_format &&
        slot.descriptor_receipt.result_uav == slot.result_uav_format &&
        (post_sr ? slot.result_srv_format : slot.source_srv_format) == token.signature.srv &&
        (post_sr ? slot.result_uav_format == token.signature.uav : slot.result_uav_format == DXGI_FORMAT_R16G16B16A16_FLOAT);
    AcquireSRWLockExclusive(&g_nr_recovery_lock);
    if (target) g_nr_recovery.BindWritebackTarget(token, slot.recovery_target);
    // D3D12 CreateSRV/CreateUAV return void. This records the owned writer's
    // programmed formats plus the successful capability decision; the stored
    // read is a separate, mandatory check before recovery can succeed.
    if (programmed) g_nr_recovery.Record(token, nr_recovery::Stage::Descriptor, true);
    ReleaseSRWLockExclusive(&g_nr_recovery_lock);
}
