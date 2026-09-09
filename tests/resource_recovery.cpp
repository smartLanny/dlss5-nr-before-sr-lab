// Direct calls into the production policy; no GPU/NGX or mirrored resolver.
#include "nr_resource_recovery.h"
#include <array>
#include <cstdio>
#include <cstring>

namespace {
using namespace nr_resource_recovery;
unsigned checks = 0, failures = 0;
void Check(bool condition, const char* name) {
    ++checks; if (!condition) { ++failures; std::printf("FAIL %s\n", name); }
}
Key Resource(std::uint64_t id = 31) {
    return {{11, 2, 21, 3, 4, 1}, id, 5, 0, 1};
}
ViewSignature View() { return {10, 10, 2, 500}; }
WritebackProof Actual(const Token& token) {
    return {token.key.resource, token.key.resource_generation, token.key.subresource, true, true, true, 0};
}
template<class Policy> Token Start(Policy& ledger, const Key& key, const ViewSignature& signature,
                                  std::uint64_t frame) {
    const auto resolved = ledger.Resolve(key, signature, SemanticState::Exact);
    Check(resolved.can_attempt, "a resolved live interpretation admits a normal attempt");
    const auto carrier = ledger.ObserveCarrier(key.scope, key.role, frame);
    Check(static_cast<bool>(carrier), "a real carrier receives an internal scope ordinal");
    const auto token = ledger.Begin(key, signature, carrier);
    Check(static_cast<bool>(token), "actual work receives an exact identity token");
    return token;
}
template<class Policy> void Events(Policy& ledger, const Token& token, bool submit = true,
                                   bool proof = false, bool end = true) {
    for (const auto stage : {Stage::Descriptor, Stage::NrEvaluate, Stage::NativeSrEvaluate, Stage::TransferRecorded})
        Check(ledger.Record(token, stage, true, stage == Stage::NativeSrEvaluate ? 1 : 0),
              "actual success is attached to its own token and stage");
    if (submit) Check(ledger.Record(token, Stage::Submitted, true), "real submit attaches to the exact frame");
    if (proof) Check(ledger.RecordWriteback(token, Actual(token)), "actual stored read attaches to its exact resource");
    if (end) Check(ledger.EndCarrier(token.key.scope, token.key.role, token.carrier, true), "end records that this carrier actually used NR");
}
template<class Policy> Token Good(Policy& ledger, const Key& key, const ViewSignature& signature,
                                 std::uint64_t frame, bool proof = false) {
    auto token = Start(ledger, key, signature, frame); Events(ledger, token, true, proof); return token;
}
void SingleFrameOracle() {
    ResourceRecoveryLedger ledger; const auto key = Resource(); const auto signature = View();
    Good(ledger, key, signature, 101, true);
    const auto evidence = ledger.Read(key, signature);
    Check(evidence.state == State::Pending && !evidence.correlated_window,
          "SINGLE_FRAME_ORACLE: one successful, submitted, stored frame is not recovered");
    Check(evidence.descriptor_verified && evidence.writeback_verified && evidence.nr_success_streak == 1 &&
          evidence.native_success_streak == 1 && evidence.submitted_count == 1,
          "one-frame evidence is accurately visible without manufacturing a three-frame window");
}
void SubmissionOracle() {
    ResourceRecoveryLedger ledger; const auto key = Resource(); const auto signature = View();
    for (unsigned i = 0; i != 3; ++i) {
        const auto token = Start(ledger, key, signature, 201 + i); Events(ledger, token, false, true);
    }
    const auto e = ledger.Read(key, signature);
    Check(e.state == State::Pending && !e.correlated_window && !e.writeback_verified && e.submitted_count == 0,
          "SUBMISSION_ORACLE: three transfer recordings and purported reads cannot replace real submission");
    Check(e.nr_success_streak == 3 && e.native_success_streak == 3,
          "NR and native successes remain distinct from missing real submissions");
}
void WaitingAndIdentity() {
    ResourceRecoveryLedger ledger; auto key = Resource(); const auto signature = View();
    auto r = ledger.Resolve(key, {}, SemanticState::Waiting);
    Check(r.state == State::Waiting && !r.can_attempt && !r.attempt_id, "waiting semantics does not begin an interpretation attempt");
    for (unsigned i = 0; i != 5; ++i) {
        const auto carrier = ledger.ObserveCarrier(key.scope, key.role, 301 + i);
        Check(!ledger.Begin(key, {}, carrier), "an unresolved native call cannot run an interpretation");
        Check(ledger.EndCarrier(key.scope, key.role, carrier, false), "a waiting native call is explicitly ended as bypass");
    }
    r = ledger.Resolve(key, signature, SemanticState::Unique);
    Check(r.state == State::Resolved && r.can_attempt && r.attempt_id == 0, "Unique resolution does not pretend descriptors or NR ran");
    Check(ledger.Read(key, signature).was_waiting, "resolved interpretation carries its prior wait state");
    for (unsigned i = 0; i != 5; ++i) Check(ledger.Resolve(key, signature, SemanticState::Unique).attempt_id == 0,
                                            "repeated semantic resolution does not allocate attempts");
    const auto first = Good(ledger, key, signature, 310, true);
    const auto second = Good(ledger, key, signature, 311);
    Check(first.attempt_id == second.attempt_id && first.attempt_id != 0, "each successful native call is not a new interpretation epoch");
    const auto e = ledger.Read(key, signature);
    Check(e.nr_success_streak == 2 && e.state == State::Pending, "two consecutive successes are still pending");
    const auto third = Good(ledger, key, signature, 312);
    Check(first.attempt_id == third.attempt_id, "third carrier retains interpretation identity");
    Check(ledger.Read(key, signature).state == State::RecoverySucceeded, "three correlated successes and same-attempt stored proof recover");
}
void ResourceRingAndSparseProof() {
    Ledger<8, 8, 4> ledger; const auto a = Resource(31), b = Resource(32), c = Resource(33); const auto signature = View();
    const auto ta = Good(ledger, a, signature, 401, true);
    const auto tb = Good(ledger, b, signature, 402);
    const auto tc = Good(ledger, c, signature, 403);
    Check(ta.attempt_id != tb.attempt_id && tb.attempt_id != tc.attempt_id, "resource ring lifetimes have separate interpretation attempts");
    Check(ledger.Read(c, signature).state == State::Pending && ledger.Read(c, signature).correlated_window,
          "ring continuity does not borrow another resource's stored proof");
    Check(ledger.RecordWriteback(tc, Actual(tc)), "current ring resource receives its own actual writeback proof");
    auto e = ledger.Read(c, signature);
    Check(e.state == State::RecoverySucceeded && e.proof_attempt_id == tc.attempt_id && e.proof_frame_id == 403,
          "normal resource ring rotation can form a three-carrier recovery window");
    Check(e.window_first_frame_id == 401 && e.window_last_frame_id == 403 && e.submitted_count == 3,
          "window exposes exactly the three real carrier identities");
    for (unsigned i = 0; i != 120; ++i) Good(ledger, c, signature, 404 + i);
    e = ledger.Read(c, signature);
    Check(e.state == State::RecoverySucceeded && e.proof_frame_id == 403 && e.proof_age == 120,
          "sparse actual sampling remains validated with explicit proof age");
    const auto pending = ledger.ObserveCarrier(c.scope, c.role, 524);
    Check(ledger.Read(c, signature).state == State::RecoverySucceeded,
          "a newly observed in-flight carrier does not erase completed recovery");
    Check(ledger.EndCarrier(c.scope, c.role, pending, false), "an actual NR bypass explicitly ends continuity");
    e = ledger.Read(c, signature);
    Check(e.state == State::Pending && !e.correlated_window && !e.writeback_verified && e.proof_frame_id == 403,
          "explicit bypass revokes validation while preserving labelled old proof metadata");
    Good(ledger, c, signature, 525); Good(ledger, c, signature, 526); const auto resumed = Good(ledger, c, signature, 527);
    Check(ledger.Read(c, signature).state == State::Pending && ledger.Read(c, signature).correlated_window,
          "a restarted recovery window cannot use a proof before its bypass break");
    ledger.RecordWriteback(resumed, Actual(resumed));
    Check(ledger.Read(c, signature).state == State::RecoverySucceeded, "a new actual proof validates the resumed window");
}
void OutOfOrderAndNoCountJoining() {
    ResourceRecoveryLedger ledger; const auto key = Resource(); const auto signature = View();
    std::array<Token, 3> frames;
    for (unsigned i = 0; i != 3; ++i) {
        frames[i] = Start(ledger, key, signature, 601 + i); Events(ledger, frames[i], false, false);
    }
    ledger.RecordWriteback(frames[2], Actual(frames[2]));
    ledger.Record(frames[2], Stage::Submitted, true); ledger.Record(frames[0], Stage::Submitted, true);
    Check(ledger.Read(key, signature).state == State::Pending, "two out-of-order submissions cannot fill the middle gap");
    ledger.Record(frames[1], Stage::Submitted, true);
    Check(ledger.Read(key, signature).state == State::RecoverySucceeded, "last-arriving middle submit completes the exact three-frame window");
    for (unsigned field = 0; field != 8; ++field) {
        auto wrong = frames[2];
        switch (field) {
        case 0: ++wrong.attempt_id; break;
        case 1: ++wrong.carrier.frame_id; break;
        case 2: ++wrong.carrier.ordinal; break;
        case 3: ++wrong.key.scope.feature_generation; break;
        case 4: ++wrong.key.resource_generation; break;
        case 5: ++wrong.signature.evidence_id; break;
        case 6: ++wrong.key.subresource; break;
        case 7: ++wrong.key.scope.mode; break;
        }
        Check(!ledger.Record(wrong, Stage::Submitted, true), "successful asynchronous evidence must match every token identity field");
    }
    ResourceRecoveryLedger separated;
    auto first = Start(separated, key, signature, 610);
    separated.Record(first, Stage::Descriptor, true); separated.Record(first, Stage::NrEvaluate, true);
    separated.EndCarrier(key.scope, key.role, first.carrier, true);
    auto second = Start(separated, key, signature, 611);
    separated.Record(second, Stage::NativeSrEvaluate, true); separated.Record(second, Stage::Submitted, true);
    separated.EndCarrier(key.scope, key.role, second.carrier, true);
    auto third = Start(separated, key, signature, 612);
    separated.Record(third, Stage::TransferRecorded, true); separated.RecordWriteback(third, Actual(third));
    separated.EndCarrier(key.scope, key.role, third.carrier, true);
    Check(separated.Read(key, signature).state == State::Pending && !separated.Read(key, signature).correlated_window,
          "independent stage totals from different frames never become a correlated recovery");
}
void ScopedOrdinalsAndBreaks() {
    ResourceRecoveryLedger ledger; const auto key = Resource(); auto other = key; ++other.scope.feature;
    const auto signature = View();
    Good(ledger, key, signature, 701, true);
    auto other_token = Good(ledger, other, signature, 702, true);
    const auto second = Good(ledger, key, signature, 703);
    const auto third = Good(ledger, key, signature, 704);
    Check(second.carrier.ordinal == 2 && third.carrier.ordinal == 3 && other_token.carrier.ordinal == 1,
          "another Feature's global frame counter does not insert a gap in this scope");
    Check(ledger.Read(key, signature).state == State::RecoverySucceeded, "interleaved Features recover independently");
    auto changed = key; ++changed.scope.config_generation;
    Good(ledger, changed, signature, 705, true);
    Check(ledger.Read(changed, signature).state == State::Pending, "new configuration starts its own three-frame window");
    auto carrier = ledger.ObserveCarrier(key.scope, key.role, 706);
    ledger.EndCarrier(key.scope, key.role, carrier, false);
    Good(ledger, key, signature, 707, true); Good(ledger, key, signature, 708);
    Check(ledger.Read(key, signature).state == State::Pending, "a bypass carrier breaks a streak even though frame ids keep increasing");
}
void FailureLatchesAndNewEvidence() {
    const auto key = Resource(); const auto signature = View();
    for (unsigned stage = 0; stage != static_cast<unsigned>(Stage::Count); ++stage) {
        ResourceRecoveryLedger ledger; const auto token = Start(ledger, key, signature, 801);
        const auto failed_stage = static_cast<Stage>(stage);
        const std::int32_t actual_result = stage == static_cast<unsigned>(Stage::NrEvaluate) ? 0x1234 : -2005270523;
        Check(ledger.Record(token, failed_stage, false, actual_result), "real failure is recorded without issuing work");
        const auto e = ledger.Read(key, signature);
        Check(e.state == State::Latched && e.first_failure.present && e.first_failure.stage == failed_stage &&
              e.first_failure.result == actual_result && e.first_failure.token == token,
              "first failure retains the actual stage, return code, attempt and frame");
        for (unsigned retry = 0; retry != 3; ++retry)
            Check(!ledger.Resolve(key, signature, SemanticState::Exact).can_attempt,
                  "same failed lifecycle and view signature are latched against repeated blind retry");
        const auto next = ledger.ObserveCarrier(key.scope, key.role, 802);
        Check(!ledger.Begin(key, signature, next), "a failed interpretation cannot begin again on a native call");
        ledger.EndCarrier(key.scope, key.role, next, false);
        auto new_signature = signature; ++new_signature.evidence_id;
        const auto renewed = Good(ledger, key, new_signature, 803, true);
        Check(renewed.attempt_id != token.attempt_id, "genuinely new view evidence permits a fresh normal attempt");
        Good(ledger, key, new_signature, 804); Good(ledger, key, new_signature, 805);
        Check(ledger.Read(key, new_signature).state == State::RecoverySucceeded, "new evidence can recover after three fresh successful carriers");
        const auto recovered = ledger.Read(key, new_signature);
        Check(recovered.first_failure_historical && recovered.first_failure.token == token &&
              recovered.first_failure.result == actual_result,
              "new interpretation preserves explicitly historical failure without treating it as active");
        ledger.ReportFailure(token, failed_stage, actual_result);
        Check(ledger.Read(key, new_signature).state == State::RecoverySucceeded,
              "duplicate notification of an old latched failure does not repeatedly reset new recovery");
        Check(!ledger.RecordWriteback(token, Actual(token)), "old failed attempt's late success cannot upgrade the new interpretation");
        ledger.ReportFailure(token, Stage::ActualStorageRead, -123);
        Check(ledger.Read(key, signature).first_failure.result == actual_result, "later failure preserves the first real return code");
    }
    for (unsigned changed_field = 0; changed_field != 11; ++changed_field) {
        ResourceRecoveryLedger ledger; const auto failed = Start(ledger, key, signature, 820);
        ledger.ReportFailure(failed, Stage::Descriptor, -3);
        auto changed = key; auto changed_signature = signature;
        switch (changed_field) {
        case 0: ++changed.scope.device; break;
        case 1: ++changed.scope.device_generation; break;
        case 2: ++changed.scope.feature; break;
        case 3: ++changed.scope.feature_generation; break;
        case 4: ++changed.scope.config_generation; break;
        case 5: ++changed.scope.mode; break;
        case 6: ++changed.resource_generation; break;
        case 7: ++changed.resource; break;
        case 8: ++changed_signature.srv; break;
        case 9: ++changed_signature.uav; break;
        case 10: ++changed_signature.provenance; break;
        }
        const auto fresh = Start(ledger, changed, changed_signature, 821);
        Check(fresh.attempt_id != failed.attempt_id, "each lifecycle or selected-view identity dimension prevents stale failure reuse");
        Check(ledger.Read(changed, changed_signature).state == State::Pending && !ledger.Read(changed, changed_signature).writeback_verified,
              "fresh interpretation does not inherit an old successful proof");
    }
}
void WritebackValidation() {
    const auto key = Resource(); const auto signature = View();
    for (unsigned problem = 0; problem != 5; ++problem) {
        ResourceRecoveryLedger ledger; const auto token = Good(ledger, key, signature, 901);
        auto proof = Actual(token);
        switch (problem) {
        case 0: proof.actual_storage_read = false; break;
        case 1: ++proof.resource; break;
        case 2: ++proof.resource_generation; break;
        case 3: proof.finite = false; break;
        case 4: proof.matches_expected = false; break;
        }
        ledger.RecordWriteback(token, proof); const auto e = ledger.Read(key, signature);
        if (!problem) Check(e.state == State::Pending && !e.writeback_verified && !e.first_failure.present,
                            "CPU/pre-store calculation without actual stored read remains unknown, not failure or proof");
        else Check(e.state == State::Latched && e.first_failure.stage == Stage::ActualStorageRead &&
            e.first_failure.kind == (problem <= 2 ? FailureKind::WrongTarget :
                                    problem == 3 ? FailureKind::NonFinite : FailureKind::WritebackMismatch),
            "wrong target, non-finite, or beyond-quantization stored mismatch revokes the interpretation");
    }
}
void BoundedEvidenceAndRetirement() {
    const auto signature = View(); const auto a = Resource(31), b = Resource(32), c = Resource(33);
    Ledger<2, 4, 2> full;
    const auto ta = Start(full, a, signature, 1001); full.ReportFailure(ta, Stage::Descriptor, -1);
    const auto tb = Start(full, b, signature, 1002); full.ReportFailure(tb, Stage::Descriptor, -2);
    const auto unavailable = full.Resolve(c, signature, SemanticState::Exact);
    Check(!unavailable.can_attempt && unavailable.capacity_unknown, "full failure ledger is unknown and does not evict a latch to retry");
    Check(!full.Resolve(a, signature, SemanticState::Exact).can_attempt, "capacity pressure preserves the oldest failure latch");
    full.RetireResource(a.scope, a.resource, a.resource_generation);
    const auto tc = Start(full, c, signature, 1003);
    Check(tc.attempt_id > tb.attempt_id && !full.ReportFailure(ta, Stage::Descriptor, -1),
          "actual resource retirement frees capacity without reusing attempt identity or accepting old callbacks");
    full.RetireScope(c.scope);
    Check(full.Resolve(a, signature, SemanticState::Exact).can_attempt, "actual Feature/config retirement frees its complete bounded scope");
    Check(!full.Record(tc, Stage::Submitted, true), "retired scope rejects asynchronous old submission proof");

    Ledger<8, 3, 2> frames; const auto lost = Start(frames, a, signature, 1010);
    Events(frames, lost, false, false);
    auto two = Start(frames, a, signature, 1011); Events(frames, two, false, false);
    auto three = Start(frames, a, signature, 1012); Events(frames, three, false, false);
    auto four = Good(frames, a, signature, 1013, true);
    Check(!frames.Record(lost, Stage::Submitted, true) && !frames.RecordWriteback(lost, Actual(lost)),
          "evicted incomplete frame cannot contribute a late successful submit or readback");
    Check(frames.Read(a, signature).state == State::Pending && frames.Read(a, signature).frame_evidence_lost,
          "frame overflow explicitly reports lost evidence and remains pending");
    Good(frames, a, signature, 1014); Good(frames, a, signature, 1015);
    Check(frames.Read(a, signature).state == State::RecoverySucceeded && frames.Read(a, signature).proof_frame_id == four.carrier.frame_id,
          "a complete fresh window can recover after bounded evidence loss");
    Check(frames.ReportFailure(lost, Stage::ActualStorageRead, -99) && frames.Read(a, signature).state == State::Latched,
          "an exact interpretation's late failure still revokes it after its frame was evicted");

    Ledger<8, 4, 1> scopes; Good(scopes, a, signature, 1020);
    auto other = a; ++other.scope.feature;
    Check(scopes.Resolve(other, signature, SemanticState::Exact).can_attempt &&
          !scopes.ObserveCarrier(other.scope, other.role, 1021), "scope capacity never invents a carrier identity");
    scopes.RetireScope(a.scope);
    Check(static_cast<bool>(scopes.ObserveCarrier(other.scope, other.role, 1022)), "true scope retirement restores bounded capacity");
}
}
int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--single-frame-oracle") == 0) SingleFrameOracle();
    else if (argc == 2 && std::strcmp(argv[1], "--submission-oracle") == 0) SubmissionOracle();
    else {
        SingleFrameOracle(); SubmissionOracle(); WaitingAndIdentity(); ResourceRingAndSparseProof();
        OutOfOrderAndNoCountJoining(); ScopedOrdinalsAndBreaks(); FailureLatchesAndNewEvidence();
        WritebackValidation(); BoundedEvidenceAndRetirement();
    }
    std::printf("%s resource recovery production policy: %u checks, %u failures; no GPU or NGX\n",
                failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 41 : 0;
}
