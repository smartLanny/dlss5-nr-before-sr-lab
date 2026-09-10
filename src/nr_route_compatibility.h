#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include "nr_resource_recovery.h"
#include "nr_resource_semantics.h"

// Selection evidence only: this bounded current-scope policy never admits a
// resource, owns a GPU object, calls NGX, writes settings or performs a retry.
namespace nr_route {
namespace recovery = nr_resource_recovery;
enum class State { None, PreBlocked, TryingPost, LockedPost, PostBlocked };
struct Scope {
    std::uint64_t device = 0, feature = 0, config = 0, color = 0, output = 0;
    unsigned requested = 0;
    bool lifetime_known = false;
    // Exact values, not a hash: descriptors, native view evidence, regions and
    // live exposure/guide identities. Re-observation call counters are absent.
    std::array<std::uint64_t, 48> contract{};
    explicit operator bool() const noexcept {
        return device && feature && config && color && output && (requested == 1 || requested == 2);
    }
};
inline bool operator==(const Scope& a, const Scope& b) noexcept {
    return a.device == b.device && a.feature == b.feature && a.config == b.config &&
        a.color == b.color && a.output == b.output && a.requested == b.requested &&
        a.lifetime_known == b.lifetime_known && a.contract == b.contract;
}
inline const char* Name(State value) noexcept {
    switch (value) {
    case State::PreBlocked: return "pre-blocked";
    case State::TryingPost: return "trying-post";
    case State::LockedPost: return "locked-post";
    case State::PostBlocked: return "post-blocked";
    default: return "none";
    }
}
inline bool DefinitivePreSemantic(const nr_resource_semantics::Decision& decision) noexcept {
    // A broad bypass label, conflicting/absent view, or a previous model
    // validation failure is not proof that PRE's resource contract is invalid.
    return decision.definitive && decision.verdict == nr_resource_semantics::Verdict::Unsupported &&
        std::string_view(decision.reason) != "prior-interpretation-validation-failed";
}
struct Snapshot {
    Scope scope{};
    State state = State::None;
    std::uint64_t frame = 0, first_frame = 0;
    bool current = false, pre_rejected = false, pre_definitive = false;
    bool post_attempted = false, post_admitted = false, post_recorded = false, post_submitted = false;
    bool post_verified = false, locked = false;
    const char* pre_reason = "none";
    const char* post_reason = "none";
    const char* invalidation = "none";
    recovery::Token token{}, proof_token{};
    recovery::RecoveryEvidence proof{};
};
inline const char* Notice(const Snapshot& s, bool rr, bool chinese) noexcept {
    if (rr) return chinese ?
        "检测到光线重建，已固定为 RR → NR；前置偏好已保留。当前画面效果仍需确认，请按 F8 反馈。" :
        "RR selects RR -> NR automatically; your front preference is retained. The displayed result still needs checking; please save F8.";
    if (!s.current || s.state == State::None) return "";
    if (s.scope.requested != 2 && s.state != State::PostBlocked) return "";
    if (s.post_verified && s.pre_definitive && !s.scope.lifetime_known) return chinese ?
        "后置执行与写回已有验证，但当前资源的生命周期证据不足，尚未锁定兼容路线；请按 F8 反馈。" :
        "Post execution/storage are verified, but current resource lifetime coverage is incomplete. The compatibility route is not locked; please save F8.";
    if (s.locked) return chinese ?
        "当前输入合同已确认不支持前置 NR，已固定兼容后置 SR → NR；原设置已保留。执行与写回已验证，最终画面待确认，请按 F8 反馈。" :
        "The current input contract rejects pre-SR NR. SR -> NR is locked for this scope; your preference is retained. Execution/storage are verified, final visuals are not. Please save F8.";
    if (s.state == State::PostBlocked) {
        const auto reason = std::string_view(s.post_reason);
        const bool unknown_view = reason == "multiple-legal-resource-interpretations" ||
            reason == "native-typed-view-not-observed" || reason == "native-view-lifetime-or-evaluate-invalid";
        if (unknown_view) return chinese ? (s.pre_rejected ?
            "前置未获准，后置 Output 视图类型也尚未确认；当前没有锁定后置。已保留原生 SR，请按 F8 反馈。" :
            "后置 Output 视图类型尚未确认，NR 未获准；已保留原生 SR，请按 F8 反馈。") :
            "The post-SR Output view type is still unknown; post NR is not admitted or automatically locked. Native SR is retained; please save F8.";
        return chinese ?
            "后置处理当前也未获准或未完成，未锁定兼容路线；已保留原生 SR。请按 F8 记录具体原因。" :
            "Post processing is blocked or not applied; no compatibility route is locked. Native SR is retained. Please save F8 for the exact reason.";
    }
    if (s.state == State::TryingPost) return chinese ? (s.pre_definitive ?
        "当前输入合同不支持前置 NR，正在尝试兼容后置；后置尚未完成验证，未锁定路线，请按 F8 反馈。" :
        "前置输入条件尚未明确，正在尝试后置；这不代表前置已确认不兼容或后置已可用，请按 F8 反馈。") :
        "Pre-SR input is blocked; post SR is being tried, not yet verified or locked. Please save F8 for the current input/output evidence.";
    return chinese ? "前置输入条件尚未满足，已保留原生 SR；请按 F8 记录具体原因。" :
        "Pre-SR input is not admitted. Native SR is retained; please save F8 for the exact reason.";
}
class Policy {
    Snapshot value_{};
    bool blocked_ = false;
    static bool SameAttempt(const recovery::Token& a, const recovery::Token& b) noexcept {
        return a && b && a.key == b.key && a.signature == b.signature && a.attempt_id == b.attempt_id;
    }
    bool Matches(const recovery::Token& token) const noexcept {
        const auto& s = value_.scope;
        return s && token && token.key.scope.device == s.device && token.key.scope.device_generation == s.device &&
            token.key.scope.feature == 1 && token.key.scope.feature_generation == s.feature &&
            token.key.scope.config_generation == s.config && token.key.scope.mode == 1 &&
            token.key.role == 1 && token.key.resource == s.output &&
            token.key.resource_generation == s.output && token.key.subresource == 0;
    }
    void UpdateState() noexcept {
        value_.locked = value_.current && value_.scope.lifetime_known && value_.scope.requested == 2 && value_.pre_rejected &&
            value_.pre_definitive && value_.post_verified && !blocked_;
        value_.state = value_.locked ? State::LockedPost :
            blocked_ && value_.post_attempted ? State::PostBlocked :
            value_.post_attempted ? State::TryingPost :
            value_.pre_rejected ? State::PreBlocked : State::None;
    }
    void Revoke(const char* reason) noexcept {
        value_.post_verified = value_.locked = false;
        value_.proof = {};
        value_.invalidation = reason;
        // An older window/readback may not relock a carrier which just failed.
        value_.first_frame = value_.frame == UINT64_MAX ? UINT64_MAX : value_.frame + 1;
    }
public:
    Snapshot Read() const noexcept { return value_; }
    void Begin(const Scope& scope, std::uint64_t frame) noexcept {
        if (!(scope == value_.scope) || !value_.current) {
            value_ = {};
            value_.scope = scope; value_.first_frame = frame;
            value_.invalidation = "scope-changed";
        }
        value_.frame = frame; value_.current = static_cast<bool>(scope) && frame;
        value_.post_attempted = value_.post_admitted = value_.post_recorded = value_.post_submitted = false;
        value_.post_verified = value_.locked = false;
        value_.post_reason = "none"; value_.token = {}; value_.proof = {};
        blocked_ = false;
        UpdateState();
    }
    void PreRejected(bool definitive, const char* reason) noexcept {
        value_.pre_rejected = true; value_.pre_definitive = definitive;
        value_.pre_reason = reason;
        if (!definitive) value_.locked = false;
        UpdateState();
    }
    void PreAdmitted() noexcept {
        value_.pre_rejected = value_.pre_definitive = false;
        value_.pre_reason = "none";
        UpdateState();
    }
    void AttemptPost() noexcept {
        value_.post_attempted = true; value_.post_reason = "pending-post-admission";
        UpdateState();
    }
    void PostReason(const char* reason) noexcept { value_.post_reason = reason; }
    void AdmitPost(const recovery::Token& token) noexcept {
        value_.post_admitted = true; value_.post_reason = "post-admitted";
        value_.token = token;
        if (Matches(token)) value_.proof_token = token;
        else { value_.proof_token = {}; Revoke("post-token-unavailable"); }
        UpdateState();
    }
    void BlockPost(const char* reason) noexcept {
        blocked_ = true; value_.post_reason = reason;
        Revoke(reason); UpdateState();
    }
    void RecordedPost(bool recorded, const char* reason = "post-not-recorded") noexcept {
        value_.post_recorded = recorded;
        if (!recorded) { BlockPost(std::string_view(reason) == "post-admitted" ? "post-not-applied" : reason); return; }
        value_.post_reason = "post-recorded";
        UpdateState();
    }
    void Stage(const recovery::Token& token, recovery::Stage stage, bool success) noexcept {
        if (!Matches(token)) return;
        if (!success && SameAttempt(value_.proof_token, token)) {
            BlockPost("post-execution-failed"); return;
        }
        if (!(value_.token == token)) return;
        if (stage == recovery::Stage::Submitted) value_.post_submitted = success;
        if (stage == recovery::Stage::TransferRecorded) value_.post_recorded = success;
    }
    void Refresh(const recovery::Token& token, const recovery::RecoveryEvidence& proof) noexcept {
        if (!Matches(token) || !SameAttempt(value_.proof_token, token)) return;
        if (proof.state == recovery::State::Latched) { BlockPost("post-recovery-failed"); return; }
        value_.proof = proof;
        const bool exact = proof.attempt_id == token.attempt_id && proof.proof_attempt_id == token.attempt_id &&
            proof.proof_target.resource == value_.scope.output &&
            proof.proof_target.resource_generation == value_.scope.output && proof.proof_target.subresource == 0;
        value_.post_verified = !blocked_ && value_.current && exact &&
            proof.state == recovery::State::RecoverySucceeded && proof.correlated_window &&
            proof.descriptor_verified && proof.writeback_verified && !proof.capacity_unknown &&
            proof.nr_success_streak >= 3 && proof.native_success_streak >= 3 && proof.submitted_count >= 3 &&
            proof.window_first_frame_id >= value_.first_frame && proof.proof_frame_id >= value_.first_frame &&
            proof.window_last_frame_id <= value_.frame && proof.proof_frame_id <= value_.frame;
        UpdateState();
    }
    void End() noexcept {
        if (value_.post_attempted && !value_.post_recorded)
            BlockPost(std::string_view(value_.post_reason) == "post-admitted" ? "post-not-applied" : value_.post_reason);
        else if (!value_.post_attempted) {
            Revoke("no-post-work-this-carrier"); UpdateState();
        }
    }
    void Invalidate(const char* reason) noexcept {
        value_.current = value_.locked = value_.post_verified = false;
        value_.proof_token = {}; value_.proof = {};
        value_.invalidation = reason; value_.state = State::None;
    }
};

// The actual prepare gate is shared by production and the CPU fixture. A lock
// skips only PRE preparation. POST still goes through its complete real gate.
template <class Before, class After, class ReadLock, class PrepareBefore, class CanFallback,
          class Attempt, class PrepareAfter>
bool Prepare(Before& before, After& after, ReadLock&& locked,
             PrepareBefore&& prepare_before, CanFallback&& can_fallback,
             Attempt&& on_attempt, PrepareAfter&& prepare_after) {
    const bool skip_pre = locked();
    if (!skip_pre) before = prepare_before();
    const bool fallback = skip_pre || can_fallback(before);
    if (fallback) on_attempt();
    after = prepare_after(fallback);
    return fallback;
}
} // namespace nr_route
