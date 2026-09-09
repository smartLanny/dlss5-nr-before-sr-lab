// CPU snapshot wiring only: the complete production Core precedes this file.
// Every event below is synthetic value evidence. No device, descriptor, NGX
// callback, command list, queue submission or actual storage read is performed.
// Separate WARP fixtures validate those physical boundaries.
#define NR_RECOVERY_SNAPSHOT_FIXTURE_API_READY 1

namespace resource_recovery_snapshot_test {
unsigned checks = 0, failures = 0;
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %s\n", message); }
}

struct ValueStream {
    nr_recovery::Key key{};
    nr_recovery::ViewSignature signature{10, 0, 1, 1};
    unsigned long long next_frame = 1;
    explicit ValueStream(unsigned generation) {
        key = {{51, 51, 1, generation, generation + 100, 2}, 71, 71, 0, 0};
        g_config_generation.store(static_cast<unsigned>(key.scope.config_generation));
        g_requested_mode_fast.store(2);
        g_enabled_fast.store(true);
        g_feature_tracking_uncertain.store(false);
        g_lifecycle_tracking_uncertain.store(false);
        g_nr_recovery_snapshot = {};
        Publish({});
    }
    ~ValueStream() { g_nr_recovery.RetireScope(key.scope); }
    void Publish(nr_recovery::Carrier carrier) {
        using namespace nr_compatibility_verdict;
        // Preserve the real overflow function's history flag while publishing
        // later synthetic carrier facts, just as a diagnostic snapshot can.
        const bool historical_overflow = g_nr_recovery_snapshot.receipt_overflow;
        auto& snapshot = g_nr_recovery_snapshot;
        snapshot = {};
        snapshot.receipt_overflow = historical_overflow;
        snapshot.frame = carrier.frame_id;
        snapshot.key = key; snapshot.signature = signature;
        auto& input = snapshot.input;
        input.current = {true, key.scope.config_generation, key.scope.feature_generation, 2, 2};
        input.resource.available = input.resource.admitted = input.resource.definitive = true;
        input.resource.scope = input.current; input.resource.state = ResourceState::Ready;
        input.resource.reason = "synthetic-CPU-resource-evidence";
        input.resource.next_action = NextAction::None;
        input.observer = {FactState::Complete, input.current, "synthetic-CPU-observer-evidence", true};
        input.command = {FactState::Complete, input.current, "synthetic-CPU-command-evidence", true};
    }
    nr_recovery::Token BeginValue() {
        const auto resolved = g_nr_recovery.Resolve(key, signature, nr_recovery::SemanticState::Exact);
        Check(resolved.can_attempt, "synthetic current interpretation is admitted by the production ledger");
        const auto carrier = g_nr_recovery.ObserveCarrier(key.scope, key.role, next_frame++);
        const auto token = g_nr_recovery.Begin(key, signature, carrier);
        Check(static_cast<bool>(token), "synthetic carrier receives a real ledger token");
        Publish(carrier); return token;
    }
    nr_recovery::Token CompleteValue(bool stored = false) {
        const auto token = BeginValue();
        for (const auto stage : {nr_recovery::Stage::Descriptor, nr_recovery::Stage::NrEvaluate,
                                 nr_recovery::Stage::NativeSrEvaluate, nr_recovery::Stage::TransferRecorded,
                                 nr_recovery::Stage::Submitted})
            Check(g_nr_recovery.Record(token, stage, true), "synthetic stage fact is accepted only for its exact token");
        if (stored) StoreValue(token);
        Check(g_nr_recovery.EndCarrier(key.scope, key.role, token.carrier, true), "synthetic carrier completion closes its exact frame");
        return token;
    }
    void StoreValue(const nr_recovery::Token& token) {
        const nr_recovery::WritebackProof synthetic{key.resource, key.resource_generation, key.subresource, true, true, true, 0};
        Check(g_nr_recovery.RecordWriteback(token, synthetic), "synthetic stored-value fact matches the bound target");
    }
};

void CheckMappedRecovery(const char* oracle) {
    const auto details = SnapshotNrRecoveryDetails();
    const auto input = SnapshotNrCompatibilityVerdict();
    const auto verdict = nr_compatibility_verdict::Resolve(input);
    Check(details.proof.state == nr_recovery::State::RecoverySucceeded && details.proof.correlated_window &&
          details.proof.writeback_verified, "production ledger contains a complete fresh synthetic recovery window");
    Check(input.recovery.state == nr_compatibility_verdict::RecoveryState::Succeeded &&
          input.recovery.correlated_window && input.recovery.proof_attempt_id == input.recovery.attempt_id &&
          verdict.stage == nr_compatibility_verdict::Stage::Visual &&
          verdict.nextAction == nr_compatibility_verdict::NextAction::NeedsVisualF7, oracle);
    Check(input.visual.state == nr_compatibility_verdict::FactState::Unobserved,
          "synthetic CPU recovery never manufactures final visual proof");
}

void ReceiptOverflowHistory() {
    ValueStream stream(401);
    const auto first = stream.CompleteValue(true);
    stream.CompleteValue(); const auto previous = stream.CompleteValue();
    CheckMappedRecovery("initial synthetic snapshot is recovered before receipt loss");

    // Actual production overflow adapter. It must interrupt this token's scope
    // immediately without allocating a made-up carrier or changing native work.
    NrRecoveryReceiptOverflow(previous);
    auto details = SnapshotNrRecoveryDetails();
    const auto interrupted = SnapshotNrCompatibilityVerdict();
    Check(details.current.receipt_overflow && details.proof.state == nr_recovery::State::Pending &&
          !details.proof.correlated_window && !details.proof.writeback_verified &&
          interrupted.recovery.state != nr_compatibility_verdict::RecoveryState::Succeeded,
          "production receipt overflow immediately revokes the prior recovery window");
    Check(details.proof.proof_attempt_id == first.attempt_id && details.proof.proof_frame_id == first.carrier.frame_id,
          "receipt interruption retains old stored identity as history");
    const auto next = stream.CompleteValue();
    Check(next.carrier.ordinal == previous.carrier.ordinal + 1 && next.attempt_id == previous.attempt_id,
          "overflow invents neither a carrier nor a new interpretation attempt");
    stream.CompleteValue(); const auto last = stream.CompleteValue();
    Check(SnapshotNrCompatibilityVerdict().recovery.state != nr_compatibility_verdict::RecoveryState::Succeeded,
          "three post-overflow complete synthetic carriers still require a new stored-value fact");
    stream.StoreValue(last);
    details = SnapshotNrRecoveryDetails();
    Check(details.current.receipt_overflow && !details.proof.capacity_unknown,
          "receipt overflow remains a historical diagnostic after fresh complete evidence");
    CheckMappedRecovery("RECEIPT_HISTORY_ORACLE: old receipt_overflow must not permanently downgrade a fresh recovered snapshot");
    std::printf("SNAPSHOT_CPU_CASE receipt-overflow history=%d recovered=%d PASS\n",
                details.current.receipt_overflow, details.proof.state == nr_recovery::State::RecoverySucceeded);
}

void FrameCapacityHistory() {
    ValueStream stream(402);
    stream.CompleteValue(true); stream.CompleteValue(); stream.CompleteValue();
    CheckMappedRecovery("initial synthetic snapshot is recovered before frame history loss");
    unsigned pending_carriers = 0;
    // Exercise the actual global ledger's capacity, not a smaller test copy.
    // Unfinished entries eventually evict an unfinished frame and create the
    // real evidence-loss break. No historical flag is assigned by this test.
    while (!SnapshotNrRecoveryDetails().proof.frame_evidence_lost && pending_carriers != 2048) {
        stream.BeginValue(); ++pending_carriers;
    }
    auto details = SnapshotNrRecoveryDetails();
    Check(pending_carriers > 3 && pending_carriers < 2048 && details.proof.frame_evidence_lost,
          "actual bounded FrameCapacity produces the historical lost-frame flag");
    Check(details.proof.state == nr_recovery::State::Pending && !details.proof.writeback_verified &&
          SnapshotNrCompatibilityVerdict().recovery.state != nr_compatibility_verdict::RecoveryState::Succeeded,
          "lost unfinished frame revokes prior synthetic recovery proof");
    stream.CompleteValue(); stream.CompleteValue(); const auto last = stream.CompleteValue();
    Check(SnapshotNrCompatibilityVerdict().recovery.state != nr_compatibility_verdict::RecoveryState::Succeeded,
          "fresh complete carriers cannot reuse the read before frame evidence loss");
    stream.StoreValue(last);
    details = SnapshotNrRecoveryDetails();
    Check(details.proof.frame_evidence_lost && !details.current.receipt_overflow && !details.proof.capacity_unknown,
          "fresh frame window retains lost-frame history while capacity and current proof are known");
    CheckMappedRecovery("FRAME_HISTORY_ORACLE: old frame_evidence_lost must not permanently downgrade a fresh recovered snapshot");
    std::printf("SNAPSHOT_CPU_CASE frame-history observed=%u lost=%d recovered=%d PASS\n",
                pending_carriers, details.proof.frame_evidence_lost, details.proof.state == nr_recovery::State::RecoverySucceeded);
}

void FailedAndUnavailableRemainDistinct() {
    ValueStream stream(403);
    const auto unavailable = SnapshotNrCompatibilityVerdict();
    Check(unavailable.recovery.state == nr_compatibility_verdict::RecoveryState::Unavailable &&
          !unavailable.recovery.correlated_window, "genuinely absent current entry remains unavailable");
    const auto failed = stream.BeginValue();
    g_nr_recovery.ReportFailure(failed, nr_recovery::Stage::NrEvaluate, -77);
    NrRecoveryReceiptOverflow(failed);
    const auto details = SnapshotNrRecoveryDetails();
    const auto input = SnapshotNrCompatibilityVerdict();
    Check(details.proof.state == nr_recovery::State::Latched && details.proof.first_failure.result == -77 &&
          input.recovery.state == nr_compatibility_verdict::RecoveryState::Failed &&
          input.recovery.failure_reason == "nr-evaluate-failed",
          "overflow history neither erases nor hides a real failed interpretation");
}

// Real runtime carrier object with explicitly synthetic values. Passing no
// command list avoids every device/COM call; only its value scope is supplied.
struct CpuCarrier {
    NrRecoveryCarrierScope runtime;
    explicit CpuCarrier(ValueStream& values) : runtime(nullptr, values.key.scope.feature_generation) {
        runtime.scopes[0] = values.key.scope;
        runtime.carriers[0] = g_nr_recovery.ObserveCarrier(values.key.scope, values.key.role, values.next_frame++);
        runtime.snapshot = g_nr_recovery_snapshot;
        runtime.snapshot.frame = runtime.carriers[0].frame_id;
        runtime.selected_role = 0;
    }
};
void NativeViewValue(const nr_native_format::Key& key, unsigned format) {
    nr_native_format::View view; view.Observe(format);
    const auto call = g_nr_format_calls.fetch_add(1) + 1;
    AcquireSRWLockExclusive(&g_nr_format_lock);
    g_nr_format_ledger.Commit(key, view, call, true);
    ReleaseSRWLockExclusive(&g_nr_format_lock);
}
NrResolvedColorView ResolvedValue(const nr_native_format::Key& key, unsigned format,
                                  nr_resource_semantics::Source source, nr_resource_semantics::Contract contract) {
    NrResolvedColorView result;
    result.key = key; result.evidence = NrFormatEvidence(key);
    result.plan.srv = format; result.plan.status = nr_format_access::Status::Ready;
    result.semantic.access = result.plan; result.semantic.source = source;
    result.semantic.contract = contract; result.semantic.definitive = true;
    result.semantic.verdict = nr_resource_semantics::Verdict::Admit;
    result.semantic.reason = "synthetic-CPU-resolved-view";
    result.semantic.missing_evidence = "none"; result.semantic.next_action = "none";
    return result;
}
void NativeCacheChurn(bool typed) {
    using namespace nr_resource_semantics;
    ValueStream stream(typed ? 404 : 405);
    const unsigned format = typed ? 10u : 28u;
    const auto contract = typed ? Contract::HdrDeclared : Contract::LdrEncoded;
    const nr_native_format::Key native_key{stream.key.scope.device, stream.key.resource,
                                         stream.key.scope.feature_generation, nr_native_format::Role::Color};
    if (!typed) NativeViewValue(native_key, format);
    const auto first_view = ResolvedValue(native_key, format, typed ? Source::TypedResource : Source::ObservedNativeView, contract);
    stream.signature = NrRecoveryViewSignature(first_view);
    stream.Publish({});
    nr_recovery::Token failed;
    {
        CpuCarrier carrier(stream);
        const auto resolved = NrRecoveryResolve(first_view, false);
        failed = NrRecoveryBegin(resolved, false);
        Check(static_cast<bool>(failed), "production Resolve/Begin admits the initial synthetic view");
        NrRecoveryRecord(failed, nr_recovery::Stage::NrEvaluate, false, 0xBAD00019u);
    }
    if (typed) NativeViewValue(native_key, format);
    const auto observed = ResolvedValue(native_key, format, Source::ObservedNativeView, contract);
    {
        CpuCarrier carrier(stream);
        const auto resolved = NrRecoveryResolve(observed, false);
        const auto retry = NrRecoveryBegin(resolved, false);
        Check(NrRecoveryViewSignature(observed) == failed.signature && !resolved && !retry,
              "SIGNATURE_SOURCE_ORACLE: stronger observation provenance cannot unlock the same failed read/write interpretation");
    }
    const auto before = NrFormatEvidence(native_key);
    Check(before.Resolved() == format && before.first_call != 0, "actual native ledger contains the synthetic target view before churn");
    for (unsigned i = 0; i != 256; ++i) {
        const nr_native_format::Key other{native_key.device, 10000u + native_key.feature * 1000u + i,
                                         native_key.feature, nr_native_format::Role::Color};
        NativeViewValue(other, format);
    }
    Check(!NrFormatEvidence(native_key).Resolved(), "256 real native-ledger insertions evict the target observation");
    if (typed) {
        const auto fallback = ResolvedValue(native_key, format, Source::TypedResource, contract);
        CpuCarrier carrier(stream);
        const auto resolved = NrRecoveryResolve(fallback, false);
        Check(!resolved && !NrRecoveryBegin(resolved, false), "typed physical fallback does not unlock the failed interpretation after native-cache loss");
    }
    NativeViewValue(native_key, format);
    const auto reobserved = ResolvedValue(native_key, format, Source::ObservedNativeView, contract);
    Check(reobserved.evidence.first_call != before.first_call && reobserved.evidence.Resolved() == format,
          "re-observation has a genuinely new cache call identity but the same typed view");
    {
        CpuCarrier carrier(stream);
        const auto resolved = NrRecoveryResolve(reobserved, false);
        const auto retry = NrRecoveryBegin(resolved, false);
        Check(NrRecoveryViewSignature(reobserved) == failed.signature && !resolved && !retry,
              "SIGNATURE_CHURN_ORACLE: native-cache eviction and same-view re-observation cannot restart a failed interpretation");
    }
    const auto failure = g_nr_recovery.Read(failed.key, failed.signature);
    Check(failure.state == nr_recovery::State::Latched && failure.first_failure.result == static_cast<std::int32_t>(0xBAD00019u),
          "real runtime failure latch and first callback result survive native-ledger churn");
    auto different = reobserved; ++different.plan.srv;
    Check(!(NrRecoveryViewSignature(different) == failed.signature), "different selected read view remains a distinct interpretation");
    different = reobserved; ++different.plan.uav;
    Check(!(NrRecoveryViewSignature(different) == failed.signature), "different selected write view remains a distinct interpretation");
    different = reobserved; different.semantic.contract = typed ? Contract::LdrEncoded : Contract::HdrDeclared;
    Check(!(NrRecoveryViewSignature(different) == failed.signature), "different semantic contract remains a distinct interpretation");
    std::printf("SNAPSHOT_CPU_NATIVE_CACHE physical=%s format=%u first_call=%llu reobserved_call=%llu retained_failure=%d\n",
                typed ? "typed" : "typeless", format, static_cast<unsigned long long>(before.first_call),
                static_cast<unsigned long long>(reobserved.evidence.first_call), failure.state == nr_recovery::State::Latched);
}

int Run(int, char**) {
    ReceiptOverflowHistory(); FrameCapacityHistory(); FailedAndUnavailableRemainDistinct();
    NativeCacheChurn(true); NativeCacheChurn(false);
    std::printf("SNAPSHOT_CPU %s checks=%u failures=%u; actual production mapper/overflow adapter, synthetic value facts; no GPU/NGX/device\n",
                failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 41 : 0;
}
} // namespace resource_recovery_snapshot_test
