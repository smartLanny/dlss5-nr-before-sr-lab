#pragma once
// Original integration helper for smartLanny/dlss5-nr-before-sr-lab, #113.
// No NGX ABI, engine frame-ID heuristic, GPU ownership, or SR call is invented here.
#include <cstdint>
#include <limits>
#include <numeric>

namespace nr_dual {
struct Extent {
    std::uint32_t width = 0, height = 0;
};
constexpr bool operator==(Extent a, Extent b) noexcept {
    return a.width == b.width && a.height == b.height;
}
constexpr bool operator!=(Extent a, Extent b) noexcept { return !(a == b); }

// Persist the exact historical rational, including 2/3, until manually edited.
struct Scale {
    std::uint32_t numerator = 1, denominator = 1;
};
inline bool ValidScale(Scale s) noexcept {
    return s.denominator != 0 && s.denominator <= 10000 &&
           s.numerator != 0 && s.numerator <= s.denominator &&
           std::uint64_t(s.numerator) * 4 >= s.denominator;
}
inline Scale Reduced(Scale s) noexcept {
    if (!ValidScale(s)) return {0, 0};
    const auto divisor = std::gcd(s.numerator, s.denominator);
    return {s.numerator / divisor, s.denominator / divisor};
}
inline bool SameScale(Scale a, Scale b) noexcept {
    return ValidScale(a) && ValidScale(b) &&
           std::uint64_t(a.numerator) * b.denominator ==
           std::uint64_t(b.numerator) * a.denominator;
}
struct Settings {
    std::uint32_t passes = 1; // Missing INI keys retain the existing single pass.
    // NR2 remains opt-in; when enabled, a missing/reset precision is 50%.
    // Keep this as a rational so exact 1/2, 2/3, etc. survive persistence.
    Scale secondary_scale{1, 2};
};
inline bool ValidSettings(const Settings& s) noexcept {
    return (s.passes == 1 || s.passes == 2) && ValidScale(s.secondary_scale);
}

// These are supplied by the Core, NOT discovered Feature18 capabilities.
// Max 16384 is this helper's D3D12 texture bound. NGX may impose tighter limits.
struct RasterRules {
    Extent minimum{1, 1};
    Extent maximum{16384, 16384};
    Extent alignment{1, 1};
};
enum class PlanError { None, InvalidSettings, InvalidRules, InvalidPrimary, UnsupportedSecondary };
enum class Path { Single, DirectSecond, MatchedResidual };
struct Plan {
    PlanError error = PlanError::InvalidSettings;
    Path path = Path::Single;
    Extent primary{}, secondary{};
    Scale requested{};
    // Actual X/Y ratios may differ slightly after legal-size rounding.
    double effective_x = 0.0, effective_y = 0.0;
    bool rounded = false;
    explicit operator bool() const noexcept { return error == PlanError::None; }
};
inline bool ValidRules(const RasterRules& r) noexcept {
    return r.minimum.width && r.minimum.height && r.alignment.width && r.alignment.height &&
           r.maximum.width >= r.minimum.width && r.maximum.height >= r.minimum.height &&
           r.maximum.width <= 16384 && r.maximum.height <= 16384;
}
inline bool InRange(Extent e, const RasterRules& r) noexcept {
    return e.width >= r.minimum.width && e.height >= r.minimum.height &&
           e.width <= r.maximum.width && e.height <= r.maximum.height;
}
inline std::uint32_t ScaledAlignedUp(std::uint32_t n, Scale s, std::uint32_t alignment) noexcept {
    const auto product = std::uint64_t(n) * s.numerator;
    const auto divisor = std::uint64_t(s.denominator) * alignment;
    const auto result = ((product + divisor - 1) / divisor) * alignment;
    return result <= std::numeric_limits<std::uint32_t>::max() ? std::uint32_t(result) : 0;
}
inline Plan MakePlan(const Settings& settings, Extent primary, const RasterRules& rules) noexcept {
    Plan p; p.primary = primary; p.requested = settings.secondary_scale;
    if (!ValidSettings(settings)) return p;
    if (!ValidRules(rules)) { p.error = PlanError::InvalidRules; return p; }
    if (!InRange(primary, rules)) { p.error = PlanError::InvalidPrimary; return p; }
    if (settings.passes == 1) { p.error = PlanError::None; return p; }
    p.secondary = SameScale(settings.secondary_scale, {1, 1}) ? primary : Extent{
        ScaledAlignedUp(primary.width, settings.secondary_scale, rules.alignment.width),
        ScaledAlignedUp(primary.height, settings.secondary_scale, rules.alignment.height)};
    if (!InRange(p.secondary, rules) || p.secondary.width > primary.width ||
        p.secondary.height > primary.height ||
        std::uint64_t(p.secondary.width) * 4 < primary.width ||
        std::uint64_t(p.secondary.height) * 4 < primary.height) {
        p.error = PlanError::UnsupportedSecondary; return p;
    }
    // 100% is the actual primary raster, never aligned down behind the user's back.
    p.path = p.secondary == primary ? Path::DirectSecond : Path::MatchedResidual;
    p.effective_x = double(p.secondary.width) / primary.width;
    p.effective_y = double(p.secondary.height) / primary.height;
    p.rounded = std::uint64_t(p.secondary.width) * settings.secondary_scale.denominator !=
                    std::uint64_t(primary.width) * settings.secondary_scale.numerator ||
                std::uint64_t(p.secondary.height) * settings.secondary_scale.denominator !=
                    std::uint64_t(primary.height) * settings.secondary_scale.numerator;
    p.error = PlanError::None;
    return p;
}

// UI-only transaction. Slider events edit a draft; Apply/mouse-release commits once.
// Keep this within the Core's existing config lock/transaction, not as a second INI writer.
struct ManualSettings {
    Settings applied{}, draft{};
    std::uint64_t revision = 0;
    bool Commit() noexcept {
        if (!ValidSettings(draft)) return false;
        draft.secondary_scale = Reduced(draft.secondary_scale);
        const bool changed = applied.passes != draft.passes ||
                             !SameScale(applied.secondary_scale, draft.secondary_scale);
        if (!changed) return true;
        if (revision == std::numeric_limits<std::uint64_t>::max()) return false;
        applied = draft; ++revision; return true;
    }
    void RevertDraft() noexcept { draft = applied; }
};

// Key for an independent secondary feature. Core must also include actual device,
// carrier generation, view ownership, format, guide-storage and model-tuning contract.
// Never use the old (carrier, mode, dimensions) lookup for both layers!
struct LayerKey {
    std::uint32_t layer = 1;
    std::uint64_t primary_instance = 0;
    std::uint64_t primary_input_epoch = 0;
    std::uint64_t secondary_generation = 0;
    Extent size{};
};
inline bool operator==(const LayerKey& a, const LayerKey& b) noexcept {
    return a.layer == b.layer && a.primary_instance == b.primary_instance &&
        a.primary_input_epoch == b.primary_input_epoch &&
        a.secondary_generation == b.secondary_generation && a.size == b.size;
}

// Tracks successful parent/child Evaluate continuity, not Present or FG frame identity.
// A skipped second Evaluate forces its next use to Reset; no old answer is held.
class AuxHistory {
public:
    bool NeedsReset(const LayerKey& key, std::uint64_t parent_evaluation,
                    bool upstream_reset) const noexcept {
        return upstream_reset || !valid_ || !(key == key_) || parent_evaluation == 0 ||
            last_parent_evaluation_ == std::numeric_limits<std::uint64_t>::max() ||
            parent_evaluation != last_parent_evaluation_ + 1;
    }
    bool CommitSuccess(const LayerKey& key, std::uint64_t parent_evaluation) noexcept {
        if ((key.layer < 1 || key.layer > 4) || !key.primary_instance || !key.secondary_generation ||
            !key.size.width || !key.size.height || !parent_evaluation) return false;
        key_ = key; last_parent_evaluation_ = parent_evaluation; valid_ = true; return true;
    }
    void Invalidate() noexcept { valid_ = false; }
private:
    LayerKey key_{};
    std::uint64_t last_parent_evaluation_ = 0;
    bool valid_ = false;
};

enum class ModelDomain : std::uint32_t {
    DirectValue = 0,  // Existing V2 (Passthrough & 1) path, not a guessed gamma.
    SrgbProxy = 1     // Decode sRGB proxy, operate on display-linear edit, re-encode.
};

// Factual receipts supplied by the Core after real checks. These fields do not
// manufacture GPU completion, prove guide phase, or identify a game frame.
struct Admission {
    const void* primary_feature = nullptr;
    const void* secondary_feature = nullptr;
    const void* primary_params = nullptr;
    const void* secondary_params = nullptr;
    Extent secondary_create_extent{};
    bool creation_completion_proven = false;
    bool recording_and_queue_admitted = false;
    bool dependencies_pinned = false;
    bool current_inputs_and_guides_bound = false;
};
inline bool Admitted(const Plan& plan, const Admission& a) noexcept {
    return bool(plan) && plan.path != Path::Single && a.primary_feature && a.secondary_feature &&
        a.primary_feature != a.secondary_feature && a.primary_params && a.secondary_params &&
        a.primary_params != a.secondary_params && a.secondary_create_extent == plan.secondary &&
        a.creation_completion_proven && a.recording_and_queue_admitted && a.dependencies_pinned &&
        a.current_inputs_and_guides_bound;
}
enum class Selection { Primary, DirectSecond, FusedResidual, NoneAfterOpaqueFailure };
struct Attempt {
    Selection selection = Selection::Primary;
    bool gpu_work_recorded = false;
    std::uint32_t ngx_result = 0;
    bool HasSecond() const noexcept {
        return selection == Selection::DirectSecond || selection == Selection::FusedResidual;
    }
};

// Backend methods must not throw/catch/retry opaque NGX failures. Recording order is
// shared by the D3D12 implementation and the executable fake-backend tests.
// ValidateNoCommands() and all admission checks happen before the first GPU write.
template<class Backend>
Attempt RecordPreparedSecond(const Plan& plan, const Admission& admission, Backend& backend) {
    if (backend.PreviouslyPoisoned())
        return {Selection::NoneAfterOpaqueFailure, false, backend.PriorFailureResult()};
    if (!Admitted(plan, admission) || !backend.ValidateNoCommands(plan)) return {};
    if (plan.path == Path::MatchedResidual) backend.RecordDownsample();
    backend.PrepareOutputForEvaluate();
    const std::uint32_t r = backend.EvaluateOnce();
    if (r != 1u) {
        backend.PoisonWithoutFurtherGpuCommands(r);
        return {Selection::NoneAfterOpaqueFailure, true, r};
    }
    backend.MakeOutputReadable();
    return {plan.path == Path::DirectSecond ? Selection::DirectSecond : Selection::FusedResidual,
            true, r};
}
} // namespace nr_dual
