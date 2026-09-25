#pragma once
#include <array>
#include <cstdint>

// CPU observations only. Durations and retained calls never authorize GPU use
// and cannot measure input-to-display latency or infer a rendered frame rate.
namespace nr_timing {
enum class Stage : unsigned {
    Carrier, ChainLock, QueueLock, FenceWait, Readback,
    NrEvaluate, NativeEvaluate, Execute, Signal, ModelCreate, Count
};
enum class WaitMode : unsigned { Event = 0, Polling = 1 };
enum Flag : unsigned {
    CarrierKnown = 1u, ResourceKnown = 2u, CommandObserved = 4u,
    NrReturned = 8u, NativeReturned = 16u, ResetBound = 32u,
    ProbeDispatched = 64u, CarrierFinished = 128u,
    ReadbackMapped = 256u, ReadbackOriginKnown = 512u,
    ExposureResetPending = 1024u, MixedReadbackOrigins = 2048u,
    DeltaStatsRecorded = 4096u, TargetKnown = 8192u,
    ApiReturned = 16384u, ApiResultKnown = 32768u, OutputTransitionsOnly = 65536u
};
enum ResetReason : unsigned {
    FirstCreate = 1u, GameReset = 2u, HistoryChanged = 4u,
    ColorContract = 8u, GuideChanged = 16u, RetryPending = 32u,
    ExposureReadback = 64u, UnknownDepth = 128u
};
struct Policy { unsigned mode = 0; std::uint64_t phase = 1; };
struct Policies { std::uint64_t epoch = 1; Policy probe{}, wait{}; };
struct Identity {
    std::uint64_t epoch = 0, frame = 0, feature = 0, config = 0; // feature is the lifecycle generation
    std::uint64_t device = 0, resource = 0, resource_generation = 0;
    std::uint64_t target = 0, target_generation = 0;
    unsigned target_subresource = 0;
    unsigned mode = 0, role = 0; // mode 0: route not yet admitted, 1: post, 2: pre
    Policy probe{}, wait{};
};
struct Detail {
    unsigned flags = 0, reset_reasons = 0, bypass = 0;
    std::int32_t nr_result = 0, native_result = 0;
    std::int32_t first_nr_result = 0, first_native_result = 0;
    unsigned nr_calls = 0, native_calls = 0;
    std::uint64_t queue = 0, fence_target = 0, fence_before = 0, fence_after = 0;
    unsigned wait_method = 0, wait_fallback = 0;
    std::int32_t wait_registration = 0;
    std::int32_t api_result = 0;
};
struct Record {
    Identity identity{}, readback_origin{};
    Detail detail{};
    Stage stage = Stage::Carrier;
    std::uint64_t sequence = 0, started_ticks = 0, duration_ticks = 0;
    bool clock_known = false;
};
inline constexpr unsigned kStageCount = static_cast<unsigned>(Stage::Count);
inline constexpr unsigned kRecentCount = 64;
inline constexpr std::array<std::uint64_t, 11> kHistogramUpperUs{
    100, 250, 500, 1000, 2000, 4000, 8000, 16000, 32000, 50000, 100000
};
struct Stats {
    std::uint64_t count = 0, measured = 0, total_ticks = 0, maximum_ticks = 0;
    std::array<std::uint64_t, kHistogramUpperUs.size() + 1> histogram{};
};
// Submission-local observations, not readiness evidence. Kept separately so
// frequent unrelated submissions cannot evict existing carrier/Signal records.
enum SubmitReason : unsigned {
    LifecycleAbsent = 1u, LifecyclePresent = 2u, UnknownList = 4u,
    CaptureFailed = 8u, Quarantined = 16u, SubmitNativeReturned = 32u,
    RetirementSignaled = 64u, TrackingUncertain = 128u
};
struct SubmitBatch {
    std::uint64_t epoch = 0, sequence = 0, queue = 0, first_token = 0;
    std::uint64_t scanned = 0, qi = 0, direct_absent = 0, token_lists = 0;
    std::uint64_t chain_hold_ticks = 0, chain_hold_max_ticks = 0;
    unsigned input_lists = 0, reasons = 0, chain_sections = 0;
    bool clock_known = true;
};
inline constexpr unsigned kSubmitRecentCount = 32;
struct SubmitStats {
    std::uint64_t count = 0, scanned = 0, qi = 0, direct_absent = 0;
    std::uint64_t lifecycle_absent = 0, unknown = 0, chain_sections = 0;
    std::uint64_t chain_hold_ticks = 0, chain_hold_max_ticks = 0, clock_unknown = 0;
    unsigned recent_count = 0;
    std::array<SubmitBatch, kSubmitRecentCount> recent{};
};
struct Snapshot {
    std::uint64_t epoch = 1, qpc_frequency = 0, observed = 0;
    std::uint64_t epoch_started_ticks = 0, captured_ticks = 0;
    std::uint64_t excluded_old_epoch = 0, recent_overwritten = 0;
    Policy probe{}, wait{};
    Identity aggregate_scope{}; // resource rotation is deliberately not a new scope
    std::uint64_t scoped_records = 0, unscoped_records = 0, route_unknown_records = 0;
    bool aggregate_scope_available = false, mixed_scopes = false;
    bool counter_overflow = false;
    bool clock_available = false;
    unsigned recent_count = 0;
    std::array<Stats, kStageCount> stages{};
    std::array<Record, kRecentCount> recent{}; // oldest to newest
    SubmitStats submissions{};
};
inline const char* WaitModeName(unsigned mode) { return mode == 0 ? "event" : mode == 1 ? "polling" : "unknown"; }
inline const char* Name(Stage stage) {
    switch (stage) {
    case Stage::Carrier: return "carrier";
    case Stage::ChainLock: return "chain-lock";
    case Stage::QueueLock: return "queue-lock";
    case Stage::FenceWait: return "submitted-fence-wait";
    case Stage::Readback: return "readback-map-copy";
    case Stage::NrEvaluate: return "nr-evaluate";
    case Stage::NativeEvaluate: return "native-evaluate";
    case Stage::Execute: return "native-execute";
    case Stage::Signal: return "retirement-signal";
    case Stage::ModelCreate: return "nr-model-create";
    default: return "unknown";
    }
}
}
