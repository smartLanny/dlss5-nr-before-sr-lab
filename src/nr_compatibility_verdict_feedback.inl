#pragma once

#include "nr_compatibility_verdict.h"

// Include after FeedbackSession and FeedbackAppend. This writer only serializes
// the supplied snapshot; it cannot sample runtime state or create recovery proof.
namespace nr_compatibility_feedback {

inline constexpr std::size_t kTextBytes = 256;
inline constexpr std::size_t kPrefixBytes = 32;

struct Record {
    std::string text;
    bool text_truncated = false;
    bool text_escaped = false;

    static std::size_t Utf8Width(std::string_view value, std::size_t offset) {
        const auto first = static_cast<unsigned char>(value[offset]);
        if (first < 0x80) return 1;
        const std::size_t width = first >= 0xC2 && first <= 0xDF ? 2 :
            (first >= 0xE0 && first <= 0xEF ? 3 : (first >= 0xF0 && first <= 0xF4 ? 4 : 0));
        if (!width || width > value.size() - offset) return 0;
        for (std::size_t n = 1; n < width; ++n) {
            const auto byte = static_cast<unsigned char>(value[offset + n]);
            if (byte < 0x80 || byte > 0xBF) return 0;
        }
        const auto second = static_cast<unsigned char>(value[offset + 1]);
        if ((first == 0xE0 && second < 0xA0) || (first == 0xED && second >= 0xA0) ||
            (first == 0xF0 && second < 0x90) || (first == 0xF4 && second >= 0x90)) return 0;
        return width;
    }

    std::string Encode(std::string_view value) {
        if (value.empty()) return "none";
        std::string result;
        result.reserve(kTextBytes + 16);
        for (std::size_t offset = 0; offset < value.size();) {
            const auto byte = static_cast<unsigned char>(value[offset]);
            const std::size_t width = Utf8Width(value, offset);
            const bool escaped = !width || byte < 0x20 || byte == 0x7F || byte == '\\';
            const std::size_t needed = escaped ? 4 : width;
            if (needed > kTextBytes - result.size()) {
                result += "[truncated]";
                text_truncated = true;
                break;
            }
            if (escaped) {
                // Escaping bytes also prevents input newlines or NULs from
                // forging report keys. Invalid UTF-8 is preserved as ASCII hex.
                constexpr char hex[] = "0123456789ABCDEF";
                result += "\\x";
                result += hex[byte >> 4];
                result += hex[byte & 15];
                text_escaped = true;
                ++offset;
            } else {
                result.append(value.data() + offset, width);
                offset += width;
            }
        }
        return result;
    }

    void Field(const std::string &root, const char *key, std::string_view value) {
        text += root;
        text += '.';
        text += key;
        text += '=';
        text += Encode(value);
        text += '\n';
    }
    void Boolean(const std::string &root, const char *key, bool value, bool known = true) {
        Field(root, key, known ? (value ? "1" : "0") : "unknown");
    }
    void Number(const std::string &root, const char *key, std::uint64_t value, bool known) {
        Field(root, key, known ? std::to_string(value) : "unknown");
    }
    void Scope(const std::string &root, const nr_compatibility_verdict::Scope &scope) {
        Boolean(root, "available", scope.available);
        // Zero identity/route IDs are absent sentinels in the policy contract.
        Number(root, "config", scope.config, scope.available && scope.config != 0);
        Number(root, "featureGeneration", scope.feature_generation,
               scope.available && scope.feature_generation != 0);
        Number(root, "requestedMode", scope.requested_mode,
               scope.available && scope.requested_mode != 0);
        Number(root, "effectiveMode", scope.effective_mode,
               scope.available && scope.effective_mode != 0);
    }
};

inline const char *ResourceName(nr_compatibility_verdict::ResourceState state) {
    using nr_compatibility_verdict::ResourceState;
    switch (state) {
    case ResourceState::Ready: return "ready";
    case ResourceState::UnknownSemantic: return "unknown-semantic";
    case ResourceState::QueryNotAttempted: return "query-not-attempted";
    case ResourceState::QueryFailed: return "query-failed";
    case ResourceState::Rejected: return "rejected";
    case ResourceState::Unspecified: return "unknown";
    }
    return "unknown";
}

inline const char *FactName(nr_compatibility_verdict::FactState state) {
    using nr_compatibility_verdict::FactState;
    switch (state) {
    case FactState::Unobserved: return "unobserved";
    case FactState::NotRequired: return "not-required";
    case FactState::Complete: return "complete";
    case FactState::Incomplete: return "incomplete";
    }
    return "unknown";
}

inline void Decision(Record &record, const std::string &root,
                     const nr_compatibility_verdict::Verdict &verdict) {
    using nr_compatibility_verdict::Name;
    record.Field(root, "schema", "1");
    record.Field(root, "title", "Compatibility Verdict");
    record.Field(root, "stage", Name(verdict.stage));
    record.Field(root, "reason", verdict.reason);
    record.Boolean(root, "autoRecoverable", verdict.autoRecoverable);
    record.Boolean(root, "recoveryAttempted", verdict.recoveryAttempted);
    record.Field(root, "recoveryResult", Name(verdict.recoveryResult));
    record.Field(root, "firstDefinitiveFailure", verdict.firstDefinitiveFailure);
    record.Boolean(root, "evidenceComplete", verdict.evidenceComplete);
    record.Field(root, "evidenceCompleteMeaning", "enough evidence for the decision, not final visual acceptance");
    record.Field(root, "missingEvidence", verdict.missingEvidence);
    record.Field(root, "nextAction", Name(verdict.nextAction));
    record.Scope(root + ".scope", verdict.scope);
    record.Boolean(root, "firstFailureHistorical", verdict.firstFailureHistorical);
    record.Scope(root + ".firstFailureScope", verdict.firstFailureScope);
}

inline void Fact(Record &record, const std::string &root,
                 const nr_compatibility_verdict::FactEvidence &fact,
                 const nr_compatibility_verdict::Scope &current, bool visual = false) {
    const bool observed = fact.state != nr_compatibility_verdict::FactState::Unobserved;
    record.Field(root, "state", FactName(fact.state));
    record.Boolean(root, "current", nr_compatibility_verdict::SameScope(fact.scope, current));
    record.Scope(root + ".scope", fact.scope);
    record.Field(root, "reason", observed ? fact.reason : "unknown");
    record.Boolean(root, "definitive", fact.definitive, observed);
    if (visual) record.Number(root, "proofAttemptId", fact.proof_attempt_id,
                              observed && fact.proof_attempt_id != 0);
}

inline void Evidence(Record &record, const std::string &root,
                     const nr_compatibility_verdict::Input &input) {
    using nr_compatibility_verdict::Name;
    using nr_compatibility_verdict::SameScope;
    const auto &block = input.first_block;
    const auto first = root + ".firstBlock";
    record.Boolean(first, "available", block.available);
    record.Boolean(first, "unresolved", block.unresolved, block.available);
    record.Boolean(first, "definitive", block.definitive, block.available);
    record.Field(first, "reason", block.available ? block.reason : "unknown");
    record.Scope(first + ".scope", block.scope);

    const auto &resource = input.resource;
    const auto res = root + ".resource";
    record.Boolean(res, "available", resource.available);
    record.Boolean(res, "current", resource.available && SameScope(resource.scope, input.current));
    record.Scope(res + ".scope", resource.scope);
    record.Field(res, "state", resource.available ? ResourceName(resource.state) : "unknown");
    record.Boolean(res, "admitted", resource.admitted, resource.available);
    record.Boolean(res, "definitive", resource.definitive, resource.available);
    record.Field(res, "reason", resource.available ? resource.reason : "unknown");
    record.Field(res, "missingEvidence", resource.available ? resource.missing_evidence : "unknown");
    record.Field(res, "nextAction", resource.available ? Name(resource.next_action) : "unknown");

    const auto &recovery = input.recovery;
    const auto rec = root + ".recovery";
    const bool attempt_known = recovery.available && recovery.attempted && recovery.attempt_id != 0;
    const bool window_known = attempt_known && recovery.correlated_window &&
        recovery.proof_attempt_id == recovery.attempt_id;
    record.Boolean(rec, "available", recovery.available);
    record.Boolean(rec, "current", recovery.available && SameScope(recovery.scope, input.current));
    record.Scope(rec + ".scope", recovery.scope);
    record.Boolean(rec, "attempted", recovery.attempted, recovery.available);
    record.Field(rec, "reportedResult", recovery.available ? Name(recovery.state) : "unknown");
    record.Number(rec, "attemptId", recovery.attempt_id, recovery.available && recovery.attempt_id != 0);
    record.Number(rec, "proofAttemptId", recovery.proof_attempt_id,
                  recovery.available && recovery.proof_attempt_id != 0);
    record.Boolean(rec, "correlatedWindow", recovery.correlated_window, attempt_known);
    record.Boolean(rec, "proofWindowAvailable", window_known);
    record.Boolean(rec, "descriptorVerified", recovery.descriptor_verified, attempt_known);
    record.Number(rec, "nrSuccessStreak", recovery.nr_success_streak, window_known);
    record.Number(rec, "nativeSuccessStreak", recovery.native_success_streak, window_known);
    record.Number(rec, "submittedCount", recovery.submitted_count, window_known);
    record.Boolean(rec, "writebackVerified", recovery.writeback_verified, window_known);
    record.Field(rec, "failureReason", recovery.available ? recovery.failure_reason : "unknown");
    record.Boolean(rec, "failureDefinitive", recovery.failure_definitive, recovery.available);
    record.Field(rec, "firstFailure", recovery.available ? recovery.first_failure : "unknown");
    record.Boolean(rec, "firstFailureDefinitive", recovery.first_failure_definitive, recovery.available);

    Fact(record, root + ".observer", input.observer, input.current);
    Fact(record, root + ".command", input.command, input.current);
    Fact(record, root + ".queue", input.queue, input.current);
    Fact(record, root + ".visual", input.visual, input.current, true);
}

inline std::string Prefix(const char *prefix, bool &rejected) {
    if (prefix != nullptr) {
        std::size_t length = 0;
        for (; length <= kPrefixBytes && prefix[length] != 0; ++length) {
            const char ch = prefix[length];
            if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                  (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.')) break;
        }
        if (length && length <= kPrefixBytes && prefix[length] == 0)
            return std::string(prefix, length);
    }
    rejected = true;
    return "snapshot";
}

} // namespace nr_compatibility_feedback

void FeedbackAppendCompatibilityVerdict(FeedbackSession &session, const char *prefix,
                                       const nr_compatibility_verdict::Input &input) {
    using namespace nr_compatibility_feedback;
    const auto verdict = nr_compatibility_verdict::Resolve(input);
    bool prefix_rejected = false;
    const auto label = Prefix(prefix, prefix_rejected);
    const auto root = label + ".decision";
    Record record;
    record.text.reserve(12288);
    Decision(record, root, verdict);
    Evidence(record, root, input);
    const bool main_decision = label == "end" && !prefix_rejected;
    if (main_decision) Decision(record, "decision", verdict);
    const bool complete = !record.text_truncated && !prefix_rejected;
    record.Boolean(root, "prefixRejected", prefix_rejected);
    record.Boolean(root, "textEscaped", record.text_escaped);
    record.Boolean(root, "textTruncated", record.text_truncated);
    record.Boolean(root, "serializationComplete", complete);
    if (main_decision) {
        record.Boolean("decision", "textEscaped", record.text_escaped);
        record.Boolean("decision", "textTruncated", record.text_truncated);
        record.Boolean("decision", "serializationComplete", complete);
    }
    if (!complete) session.report_truncated = true;
    const auto before = session.report_used;
    // One transaction through the existing append contract: even a full report
    // cannot retain a success headline while dropping this snapshot's proof.
    FeedbackAppend(session, "%s", record.text.c_str());
    if (session.report_used == before) {
        FeedbackAppend(session, "%s.serializationComplete=0\n%s.serializationFailure=report-capacity\n",
                       root.c_str(), root.c_str());
    }
}
