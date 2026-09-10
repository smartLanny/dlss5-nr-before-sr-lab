// Runtime adapter for the pure NR2 observation aggregator.
//
// This file is included by the Core after NrContext, g_config, g_chain_cs and
// nr_dual_runtime's fatal_result have been declared.  RecordObservation is
// called by the rendering path while g_chain_cs is already held.  SnapshotUi
// takes that same lock itself so the UI never reads a half-updated context.
#include "nr_dual_observation.hpp"

namespace nr_dual_status_runtime {

using UiState = nr_dual_observation::UiState;
using UiSnapshot = nr_dual_observation::Snapshot;

// An opaque route identity is intentionally derived only from the live
// primary context.  The auxiliary context may be absent while it is being
// created; including its pointer or instance here would reset the 500 ms
// window on every such frame.  Primary instance and mode are stable for the
// lifetime of the route and change when the primary Feature 18 contract is
// rebuilt.
inline nr_dual_observation::Route RouteForPrimary(
    const NrContext &primary) noexcept {
    const auto mode = static_cast<std::uint32_t>(primary.mode);
    const bool valid = primary.instance_id != 0 && primary.mode != 0;
    // Keep the route opaque while retaining both source fields.  The mode is
    // mixed instead of stored in a second mutable generation counter.
    const std::uint64_t id = primary.instance_id ^
        (static_cast<std::uint64_t>(mode) * 0x9E3779B97F4A7C15ull);
    return {id, valid};
}

nr_dual_observation::Aggregator g_observation{};
nr_dual_observation::Route g_last_route{};
std::uint32_t g_last_config_generation = 0;
bool g_last_enabled = false;
bool g_route_initialized = false;

// This snapshot is protected by g_chain_cs.  It represents a successful
// secondary Evaluate, including a successful warm-up evaluation; warmed_up is
// recorded separately so F8 cannot mistake the model parameter receipt for a
// final adopted image.
struct SecondaryIdentity {
    bool valid = false;
    nr_dual_observation::Route route{};
    std::uint64_t instance_id = 0;
    std::uint64_t feature_generation = 0;
    std::uint32_t config_generation = 0;
    std::uint32_t evaluation = 0;
    std::uint32_t since_reset = 0;
    bool reset_requested = false;
    std::uint32_t reset_reasons = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool warmed_up = false;
    NrTuningSignature tuning{};
};
SecondaryIdentity g_secondary_identity{};

// Called with g_chain_cs held by the caller.  The secondary pointer is an
// input receipt only; it must not participate in the route key while it is
// null or being replaced.
void RecordObservation(const NrContext &primary, const NrContext *secondary,
                       bool eligible, bool evaluated, bool adopted,
                       bool warmup_discarded) noexcept {
    (void)secondary;
    const auto route = RouteForPrimary(primary);
    const auto config_generation =
        g_config_generation.load(std::memory_order_relaxed);
    const bool enabled = g_config.enabled && g_config.nr_passes == 2;
    if (!g_route_initialized || route != g_last_route ||
        config_generation != g_last_config_generation ||
        enabled != g_last_enabled) {
        // A new key must not retain the previous secondary's actual tuning
        // receipt.  The pure aggregator also clears its counters here.
        g_secondary_identity = {};
        g_route_initialized = true;
        g_last_route = route;
        g_last_config_generation = config_generation;
        g_last_enabled = enabled;
    }

    nr_dual_observation::FrameResult record{};
    record.tick_ms = GetTickCount64();
    record.config_generation = config_generation;
    record.route = route;
    record.enabled = enabled;
    record.eligible_primary = eligible;
    record.secondary_evaluate_success = evaluated;
    record.final_adopted = adopted;
    record.warmup_discarded = warmup_discarded;
    const auto fatal = nr_dual_runtime::fatal_result.load(
        std::memory_order_acquire);
    record.fatal = fatal != 0;
    record.fatal_code = static_cast<std::uint32_t>(fatal);
    g_observation.Record(record);
}

// Called after the independent secondary Evaluate has returned success and
// while g_chain_cs is held.  All values are copied from the actual auxiliary
// context; no model face-detection result is inferred here.
void CaptureSecondaryIdentity(const NrContext &context) noexcept {
    if (context.layer_index != 1 || context.instance_id == 0 ||
        context.parent_instance_id == 0 || context.mode == 0 ||
        !context.tuning_signature_initialized) {
        g_secondary_identity = {};
        return;
    }
    SecondaryIdentity snapshot{};
    snapshot.valid = true;
    // The secondary context stores the primary instance in
    // parent_instance_id; its own instance must remain separate for F8.
    const auto mode = static_cast<std::uint32_t>(context.mode);
    snapshot.route = {
        context.parent_instance_id ^
            (static_cast<std::uint64_t>(mode) * 0x9E3779B97F4A7C15ull),
        true};
    snapshot.instance_id = context.instance_id;
    snapshot.feature_generation = context.feature_generation;
    snapshot.config_generation = context.config_generation;
    snapshot.evaluation = context.evaluations;
    snapshot.since_reset = context.successful_evaluations_since_reset;
    snapshot.reset_requested = context.diagnostic_reset_requested;
    snapshot.reset_reasons = context.diagnostic_reset_reasons;
    snapshot.width = context.width;
    snapshot.height = context.height;
    snapshot.warmed_up = context.successful_evaluations_since_reset >
        g_config.warmup_after_create;
    snapshot.tuning = context.tuning_signature;
    g_secondary_identity = snapshot;
}

SecondaryIdentity SnapshotSecondaryIdentityAlreadyLocked() noexcept {
    SecondaryIdentity snapshot = g_secondary_identity;
    const auto generation =
        g_config_generation.load(std::memory_order_relaxed);
    const bool enabled = g_config.enabled && g_config.nr_passes == 2;
    if (!snapshot.valid || !enabled ||
        snapshot.config_generation != generation ||
        snapshot.route != g_last_route)
        snapshot = {};
    return snapshot;
}

// Binding diagnostics are copied while g_chain_cs is held, alongside the
// observation and secondary identity.  A generation mismatch is useful
// evidence that the refusal belongs to an older requested configuration.
struct BindingSnapshot {
    nr_binding::Refusal reason{};
    std::uint32_t generation = 0;
    std::uint64_t context = 0;
    std::uint32_t config_generation = 0;
    std::uint32_t guide_flags = 0;
    std::uint32_t status = 0;
    bool generation_matches_config = false;
};

struct BindingSnapshotPair {
    BindingSnapshot dual{};
    BindingSnapshot region{};
};

BindingSnapshotPair SnapshotBindingAlreadyLocked() noexcept {
    const auto config_generation =
        g_config_generation.load(std::memory_order_relaxed);
    BindingSnapshotPair snapshot{};
    snapshot.dual.reason = nr_dual_runtime::binding_reason.load(
        std::memory_order_acquire);
    snapshot.dual.generation = nr_dual_runtime::binding_generation.load(
        std::memory_order_acquire);
    snapshot.dual.context = nr_dual_runtime::binding_context.load(
        std::memory_order_acquire);
    snapshot.dual.config_generation = config_generation;
    snapshot.dual.status = static_cast<std::uint32_t>(
        nr_dual_runtime::status.load(std::memory_order_acquire));
    snapshot.dual.generation_matches_config =
        snapshot.dual.generation == config_generation;

    snapshot.region.reason = nr_regions::binding_reason.load(
        std::memory_order_acquire);
    snapshot.region.generation = nr_regions::config_generation.load(
        std::memory_order_acquire);
    snapshot.region.config_generation = config_generation;
    snapshot.region.guide_flags = nr_regions::guide_flags.load(
        std::memory_order_acquire);
    snapshot.region.status = nr_regions::status.load(
        std::memory_order_acquire);
    snapshot.region.generation_matches_config =
        snapshot.region.generation == config_generation;
    return snapshot;
}

SecondaryIdentity SnapshotSecondaryIdentity() noexcept {
    EnterCriticalSection(&g_chain_cs);
    const auto snapshot = SnapshotSecondaryIdentityAlreadyLocked();
    LeaveCriticalSection(&g_chain_cs);
    return snapshot;
}

UiSnapshot SnapshotUiAlreadyLocked() noexcept;

struct SnapshotPair {
    UiSnapshot ui{};
    SecondaryIdentity secondary{};
    BindingSnapshotPair binding{};
};

SnapshotPair SnapshotUiAndSecondaryAlreadyLocked() noexcept {
    return {SnapshotUiAlreadyLocked(), SnapshotSecondaryIdentityAlreadyLocked(),
            SnapshotBindingAlreadyLocked()};
}

SnapshotPair SnapshotUiAndSecondary() noexcept {
    EnterCriticalSection(&g_chain_cs);
    const auto snapshot = SnapshotUiAndSecondaryAlreadyLocked();
    LeaveCriticalSection(&g_chain_cs);
    return snapshot;
}

// This read-only helper requires g_chain_cs.  It is separate so a serializer
// which already holds the Core lock cannot deadlock by taking it again.
UiSnapshot SnapshotUiAlreadyLocked() noexcept {
    const auto config_generation =
        g_config_generation.load(std::memory_order_relaxed);
    const bool enabled = g_config.enabled && g_config.nr_passes == 2;
    nr_dual_observation::SnapshotQuery query{};
    query.tick_ms = GetTickCount64();
    query.config_generation = config_generation;
    query.route = g_last_route;
    query.enabled = enabled;
    auto snapshot = g_observation.SnapshotUi(query);
    const auto fatal = nr_dual_runtime::fatal_result.load(
        std::memory_order_acquire);
    if (fatal != 0) {
        snapshot.state = UiState::Failed;
        snapshot.counts = {};
        snapshot.config_generation = config_generation;
        snapshot.route = g_last_route;
        snapshot.fatal_code = static_cast<std::uint32_t>(fatal);
    }
    return snapshot;
}

// SnapshotUi takes g_chain_cs internally.  A raw fatal_result is authoritative
// for the current process and wins even when the pure window has not received
// its next frame receipt yet.
UiSnapshot SnapshotUi() noexcept {
    EnterCriticalSection(&g_chain_cs);
    const auto snapshot = SnapshotUiAlreadyLocked();
    LeaveCriticalSection(&g_chain_cs);
    return snapshot;
}

} // namespace nr_dual_status_runtime
