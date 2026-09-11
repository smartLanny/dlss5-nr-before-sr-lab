#pragma once
// Project-owned policy. No SDK, GPU, game names, or guessed resource semantics.
#include <cstdint>
#include <limits>

namespace nr::fullres {
enum class Backend : std::uint32_t { amd = 1, nvidia = 2 };
enum class MotionSource { unavailable, native, amd, nvidia };
struct Extent {
    std::uint32_t width = 0, height = 0;
    constexpr bool valid() const noexcept { return width != 0 && height != 0; }
    constexpr bool operator==(Extent b) const noexcept {
        return width == b.width && height == b.height;
    }
};
struct Settings {
    bool enabled = false;                  // Experiment is never auto-enabled.
    Backend fallback = Backend::amd;        // Owner-requested default.
    bool force_synthetic_diagnostic = false;
};
constexpr bool valid_backend(Backend b) noexcept {
    return b == Backend::amd || b == Backend::nvidia;
}
// A block-vector grid is NOT a resized input. Extent remains exact, including
// non-multiple-of-8 sizes. The SDK may maintain its own internal pyramid.
constexpr std::uint32_t grid_extent(std::uint32_t n, std::uint32_t grid) noexcept {
    return grid ? n / grid + (n % grid != 0) : 0;
}
constexpr bool completed(std::uint64_t observed, std::uint64_t target) noexcept {
    return target != 0 && observed != std::numeric_limits<std::uint64_t>::max()
        && observed >= target;
}
struct Frame {
    std::uint64_t stream = 0, epoch = 0, sequence = 0;
    Extent extent{};
    bool reset = false;
    constexpr bool valid() const noexcept {
        return stream && epoch && sequence && extent.valid();
    }
};
struct NativeMotion {
    bool available = false, current_call = false, lifetime_known = false;
    bool convention_known = false, region_known = false;
    Frame frame{};
    std::uint64_t reference_sequence = 0;
};
constexpr bool native_admissible(const NativeMotion &m, const Frame &f,
                                std::uint64_t reference) noexcept {
    return f.valid() && m.available && m.current_call && m.lifetime_known &&
        m.convention_known && m.region_known && m.frame.stream == f.stream &&
        m.frame.epoch == f.epoch && m.frame.sequence == f.sequence &&
        m.frame.extent == f.extent && m.frame.reset == f.reset &&
        (f.reset ? reference == 0 : reference != 0) &&
        m.reference_sequence == reference;
}
constexpr MotionSource select_motion(const Settings &s, const Frame &f,
                                     const NativeMotion &n,
                                     std::uint64_t reference) noexcept {
    // Synthetic being disabled must not disable the existing native route.
    if ((!s.enabled || !s.force_synthetic_diagnostic) && native_admissible(n, f, reference))
        return MotionSource::native;
    if (!s.enabled || !f.valid() || !valid_backend(s.fallback))
        return MotionSource::unavailable;
    return s.fallback == Backend::amd ? MotionSource::amd : MotionSource::nvidia;
}
struct Pair { bool accepted = false, reset = true; std::uint64_t reference = 0; };
class History {
    Frame last_{};
    Backend backend_ = Backend::amd;
public:
    Pair plan(const Frame &f, Backend backend) const noexcept {
        if (!f.valid() || !valid_backend(backend)) return {};
        const bool same = last_.valid() && last_.stream == f.stream &&
            last_.epoch == f.epoch && last_.extent == f.extent && backend_ == backend;
        // Duplicate and out-of-order captures are not new temporal input.
        if (same && f.sequence <= last_.sequence) return {};
        const bool reset = f.reset || !same;
        return {true, reset, reset ? 0 : last_.sequence};
    }
    // Call only after an actual completion signal was successfully published.
    // A dropped capture does not become the next optical-flow reference.
    bool commit(const Frame &f, Backend backend, bool submitted) noexcept {
        if (!submitted || !plan(f, backend).accepted) return false;
        last_ = f; backend_ = backend; return true;
    }
    void clear() noexcept { last_ = {}; }
};
} // namespace nr::fullres
