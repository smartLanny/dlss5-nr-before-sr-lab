#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <string_view>

using ULONGLONG = unsigned long long;
namespace reshade { namespace api { struct effect_runtime; } }

// The runner extracts these definitions verbatim from nr_feedback_capture.inl,
// including its actual report capacity and reserved final-status implementation.
#include "feedback_append_fixture.inl"
#include "nr_compatibility_verdict_feedback.inl"

using namespace nr_compatibility_verdict;
unsigned int checks = 0;
void Check(bool value, const char *message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

using Fields = std::map<std::string, std::string>;
Fields Parse(const FeedbackSession &session) {
    Fields fields;
    std::string_view text(session.report, session.report_used);
    for (std::size_t offset = 0; offset != text.size();) {
        const auto end = text.find('\n', offset);
        Check(end != std::string_view::npos, "every emitted record has a complete line ending");
        const auto split = text.find('=', offset);
        Check(split != std::string_view::npos && split < end, "each emitted line has one stable key");
        Check(fields.emplace(std::string(text.substr(offset, split - offset)),
                             text.substr(split + 1, end - split - 1)).second,
              "input text must not forge or duplicate decision keys");
        offset = end + 1;
    }
    return fields;
}

bool ValidUtf8(std::string_view text) {
    // Independent decoder: reject overlong forms, surrogates and invalid scalar
    // values, so byte-boundary truncation cannot be blessed by the writer helper.
    std::uint32_t scalar = 0, minimum = 0;
    unsigned int remaining = 0;
    for (const unsigned char byte : text) {
        if (!remaining) {
            if (byte < 0x80) continue;
            if ((byte & 0xE0) == 0xC0) { scalar = byte & 0x1F; minimum = 0x80; remaining = 1; }
            else if ((byte & 0xF0) == 0xE0) { scalar = byte & 0x0F; minimum = 0x800; remaining = 2; }
            else if ((byte & 0xF8) == 0xF0) { scalar = byte & 0x07; minimum = 0x10000; remaining = 3; }
            else return false;
        } else {
            if ((byte & 0xC0) != 0x80) return false;
            scalar = (scalar << 6) | (byte & 0x3F);
            if (!--remaining && (scalar < minimum || scalar > 0x10FFFF ||
                                 (scalar >= 0xD800 && scalar <= 0xDFFF))) return false;
        }
    }
    return remaining == 0;
}

void Expect(const Fields &fields, const char *key, const char *value, const char *message) {
    const auto found = fields.find(key);
    Check(found != fields.end() && found->second == value, message);
}

void ExpectDecision(const Fields &fields, const std::string &root, const Input &input) {
    const auto verdict = Resolve(input);
    const auto verify = [&](const char *key, const std::string &value) {
        const auto full = root + '.' + key;
        const auto found = fields.find(full);
        Check(found != fields.end() && found->second == value, full.c_str());
    };
    verify("stage", Name(verdict.stage));
    verify("reason", verdict.reason);
    verify("autoRecoverable", verdict.autoRecoverable ? "1" : "0");
    verify("recoveryAttempted", verdict.recoveryAttempted ? "1" : "0");
    verify("recoveryResult", Name(verdict.recoveryResult));
    verify("firstDefinitiveFailure", verdict.firstDefinitiveFailure.empty() ? "none" : verdict.firstDefinitiveFailure);
    verify("evidenceComplete", verdict.evidenceComplete ? "1" : "0");
    verify("evidenceCompleteMeaning", "enough evidence for the decision, not final visual acceptance");
    verify("missingEvidence", verdict.missingEvidence.empty() ? "none" : verdict.missingEvidence);
    verify("nextAction", Name(verdict.nextAction));
    verify("firstFailureHistorical", verdict.firstFailureHistorical ? "1" : "0");
}

Input Proven() {
    Input input;
    input.current = {true, 7, 41, 2, 2};
    input.resource.available = input.resource.admitted = input.resource.definitive = true;
    input.resource.scope = input.current;
    input.resource.state = ResourceState::Ready;
    input.resource.reason = "admitted";
    input.resource.next_action = NextAction::None;
    input.observer = {FactState::Complete, input.current, {}, true};
    input.command = input.queue = input.observer;
    input.recovery.available = input.recovery.attempted = true;
    input.recovery.scope = input.current;
    input.recovery.state = RecoveryState::Succeeded;
    input.recovery.attempt_id = input.recovery.proof_attempt_id = 17;
    input.recovery.correlated_window = input.recovery.descriptor_verified = true;
    input.recovery.nr_success_streak = input.recovery.native_success_streak = 3;
    input.recovery.submitted_count = 3;
    input.recovery.writeback_verified = true;
    input.recovery.first_failure = "unsupported-input-format";
    input.recovery.first_failure_definitive = true;
    return input;
}

int main() {
    auto unknown_storage = std::make_unique<FeedbackSession>(); auto &unknown = *unknown_storage;
    FeedbackAppendCompatibilityVerdict(unknown, "start", {});
    auto fields = Parse(unknown);
    ExpectDecision(fields, "start.decision", {});
    Expect(fields, "start.decision.scope.config", "unknown", "UNKNOWN_IS_NOT_ZERO");
    Expect(fields, "start.decision.scope.featureGeneration", "unknown", "missing feature generation is explicit");
    Expect(fields, "start.decision.scope.requestedMode", "unknown", "missing requested mode is explicit");
    Expect(fields, "start.decision.recovery.attemptId", "unknown", "missing attempt ID is explicit");
    Expect(fields, "start.decision.recovery.nativeSuccessStreak", "unknown", "missing window is not a zero measurement");
    Expect(fields, "start.decision.recovery.writebackVerified", "unknown", "missing writeback is not a measured false");
    Expect(fields, "start.decision.firstDefinitiveFailure", "none", "empty history uses none");
    Check(fields.find("decision.stage") == fields.end(), "start must not publish the end decision alias");
    Check(!unknown.report_truncated, "ordinary unknown snapshot is fully serialized");

    auto input = Proven();
    auto success_storage = std::make_unique<FeedbackSession>(); auto &success = *success_storage;
    FeedbackAppendCompatibilityVerdict(success, "end", input);
    fields = Parse(success);
    ExpectDecision(fields, "end.decision", input);
    ExpectDecision(fields, "decision", input);
    Expect(fields, "decision.stage", "visual", "execution proof alone cannot claim final visual completion");
    Expect(fields, "decision.nextAction", "needs-visual-F7", "execution proof retains visual next action");
    Expect(fields, "end.decision.recovery.attemptId", "17", "attempt ID is preserved");
    Expect(fields, "end.decision.recovery.proofAttemptId", "17", "proof ID is preserved");
    Expect(fields, "end.decision.recovery.nrSuccessStreak", "3", "matching window is serialized");
    Expect(fields, "decision.firstFailureHistorical", "1", "preserved first failure is labelled historical");
    Expect(fields, "decision.firstFailureScope.config", "7", "historical failure retains its real scope");

    input.visual = {FactState::Complete, input.current, {}, true, 17};
    input.recovery.first_failure = {};
    auto visual_storage = std::make_unique<FeedbackSession>(); auto &visual = *visual_storage;
    FeedbackAppendCompatibilityVerdict(visual, "end", input);
    FinishFeedbackReport(&visual);
    fields = Parse(visual);
    ExpectDecision(fields, "decision", input);
    Expect(fields, "decision.stage", "complete", "same-attempt visual proof is accepted by actual policy");
    Expect(fields, "decision.missingEvidence", "none", "empty missing evidence uses none");
    Expect(fields, "collection.metadataTruncated", "0", "ordinary record retains final report status");
    Expect(fields, "collection.timelineRowsOmitted", "0", "ordinary record retains final timeline status");

    input = Proven();
    input.recovery.scope.config = 6;
    auto stale_storage = std::make_unique<FeedbackSession>(); auto &stale = *stale_storage;
    FeedbackAppendCompatibilityVerdict(stale, "end", input);
    fields = Parse(stale);
    ExpectDecision(fields, "decision", input);
    Expect(fields, "decision.recoveryResult", "unavailable", "STALE_SCOPE_CANNOT_SUCCEED");
    Expect(fields, "decision.scope.config", "7", "current scope is retained independently of history");
    Expect(fields, "decision.firstFailureScope.config", "6", "old failure scope is not rewritten to current");
    Expect(fields, "end.decision.recovery.current", "0", "old proof remains explicitly out of scope");
    Expect(fields, "end.decision.recovery.scope.config", "6", "raw proof retains old scope");

    input = Proven();
    input.recovery.correlated_window = false;
    input.recovery.native_success_streak = 999;
    auto uncorrelated_storage = std::make_unique<FeedbackSession>(); auto &uncorrelated = *uncorrelated_storage;
    FeedbackAppendCompatibilityVerdict(uncorrelated, "end", input);
    fields = Parse(uncorrelated);
    ExpectDecision(fields, "decision", input);
    Expect(fields, "end.decision.recovery.nativeSuccessStreak", "unknown", "uncorrelated counters are not reported as proof");
    Expect(fields, "end.decision.recovery.writebackVerified", "unknown", "uncorrelated writeback is not proof");
    input.recovery.correlated_window = true;
    input.recovery.native_success_streak = 0;
    auto measured_zero_storage = std::make_unique<FeedbackSession>(); auto &measured_zero = *measured_zero_storage;
    FeedbackAppendCompatibilityVerdict(measured_zero, "end", input);
    fields = Parse(measured_zero);
    Expect(fields, "end.decision.recovery.nativeSuccessStreak", "0", "a supplied matching window may contain a real zero");
    Expect(fields, "decision.recoveryResult", "pending", "measured zero does not create recovery success");

    // This is deliberately an unknown reason. The writer must preserve UTF-8
    // without classifying the supplied text or turning it into a successful gate.
    input = {};
    input.current = {true, 19, 77, 1, 1};
    const std::string chinese = u8"未知资源语义：颜色与曝光尚未核验";
    input.first_block = {true, true, input.current, chinese, true};
    auto localized_storage = std::make_unique<FeedbackSession>(); auto &localized = *localized_storage;
    FeedbackAppendCompatibilityVerdict(localized, "end", input);
    fields = Parse(localized);
    ExpectDecision(fields, "decision", input);
    Expect(fields, "decision.reason", chinese.c_str(), "Chinese reason is preserved byte-for-byte");
    Expect(fields, "decision.stage", "unknown", "unknown localized reason is not guessed");
    Expect(fields, "decision.evidenceComplete", "0", "unknown localized reason cannot become complete");
    Check(ValidUtf8({localized.report, localized.report_used}), "localized report is valid UTF-8");

    std::string hostile = "reason\ndecision.evidenceComplete=1\r\n";
    hostile += std::string("\0\xC0\xAF", 3);
    hostile += u8"保留";
    input.first_block.reason = hostile;
    auto escaped_storage = std::make_unique<FeedbackSession>(); auto &escaped = *escaped_storage;
    FeedbackAppendCompatibilityVerdict(escaped, "end", input);
    fields = Parse(escaped);
    Expect(fields, "decision.evidenceComplete", "0", "text cannot inject a forged complete decision");
    Expect(fields, "decision.textEscaped", "1", "control/invalid bytes are explicitly escaped");
    Check(fields.at("decision.reason").find("\\x0A") != std::string::npos &&
          fields.at("decision.reason").find("\\x00") != std::string::npos,
          "embedded newlines and NUL are preserved as printable bytes");
    Check(ValidUtf8({escaped.report, escaped.report_used}), "invalid source bytes cannot invalidate the report");

    std::string oversized;
    for (unsigned int n = 0; n != 5000; ++n) oversized += u8"中文未知原因🙂";
    input.first_block.reason = oversized;
    input.resource.available = true;
    input.resource.scope = input.current;
    input.resource.reason = input.resource.missing_evidence = oversized;
    input.recovery.available = true;
    input.recovery.scope = input.current;
    input.recovery.failure_reason = input.recovery.first_failure = oversized;
    input.recovery.first_failure_definitive = true;
    input.observer = {FactState::Incomplete, input.current, oversized, true};
    input.command = input.queue = input.visual = input.observer;
    auto bounded_storage = std::make_unique<FeedbackSession>(); auto &bounded = *bounded_storage;
    FeedbackAppendCompatibilityVerdict(bounded, "start", input);
    FeedbackAppendCompatibilityVerdict(bounded, "end", input);
    const auto bounded_bytes = bounded.report_used;
    FinishFeedbackReport(&bounded);
    fields = Parse(bounded);
    Expect(fields, "decision.stage", "unknown", "oversized text never creates a known policy stage");
    Expect(fields, "decision.evidenceComplete", "0", "oversized unknown reason cannot become complete");
    Expect(fields, "decision.serializationComplete", "0", "bounded text is explicitly incomplete");
    Expect(fields, "decision.textTruncated", "1", "bounded text marks truncation");
    Expect(fields, "collection.metadataTruncated", "1", "TEXT_TRUNCATION_MARKS_REPORT");
    Check(bounded_bytes < 24576, "even all oversized fields have a bounded start/end report footprint");
    Check(fields.at("decision.reason").find("[truncated]") != std::string::npos,
          "truncated reason carries an inline marker");
    Check(ValidUtf8({bounded.report, bounded.report_used}), "UTF8_TRUNCATION_IS_WHOLE_CODEPOINT");

    // Leave enough room for an ordinary success headline but not a complete
    // snapshot. The actual append must reject the whole decision transaction.
    auto capacity_storage = std::make_unique<FeedbackSession>(); auto &capacity = *capacity_storage;
    const auto filler_size = sizeof(capacity.report) - 256 - 1300;
    std::string filler = "existing=" + std::string(filler_size - 10, 'x') + '\n';
    FeedbackAppend(capacity, "%s", filler.c_str());
    const auto before = capacity.report_used;
    input = Proven();
    FeedbackAppendCompatibilityVerdict(capacity, "end", input);
    FinishFeedbackReport(&capacity);
    fields = Parse(capacity);
    Check(std::string_view(capacity.report, before) == filler, "overflow leaves earlier records unchanged");
    Check(fields.find("decision.recoveryResult") == fields.end() && fields.find("end.decision.stage") == fields.end(),
          "CAPACITY_REJECTS_WHOLE_DECISION");
    Expect(fields, "end.decision.serializationComplete", "0", "overflow exposes an incomplete snapshot marker when room remains");
    Expect(fields, "collection.metadataTruncated", "1", "REPORT_TAIL_SURVIVES_CAPACITY");
    Expect(fields, "collection.timelineRowsOmitted", "0", "overflow retains the entire reserved tail");
    Check(ValidUtf8({capacity.report, capacity.report_used}), "capacity rejection cannot leave split UTF-8");

    auto full_storage = std::make_unique<FeedbackSession>(); auto &full = *full_storage;
    filler = "existing=" + std::string(sizeof(full.report) - 256 - 11, 'y') + '\n';
    FeedbackAppend(full, "%s", filler.c_str());
    FeedbackAppendCompatibilityVerdict(full, "end", input);
    FinishFeedbackReport(&full);
    fields = Parse(full);
    Expect(fields, "collection.metadataTruncated", "1", "reserved final status survives even when fallback cannot fit");
    Check(full.report_used < sizeof(full.report), "the final status retains its NUL terminator");

    auto bad_prefix_storage = std::make_unique<FeedbackSession>(); auto &bad_prefix = *bad_prefix_storage;
    FeedbackAppendCompatibilityVerdict(bad_prefix, "end\ndecision.stage=complete", {});
    fields = Parse(bad_prefix);
    Expect(fields, "snapshot.decision.prefixRejected", "1", "unsafe prefixes are explicitly rejected");
    Check(fields.find("decision.stage") == fields.end(), "invalid prefix cannot publish a main decision alias");
    Expect(fields, "snapshot.decision.serializationComplete", "0", "rejected prefix is not silently called complete");

    std::printf("PASS compatibility verdict feedback: %u checks; report capacity=%zu; all-long start/end=%zu bytes\n",
                checks, sizeof(bounded.report), bounded_bytes);
}
