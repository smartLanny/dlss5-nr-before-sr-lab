#include "nr_compatibility_verdict.h"
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace nr_compatibility_verdict;
unsigned int checks = 0;
void Check(bool value, const char *message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
bool Has(const std::string &text, const char *value) { return text.find(value) != std::string::npos; }

Input Ready() {
    Input i;
    i.current = {true, 7, 41, 2, 2};
    i.first_block = {true, false, i.current, "unsupported-input-format", true};
    i.resource.available = true;
    i.resource.scope = i.current;
    i.resource.admitted = i.resource.definitive = true;
    i.resource.state = ResourceState::Ready;
    i.resource.reason = "admitted";
    i.resource.next_action = NextAction::None;
    i.observer = {FactState::Complete, i.current, {}, true};
    i.command = i.observer;
    i.queue = i.observer;
    i.recovery.available = i.recovery.attempted = true;
    i.recovery.scope = i.current;
    i.recovery.state = RecoveryState::Pending;
    i.recovery.attempt_id = i.recovery.proof_attempt_id = 17;
    i.recovery.correlated_window = i.recovery.descriptor_verified = true;
    i.recovery.first_failure = "unsupported-input-format";
    i.recovery.first_failure_definitive = true;
    return i;
}
Input Proven() {
    Input i = Ready();
    i.recovery.native_success_streak = i.recovery.nr_success_streak = 3;
    i.recovery.submitted_count = 3;
    i.recovery.writeback_verified = true;
    i.recovery.state = RecoveryState::Succeeded;
    return i;
}
void RejectedResource(Input &i, ResourceState state, const char *reason) {
    i.resource.admitted = false;
    i.resource.state = state;
    i.resource.reason = reason;
    i.resource.next_action = NextAction::NeedsCode;
}

int main() {
    // #2: waiting -> descriptor/capability evidence -> a real retry -> proof.
    Input waiting;
    waiting.current = {true, 7, 0, 2, 0};
    waiting.first_block = {true, true, waiting.current, "waiting-feature1", true};
    auto v = Resolve(waiting);
    Check(v.stage == Stage::Waiting && v.nextAction == NextAction::AutoRetry &&
          !v.recoveryAttempted && v.recoveryResult != RecoveryState::Succeeded,
          "waiting needs an actual carrier, not a fabricated success");
    Check(Has(v.missingEvidence, "first-current-carrier-evaluate"), "waiting missing field is explicit");

    auto i = Ready();
    i.first_block.unresolved = true;
    i.recovery.attempted = false;
    i.recovery.state = RecoveryState::Unavailable;
    RejectedResource(i, ResourceState::QueryNotAttempted, "format-query-not-attempted");
    i.resource.definitive = false;
    v = Resolve(i);
    Check(v.stage == Stage::Resource && v.reason == "unsupported-input-format" &&
          v.autoRecoverable && v.nextAction == NextAction::AutoRetry && !v.evidenceComplete,
          "query not attempted keeps first cause and requests real evidence");
    Check(Has(v.missingEvidence,"format-capability-query-result") && !v.recoveryAttempted,
          "unattempted query does not manufacture a recovery attempt");

    i = Ready();
    v = Resolve(i);
    Check(v.recoveryAttempted && v.recoveryResult == RecoveryState::Pending && v.stage == Stage::Native,
          "descriptor evidence alone is not successful recovery");
    i.recovery.native_success_streak = i.recovery.nr_success_streak = i.recovery.submitted_count = 1;
    i.recovery.writeback_verified = true;
    v = Resolve(i);
    Check(v.recoveryResult != RecoveryState::Succeeded && v.nextAction != NextAction::NeedsVisualF7,
          "ONE_SUCCESS_NOT_RECOVERY");
    i.recovery.native_success_streak = 3;
    v = Resolve(i);
    Check(v.stage == Stage::Neural && Has(v.missingEvidence,"three-consecutive-NR-successes"),
          "native streak does not stand in for NR streak");
    i.recovery.nr_success_streak = 3; i.recovery.submitted_count = 2;
    v = Resolve(i);
    Check(v.stage == Stage::Submission && v.recoveryResult == RecoveryState::Pending,
          "successful evaluations do not stand in for matching submissions");
    i.recovery.submitted_count = 3; i.recovery.writeback_verified = false;
    v = Resolve(i);
    Check(v.stage == Stage::Writeback && v.recoveryResult == RecoveryState::Pending &&
          v.nextAction != NextAction::NeedsVisualF7, "WRITEBACK_REQUIRED");
    i.recovery.writeback_verified = true;
    v = Resolve(i);
    Check(v.recoveryResult == RecoveryState::Succeeded && v.stage == Stage::Visual &&
          v.autoRecoverable && v.evidenceComplete && v.nextAction == NextAction::NeedsVisualF7,
          "execution recovery is sufficient for the next visual action, not visual acceptance");
    Check(v.firstDefinitiveFailure == "unsupported-input-format" && v.firstFailureHistorical &&
          v.reason != v.firstDefinitiveFailure, "recovered history is retained without becoming the current reason");
    i.visual = {FactState::Complete, i.current, {}, true, i.recovery.attempt_id};
    v = Resolve(i);
    Check(v.stage == Stage::Complete && v.nextAction == NextAction::None &&
          v.recoveryResult == RecoveryState::Succeeded, "same-attempt visual confirmation completes next action");
    --i.visual.proof_attempt_id;
    Check(Resolve(i).nextAction == NextAction::NeedsVisualF7, "an old attempt's picture confirmation cannot certify a new retry");
    i.visual.proof_attempt_id = i.recovery.attempt_id;
    i.visual.state = FactState::Incomplete;
    i.visual.reason = "visual-mismatch";
    v = Resolve(i);
    Check(v.recoveryResult == RecoveryState::Failed && v.stage == Stage::Visual &&
          v.nextAction == NextAction::NeedsCode, "known visual failure is code work, not another unproven recovery success");

    // #203: a partial R10 comparison has no storage proof ID. This does not
    // erase the ledger's independently correlated native/NR/submission window.
    i = Proven();
    i.recovery.proof_attempt_id = 0;
    i.recovery.writeback_verified = false;
    i.recovery.state = RecoveryState::Pending;
    v = Resolve(i);
    Check(v.stage == Stage::Writeback && v.reason == "storage-comparison-unverified" &&
          v.recoveryResult == RecoveryState::Pending && !v.evidenceComplete &&
          v.nextAction == NextAction::AutoRetry && v.autoRecoverable &&
          Has(v.missingEvidence, "same-attempt-actual-storage-comparison") &&
          Has(v.missingEvidence, "final-consumer-and-present-verification") &&
          !Has(v.missingEvidence, "streak-and-submission-window"), "PARTIAL_STORAGE_PRESERVES_EXECUTION");
    Check(v.firstFailureHistorical && v.firstDefinitiveFailure == "unsupported-input-format" &&
          i.recovery.nr_success_streak == 3 && i.recovery.submitted_count == 3,
          "unverified comparison neither latches a new failure nor changes execution input");
    i.recovery.proof_attempt_id = i.recovery.attempt_id - 1;
    i.recovery.writeback_verified = true;
    v = Resolve(i);
    Check(v.stage == Stage::Writeback && v.recoveryResult == RecoveryState::Pending &&
          v.nextAction != NextAction::NeedsVisualF7, "old storage proof cannot certify current execution");
    i.recovery.proof_attempt_id = 0; i.recovery.writeback_verified = false;
    i.recovery.native_success_streak = 2;
    Check(Resolve(i).stage == Stage::Native, "missing storage proof preserves an actual short native streak");
    i.recovery.native_success_streak = 3; i.recovery.nr_success_streak = 2;
    Check(Resolve(i).stage == Stage::Neural, "missing storage proof preserves an actual short NR streak");
    i.recovery.nr_success_streak = 3; i.recovery.submitted_count = 2;
    Check(Resolve(i).stage == Stage::Submission, "missing storage proof preserves an actual short submission window");
    i = Proven(); i.recovery.proof_attempt_id = 0; i.recovery.writeback_verified = false;
    i.recovery.state = RecoveryState::Failed; i.recovery.failure_reason = "writeback-failed";
    i.recovery.failure_definitive = true;
    v = Resolve(i);
    Check(v.stage == Stage::Writeback && v.reason == "writeback-failed" &&
          v.recoveryResult == RecoveryState::Failed && v.evidenceComplete &&
          v.nextAction == NextAction::NeedsCode && !v.autoRecoverable,
          "an actual storage failure is distinct from an incomplete comparison");

    // #202/#229: the next exposure gate must stay exposure-specific.
    i = Proven();
    i.first_block = {true,true,i.current,"exposure-texture-unverified",true};
    RejectedResource(i,ResourceState::Rejected,"typed-exposure-view-rejected");
    i.resource.missing_evidence = "legal-typed-exposure-view";
    v = Resolve(i);
    Check(v.stage == Stage::Resource && v.reason == "exposure-texture-unverified" &&
          v.recoveryResult != RecoveryState::Succeeded && v.nextAction == NextAction::NeedsCode,
          "EXPOSURE_IS_NEXT_GATE");
    Check(v.firstDefinitiveFailure == "unsupported-input-format" && Has(v.missingEvidence,"legal-typed-exposure-view"),
          "prior input failure remains historical while current exposure explains the block");
    i.resource.next_action = NextAction::NeedsVisualF7;
    Check(Resolve(i).nextAction != NextAction::NeedsVisualF7,
          "a resource resolver cannot request F7 before real execution and writeback proof");
    i.resource.next_action = NextAction::NeedsCode;
    i.first_block.unresolved = false;
    v = Resolve(i);
    Check(v.reason == "typed-exposure-view-rejected" && !Has(v.reason,"query") &&
          !Has(v.reason,"device") && v.stage == Stage::Resource, "TYPED_EXPOSURE_IS_NOT_QUERY_FAILURE");
    i.resource.state = ResourceState::QueryFailed;
    i.resource.reason = "format-query-failed";
    v = Resolve(i);
    Check(v.reason == "format-query-failed" && !v.autoRecoverable && v.evidenceComplete,
          "explicit failed capability query is distinct from typed exposure rejection");
    i.resource.state = ResourceState::QueryNotAttempted;
    v = Resolve(i);
    Check(v.reason == "format-query-not-attempted" && v.autoRecoverable && !v.evidenceComplete,
          "capability query not attempted is not a failed query");
    i.resource.state = ResourceState::UnknownSemantic;
    v = Resolve(i);
    Check(v.reason == "unknown-resource-semantic" && !v.autoRecoverable &&
          v.nextAction == NextAction::NeedsCode && Has(v.missingEvidence,"authoritative-resource-semantic"),
          "unknown semantics request code/evidence, not a guessed conversion or an F7");
    i.resource.state = ResourceState::Unspecified;
    v = Resolve(i);
    Check(v.stage == Stage::Unknown && Has(v.missingEvidence,"resource-state"), "unclassified resource evidence remains unknown");

    // Current first block is authoritative; later errors/counters cannot erase it.
    i = Proven();
    i.first_block = {true,true,i.current,"module-observer-uncertain",true};
    RejectedResource(i,ResourceState::Rejected,"typed-exposure-view-rejected");
    i.queue = {FactState::Incomplete,i.current,"queue-affinity-unverified",true};
    v = Resolve(i);
    Check(v.stage == Stage::Observer && v.reason == "module-observer-uncertain" &&
          !v.autoRecoverable && v.nextAction == NextAction::NeedsCode &&
          v.recoveryResult != RecoveryState::Succeeded, "observer first cause outranks derived resource/queue symptoms");
    i.recovery.state = RecoveryState::NotNeeded;
    Check(Resolve(i).recoveryResult != RecoveryState::NotNeeded,
          "a current real rejection cannot be relabelled as not needing recovery");
    i.recovery.state = RecoveryState::Succeeded;
    Check(Has(v.missingEvidence,"complete-current-handle-lifecycle"), "unknown observer lineage is explicit missing evidence");
    i.first_block.reason = "queue-affinity-unverified";
    v = Resolve(i);
    Check(v.stage == Stage::Queue && v.reason == "queue-affinity-unverified" &&
          v.nextAction == NextAction::AutoRetry, "actual first queue block is not overwritten by later resource metadata");
    i.first_block.reason = "future-device-format-reason";
    v = Resolve(i);
    Check(v.stage == Stage::Unknown && v.reason == "future-device-format-reason" &&
          !v.evidenceComplete && Has(v.missingEvidence,"recognized-first-active-block-reason"),
          "unknown reason vocabulary is never guessed from substrings");
    i.first_block.unresolved = false;
    v = Resolve(i);
    Check(v.stage == Stage::Unknown && v.reason == "first-block-order-unobserved",
          "unordered concurrent evidence cannot manufacture an earliest cause");
    i = Proven();
    i.queue = {FactState::Incomplete,i.current,"queue-affinity-unverified",true};
    v = Resolve(i);
    Check(v.stage == Stage::Queue && v.recoveryResult != RecoveryState::Succeeded &&
          v.nextAction != NextAction::NeedsVisualF7, "current queue rejection revokes otherwise sufficient success counters");

    // Stale config/Feature/mode evidence, including a stale attempt, cannot pass.
    i = Proven(); ++i.recovery.scope.config;
    v = Resolve(i);
    Check(v.recoveryResult != RecoveryState::Succeeded && !v.recoveryAttempted &&
          v.reason == "recovery-scope-mismatch", "STALE_SCOPE_CANNOT_SUCCEED");
    for (int field = 0; field != 3; ++field) {
        i = Proven();
        if (field == 0) ++i.recovery.scope.feature_generation;
        if (field == 1) i.recovery.scope.requested_mode = 1;
        if (field == 2) i.recovery.scope.effective_mode = 1;
        v = Resolve(i);
        Check(v.recoveryResult != RecoveryState::Succeeded && !v.recoveryAttempted,
              "stale Feature or requested/effective mode proof rejected");
    }
    i = Proven(); ++i.resource.scope.config;
    Check(Resolve(i).reason == "resource-scope-mismatch", "current recovery proof cannot reuse an old resource decision");
    i = Proven(); i.queue.scope.feature_generation = 0;
    v = Resolve(i);
    Check(v.stage == Stage::Queue && v.recoveryResult != RecoveryState::Succeeded,
          "old queue facts cannot certify the current Feature");
    i = Proven(); i.first_block = {true,true,i.current,"unsupported-input-format",true}; ++i.first_block.scope.config;
    Check(Resolve(i).reason == "active-block-scope-mismatch", "stale unresolved first-block receipt is explicit");
    i = Proven(); ++i.recovery.attempt_id;
    v = Resolve(i);
    Check(v.recoveryResult == RecoveryState::Pending && Has(v.missingEvidence,"same-attempt"),
          "new attempt cannot inherit the previous attempt's proof window");
    i.recovery.proof_attempt_id = i.recovery.attempt_id;
    i.recovery.correlated_window = false;
    i.recovery.native_success_streak = i.recovery.nr_success_streak = i.recovery.submitted_count = 999999;
    v = Resolve(i);
    Check(v.recoveryResult != RecoveryState::Succeeded && v.nextAction != NextAction::NeedsVisualF7,
          "cumulative totals are not correlated consecutive-success evidence");

    i = Proven(); i.recovery.state = RecoveryState::Failed;
    i.recovery.failure_reason = "nr-evaluate-failed"; i.recovery.failure_definitive = true;
    v = Resolve(i);
    Check(v.recoveryResult == RecoveryState::Failed && v.stage == Stage::Neural &&
          v.nextAction == NextAction::NeedsCode && !v.autoRecoverable,
          "failed current attempt revokes stale successful counters and automatic retry");
    RejectedResource(i,ResourceState::Rejected,"typed-exposure-view-rejected");
    Check(Resolve(i).reason == "first-block-order-unobserved",
          "a later resource symptom cannot outrank a failed attempt without an ordered first-block receipt");
    i.resource=Ready().resource;
    i.recovery.attempted = false;
    v = Resolve(i);
    Check(!v.evidenceComplete && v.recoveryResult == RecoveryState::Unavailable &&
          Has(v.missingEvidence,"recovery-attempt-marker"), "failure without an observed attempt remains qualified");
    i = Proven(); i.recovery.state = RecoveryState::Unavailable;
    Check(Resolve(i).recoveryResult == RecoveryState::Unavailable, "unavailable state cannot be promoted by counts");
    i = Proven(); i.recovery.attempted = false;
    Check(Resolve(i).recoveryResult == RecoveryState::Pending && !Resolve(i).recoveryAttempted,
          "reported success without attempt marker stays pending");
    i = Proven(); i.recovery.attempt_id = 0; i.recovery.proof_attempt_id = 0;
    Check(Resolve(i).recoveryResult == RecoveryState::Pending, "zero attempt IDs do not establish correlation");
    i = Proven(); i.recovery.descriptor_verified = false;
    Check(Resolve(i).stage == Stage::Resource && Resolve(i).nextAction != NextAction::NeedsVisualF7,
          "actual descriptor verification is required independently of the resolver's admission");
    i = Proven(); i.resource.definitive = false;
    Check(Resolve(i).reason == "resource-admission-not-definitive", "tentative resource admission cannot prove recovery");
    i = Proven(); i.resource.missing_evidence = "exposure-value-semantic";
    Check(Resolve(i).reason == "resource-evidence-incomplete" &&
          Resolve(i).recoveryResult != RecoveryState::Succeeded &&
          Has(Resolve(i).missingEvidence,"exposure-value-semantic"),
          "explicit unresolved resource evidence cannot disappear behind positive counters");

    i = Proven(); i.observer = {};
    v = Resolve(i);
    Check(v.stage == Stage::Observer && Has(v.missingEvidence,"current-observer-proof") &&
          !v.evidenceComplete, "absent gate facts are not treated as complete");
    i.observer = {FactState::NotRequired,i.current,{},true};
    Check(Resolve(i).recoveryResult == RecoveryState::Succeeded, "not-required gate status must be explicit");
    ++i.observer.scope.config;
    Check(Resolve(i).recoveryResult != RecoveryState::Succeeded, "not-required is also scoped");
    i = Ready(); i.recovery.state = RecoveryState::NotNeeded; i.recovery.attempted = false;
    v = Resolve(i);
    Check(v.recoveryResult == RecoveryState::NotNeeded && v.stage == Stage::Complete &&
          v.nextAction == NextAction::None && v.evidenceComplete, "no current block is not relabelled as successful recovery");
    i.first_block = {true,true,i.current,"master-disabled",true};
    Check(Resolve(i).stage == Stage::Disabled && Resolve(i).nextAction == NextAction::None,
          "explicitly disabled processing does not trigger an automatic recovery");
    i = Proven(); i.recovery.available = false;
    Check(Resolve(i).firstDefinitiveFailure.empty(), "unavailable history is not promoted to a definitive failure");
    i = Proven(); i.current.available = false;
    Check(Resolve(i).stage == Stage::Unknown && !Resolve(i).evidenceComplete, "missing current scope remains unknown");
    i = Ready(); i.current.effective_mode = 0;
    i.resource.scope=i.recovery.scope=i.observer.scope=i.command.scope=i.queue.scope=i.current;
    Check(Resolve(i).reason == "execution-scope-incomplete", "unknown effective mode cannot certify execution");

    // No global state or borrowed output strings are needed for a stable F8.
    i = Proven();
    const auto first = Resolve(i), second = Resolve(i);
    Check(first.stage == second.stage && first.reason == second.reason &&
          first.missingEvidence == second.missingEvidence && i.recovery.nr_success_streak == 3,
          "Resolve is deterministic and leaves input evidence untouched");
    std::string transient = "queue-affinity-unverified";
    i.first_block={true,true,i.current,transient,true}; v=Resolve(i); transient="changed";
    Check(v.reason == "queue-affinity-unverified", "Verdict owns text copied from input views");
    Check(std::string(Name(NextAction::None))=="none" && std::string(Name(NextAction::AutoRetry))=="auto-retry" &&
          std::string(Name(NextAction::NeedsCode))=="needs-code" && std::string(Name(NextAction::NeedsVisualF7))=="needs-visual-F7",
          "nextAction vocabulary stays within the four integration values");
    std::printf("PASS actual Compatibility Verdict header: %u checks; pure host policy, no process/GPU inference\n",checks);
}
