#pragma once
#include <atomic>
#include <cstdint>
#include <limits>

// Process-local experiment identity. It is deliberately independent of saved
// picture settings, configuration generation and the model's history.
namespace nr_probe_policy {
enum class Mode : unsigned { Normal = 0, Paused = 1, OutputTransitionsOnly = 2 };
struct Snapshot {
    std::uint64_t phase = 1;
    Mode mode = Mode::Normal;
    bool AutomaticSampling() const { return mode == Mode::Normal; }
    bool OutputTransitionsOnly() const { return mode == Mode::OutputTransitionsOnly; }
    bool ScheduleCheckpoint() const { return AutomaticSampling() || OutputTransitionsOnly(); }
};
class Control {
    // A single atomic keeps mode and phase coherent for feedback/UI readers.
    std::atomic<std::uint64_t> state_{4};
public:
    Snapshot Read() const {
        const auto state = state_.load(std::memory_order_acquire);
        return {state >> 2, static_cast<Mode>(state & 3)};
    }
    // The runtime serializes writers with its existing chain lock. The CAS
    // also makes this small policy safe in independent CPU tests.
    bool Change(Mode mode) {
        if (mode != Mode::Normal && mode != Mode::Paused && mode != Mode::OutputTransitionsOnly) return false;
        auto previous = state_.load(std::memory_order_acquire);
        for (;;) {
            if ((previous & 3) == static_cast<unsigned>(mode)) return false;
            if ((previous >> 2) == (std::numeric_limits<std::uint64_t>::max() >> 2))
                return false; // Never reuse an earlier experiment identity.
            const auto next = (((previous >> 2) + 1) << 2) | static_cast<unsigned>(mode);
            if (state_.compare_exchange_weak(previous, next,
                    std::memory_order_acq_rel, std::memory_order_acquire)) return true;
        }
    }
};
inline const char *Name(Mode mode) {
    switch (mode) {
    case Mode::Normal: return "normal";
    case Mode::Paused: return "paused";
    case Mode::OutputTransitionsOnly: return "output-transitions-only";
    default: return "unknown";
    }
}
} // namespace nr_probe_policy
