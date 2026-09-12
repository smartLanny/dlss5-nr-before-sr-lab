#pragma once

// Only PrepareBeforeSr's existing, pre-recording Color admission exits may
// publish a cause. A general bypass, failed NR evaluation or stale status is
// never an authorization to run a second NR pass.
namespace nr_pre_fallback {
enum class Cause : unsigned {
    None = 0,
    InputStorage = 1,
    InputEncoding = 2,
    R8Exposure = 3,
    PqExposure = 4,
    ShaderLoad = 5,
};
inline bool CanAttempt(bool marked, bool pre_active, Cause cause) {
    if (!marked || pre_active) return false;
    switch (cause) {
    case Cause::InputStorage:
    case Cause::InputEncoding:
    case Cause::R8Exposure:
    case Cause::PqExposure:
    case Cause::ShaderLoad: return true;
    default: return false;
    }
}
} // namespace nr_pre_fallback
