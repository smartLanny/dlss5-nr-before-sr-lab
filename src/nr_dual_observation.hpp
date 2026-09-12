#pragma once

// Pure, platform-independent aggregation for the user-facing NR2 state.
//
// The Core owns the one instance and calls Record while holding g_chain_cs.
// This header intentionally knows nothing about NGX, GPU fences, Present, or
// the runtime's internal status enum.  A record is a receipt from the Core:
// eligible_primary means the current primary route was genuinely eligible;
// secondary_evaluate_success means the independent second Feature 18 call
// returned success; final_adopted means the final composition selected that
// answer.  A successful warm-up evaluation must set warmup_discarded and must
// not set final_adopted.
#include <cstdint>
#include <limits>

namespace nr_dual_observation {

constexpr std::uint64_t kWindowMs = 500;
constexpr std::uint64_t kStaleProcessingMs = 2000;

enum class UiState : std::uint8_t {
    Off,
    WaitingForFrame,
    Preparing,
    Active,
    Intermittent,
    NotYetApplied,
    Failed,
};

// This is an opaque route identity supplied by the Core.  It should change
// whenever the valid primary/secondary route changes (for example, a new
// Feature 1 generation, route direction, or resource contract).  A route can
// be invalid while a requested configuration is waiting for a usable frame.
struct Route {
    std::uint64_t id = 0;
    bool valid = false;
};

constexpr bool operator==(Route a, Route b) noexcept {
    return a.id == b.id && a.valid == b.valid;
}
constexpr bool operator!=(Route a, Route b) noexcept { return !(a == b); }

// FrameResult is deliberately a small factual protocol.  The Core supplies the
// config generation and route identity on every call, including a quiet
// record when a new configuration is committed but no frame is ready yet.
struct FrameResult {
    std::uint64_t tick_ms = 0;
    std::uint32_t config_generation = 0;
    Route route{};
    // "enabled" means the NR2 path is requested for this snapshot.  The
    // caller normally supplies config.enabled && config.nr_passes == 2.
    bool enabled = false;
    bool eligible_primary = false;
    bool secondary_evaluate_success = false;
    bool warmup_discarded = false;
    bool final_adopted = false;
    bool fatal = false;
    std::uint32_t fatal_code = 0;
};

// SnapshotUi takes the current request key as well as the clock.  That lets a
// UI poll hide an older active result immediately after a config/route change,
// even before the first quiet Record for the new key arrives.
struct SnapshotQuery {
    std::uint64_t tick_ms = 0;
    std::uint32_t config_generation = 0;
    Route route{};
    bool enabled = false;
};

struct Counts {
    std::uint64_t eligible_primary = 0;
    std::uint64_t secondary_evaluate_success = 0;
    std::uint64_t warmup_discarded = 0;
    std::uint64_t final_adopted = 0;
};

struct Snapshot {
    UiState state = UiState::Off;
    Counts counts{};
    std::uint64_t processing_age_ms = 0;
    std::uint64_t no_adoption_age_ms = 0;
    std::uint64_t window_start_ms = 0;
    std::uint32_t config_generation = 0;
    Route route{};
    std::uint32_t fatal_code = 0;
};

inline const char *UiStateName(UiState state) noexcept {
    switch (state) {
    case UiState::Off: return "off";
    case UiState::WaitingForFrame: return "waiting-frame";
    case UiState::Preparing: return "preparing";
    case UiState::Active: return "active";
    case UiState::Intermittent: return "intermittent";
    case UiState::NotYetApplied: return "not-yet-applied";
    case UiState::Failed: return "failed";
    }
    return "failed";
}

class Aggregator {
public:
    void Reset() noexcept {
        *this = Aggregator{};
    }

    void Record(const FrameResult &record) noexcept {
        if (!has_key_ || record.config_generation != config_generation_ ||
            record.route != route_ || record.enabled != enabled_) {
            BeginKey(record);
        }

        // A delayed callback from an older frame cannot move the window or
        // manufacture a current receipt.  A fatal receipt still latches so a
        // late opaque failure is visible immediately for this key.
        if (record.tick_ms < last_tick_ms_) {
            if (record.fatal) LatchFatal(record.fatal_code);
            return;
        }
        last_tick_ms_ = record.tick_ms;

        if (record.fatal) {
            LatchFatal(record.fatal_code);
            return;
        }

        if (record.tick_ms - window_start_ms_ >= kWindowMs) {
            window_ = {};
            window_start_ms_ = record.tick_ms;
        }

        // Disabled or invalid routes are key observations only.  Never count
        // a result which the current request could not legally use.
        if (!enabled_ || !route_.valid) return;

        const bool processed = record.eligible_primary ||
            record.secondary_evaluate_success || record.warmup_discarded ||
            record.final_adopted;
        if (processed) {
            has_processing_ = true;
            last_processing_ms_ = record.tick_ms;
        }
        if (record.eligible_primary) {
            SaturatingIncrement(window_.eligible_primary);
            has_primary_eligible_ = true;
            last_primary_eligible_ms_ = record.tick_ms;
        }
        if (record.secondary_evaluate_success)
            SaturatingIncrement(window_.secondary_evaluate_success);
        if (record.warmup_discarded)
            SaturatingIncrement(window_.warmup_discarded);

        // A warm-up answer is intentionally excluded even if a caller left
        // final_adopted set by mistake.  Requiring evaluate success also
        // prevents an unproven composition from inflating the adoption ratio.
        const bool adopted = record.final_adopted &&
            record.secondary_evaluate_success && !record.warmup_discarded;
        if (adopted) {
            SaturatingIncrement(window_.final_adopted);
            ever_adopted_ = true;
            no_adoption_streak_ = false;
            no_adoption_since_ms_ = 0;
        } else if (record.eligible_primary && !no_adoption_streak_) {
            // A stream of real primary frames can continue while every
            // secondary attempt is rejected or discarded during warm-up.
            // Keep this separate from processing_age_ms so that stream does
            // not look healthy forever merely because it is being processed.
            no_adoption_streak_ = true;
            no_adoption_since_ms_ = record.tick_ms;
        }
    }

    // SnapshotUi is const: route/config mismatch is represented as an empty
    // waiting snapshot, so an old active window cannot leak into a new key.
    Snapshot SnapshotUi(const SnapshotQuery &query) const noexcept {
        Snapshot snapshot{};
        snapshot.config_generation = query.config_generation;
        snapshot.route = query.route;
        if (!query.enabled) {
            snapshot.state = UiState::Off;
            return snapshot;
        }

        if (!has_key_ || query.config_generation != config_generation_ ||
            query.route != route_ || query.enabled != enabled_) {
            snapshot.state = UiState::WaitingForFrame;
            return snapshot;
        }

        snapshot.counts = window_;
        snapshot.window_start_ms = window_start_ms_;
        snapshot.fatal_code = fatal_code_;
        if (fatal_) {
            snapshot.state = UiState::Failed;
            return snapshot;
        }
        if (!query.route.valid) {
            snapshot.state = UiState::WaitingForFrame;
            return snapshot;
        }
        if (!has_processing_ || !has_primary_eligible_) {
            snapshot.state = UiState::WaitingForFrame;
            return snapshot;
        }

        // Eligibility is the authoritative sign that the current primary
        // route still has a usable frame. Secondary-only receipts must not
        // keep an idle primary route looking live.
        snapshot.processing_age_ms = Age(query.tick_ms, last_primary_eligible_ms_);
        snapshot.no_adoption_age_ms = no_adoption_streak_
            ? Age(query.tick_ms, no_adoption_since_ms_) : 0;
        if (snapshot.processing_age_ms > kStaleProcessingMs) {
            // Old counts are no longer current evidence once the primary has
            // stopped supplying eligible frames.
            snapshot.counts = {};
            snapshot.state = UiState::WaitingForFrame;
            return snapshot;
        }
        if (no_adoption_streak_ &&
            snapshot.no_adoption_age_ms > kStaleProcessingMs) {
            // Primary frames are still arriving, so this is a live route
            // that has failed to produce an adopted second result.
            snapshot.state = UiState::NotYetApplied;
            return snapshot;
        }

        if (window_.eligible_primary == 0) {
            // A current successful secondary evaluation without an adopted
            // result is still preparation.  Once the route goes quiet for
            // two seconds the branch above reports waiting/not-yet-applied.
            snapshot.state = ever_adopted_ ? UiState::WaitingForFrame
                                           : UiState::Preparing;
            return snapshot;
        }
        if (window_.final_adopted == 0) {
            snapshot.state = UiState::Preparing;
        } else if (window_.final_adopted >= window_.eligible_primary) {
            snapshot.state = UiState::Active;
        } else {
            snapshot.state = UiState::Intermittent;
        }
        return snapshot;
    }

    Snapshot SnapshotUi(std::uint64_t tick_ms) const noexcept {
        SnapshotQuery query{};
        query.tick_ms = tick_ms;
        query.config_generation = config_generation_;
        query.route = route_;
        query.enabled = enabled_;
        return SnapshotUi(query);
    }

private:
    static void SaturatingIncrement(std::uint64_t &value) noexcept {
        if (value != std::numeric_limits<std::uint64_t>::max()) ++value;
    }

    static std::uint64_t Age(std::uint64_t now, std::uint64_t then) noexcept {
        return now >= then ? now - then : 0;
    }

    void BeginKey(const FrameResult &record) noexcept {
        has_key_ = true;
        config_generation_ = record.config_generation;
        route_ = record.route;
        enabled_ = record.enabled;
        window_ = {};
        window_start_ms_ = record.tick_ms;
        last_tick_ms_ = record.tick_ms;
        last_processing_ms_ = 0;
        has_processing_ = false;
        has_primary_eligible_ = false;
        last_primary_eligible_ms_ = 0;
        ever_adopted_ = false;
        no_adoption_streak_ = false;
        no_adoption_since_ms_ = 0;
        fatal_ = false;
        fatal_code_ = 0;
    }

    void LatchFatal(std::uint32_t code) noexcept {
        fatal_ = true;
        fatal_code_ = code;
    }

    bool has_key_ = false;
    std::uint32_t config_generation_ = 0;
    Route route_{};
    bool enabled_ = false;
    Counts window_{};
    std::uint64_t window_start_ms_ = 0;
    std::uint64_t last_tick_ms_ = 0;
    std::uint64_t last_processing_ms_ = 0;
    bool has_processing_ = false;
    bool has_primary_eligible_ = false;
    std::uint64_t last_primary_eligible_ms_ = 0;
    bool ever_adopted_ = false;
    bool no_adoption_streak_ = false;
    std::uint64_t no_adoption_since_ms_ = 0;
    bool fatal_ = false;
    std::uint32_t fatal_code_ = 0;
};

} // namespace nr_dual_observation
