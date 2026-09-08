#pragma once
#include <cstdint>
#include <vector>

namespace nr::nvfp4::detail {
// Pure policy used by the actual pool. Fence completion alone is insufficient:
// a closed command list can be replayed until its successful Reset is observed.
struct RecordingLifetime {
    struct Submission { uintptr_t queue, fence; uint64_t value; bool complete = false; };
    uint64_t token = 0;
    bool reset = false, abandoned = false, quarantined = false;
    std::vector<Submission> submissions;
    bool Submit(uintptr_t queue, uintptr_t fence, uint64_t value) {
        if (!queue || !fence || !value || reset || abandoned) { quarantined = true; return false; }
        for (auto& s : submissions) {
            if (s.queue == queue && s.fence == fence) {
                if (value > s.value) { s.value = value; s.complete = false; }
                return true;
            }
        }
        submissions.push_back({queue, fence, value, false});
        return true;
    }
    void Reset(bool definitely_unsubmitted) {
        reset = true;
        if (definitely_unsubmitted && submissions.empty()) abandoned = true;
    }
    bool Reclaimable() const {
        // An unknown submission/replay can outlive every fence we know about.
        // Keep this separate from a recording failure with fully known fences.
        if (quarantined) return false;
        if (!reset) return false;
        if (abandoned && submissions.empty()) return true;
        if (submissions.empty()) return false;
        for (const auto& s : submissions) if (!s.complete) return false;
        return true;
    }
};
} // namespace nr::nvfp4::detail
