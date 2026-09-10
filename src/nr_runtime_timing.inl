// Included inside the private runtime namespace; the value header is included
// outside it. This lock protects values only, never Win32 waits, COM or NGX.
#define NR_HAS_TIMING 1
SRWLOCK g_nr_timing_lock = SRWLOCK_INIT;
nr_timing::Snapshot g_nr_timing;
unsigned g_nr_timing_recent_next = 0;
std::uint64_t g_nr_timing_sequence = 0;

std::uint64_t NrTimingFrequency() {
    static const auto frequency = [] {
        LARGE_INTEGER value{};
        return QueryPerformanceFrequency(&value) && value.QuadPart > 0
            ? static_cast<std::uint64_t>(value.QuadPart) : 0ull;
    }();
    return frequency;
}
std::uint64_t NrTimingCounter() {
    LARGE_INTEGER value{};
    return QueryPerformanceCounter(&value) && value.QuadPart > 0
        ? static_cast<std::uint64_t>(value.QuadPart) : 0ull;
}
void NrTimingIncrement(std::uint64_t& value, std::uint64_t amount = 1) {
    if (UINT64_MAX - value < amount) { value = UINT64_MAX; g_nr_timing.counter_overflow = true; }
    else value += amount;
}
void NrTimingSetPolicy(bool probe, unsigned mode, std::uint64_t phase) {
    if (mode > (probe ? 2u : 1u)) return;
    AcquireSRWLockExclusive(&g_nr_timing_lock);
    auto next_probe = g_nr_timing.probe, next_wait = g_nr_timing.wait;
    auto& next = probe ? next_probe : next_wait;
    if (next.mode != mode || next.phase != phase) {
        next = {mode, phase};
        const auto epoch = g_nr_timing.epoch;
        g_nr_timing = {};
        g_nr_timing.epoch = epoch == UINT64_MAX ? UINT64_MAX : epoch + 1;
        g_nr_timing.counter_overflow = epoch == UINT64_MAX;
        g_nr_timing.probe = next_probe; g_nr_timing.wait = next_wait;
        g_nr_timing.epoch_started_ticks = NrTimingCounter();
        g_nr_timing_recent_next = 0;
    }
    ReleaseSRWLockExclusive(&g_nr_timing_lock);
}
// Nonpersistent diagnostic choices. Neither setter touches INI, NR settings,
// history, the wait budget, sampling policy or recording/retirement ownership.
void NrTimingSetProbePolicy(unsigned mode, std::uint64_t phase) { NrTimingSetPolicy(true, mode, phase); }
void NrTimingSetWaitPolicy(unsigned mode, std::uint64_t phase) { NrTimingSetPolicy(false, mode, phase); }
nr_timing::Policies NrTimingPolicySnapshot() {
    AcquireSRWLockShared(&g_nr_timing_lock);
    const nr_timing::Policies value{g_nr_timing.epoch, g_nr_timing.probe, g_nr_timing.wait};
    ReleaseSRWLockShared(&g_nr_timing_lock);
    return value;
}

struct NrTimingCarrierScope;
thread_local NrTimingCarrierScope* g_nr_timing_carrier = nullptr;
struct NrTimingCarrierScope {
    NrTimingCarrierScope* parent = nullptr;
    nr_timing::Identity identity{};
    nr_timing::Detail detail{};
    nr_timing::Identity readback_origin{};
    std::uint64_t started = 0;
    NrTimingCarrierScope() = default;
    void Begin(std::uint64_t frame, std::uint64_t feature, std::uint64_t config,
               unsigned mode, std::uint64_t device);
    ~NrTimingCarrierScope();
};
nr_timing::Identity NrTimingNewIdentity() {
    nr_timing::Identity value{};
    AcquireSRWLockShared(&g_nr_timing_lock);
    value.epoch = g_nr_timing.epoch; value.probe = g_nr_timing.probe; value.wait = g_nr_timing.wait;
    ReleaseSRWLockShared(&g_nr_timing_lock);
    return value;
}
nr_timing::Identity NrTimingCurrentIdentity() {
    // A carrier freezes its phase at entry. Work it starts after a concurrent
    // phase change is still part of that old carrier, including pending probes.
    if (g_nr_timing_carrier) return g_nr_timing_carrier->identity;
    return NrTimingNewIdentity();
}
void NrTimingRecord(nr_timing::Record record) {
    const auto index = static_cast<unsigned>(record.stage);
    if (index >= nr_timing::kStageCount) return;
    const auto frequency = NrTimingFrequency();
    AcquireSRWLockExclusive(&g_nr_timing_lock);
    if (record.identity.epoch != g_nr_timing.epoch) {
        NrTimingIncrement(g_nr_timing.excluded_old_epoch);
        ReleaseSRWLockExclusive(&g_nr_timing_lock);
        return;
    }
    g_nr_timing.qpc_frequency = frequency;
    if (!g_nr_timing.epoch_started_ticks)
        g_nr_timing.epoch_started_ticks = record.started_ticks;
    NrTimingIncrement(g_nr_timing.observed);
    if (record.identity.frame && record.identity.feature && record.identity.config && record.identity.device && record.identity.mode) {
        NrTimingIncrement(g_nr_timing.scoped_records);
        if (!g_nr_timing.aggregate_scope_available) {
            g_nr_timing.aggregate_scope = record.identity;
            g_nr_timing.aggregate_scope.resource = g_nr_timing.aggregate_scope.resource_generation = 0;
            g_nr_timing.aggregate_scope.target = g_nr_timing.aggregate_scope.target_generation = 0;
            g_nr_timing.aggregate_scope.target_subresource = 0;
            g_nr_timing.aggregate_scope.role = 0;
            g_nr_timing.aggregate_scope_available = true;
        } else {
            const auto& scope = g_nr_timing.aggregate_scope;
            if (scope.device != record.identity.device || scope.feature != record.identity.feature ||
                scope.config != record.identity.config || scope.mode != record.identity.mode)
                g_nr_timing.mixed_scopes = true;
        }
    } else {
        NrTimingIncrement(g_nr_timing.unscoped_records);
        if (record.identity.frame && record.identity.feature && record.identity.config && !record.identity.mode)
            NrTimingIncrement(g_nr_timing.route_unknown_records);
    }
    auto& stats = g_nr_timing.stages[index];
    NrTimingIncrement(stats.count);
    record.clock_known = record.clock_known && frequency != 0;
    if (record.clock_known) {
        NrTimingIncrement(stats.measured); NrTimingIncrement(stats.total_ticks, record.duration_ticks);
        stats.maximum_ticks = std::max(stats.maximum_ticks, record.duration_ticks);
        const long double us = static_cast<long double>(record.duration_ticks) * 1000000.0L / frequency;
        unsigned bucket = 0;
        while (bucket < nr_timing::kHistogramUpperUs.size() && us > nr_timing::kHistogramUpperUs[bucket]) ++bucket;
        NrTimingIncrement(stats.histogram[bucket]);
    }
    // Retain completed carriers, asynchronous submit/readback facts and slow
    // phases. Fast per-phase observations still enter the complete histogram.
    const bool retain = record.stage == nr_timing::Stage::Carrier || record.stage == nr_timing::Stage::Execute ||
        record.stage == nr_timing::Stage::Signal || record.stage == nr_timing::Stage::Readback ||
        !record.clock_known || static_cast<long double>(record.duration_ticks) * 1000.0L >= frequency;
    if (retain) {
        if (g_nr_timing_sequence != UINT64_MAX) ++g_nr_timing_sequence;
        else g_nr_timing.counter_overflow = true;
        record.sequence = g_nr_timing_sequence;
        g_nr_timing.recent[g_nr_timing_recent_next] = record;
        g_nr_timing_recent_next = (g_nr_timing_recent_next + 1) % nr_timing::kRecentCount;
        if (g_nr_timing.recent_count < nr_timing::kRecentCount) ++g_nr_timing.recent_count;
        else NrTimingIncrement(g_nr_timing.recent_overwritten);
    }
    ReleaseSRWLockExclusive(&g_nr_timing_lock);
}
void SnapshotNrTiming(nr_timing::Snapshot& output) {
    AcquireSRWLockShared(&g_nr_timing_lock);
    output = g_nr_timing;
    output.qpc_frequency = NrTimingFrequency();
    output.captured_ticks = NrTimingCounter();
    output.clock_available = output.qpc_frequency && output.epoch_started_ticks &&
        output.captured_ticks >= output.epoch_started_ticks;
    const auto first = g_nr_timing.recent_count == nr_timing::kRecentCount ? g_nr_timing_recent_next : 0u;
    for (unsigned i = 0; i < g_nr_timing.recent_count; ++i)
        output.recent[i] = g_nr_timing.recent[(first + i) % nr_timing::kRecentCount];
    ReleaseSRWLockShared(&g_nr_timing_lock);
}
void NrTimingCarrierScope::Begin(std::uint64_t frame, std::uint64_t feature,
        std::uint64_t config, unsigned mode, std::uint64_t device) {
    parent = g_nr_timing_carrier;
    identity = NrTimingNewIdentity();
    identity.frame = frame; identity.feature = feature; identity.config = config;
    identity.mode = mode; identity.device = device; identity.resource = identity.resource_generation = 0;
    identity.target = identity.target_generation = 0; identity.target_subresource = 0;
    detail.flags = nr_timing::CarrierKnown;
    started = NrTimingCounter(); g_nr_timing_carrier = this;
}
NrTimingCarrierScope::~NrTimingCarrierScope() {
    if (g_nr_timing_carrier != this) return;
    const auto finished = NrTimingCounter();
    detail.flags |= nr_timing::CarrierFinished;
    nr_timing::Record record{};
    record.identity = identity; record.detail = detail; record.readback_origin = readback_origin;
    record.stage = nr_timing::Stage::Carrier; record.started_ticks = started;
    record.clock_known = started && finished >= started;
    record.duration_ticks = record.clock_known ? finished - started : 0;
    NrTimingRecord(record); g_nr_timing_carrier = parent;
}
struct NrTimingScope {
    nr_timing::Record record{};
    NrTimingCarrierScope* carrier = nullptr;
    bool stopped = false;
    explicit NrTimingScope(nr_timing::Stage stage) : carrier(g_nr_timing_carrier) {
        record.identity = NrTimingCurrentIdentity(); record.stage = stage;
        record.started_ticks = NrTimingCounter();
    }
    void Stop() {
        if (stopped) return;
        stopped = true; const auto finished = NrTimingCounter();
        if (carrier && carrier == g_nr_timing_carrier) {
            record.detail.flags |= carrier->detail.flags;
            record.detail.reset_reasons |= carrier->detail.reset_reasons;
            record.detail.bypass = carrier->detail.bypass;
            record.detail.nr_result = carrier->detail.nr_result;
            record.detail.native_result = carrier->detail.native_result;
            record.detail.first_nr_result = carrier->detail.first_nr_result;
            record.detail.first_native_result = carrier->detail.first_native_result;
            record.detail.nr_calls = carrier->detail.nr_calls;
            record.detail.native_calls = carrier->detail.native_calls;
            if (!record.identity.resource) record.detail.flags &= ~nr_timing::ResourceKnown;
            if (!record.identity.target) record.detail.flags &= ~nr_timing::TargetKnown;
        }
        record.clock_known = record.started_ticks && finished >= record.started_ticks;
        record.duration_ticks = record.clock_known ? finished - record.started_ticks : 0;
        NrTimingRecord(record);
    }
    ~NrTimingScope() { Stop(); }
};
void NrTimingEnterChainLock(CRITICAL_SECTION* lock) {
    NrTimingScope timing(nr_timing::Stage::ChainLock);
    EnterCriticalSection(lock);
    timing.Stop();
}
void NrTimingSetRoute(unsigned mode, std::uint64_t config) {
    if (auto* frame = g_nr_timing_carrier) {
        frame->identity.mode = mode;
        frame->identity.config = config;
    }
}
void NrTimingSetResource(std::uint64_t device, std::uint64_t resource, std::uint64_t generation,
                         unsigned role, std::uint64_t config) {
    if (auto* frame = g_nr_timing_carrier) {
        frame->identity.device = device; frame->identity.resource = resource;
        frame->identity.resource_generation = generation;
        frame->identity.role = role; frame->identity.config = config;
        frame->identity.target = frame->identity.target_generation = 0;
        frame->detail.flags &= ~nr_timing::TargetKnown;
        if (resource && generation) frame->detail.flags |= nr_timing::ResourceKnown;
        else frame->detail.flags &= ~nr_timing::ResourceKnown;
    }
}
void NrTimingSetTarget(std::uint64_t target, std::uint64_t generation, unsigned subresource) {
    if (auto* frame = g_nr_timing_carrier) {
        frame->identity.target = target; frame->identity.target_generation = generation;
        frame->identity.target_subresource = subresource;
        if (target && generation) frame->detail.flags |= nr_timing::TargetKnown;
        else frame->detail.flags &= ~nr_timing::TargetKnown;
    }
}
void NrTimingBypass(unsigned reason) { if (g_nr_timing_carrier) g_nr_timing_carrier->detail.bypass = reason; }
void NrTimingCommandObserved() { if (g_nr_timing_carrier) g_nr_timing_carrier->detail.flags |= nr_timing::CommandObserved; }
void NrTimingNativeResult(std::int32_t result) {
    if (auto* frame = g_nr_timing_carrier) {
        if (!(frame->detail.flags & nr_timing::NativeReturned)) frame->detail.first_native_result = result;
        frame->detail.flags |= nr_timing::NativeReturned; frame->detail.native_result = result;
        if (frame->detail.native_calls != UINT_MAX) ++frame->detail.native_calls;
    }
}
void NrTimingNrResult(std::int32_t result) {
    if (auto* frame = g_nr_timing_carrier) {
        if (!(frame->detail.flags & nr_timing::NrReturned)) frame->detail.first_nr_result = result;
        frame->detail.flags |= nr_timing::NrReturned; frame->detail.nr_result = result;
        if (frame->detail.nr_calls != UINT_MAX) ++frame->detail.nr_calls;
    }
}
template <class Callback>
auto NrTimingCallNr(Callback callback) {
    NrTimingScope timing(nr_timing::Stage::NrEvaluate);
    const auto result = callback();
    NrTimingNrResult(static_cast<std::int32_t>(result));
    timing.Stop();
    return result;
}
void NrTimingResetBound(bool requested, unsigned reasons) {
    if (auto* frame = g_nr_timing_carrier) {
        frame->detail.flags |= nr_timing::ResetBound;
        frame->detail.reset_reasons = requested ? reasons : 0;
    }
}
nr_timing::Identity NrTimingProbeDispatched() {
    if (g_nr_timing_carrier) g_nr_timing_carrier->detail.flags |= nr_timing::ProbeDispatched;
    return NrTimingCurrentIdentity();
}
nr_timing::Identity NrTimingDiagnosticRecorded() {
    if (g_nr_timing_carrier) g_nr_timing_carrier->detail.flags |= nr_timing::DeltaStatsRecorded;
    return NrTimingCurrentIdentity();
}
void NrTimingOutputTransitionsRecorded() {
    if (g_nr_timing_carrier) g_nr_timing_carrier->detail.flags |= nr_timing::OutputTransitionsOnly;
}
void NrTimingReadbackOrigin(const nr_timing::Identity& origin, bool mapped, bool exposure_pending) {
    if (auto* frame = g_nr_timing_carrier) {
        if (frame->readback_origin.frame && (frame->readback_origin.frame != origin.frame ||
            frame->readback_origin.epoch != origin.epoch || frame->readback_origin.resource != origin.resource ||
            frame->readback_origin.target != origin.target))
            frame->detail.flags |= nr_timing::MixedReadbackOrigins;
        frame->readback_origin = origin;
        if (origin.frame) frame->detail.flags |= nr_timing::ReadbackOriginKnown;
        if (mapped) frame->detail.flags |= nr_timing::ReadbackMapped;
        if (exposure_pending) frame->detail.flags |= nr_timing::ExposureResetPending;
    }
}
