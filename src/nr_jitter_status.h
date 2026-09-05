#pragma once
namespace nr_jitter_status {
enum class Status { NoFrame, Bound, Unknown, Inactive, Pending, Bypassed, Changed, Stale };
struct Snapshot {
    unsigned int config = 0;
    unsigned long long feature = 0, instance = 0, frame = 0, tick = 0;
    int mode = 0;
    bool known = false;
    Status status = Status::NoFrame;
};
inline Status StatusFor(const Snapshot &s, unsigned int config,
    unsigned long long feature, int mode, bool live_instance, bool active,
    bool bypassed, unsigned long long now) {
    if (!active) return Status::Inactive;
    if (bypassed) return Status::Bypassed;
    if (s.frame == 0) return Status::NoFrame;
    if (s.config != config || s.mode != mode) return Status::Pending;
    if (s.feature != feature || !live_instance) return Status::Changed;
    if (now < s.tick || now - s.tick > 2000) return Status::Stale;
    return s.known ? Status::Bound : Status::Unknown;
}
}
