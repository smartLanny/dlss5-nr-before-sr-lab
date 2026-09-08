#pragma once
#include <cstdint>

namespace nr_gpu_timing {
using Ticket = std::uint64_t;
constexpr Ticket kNoTicket = 0;
enum class Boundary : unsigned { EncodeBegin, EncodeEnd, FeatureBegin, FeatureEnd, CompositeEnd, SrBegin, SrEnd, Count };

// The query allocation belongs to a recording, not a texture slot or a command
// list address. Completion alone cannot prevent a still-live list replay.
struct Lifetime {
    std::uint64_t token = 0, fence_value = 0;
    std::uintptr_t queue = 0, fence = 0;
    unsigned submissions = 0;
    bool recording_alive = false, unknown = false, replayed = false;
    void Record(std::uint64_t value) { *this = {}; token=value; recording_alive=value!=0; }
    bool Submit(std::uintptr_t q, std::uintptr_t f, std::uint64_t value) {
        if (!token || !q || !f || !value || unknown) { unknown=true; return false; }
        if (submissions && (queue!=q || fence!=f || value<=fence_value)) { unknown=true; return false; }
        replayed |= submissions!=0;
        if (++submissions==0) { unknown=true; return false; }
        queue=q; fence=f; fence_value=value; return true;
    }
    void Reset(bool definitely_unsubmitted) {
        recording_alive=false;
        if (!submissions && !definitely_unsubmitted) unknown=true;
    }
    bool Retirable(std::uint64_t completed) const {
        return token && !recording_alive && !unknown &&
            (!submissions || (completed!=UINT64_MAX && completed>=fence_value));
    }
};

struct Key {
    std::uint64_t context = 0;
    unsigned config = 0, precision = 0, width = 0, height = 0;
    bool Same(const Key &b) const {
        return context==b.context && config==b.config && precision==b.precision && width==b.width && height==b.height;
    }
};
struct Snapshot {
    Key key{};
    std::uint64_t completed = 0, dropped = 0, invalid = 0, pending = 0;
    double encoding_ms = 0, feature18_ms = 0, composition_ms = 0, sr_ms = 0;
    bool available = false;
};
} // namespace nr_gpu_timing
