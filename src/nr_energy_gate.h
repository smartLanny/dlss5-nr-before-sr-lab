#pragma once

namespace nr_energy_gate {
using Tick = unsigned long long;

inline constexpr Tick kEvidenceMaxAgeMs = 2000;
inline constexpr Tick kExplicitDebounceMs = 16;
inline constexpr Tick kGuessHoldMs = 300;
inline constexpr unsigned kExplicitSamples = 2;
inline constexpr unsigned kGuessSamples = 4;

enum class Cause : unsigned { None, Explicit, Heuristic };
enum class Transition : unsigned {
    None,
    PausedExplicit,
    PausedHeuristic,
    Resumed,
    Stale,
    Disabled,
};

struct Options {
    bool conservative = true;
    bool heuristic = false;
};

struct State {
    bool explicit_active = false;
    bool heuristic_active = false;
    unsigned explicit_samples = 0;
    Tick explicit_first_ms = 0;
    Tick explicit_last_ms = 0;
    unsigned long long guess_frame_id = 0;
    unsigned guess_samples = 0;
    Tick guess_since_ms = 0;
    Tick guess_last_ms = 0;

    constexpr bool Active() const noexcept { return explicit_active || heuristic_active; }
    constexpr Cause ActiveCause() const noexcept {
        return explicit_active ? Cause::Explicit : heuristic_active ? Cause::Heuristic : Cause::None;
    }
};

struct Result {
    State state{};
    Transition transition = Transition::None;
};

constexpr void ClearExplicit(State& state) noexcept {
    state.explicit_active = false;
    state.explicit_samples = 0;
    state.explicit_first_ms = 0;
    state.explicit_last_ms = 0;
}
constexpr void ClearGuess(State& state) noexcept {
    state.heuristic_active = false;
    state.guess_frame_id = 0;
    state.guess_samples = 0;
    state.guess_since_ms = 0;
    state.guess_last_ms = 0;
}

constexpr Result Poll(State state, Options options, Tick now) noexcept {
    Result result{state, Transition::None};
    const bool before = result.state.Active();
    bool disabled_active = false;
    bool stale_active = false;

    if (!options.conservative) {
        disabled_active |= result.state.explicit_active;
        ClearExplicit(result.state);
    } else if (result.state.explicit_last_ms &&
               (now < result.state.explicit_last_ms ||
                now - result.state.explicit_last_ms > kEvidenceMaxAgeMs)) {
        stale_active |= result.state.explicit_active;
        ClearExplicit(result.state);
    }

    if (!options.heuristic) {
        disabled_active |= result.state.heuristic_active;
        ClearGuess(result.state);
    } else if (result.state.guess_last_ms &&
               (now < result.state.guess_last_ms ||
                now - result.state.guess_last_ms > kEvidenceMaxAgeMs)) {
        stale_active |= result.state.heuristic_active;
        ClearGuess(result.state);
    }

    if (before && !result.state.Active())
        result.transition = disabled_active ? Transition::Disabled :
            stale_active ? Transition::Stale : Transition::Resumed;
    return result;
}

constexpr Result Observe(State state, Options options, bool paused_known,
                         bool paused, unsigned long long frame_id, Tick now) noexcept {
    Result result = Poll(state, options, now);
    const bool before_observation = result.state.Active();
    const Transition poll_transition = result.transition;

    if (paused_known) {
        // The game-provided pause bit is authoritative. A known false resumes
        // immediately and clears every speculative candidate.
        if (!paused) {
            ClearExplicit(result.state);
            ClearGuess(result.state);
        } else {
            ClearGuess(result.state);
            if (options.conservative) {
                // If the heuristic was already holding the gate, an explicit
                // true simply upgrades the cause without reopening NR.
                if (before_observation && state.heuristic_active) {
                    result.state.explicit_active = true;
                    result.state.explicit_samples = kExplicitSamples;
                    result.state.explicit_first_ms = now;
                    result.state.explicit_last_ms = now;
                } else {
                    if (!result.state.explicit_samples) {
                        result.state.explicit_samples = 1;
                        result.state.explicit_first_ms = now;
                    } else if (result.state.explicit_samples != ~0u) {
                        ++result.state.explicit_samples;
                    }
                    result.state.explicit_last_ms = now;
                    if (!result.state.explicit_active &&
                        result.state.explicit_samples >= kExplicitSamples &&
                        now >= result.state.explicit_first_ms &&
                        now - result.state.explicit_first_ms >= kExplicitDebounceMs)
                        result.state.explicit_active = true;
                }
            } else {
                ClearExplicit(result.state);
            }
        }
    } else {
        // Guess mode is deliberately source-identity based, not an image/static
        // classifier. It is available only when the explicit bit is absent.
        if (options.heuristic && frame_id) {
            if (result.state.guess_frame_id != frame_id) {
                const bool was_heuristic = result.state.heuristic_active;
                ClearGuess(result.state);
                result.state.guess_frame_id = frame_id;
                result.state.guess_samples = 1;
                result.state.guess_since_ms = now;
                result.state.guess_last_ms = now;
                if (was_heuristic && !result.state.explicit_active &&
                    result.transition == Transition::None)
                    result.transition = Transition::Resumed;
            } else {
                if (result.state.guess_samples != ~0u) ++result.state.guess_samples;
                result.state.guess_last_ms = now;
                if (!result.state.heuristic_active &&
                    result.state.guess_samples >= kGuessSamples &&
                    now >= result.state.guess_since_ms &&
                    now - result.state.guess_since_ms >= kGuessHoldMs)
                    result.state.heuristic_active = true;
            }
        } else {
            const bool was_heuristic = result.state.heuristic_active;
            ClearGuess(result.state);
            if (was_heuristic && !result.state.explicit_active &&
                result.transition == Transition::None)
                result.transition = Transition::Resumed;
        }
    }

    const bool after = result.state.Active();
    if (!before_observation && after) {
        result.transition = result.state.ActiveCause() == Cause::Explicit ?
            Transition::PausedExplicit : Transition::PausedHeuristic;
    } else if (before_observation && !after && result.transition == Transition::None) {
        result.transition = Transition::Resumed;
    } else if (poll_transition != Transition::None && !after) {
        result.transition = poll_transition;
    }
    return result;
}

constexpr bool ExplicitFresh(const State& state, Tick now) noexcept {
    return state.explicit_last_ms && now >= state.explicit_last_ms &&
        now - state.explicit_last_ms <= kEvidenceMaxAgeMs;
}
constexpr bool GuessFresh(const State& state, Tick now) noexcept {
    return state.guess_last_ms && now >= state.guess_last_ms &&
        now - state.guess_last_ms <= kEvidenceMaxAgeMs;
}
} // namespace nr_energy_gate
