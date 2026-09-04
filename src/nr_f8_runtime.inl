// Pure F8 session contract.  It contains no ReShade, Win32, GPU or file I/O;
// callers provide the apply/restore and immutable raw-slot callbacks.
#ifndef NR_F8_RUNTIME_INL
#define NR_F8_RUNTIME_INL

#include <cstring>

namespace nr_f8_runtime {

constexpr unsigned int kGroups = 3;
constexpr unsigned int kFrames = 16;

enum class State : unsigned int { Idle, Requested, Running, Restoring,
    Packaging, Complete, Partial, Failed };
enum class Mode : unsigned int { NativeSr, NrToSr, SrToNr };

struct Options {
    unsigned int issue = 0;
    float intensity = 1.0f;
    bool intensity_override = false;
    bool region_protection = false;
    bool jitter_experiment = false;
};

struct Identity {
    unsigned long long frame = 0;
    unsigned long long instance = 0;
    unsigned int generation = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    bool warmed = false;
};

struct RawSample {
    Identity identity = {};
    unsigned int slot = 0;
    bool complete = false;
};

struct Callbacks {
    bool (*apply_group)(unsigned int, Mode, const Options &, unsigned int *) = nullptr;
    bool (*request_raw_slot)(unsigned int, unsigned int) = nullptr;
    bool (*restore)(const void *) = nullptr;
    void (*package)(bool) = nullptr;
};

template <typename Config>
class Session {
public:
    static constexpr unsigned long long kGroupTimeoutMs = 30000;
    static constexpr unsigned long long kCountdownMs = 3000;

    State state() const { return state_; }
    unsigned int group() const { return group_; }
    bool partial() const { return partial_; }
    unsigned int frame_count(unsigned int group) const {
        return group < kGroups ? groups_[group].frame_count : 0;
    }
    unsigned int raw_count(unsigned int group) const {
        return group < kGroups ? groups_[group].raw_count : 0;
    }
    const Config &original() const { return original_; }

    // Request is called under the caller's UI/session lock. It copies all
    // options before publishing Requested, so Present can never observe a
    // half-written selection or consume an old request.
    bool request(const Options &options) {
        if (state_ != State::Idle && state_ != State::Complete &&
            state_ != State::Partial && state_ != State::Failed) return false;
        pending_ = options;
        state_ = State::Requested;
        return true;
    }

    bool begin(const Config &original, const Callbacks &callbacks,
               unsigned long long now) {
        if (state_ != State::Requested || callbacks.apply_group == nullptr) return false;
        original_ = original;
        callbacks_ = callbacks;
        options_ = pending_;
        pending_ = {};
        state_ = State::Running;
        partial_ = false;
        group_ = 0;
        started_ = now;
        return start_group(now);
    }

    void screenshot_saved(unsigned int group) {
        if (state_ == State::Running && group < kGroups && group == group_)
            groups_[group].screenshot = true;
    }

    // This is the only input that advances the temporal sequence. A gap,
    // reset, instance, config generation or extent change starts a new segment
    // and marks the group partial; the discontinuity itself is never appended.
    void successful_frame(const Identity &identity, bool reset) {
        if (state_ != State::Running || group_ >= kGroups) return;
        Group &group = groups_[group_];
        if (group.mode == Mode::NativeSr || !group.applied || !identity.warmed)
            return;
        if (identity.generation != group.generation) {
            break_sequence(group, "config-generation");
            group.applied = false;
            return;
        }
        if (reset) {
            break_sequence(group, "reset");
            return;
        }
        if (group.frame_count != 0) {
            const Identity &last = group.frames[group.frame_count - 1];
            if (identity.instance != last.instance ||
                identity.generation != last.generation ||
                identity.width != last.width || identity.height != last.height ||
                identity.frame != last.frame + 1) {
                break_sequence(group, "identity-or-frame-gap");
            }
        }
        if (group.frame_count < kFrames) {
            group.frames[group.frame_count] = identity;
            group.raw[group.frame_count] = false;
            ++group.frame_count;
            if (group.frame_count == kFrames) group.sequence_complete = true;
        }
    }

    void applied_identity(const Identity &identity) {
        if (state_ != State::Running || group_ >= kGroups) return;
        Group &group = groups_[group_];
        if (group.mode != Mode::NativeSr && identity.generation == group.generation)
            group.applied = true;
    }

    // Raw slots are immutable per submitted frame and can complete out of
    // order. Match by full identity; never attach a late slot to a new frame.
    void raw_sample(const RawSample &sample) {
        if (state_ != State::Running || group_ >= kGroups || !sample.complete) return;
        Group &group = groups_[group_];
        for (unsigned int i = 0; i < group.frame_count; ++i) {
            const Identity &identity = group.frames[i];
            if (identity.frame == sample.identity.frame &&
                identity.instance == sample.identity.instance &&
                identity.generation == sample.identity.generation &&
                identity.width == sample.identity.width &&
                identity.height == sample.identity.height && !group.raw[i]) {
                group.raw[i] = true;
                ++group.raw_count;
                return;
            }
        }
        group.partial = true;
        group.dropped_raw = true;
    }

    void cancel() {
        if (state_ == State::Requested) {
            pending_ = {};
            state_ = State::Idle;
        } else if (state_ == State::Running) {
            enter_restore(true);
        }
    }

    // Returns true when the caller should call Present/worker restore hooks.
    bool tick(unsigned long long now) {
        if (state_ != State::Running || group_ >= kGroups) return false;
        Group &group = groups_[group_];
        if (now - group.started >= kGroupTimeoutMs) {
            group.partial = true;
            enter_restore(true);
            return true;
        }
        if (now - group.started < kCountdownMs) return false;
        const bool native_ready = group.mode == Mode::NativeSr && group.screenshot;
        const bool nr_ready = group.mode != Mode::NativeSr &&
            group.sequence_complete && group.raw_count == kFrames;
        if (!(native_ready || nr_ready)) return false;
        if (group_ + 1 < kGroups) {
            ++group_;
            started_ = now;
            return start_group(now);
        }
        enter_restore(false);
        return true;
    }

    // The owner calls this after its transient ApplyConfig(original) returns.
    // No INI persistence is implied by this contract.
    void restore_finished(bool verified) {
        if (state_ != State::Restoring) return;
        if (!verified) partial_ = true;
        state_ = State::Packaging;
        if (callbacks_.package != nullptr) callbacks_.package(partial_);
    }

    void packaged(bool complete) {
        state_ = complete && !partial_ ? State::Complete : State::Partial;
    }

private:
    struct Group {
        Mode mode = Mode::NativeSr;
        unsigned int generation = 0;
        unsigned long long started = 0;
        bool applied = false;
        bool screenshot = false;
        bool sequence_complete = false;
        bool partial = false;
        bool dropped_raw = false;
        unsigned int frame_count = 0;
        unsigned int raw_count = 0;
        Identity frames[kFrames] = {};
        bool raw[kFrames] = {};
    };

    void break_sequence(Group &group, const char *) {
        group.partial = true;
        partial_ = true;
        group.sequence_complete = false;
        group.frame_count = 0;
        group.raw_count = 0;
        std::memset(group.raw, 0, sizeof(group.raw));
    }

    bool start_group(unsigned long long now) {
        Group &group = groups_[group_];
        group = {};
        group.mode = group_ == 0 ? Mode::NativeSr :
            group_ == 1 ? Mode::NrToSr : Mode::SrToNr;
        group.started = now;
        unsigned int generation = 0;
        if (!callbacks_.apply_group(group_, group.mode, options_, &generation)) {
            group.partial = true;
            partial_ = true;
            enter_restore(true);
            return false;
        }
        group.generation = generation;
        if (group.mode != Mode::NativeSr && callbacks_.request_raw_slot != nullptr) {
            group.raw_count = 0;
            // Slot requests are immutable and bounded. The callback may defer
            // individual slots, but no request is made per frame or per config.
            for (unsigned int slot = 0; slot < kFrames; ++slot)
                if (!callbacks_.request_raw_slot(group_, slot)) group.partial = true;
        }
        return true;
    }

    void enter_restore(bool partial) {
        partial_ = partial_ || partial;
        state_ = State::Restoring;
    }

    State state_ = State::Idle;
    unsigned int group_ = 0;
    unsigned long long started_ = 0;
    Config original_ = {};
    Options pending_ = {}, options_ = {};
    Callbacks callbacks_ = {};
    Group groups_[kGroups] = {};
    bool partial_ = false;
};

} // namespace nr_f8_runtime
#endif
