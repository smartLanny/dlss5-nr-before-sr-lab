// One decision feeds composition constants, completed-sample diagnostics and
// the controls. Admission of a new HDR contract belongs elsewhere; this policy
// deliberately preserves the existing FP16/unit-exposure post-SR exception.
#include "nr_color_amplification.h"

enum class NrColorReason : unsigned int {
    Unknown,
    NonHdr,
    LinearSdr,
    SrgbSdr,
    ModelValueDomain,
    PostSrFp16UnitExposure,
    UnverifiedHdr,
    ExperimentalUnverifiedHdr,
};

enum class NrLightingReason : unsigned int {
    Unknown,
    RequestedOff,
    NrBeforeSr,
    OnlyNrBeforeSr,
    AfterSr,
};

struct NrColorPolicy {
    float requested_color_strength = 0.0f;
    float effective_color_strength = 0.0f;
    bool color_allowed = false;
    float amplification = 1.0f;
    bool amplification_color_limited = false;
    NrColorReason color_reason = NrColorReason::Unknown;
    bool requested_preserve_lighting = false;
    bool effective_preserve_lighting = false;
    NrLightingReason lighting_reason = NrLightingReason::Unknown;
};

NrColorPolicy ResolveNrColorPolicy(int mode, bool hdr_input,
                                   CodecColorMode color_mode,
                                   bool result_is_fp16, bool unity_exposure,
                                   float requested_color_strength,
                                   bool requested_preserve_lighting) {
    NrColorPolicy result = {};
    result.requested_color_strength = requested_color_strength;
    result.requested_preserve_lighting = requested_preserve_lighting;
    if (mode != 1 && mode != 2) return result;
    const bool verified_post_sr_fp16 = mode == 1 && hdr_input &&
        color_mode == CodecColorMode::Automatic && result_is_fp16 &&
        unity_exposure;
    const bool unverified_hdr = hdr_input &&
        (color_mode == CodecColorMode::Automatic ||
         color_mode == CodecColorMode::PqEncodedHdr) &&
        !verified_post_sr_fp16;
    result.color_allowed = !unverified_hdr;
    result.effective_color_strength = result.color_allowed
        ? requested_color_strength : 0.0f;
    result.color_reason = unverified_hdr ? NrColorReason::UnverifiedHdr :
        verified_post_sr_fp16 ? NrColorReason::PostSrFp16UnitExposure :
        color_mode == CodecColorMode::LinearSdr ? NrColorReason::LinearSdr :
        color_mode == CodecColorMode::SrgbEncodedSdr ? NrColorReason::SrgbSdr :
        color_mode == CodecColorMode::ModelEncodedValueDomain
            ? NrColorReason::ModelValueDomain : NrColorReason::NonHdr;
    result.effective_preserve_lighting = mode == 2 && requested_preserve_lighting;
    result.lighting_reason = mode != 2 ? NrLightingReason::OnlyNrBeforeSr :
        requested_preserve_lighting ? NrLightingReason::NrBeforeSr :
                                     NrLightingReason::RequestedOff;
    return result;
}

// Finalize the LightingLock request for the selected composition route. This
// is deliberately independent from HDR colour admission: post-SR local
// lighting matching must not grant colour processing for an unverified input.
void ApplyLightingLockPolicy(NrColorPolicy &policy, int mode, float strength) {
    const bool supported = mode == 1 || mode == 2;
    const bool requested = supported && std::isfinite(strength) && strength > 0.000001f;
    policy.requested_preserve_lighting = requested;
    policy.effective_preserve_lighting = requested;
    policy.lighting_reason = !supported ? NrLightingReason::Unknown :
        !requested ? NrLightingReason::RequestedOff :
        mode == 1 ? NrLightingReason::AfterSr : NrLightingReason::NrBeforeSr;
}

// Called inside DispatchCodec's existing configuration lock before constants
// and diagnostic snapshots are published. The explicit optional argument keeps
// host tests independent; the production call uses the same committed strength
// as TransferStrength. This finalization runs for BOTH verified colour and the
// explicit unverified-HDR opt-in, not just for the latter.
void ApplyRequestedUnverifiedHdrColor(NrColorPolicy &policy, bool allow, float strength,
    float amplification = ActiveTransferStrength(g_config), bool protect_color = true,
    nr_color_amplification::ChromaConsumer consumer = nr_color_amplification::ChromaConsumer::GainCoupled) {
    if (allow && policy.color_reason == NrColorReason::UnverifiedHdr) {
        policy.color_allowed = true;
        policy.effective_color_strength = std::isfinite(strength) ? std::clamp(strength, 0.0f, 2.0f) : 0.0f;
        policy.color_reason = NrColorReason::ExperimentalUnverifiedHdr;
    }
    const auto budget = nr_color_amplification::ResolveForConsumer(
        policy.effective_color_strength, amplification, protect_color, consumer);
    policy.effective_color_strength = policy.color_allowed ? budget.coefficient : 0.0f;
    policy.amplification = budget.amplification;
    policy.amplification_color_limited = policy.color_allowed && budget.limited;
    // Legacy post composition retains its conservative colour MIX reduction.
    // Shared independent consumers keep their own confidence/ratio/gamut caps.
}

const char *NrColorReasonName(NrColorReason reason) {
    switch (reason) {
    case NrColorReason::NonHdr: return "non-hdr-input";
    case NrColorReason::LinearSdr: return "verified-linear-sdr";
    case NrColorReason::SrgbSdr: return "verified-srgb-sdr";
    case NrColorReason::ModelValueDomain: return "bounded-model-value-domain";
    case NrColorReason::PostSrFp16UnitExposure: return "post-sr-fp16-unit-exposure";
    case NrColorReason::ExperimentalUnverifiedHdr: return "experimental-unverified-hdr";
    case NrColorReason::UnverifiedHdr: return "unverified-hdr-luminance-only";
    default: return "unknown-live-contract";
    }
}

const char *NrLightingReasonName(NrLightingReason reason) {
    switch (reason) {
    case NrLightingReason::RequestedOff: return "requested-off";
    case NrLightingReason::NrBeforeSr: return "nr-before-sr-local-match";
    case NrLightingReason::OnlyNrBeforeSr: return "only-nr-before-sr";
    case NrLightingReason::AfterSr: return "post-sr-local-match";
    default: return "unknown-live-contract";
    }
}

enum class NrColorPolicyStatus : unsigned int {
    NoFrame,
    Current,
    Inactive,
    Pending,
    Bypassed,
    ContractChanged,
    Stale,
};

struct NrColorPolicySnapshot {
    NrColorPolicy policy = {};
    NrColorPolicyStatus status = NrColorPolicyStatus::NoFrame;
    unsigned int config_generation = 0;
    unsigned long long feature_generation = 0;
    unsigned long long frame_id = 0;
    unsigned long long recorded_tick_ms = 0;
    int mode = 0;
};

// Fresh means a recent recorded Combine with the same configuration/contract.
// It is not a fence-completed or presented-frame claim. An absent or stale
// sample must never be used to infer a game's current HDR/SDR contract.
NrColorPolicyStatus ColorPolicyStatusFor(
    const NrColorPolicySnapshot &snapshot, unsigned int config_generation,
    unsigned long long feature_generation, int mode, bool active,
    bool bypassed, unsigned long long now_ms) {
    if (!active) return NrColorPolicyStatus::Inactive;
    // A current bypass explains why neither pending nor historical values
    // are effective. Keep the recorded snapshot intact for later recovery.
    if (bypassed) return NrColorPolicyStatus::Bypassed;
    if (snapshot.frame_id == 0) return NrColorPolicyStatus::NoFrame;
    if (snapshot.config_generation != config_generation || snapshot.mode != mode)
        return NrColorPolicyStatus::Pending;
    if (snapshot.feature_generation != feature_generation)
        return NrColorPolicyStatus::ContractChanged;
    if (now_ms < snapshot.recorded_tick_ms ||
        now_ms - snapshot.recorded_tick_ms > 2000)
        return NrColorPolicyStatus::Stale;
    return NrColorPolicyStatus::Current;
}

const char *NrColorPolicyStatusName(NrColorPolicyStatus status) {
    switch (status) {
    case NrColorPolicyStatus::Current: return "recent-recorded-combine";
    case NrColorPolicyStatus::Inactive: return "inactive-or-identity";
    case NrColorPolicyStatus::Pending: return "pending-settings";
    case NrColorPolicyStatus::Bypassed: return "bypassed";
    case NrColorPolicyStatus::ContractChanged: return "contract-changed";
    case NrColorPolicyStatus::Stale: return "no-recent-combine";
    default: return "no-combine-observed";
    }
}

const char *NrColorPolicyStatusText(NrColorPolicyStatus status, bool chinese) {
    switch (status) {
    case NrColorPolicyStatus::Inactive:
        return chinese ? "NR 当前未启用，AI 色彩暂不处理；设置已保留。" :
            "NR composition is inactive; effective values are unknown. Requests are retained.";
    case NrColorPolicyStatus::Pending:
        return chinese ? "正在应用设置，请等待 AI 重新开始处理画面。" :
            "Settings pending; waiting for a composition frame with this configuration.";
    case NrColorPolicyStatus::Bypassed:
        return chinese ? "NR 当前未参与画面处理，AI 色彩暂不生效；设置已保留。" :
            "The current frame is bypassed; effective values are unknown. Requests are retained.";
    case NrColorPolicyStatus::ContractChanged:
        return chinese ? "游戏刚切换了画面模式，正在等待 AI 恢复处理。" :
            "The game input contract changed; waiting for a new composition frame.";
    default:
        return chinese ? "暂无最近的 AI 画面处理记录；设置已保留。" :
            "No current composition contract; effective values are unknown. Requests are retained.";
    }
}

// Reads and writes use g_chain_cs, including bundle collection. Retain the
// recorded values when status is not Current so exports can identify history.
NrColorPolicySnapshot g_recorded_color_policy = {};

int EffectivePictureMode(int requested_mode) {
    return IsRayReconstructionRouteObserved() ||
        (requested_mode==2 && IsCompatibilityPostObserved()) ? 1 : requested_mode;
}

NrColorPolicySnapshot SnapshotColorPolicy() {
    EnterCriticalSection(&g_chain_cs);
    NrColorPolicySnapshot snapshot = g_recorded_color_policy;
    snapshot.status = ColorPolicyStatusFor(snapshot,
        g_config_generation.load(std::memory_order_relaxed),
        g_pipeline_stats_generation.load(std::memory_order_relaxed),
        EffectivePictureMode(g_config.mode), g_config.enabled && !g_config.identity_use_original &&
            ActiveTransferStrength(g_config) > 0.001f,
        g_last_bypass_reason.load(std::memory_order_relaxed) !=
            static_cast<int>(BypassReason::None), GetTickCount64());
    LeaveCriticalSection(&g_chain_cs);
    return snapshot;
}

struct CompatibilityFallbackSnapshot {
    bool recent = false;
    bool recorded = false;
    int requested_mode = 2, effective_mode = 2;
    unsigned cause = 0;
    BypassReason failure = BypassReason::None;
    unsigned long long attempts = 0, recordings = 0;
};
CompatibilityFallbackSnapshot SnapshotCompatibilityFallback() {
    // The producer publishes under this same recursive chain lock. Do not
    // join a previous request's reason with a newer route/configuration.
    EnterCriticalSection(&g_chain_cs);
    CompatibilityFallbackSnapshot out;
    out.requested_mode = g_config.mode;
    out.effective_mode = EffectivePictureMode(g_config.mode);
    out.attempts = g_compat_fallback_attempts.load(std::memory_order_relaxed);
    out.recordings = g_compat_fallback_recorded.load(std::memory_order_relaxed);
    out.recent = g_config.enabled && g_config.mode == 2 &&
        !g_config.identity_use_original && ActiveTransferStrength(g_config) > 0.001f &&
        !IsRayReconstructionRouteObserved() && IsCompatibilityPostObserved();
    if (out.recent) {
        out.cause = g_compat_fallback_cause.load(std::memory_order_relaxed);
        out.failure = static_cast<BypassReason>(g_last_bypass_reason.load(std::memory_order_relaxed));
        const auto colour = SnapshotColorPolicy();
        out.recorded = colour.status == NrColorPolicyStatus::Current && colour.mode == 1;
    }
    LeaveCriticalSection(&g_chain_cs);
    return out;
}
