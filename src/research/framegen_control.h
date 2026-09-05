#pragma once
#include <algorithm>
#include <cstdint>

namespace nr::research::fg {
enum class Backend { Unknown, NativeDlssG, ExternalAda, Fsr, Xe };
enum class Mode { FollowGame, Off, Fixed, Dynamic };
enum class Action { None, Release, Apply, Suspend, Deferred, Rejected };
enum class Reason {
    None, FollowGame, Unavailable, UnknownOwner, UnsafeSession, InvalidCaps,
    ExternalUnverified, PacingUnknown, InputsUnknown, DynamicUnsupported,
    RequestInvalid, RestartRequired, CaptureLocked, UnsafeBoundary,
    Transition, RuntimeInvalid, Limited, ReleaseUnsupported, DisableUnsupported
};
struct Request { Mode mode = Mode::FollowGame; std::uint32_t multiplier = 2, targetFps = 0; };
struct Caps {
    Backend backend = Backend::Unknown;
    std::uint64_t generation = 0;
    std::uint32_t maxGenerated = 0, verifiedMaxMultiplier = 0;
    bool available = false, ownerKnown = false, runtimeValid = false;
    bool pacingValid = false, inputsValid = false, dynamicSupported = false;
    bool externalAbiVerified = false, externalTimingVerified = false;
    bool liveControl = false, releaseSupported = false, disableSupported = false;
};
struct Conditions {
    bool offlineAllowed = false, conflict = false, captureLocked = false;
    bool atSafeBoundary = false, transition = false, controlOwned = false;
};
struct Decision {
    Action action = Action::None;
    Reason reason = Reason::None;
    Mode mode = Mode::FollowGame;
    std::uint32_t multiplier = 0, generated = 0, targetFps = 0;
};
inline Decision Release(const Caps& c, const Conditions& s, Reason why) {
    if (!s.controlOwned) return {Action::None, why};
    if (!c.ownerKnown) return {Action::Rejected, Reason::UnknownOwner};
    if (!c.releaseSupported) return {Action::Rejected, Reason::ReleaseUnsupported};
    if (!s.atSafeBoundary) return {Action::Deferred, Reason::UnsafeBoundary};
    return {Action::Release, why};
}
inline Decision Decide(Request r, const Caps& c, const Conditions& s) {
    // Release ownership, not somebody else's FG. NR master-off must not disable
    // native/external frame generation. FollowGame is explicitly not Off.
    if (r.mode == Mode::FollowGame) return Release(c,s,Reason::FollowGame);
    if (!s.offlineAllowed || s.conflict) return Release(c,s,Reason::UnsafeSession);
    if (!c.ownerKnown) return {Action::Rejected,Reason::UnknownOwner};
    if (!c.available || c.backend == Backend::Unknown || !c.generation)
        return Release(c,s,Reason::Unavailable);
    if (r.mode != Mode::Off && r.mode != Mode::Fixed && r.mode != Mode::Dynamic)
        return {Action::Rejected,Reason::RequestInvalid};
    // Safety suspension precedes F8/UI locks. This controls only an owned path.
    if (!c.runtimeValid || s.transition || !c.pacingValid || !c.inputsValid) {
        const Reason why = !c.runtimeValid ? Reason::RuntimeInvalid : s.transition ?
            Reason::Transition : !c.pacingValid ? Reason::PacingUnknown : Reason::InputsUnknown;
        if (!s.controlOwned) return {Action::Rejected,why};
        if (!c.disableSupported) return Release(c,s,why);
        if (!s.atSafeBoundary) return {Action::Deferred,Reason::UnsafeBoundary};
        return {Action::Suspend,why,Mode::Off};
    }
    if (r.mode == Mode::Off) {
        if (!c.disableSupported) return {Action::Rejected,Reason::DisableUnsupported};
    } else {
        // Bound corrupted observations; this is an implementation ceiling,
        // not a statement about what NVIDIA or future backends support.
        if (!c.maxGenerated || c.maxGenerated > 31 || c.verifiedMaxMultiplier < 2 ||
            c.verifiedMaxMultiplier > 32) return {Action::Rejected,Reason::InvalidCaps};
        if (c.backend == Backend::ExternalAda &&
            (!c.externalAbiVerified || !c.externalTimingVerified))
            return {Action::Rejected,Reason::ExternalUnverified};
        if (r.mode == Mode::Dynamic && (!c.dynamicSupported ||
            (c.backend != Backend::NativeDlssG && c.backend != Backend::ExternalAda)))
            return {Action::Rejected,Reason::DynamicUnsupported};
        if ((r.mode == Mode::Fixed && (r.multiplier < 2 || r.multiplier > 32)) ||
            (r.mode == Mode::Dynamic && r.targetFps > 1000))
            return {Action::Rejected,Reason::RequestInvalid};
    }
    if (s.captureLocked) return {Action::Deferred,Reason::CaptureLocked};
    if (!c.liveControl) return {Action::Deferred,Reason::RestartRequired};
    if (!s.atSafeBoundary) return {Action::Deferred,Reason::UnsafeBoundary};
    if (r.mode == Mode::Off) return {Action::Apply,Reason::None,Mode::Off};
    if (r.mode == Mode::Dynamic)
        return {Action::Apply,Reason::None,Mode::Dynamic,0,0,r.targetFps};
    const auto maximum = std::min(c.maxGenerated + 1, c.verifiedMaxMultiplier);
    const auto actual = std::min(r.multiplier, maximum);
    return {Action::Apply,actual != r.multiplier ? Reason::Limited : Reason::None,
        Mode::Fixed,actual,actual - 1,0};
}
inline bool Same(const Decision& a, const Decision& b) {
    return a.action == b.action && a.mode == b.mode && a.multiplier == b.multiplier &&
        a.generated == b.generated && a.targetFps == b.targetFps;
}
// Single control-thread only. UI requests must be marshalled to that thread.
// No retry storm after failure: retry requires a new request/capability generation
// or an explicit Retry(). Acceptance is NOT proof of actual presentation.
class ChangeGate {
public:
    std::uint64_t Begin(const Decision& d, std::uint64_t request, std::uint64_t caps) {
        if (d.action != Action::Apply && d.action != Action::Release &&
            d.action != Action::Suspend) return 0;
        if (!request || !caps) return 0;
        if (attempted_ && request == request_ && caps == caps_ && Same(d,last_)) return 0;
        request_ = request; caps_ = caps; last_ = d; attempted_ = true;
        accepted_ = observed_ = false; pending_ = ++serial_;
        return pending_;
    }
    bool Acknowledge(std::uint64_t token, bool accepted) {
        if (!token || token != pending_) return false;
        pending_ = 0; accepted_ = accepted; return true;
    }
    bool Observe(std::uint64_t request, std::uint64_t caps, const Decision& actual) {
        observed_ = accepted_ && request == request_ && caps == caps_ && Same(last_,actual);
        return observed_;
    }
    void Invalidate() { pending_ = 0; accepted_ = observed_ = false; }
    void Retry() { Invalidate(); attempted_ = false; }
    bool Accepted() const { return accepted_; }
    bool Observed() const { return observed_; }
private:
    Decision last_{};
    std::uint64_t request_ = 0, caps_ = 0, serial_ = 0, pending_ = 0;
    bool attempted_ = false, accepted_ = false, observed_ = false;
};
} // namespace nr::research::fg
