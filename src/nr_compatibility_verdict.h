#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// F8 snapshot policy only. This module reads no process state, chooses no GPU
// route, performs no retries and cannot relax an admission guard.
namespace nr_compatibility_verdict {

enum class Stage {
    Unknown, Disabled, Waiting, Observer, Ownership, Resource, Command, Queue,
    Runtime, Native, Neural, Submission, Writeback, Recovery, Visual, Complete
};
enum class NextAction { None, AutoRetry, NeedsCode, NeedsVisualF7 };
enum class RecoveryState { NotNeeded, Pending, Succeeded, Failed, Unavailable };
enum class ResourceState { Unspecified, Ready, UnknownSemantic, QueryNotAttempted, QueryFailed, Rejected };
enum class FactState { Unobserved, NotRequired, Complete, Incomplete };

inline constexpr unsigned int kRequiredSuccessStreak = 3;
inline constexpr unsigned int kRequiredSubmissions = 3;

struct Scope {
    bool available = false;
    std::uint64_t config = 0;
    std::uint64_t feature_generation = 0;
    unsigned int requested_mode = 0;
    unsigned int effective_mode = 0;
};

inline bool SameScope(const Scope &a, const Scope &b) {
    return a.available && b.available && a.config == b.config &&
        a.feature_generation == b.feature_generation &&
        a.requested_mode == b.requested_mode && a.effective_mode == b.effective_mode;
}

struct FirstBlock {
    bool available = false;
    bool unresolved = false;
    Scope scope;
    std::string_view reason;
    bool definitive = false;
};

struct ResourceEvidence {
    bool available = false;
    Scope scope;
    bool admitted = false;
    bool definitive = false;
    ResourceState state = ResourceState::Unspecified;
    std::string_view reason;
    std::string_view missing_evidence;
    NextAction next_action = NextAction::NeedsCode;
};

struct RecoveryEvidence {
    bool available = false;
    Scope scope;
    bool attempted = false;
    RecoveryState state = RecoveryState::Unavailable;
    // The producer must reset its streak window on a failure, a changed
    // resource contract, a new attempt or any Scope change. These are never
    // process-total counters. Both IDs and correlated_window must be supplied.
    std::uint64_t attempt_id = 0;
    std::uint64_t proof_attempt_id = 0;
    bool correlated_window = false;
    bool descriptor_verified = false;
    unsigned int nr_success_streak = 0;
    unsigned int native_success_streak = 0;
    unsigned int submitted_count = 0;
    bool writeback_verified = false;
    std::string_view failure_reason; // current failed attempt, if known
    bool failure_definitive = false;
    std::string_view first_failure; // retained history, including after success
    bool first_failure_definitive = false;
};

struct FactEvidence {
    FactState state = FactState::Unobserved;
    Scope scope;
    std::string_view reason;
    bool definitive = false;
    // Used only for final visual verification. Ordinary gate facts describe
    // the current snapshot. NotRequired must be explicit and scoped too.
    std::uint64_t proof_attempt_id = 0;
};

struct Input {
    Scope current;
    FirstBlock first_block; // earliest CURRENT unresolved block, not last error
    ResourceEvidence resource;
    RecoveryEvidence recovery;
    FactEvidence observer, command, queue, visual;
};

struct Verdict {
    Scope scope;
    Stage stage = Stage::Unknown;
    std::string reason = "not-observed";
    bool autoRecoverable = false;
    bool recoveryAttempted = false;
    RecoveryState recoveryResult = RecoveryState::Unavailable;
    std::string firstDefinitiveFailure;
    bool evidenceComplete = false;
    std::string missingEvidence;
    NextAction nextAction = NextAction::AutoRetry;
    // Prevent a preserved failure from being mistaken for the active cause.
    bool firstFailureHistorical = false;
    Scope firstFailureScope;
};

inline const char *Name(Stage value) {
    switch (value) {
    case Stage::Unknown: return "unknown";
    case Stage::Disabled: return "disabled";
    case Stage::Waiting: return "waiting-carrier";
    case Stage::Observer: return "observer";
    case Stage::Ownership: return "ownership";
    case Stage::Resource: return "resource";
    case Stage::Command: return "command-state";
    case Stage::Queue: return "queue";
    case Stage::Runtime: return "runtime";
    case Stage::Native: return "native";
    case Stage::Neural: return "nr";
    case Stage::Submission: return "submission";
    case Stage::Writeback: return "writeback";
    case Stage::Recovery: return "recovery";
    case Stage::Visual: return "visual";
    case Stage::Complete: return "complete";
    }
    return "unknown";
}
inline const char *Name(NextAction value) {
    switch (value) {
    case NextAction::None: return "none";
    case NextAction::AutoRetry: return "auto-retry";
    case NextAction::NeedsCode: return "needs-code";
    case NextAction::NeedsVisualF7: return "needs-visual-F7";
    }
    return "needs-code";
}
inline const char *Name(RecoveryState value) {
    switch (value) {
    case RecoveryState::NotNeeded: return "not-needed";
    case RecoveryState::Pending: return "pending";
    case RecoveryState::Succeeded: return "succeeded";
    case RecoveryState::Failed: return "failed";
    case RecoveryState::Unavailable: return "unavailable";
    }
    return "unavailable";
}

struct ReasonPolicy { Stage stage; NextAction action; bool known; };
inline ReasonPolicy ClassifyReason(std::string_view reason) {
    if (reason == "master-disabled") return {Stage::Disabled, NextAction::None, true};
    if (reason == "waiting-feature1") return {Stage::Waiting, NextAction::AutoRetry, true};
    if (reason == "module-observer-installing") return {Stage::Observer, NextAction::AutoRetry, true};
    if (reason == "module-observer-uncertain" || reason == "unknown-evaluate" ||
        reason == "unknown-release" || reason == "lifecycle-isolation")
        return {Stage::Observer, NextAction::NeedsCode, true};
    if (reason == "rr-safe-bypass" || reason == "shortfuse-owns-nr" ||
        reason == "external-feature18-owner" || reason == "renodx-dlss5-conflict")
        return {Stage::Ownership, NextAction::None, true};
    if (reason == "unsupported-input-format" || reason == "input-color-contract-unverified" ||
        reason == "unsupported-output-format" || reason == "output-color-contract-unverified" ||
        reason == "exposure-texture-unverified" || reason == "unknown-resource-semantic" ||
        reason == "format-query-not-attempted" || reason == "format-query-failed" ||
        reason == "typed-exposure-view-rejected" || reason == "descriptor-verification-failed")
        return {Stage::Resource, NextAction::NeedsCode, true};
    if (reason == "command-state-unverified" || reason == "feature1-transaction-busy")
        return {Stage::Command, NextAction::AutoRetry, true};
    if (reason == "queue-affinity-unverified" || reason == "queue-proof-unavailable")
        return {Stage::Queue, NextAction::AutoRetry, true};
    if (reason == "queue-retirement-timeout") return {Stage::Queue, NextAction::NeedsCode, true};
    if (reason == "nr-runtime-unavailable" || reason == "codec-unavailable")
        return {Stage::Runtime, NextAction::NeedsCode, true};
    if (reason == "native-evaluate-failed") return {Stage::Native, NextAction::NeedsCode, true};
    if (reason == "nr-evaluate-failed" || reason == "runtime-rejected" ||
        reason == "failed-create-quarantine") return {Stage::Neural, NextAction::NeedsCode, true};
    if (reason == "submission-failed") return {Stage::Submission, NextAction::NeedsCode, true};
    if (reason == "writeback-failed") return {Stage::Writeback, NextAction::NeedsCode, true};
    if (reason == "visual-mismatch") return {Stage::Visual, NextAction::NeedsCode, true};
    // Never infer a stage from a substring such as "device", "exposure", or
    // "format". New reason vocabulary must be wired explicitly by the caller.
    return {Stage::Unknown, NextAction::NeedsCode, false};
}

inline void Missing(Verdict &v, std::string_view field) {
    if (field.empty()) return;
    if (!v.missingEvidence.empty()) v.missingEvidence += ";";
    v.missingEvidence += field;
}
inline void Pending(Verdict &v, Stage stage, std::string_view reason, std::string_view missing) {
    v.stage = stage;
    v.reason = reason;
    v.evidenceComplete = false;
    v.autoRecoverable = true;
    v.nextAction = NextAction::AutoRetry;
    if (v.recoveryResult != RecoveryState::Failed) v.recoveryResult = RecoveryState::Pending;
    Missing(v, missing);
}
inline void QualifyActiveRecovery(Verdict &v) {
    if (v.recoveryResult == RecoveryState::NotNeeded)
        v.recoveryResult = v.autoRecoverable ? RecoveryState::Pending : RecoveryState::Unavailable;
    if (v.recoveryResult == RecoveryState::Failed && v.nextAction == NextAction::AutoRetry) {
        v.autoRecoverable = false;
        v.nextAction = NextAction::NeedsCode;
    }
}
inline void PreserveDefinitiveFailure(Verdict &v, std::string_view reason,
                                       bool definitive, const Scope &scope, bool historical) {
    if (!definitive || !scope.available || reason.empty() || !v.firstDefinitiveFailure.empty()) return;
    v.firstDefinitiveFailure = reason;
    v.firstFailureScope = scope;
    v.firstFailureHistorical = historical;
}

inline void ResourceVerdict(Verdict &v, const ResourceEvidence &resource,
                            bool keep_first_reason) {
    v.stage = Stage::Resource;
    v.evidenceComplete = resource.definitive;
    v.autoRecoverable = false;
    v.nextAction = NextAction::NeedsCode;
    Missing(v, resource.missing_evidence);
    if (!keep_first_reason && !resource.reason.empty()) v.reason = resource.reason;
    switch (resource.state) {
    case ResourceState::QueryNotAttempted:
        if (!keep_first_reason) v.reason = "format-query-not-attempted";
        v.evidenceComplete = false;
        v.autoRecoverable = true;
        v.nextAction = NextAction::AutoRetry;
        Missing(v, "format-capability-query-result");
        break;
    case ResourceState::QueryFailed:
        if (!keep_first_reason && resource.reason.empty()) v.reason = "format-query-failed";
        break;
    case ResourceState::UnknownSemantic:
        if (!keep_first_reason) v.reason = "unknown-resource-semantic";
        v.evidenceComplete = false;
        Missing(v, "authoritative-resource-semantic");
        break;
    case ResourceState::Rejected:
        if (!keep_first_reason && resource.reason.empty()) v.reason = "resource-rejected";
        // Only a resolver's explicit safe retry proposal may retry a rejection.
        if (resource.next_action == NextAction::AutoRetry) {
            v.autoRecoverable = true;
            v.nextAction = NextAction::AutoRetry;
        } else if (resource.next_action == NextAction::None) v.nextAction = NextAction::None;
        break;
    case ResourceState::Ready:
        // A still-active rejection is not erased by later admitted metadata.
        v.evidenceComplete = false;
        v.autoRecoverable = true;
        v.nextAction = NextAction::AutoRetry;
        Missing(v, resource.admitted ? "active-block-clear-or-retry-result" : "resource-admission-result");
        if (!keep_first_reason) v.reason = "resource-admission-unconfirmed";
        break;
    default:
        if (!keep_first_reason) v.stage = Stage::Unknown;
        if (!keep_first_reason) v.reason = "resource-classification-unavailable";
        v.evidenceComplete = false;
        Missing(v, "resource-state");
        break;
    }
}

inline bool GateReady(const FactEvidence &fact, const Scope &scope) {
    return SameScope(fact.scope, scope) &&
        (fact.state == FactState::Complete || fact.state == FactState::NotRequired);
}
inline bool GateBlocked(const FactEvidence &fact, const Scope &scope) {
    return SameScope(fact.scope, scope) && fact.state == FactState::Incomplete;
}

inline Verdict Resolve(const Input &input) {
    Verdict v;
    v.scope = input.current;
    const auto &recovery = input.recovery;
    // History is explicitly labelled and never participates in current routing.
    if (recovery.available)
        PreserveDefinitiveFailure(v, recovery.first_failure, recovery.first_failure_definitive,
                                 recovery.scope, true);
    if (!input.current.available) {
        v.reason = "current-scope-unavailable";
        Missing(v, "current-config-feature-mode-scope");
        return v;
    }
    const bool recovery_current = recovery.available && SameScope(recovery.scope, input.current);
    v.recoveryAttempted = recovery_current && recovery.attempted;
    if (recovery_current) {
        if (recovery.state == RecoveryState::Failed && recovery.attempted)
            v.recoveryResult = RecoveryState::Failed;
        else if (recovery.state == RecoveryState::NotNeeded)
            v.recoveryResult = RecoveryState::NotNeeded;
        else if (recovery.state == RecoveryState::Pending || recovery.state == RecoveryState::Succeeded)
            v.recoveryResult = RecoveryState::Pending; // reported success is not proof
    }
    const auto &block = input.first_block;
    if (block.available && block.unresolved) {
        if (!SameScope(block.scope, input.current)) {
            v.reason = "active-block-scope-mismatch";
            Missing(v, "current-first-active-block");
            return v;
        }
        const auto policy = ClassifyReason(block.reason);
        v.stage = policy.stage;
        v.reason = block.reason.empty() ? "active-block-reason-unavailable" : std::string(block.reason);
        v.nextAction = policy.action;
        v.autoRecoverable = policy.action == NextAction::AutoRetry;
        v.evidenceComplete = policy.known && block.definitive;
        PreserveDefinitiveFailure(v, block.reason, block.definitive, block.scope, false);
        if (!policy.known) Missing(v, "recognized-first-active-block-reason");
        else if (policy.stage == Stage::Resource) {
            if (input.resource.available && SameScope(input.resource.scope, input.current))
                ResourceVerdict(v, input.resource, true);
            else {
                v.evidenceComplete = false;
                Missing(v, input.resource.available ? "current-resource-scope" : "resource-evidence");
            }
        } else if (policy.stage == Stage::Observer) Missing(v, "complete-current-handle-lifecycle");
        else if (policy.stage == Stage::Command) Missing(v, "current-command-state-proof");
        else if (policy.stage == Stage::Queue) Missing(v, "current-queue-proof");
        else if (policy.stage == Stage::Waiting) Missing(v, "first-current-carrier-evaluate");
        if (policy.stage != Stage::Disabled && policy.stage != Stage::Ownership)
            QualifyActiveRecovery(v);
        return v;
    }

    const auto &resource = input.resource;
    const bool resource_current = resource.available && SameScope(resource.scope, input.current);
    const bool resource_blocked = resource_current &&
        (!resource.admitted || resource.state != ResourceState::Ready);
    const bool observer_blocked = GateBlocked(input.observer, input.current);
    const bool command_blocked = GateBlocked(input.command, input.current);
    const bool queue_blocked = GateBlocked(input.queue, input.current);
    const bool failed_attempt_block = recovery_current && recovery.attempted &&
        recovery.state == RecoveryState::Failed;
    const unsigned int unordered_blocks = unsigned(resource_blocked) + unsigned(observer_blocked) +
        unsigned(command_blocked) + unsigned(queue_blocked) + unsigned(failed_attempt_block);
    if (unordered_blocks > 1) {
        v.reason = "first-block-order-unobserved";
        v.nextAction = NextAction::NeedsCode;
        Missing(v, "ordered-first-current-unresolved-block");
        QualifyActiveRecovery(v);
        return v;
    }
    if (resource_blocked) {
        ResourceVerdict(v, resource, false);
        PreserveDefinitiveFailure(v, v.reason, resource.definitive, resource.scope, false);
        QualifyActiveRecovery(v);
        return v;
    }
    for (unsigned int i = 0; i != 3; ++i) {
        const FactEvidence &fact = i == 0 ? input.observer : (i == 1 ? input.command : input.queue);
        if (!GateBlocked(fact, input.current)) continue;
        const auto policy = ClassifyReason(fact.reason);
        v.stage = policy.stage;
        v.reason = fact.reason.empty() ? "gate-rejection-reason-unavailable" : std::string(fact.reason);
        v.nextAction = policy.action;
        v.autoRecoverable = policy.action == NextAction::AutoRetry && v.recoveryResult != RecoveryState::Failed;
        if (!v.autoRecoverable && v.nextAction == NextAction::AutoRetry) v.nextAction = NextAction::NeedsCode;
        v.evidenceComplete = policy.known && fact.definitive;
        if (!policy.known) Missing(v, "recognized-current-gate-reason");
        PreserveDefinitiveFailure(v, fact.reason, fact.definitive, fact.scope, false);
        QualifyActiveRecovery(v);
        return v;
    }
    if (recovery_current && recovery.state == RecoveryState::Failed) {
        const auto policy = ClassifyReason(recovery.failure_reason);
        v.stage = policy.stage;
        v.reason = recovery.failure_reason.empty() ? "recovery-attempt-failed" : std::string(recovery.failure_reason);
        v.nextAction = NextAction::NeedsCode;
        v.evidenceComplete = recovery.attempted && recovery.failure_definitive && policy.known;
        if (!recovery.attempted) Missing(v, "recovery-attempt-marker");
        if (!policy.known) Missing(v, "current-attempt-failure-reason");
        PreserveDefinitiveFailure(v, recovery.failure_reason, recovery.failure_definitive,
                                 recovery.scope, false);
        return v;
    }
    if (!resource_current) {
        Pending(v, Stage::Resource, resource.available ? "resource-scope-mismatch" : "resource-evidence-unavailable",
                resource.available ? "current-resource-scope" : "resource-evidence");
        return v;
    }
    if (!resource.definitive) {
        Pending(v, Stage::Resource, "resource-admission-not-definitive", "definitive-resource-contract");
        Missing(v, resource.missing_evidence);
        return v;
    }
    if (!resource.missing_evidence.empty()) {
        v.stage = Stage::Resource;
        v.reason = "resource-evidence-incomplete";
        Missing(v, resource.missing_evidence);
        v.autoRecoverable = resource.next_action == NextAction::AutoRetry;
        v.nextAction = v.autoRecoverable ? NextAction::AutoRetry : NextAction::NeedsCode;
        QualifyActiveRecovery(v);
        return v;
    }
    for (unsigned int i = 0; i != 3; ++i) {
        const FactEvidence &fact = i == 0 ? input.observer : (i == 1 ? input.command : input.queue);
        if (GateReady(fact, input.current)) continue;
        Pending(v, i == 0 ? Stage::Observer : (i == 1 ? Stage::Command : Stage::Queue),
                "current-gate-proof-unavailable", i == 0 ? "current-observer-proof" :
                (i == 1 ? "current-command-state-proof" : "current-queue-proof"));
        return v;
    }
    if (!recovery_current) {
        Pending(v, Stage::Recovery, recovery.available ? "recovery-scope-mismatch" : "recovery-evidence-unavailable",
                recovery.available ? "current-recovery-scope" : "recovery-evidence");
        v.recoveryResult = RecoveryState::Unavailable;
        return v;
    }
    if (recovery.state == RecoveryState::NotNeeded) {
        v.stage = Stage::Complete;
        v.reason = "no-current-compatibility-block";
        v.recoveryResult = RecoveryState::NotNeeded;
        v.evidenceComplete = true;
        v.nextAction = NextAction::None;
        return v;
    }
    if (recovery.state != RecoveryState::Pending && recovery.state != RecoveryState::Succeeded) {
        v.stage = Stage::Recovery;
        v.reason = "recovery-state-unavailable";
        Missing(v, "observed-recovery-state");
        return v;
    }
    if (!input.current.config || !input.current.feature_generation ||
        !(input.current.requested_mode == 1 || input.current.requested_mode == 2) ||
        !(input.current.effective_mode == 1 || input.current.effective_mode == 2)) {
        Pending(v, Stage::Waiting, "execution-scope-incomplete", "active-config-feature-requested-effective-mode");
        return v;
    }
    if (!recovery.attempted || !recovery.attempt_id) {
        Pending(v, Stage::Recovery, "recovery-not-yet-attempted", "current-attempt-id-and-marker");
        return v;
    }
    if (!recovery.correlated_window || recovery.proof_attempt_id != recovery.attempt_id) {
        Pending(v, Stage::Recovery, "recovery-proof-unavailable", "same-attempt-correlated-streak-and-submission-window");
        return v;
    }
    if (!recovery.descriptor_verified) {
        Pending(v, Stage::Resource, "descriptor-verification-pending", "actual-descriptor-verification");
        return v;
    }
    if (recovery.native_success_streak < kRequiredSuccessStreak) {
        Pending(v, Stage::Native, "native-success-streak-pending", "three-consecutive-native-successes");
        return v;
    }
    if (recovery.nr_success_streak < kRequiredSuccessStreak) {
        Pending(v, Stage::Neural, "nr-success-streak-pending", "three-consecutive-NR-successes");
        return v;
    }
    if (recovery.submitted_count < kRequiredSubmissions) {
        Pending(v, Stage::Submission, "matching-submissions-pending", "three-matching-submissions");
        return v;
    }
    if (!recovery.writeback_verified) {
        Pending(v, Stage::Writeback, "writeback-verification-pending", "actual-writeback-verification");
        return v;
    }
    v.recoveryResult = RecoveryState::Succeeded;
    v.autoRecoverable = true;
    v.evidenceComplete = true; // Enough to decide the next action, not visual acceptance.
    if (SameScope(input.visual.scope, input.current) &&
        input.visual.proof_attempt_id == recovery.attempt_id &&
        input.visual.state == FactState::Incomplete) {
        v.stage = Stage::Visual;
        v.reason = input.visual.reason.empty() ? "visual-verification-failed" : std::string(input.visual.reason);
        v.recoveryResult = RecoveryState::Failed;
        v.autoRecoverable = false;
        v.evidenceComplete = input.visual.definitive;
        v.nextAction = NextAction::NeedsCode;
        PreserveDefinitiveFailure(v, v.reason, input.visual.definitive, input.visual.scope, false);
        return v;
    }
    if (SameScope(input.visual.scope, input.current) &&
        input.visual.proof_attempt_id == recovery.attempt_id &&
        input.visual.state == FactState::Complete) {
        v.stage = Stage::Complete;
        v.reason = "recovery-and-visual-verification-complete";
        v.nextAction = NextAction::None;
    } else {
        v.stage = Stage::Visual;
        v.reason = "execution-recovered-final-visual-unverified";
        v.nextAction = NextAction::NeedsVisualF7;
        Missing(v, "final-visual-verification-for-current-attempt");
    }
    return v;
}

} // namespace nr_compatibility_verdict
