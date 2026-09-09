// Included after the complete, canonically assembled production Core TU.
// Only fixture values and the Win32 clock fault are synthetic. Every snapshot,
// activity writer, sanitizer and final text writer below is production code.
#if !NR_HAS_TIMING || !NR_HAS_PROBE_POLICY || !NR_HAS_RUNTIME_LOAD_EVIDENCE
#error This fixture requires the assembled Core timing/probe/runtime writers.
#endif

#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace feedback_timing_test {
unsigned checks = 0, failures = 0;
void Check(bool value, const char *label) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %s\n", label); }
}
std::string Read(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void Write(const std::filesystem::path &path, const std::string &text) {
    std::ofstream file(path, std::ios::binary);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    Check(file.good(), "FULL_CORE_F8_ARTIFACT_WRITE");
}
std::string Value(const std::string &text, const std::string &key) {
    const auto marker = key + "=";
    auto begin = text.find(marker);
    while (begin != std::string::npos && begin != 0 && text[begin - 1] != '\n')
        begin = text.find(marker, begin + 1);
    if (begin == std::string::npos) return {};
    begin += marker.size();
    const auto end = text.find('\n', begin);
    return text.substr(begin, end - begin);
}
bool Field(const std::string &line, const std::string &key, const std::string &value) {
    const auto match = key + ":" + value;
    const auto begin = line.find(match);
    if (begin == std::string::npos || (begin && line[begin - 1] != ',')) return false;
    return begin + match.size() == line.size() || line[begin + match.size()] == ',';
}
std::string U(std::uint64_t value) { return std::to_string(value); }

void SeedBoundedEvidence(nr_probe_policy::Snapshot probe) {
    constexpr auto wide = std::numeric_limits<unsigned long long>::max();
    constexpr auto word = std::numeric_limits<unsigned int>::max();
    g_config.enabled = true; g_config.mode = 2; g_config_generation = 7;
    g_enabled_fast = true; g_requested_mode_fast = 2;
    g_pipeline_stats_generation = g_pipeline_stats_epoch = 41;
    g_pipeline_stats_mode = 2;
    g_pipeline_feature18_success = g_pipeline_transfer_submitted = wide;
    g_queue_retirement_waits = g_queue_retirement_wait_completed = wide;
    g_queue_retirement_wait_timeouts = g_queue_retirement_wait_ms = g_queue_retirement_wait_max_ms = wide;

    g_runtime_load_evidence = {};
    g_runtime_load_evidence.available = true;
    g_runtime_load_evidence.stage = "constructed-capacity-record-no-runtime-loaded";
    g_runtime_load_evidence.search = "private-fixture-no-library-search";
    for (unsigned role = 0; role < 2; ++role) {
        auto &format = g_nr_format_diagnostics.latest[role]; format = {};
        format.available = format.coverage = true;
        format.physical = format.srv = format.uav = format.flags = word;
        format.width = format.height = format.arrays = format.mips = format.samples = word;
        format.config = word; format.feature = format.device = format.resource = format.observed_call = wide;
        format.status = nr_format_access::Status::AmbiguousType;
        format.provenance = nr_format_access::Provenance::ObservedView;
        format.observed.format = word;
        format.observed.count = static_cast<unsigned>(format.observed.formats.size());
        for (auto &value : format.observed.formats) value = word;
        auto &semantic = format.semantic;
        semantic.source = nr_resource_semantics::Source::ObservedNativeView;
        semantic.contract = nr_resource_semantics::Contract::HdrDeclared;
        semantic.verdict = nr_resource_semantics::Verdict::Ambiguous;
        semantic.reason = "native-view-lifetime-or-evaluate-invalid";
        semantic.missing_evidence = "successful-native-view-in-current-resource-and-feature-generation";
        semantic.next_action = "needs-code";
        semantic.access.srv = semantic.access.uav = word;
        semantic.candidate_count = static_cast<unsigned>(semantic.candidates.size());
        for (auto &candidate : semantic.candidates) {
            candidate.view = candidate.write_view = word;
            candidate.contract_possible = candidate.layout_legal = candidate.codec_implemented = true;
            candidate.capability = {true, std::numeric_limits<std::int32_t>::min(), true, true, true};
        }
        g_nr_format_diagnostics.first_unknown[role] = format;
    }
    auto &exposure = g_exposure_semantic_evidence; exposure = {};
    exposure.available = exposure.present = exposure.required = true;
    exposure.physical = exposure.flags = exposure.width = exposure.height = exposure.mips = exposure.samples = word;
    exposure.config = word; exposure.mode = 2;
    exposure.device = exposure.resource = exposure.feature = wide;
    exposure.access.view = static_cast<DXGI_FORMAT>(word);
    exposure.access.query_attempted = true; exposure.access.query_result = E_INVALIDARG;
    exposure.access.support1 = exposure.access.support2 = word;
    exposure.semantic = g_nr_format_diagnostics.latest[0].semantic;
    exposure.semantic.contract = nr_resource_semantics::Contract::ExposureScale;
    auto &state = g_command_state_evidence; state = {};
    state.available = state.captured = state.registered = state.hooks_ready = true;
    state.guard_requested = state.guard_covered = state.fresh_reset = state.tainted = true;
    state.meta_known = state.has_pipeline = state.has_ray_pipeline = state.null_baseline = true;
    state.roots[0] = state.roots[1] = state.heaps[0] = state.heaps[1] = true;
    state.reason = "command-state-unverified-after-native-recording-baseline";
    state.baseline = "Create-first-recording"; state.route = "native-carrier-transaction-restore";
    state.dirty = state.meta_init = state.meta_execute = state.vertex_mask = state.stream_mask = word;
    state.arguments[0] = state.arguments[1] = word;
    state.sequence = state.token = state.reset_serial = state.pipeline_writes = state.dynamic = wide;
    state.creation_serial = state.creation_tick = state.registered_tick = wide;
    state.reset_notifications = state.native_resets = state.reset_successes = state.captures = wide;
    state.creation_method = word;
    state.initial_pso = state.genuine_init_event = state.first_recording_observed = state.blend_observed = true;
    for (auto &value : state.blend) value = -std::numeric_limits<float>::max();
    g_command_history.count = kCommandHistoryLimit;
    for (auto &entry : g_command_history.entries) entry = state;
    g_command_history_dropped = wide;
    g_external_nr_handles = g_external_nr_creates = 0;
    g_external_nr_evaluations = wide;
    auto &life = g_ngx_lifecycle_evidence; life = {};
    life.frozen = true; life.events = wide;
    life.recent_count = static_cast<unsigned>(std::size(life.recent));
    life.fault_count = static_cast<unsigned>(std::size(life.first_fault));
    NgxLifecycleEvent event;
    event.sequence = event.call = event.parent = event.handle_token = event.generation = event.epoch = wide;
    event.layer = event.owner = event.operation = event.result = word;
    event.feature = std::numeric_limits<int>::min();
    for (auto &value : life.recent) value = event;
    for (auto &value : life.first_fault) value = event;

    g_post_store_evidence = {};
    auto &stored = g_post_store_evidence;
    stored.mode = 1; stored.frame = stored.feature_generation = wide; stored.config = 7;
    stored.samples = stored.expected_samples = stored.changed = stored.expected_changed = stored.invalid = word;
    stored.before_hash = stored.expected_hash = stored.actual_hash = word;
    stored.expected_mean = stored.actual_mean = stored.expected_max = stored.actual_max = std::numeric_limits<float>::max();
    stored.source_physical = stored.source_view = stored.result_physical = stored.result_view = stored.result_uav = word;
    stored.probe_policy = probe;
    stored.comparison_known = stored.matches_expected = false;
    stored.boundary_trace_id = wide;
    g_pre_store_evidence = stored; g_pre_store_evidence.mode = 2;
    nr_post_boundary::Window window;
    window.trace = stored.boundary_trace_id; window.frame = stored.frame;
    window.feature = stored.feature_generation; window.config = stored.config;
    window.origin_queue = window.origin_submission = wide; window.origin_submitted = true;
    window.close = nr_post_boundary::Close::Present;
    window.barriers = window.read_barriers = window.write_barriers = word;
    window.copy_reads = window.copy_writes = window.render_target_binds = word;
    window.first_before = window.first_after = window.last_after = word;
    window.coverage = window.exact_following = window.unknown_operations = word;
    window.count = nr_post_boundary::kFollowingEvents; window.native.count = nr_post_boundary::kNativeEvents;
    window.native.callbacks = window.native.matches = window.native.other_lists = word;
    window.native.first_before = window.native.last_after = word;
    window.native.exact_matches = window.native.exact_other_lists = window.native.exact_global = word;
    window.native.unknown_operations = window.native.coverage = word;
    nr_post_boundary::Event boundary;
    boundary.kind = nr_post_boundary::Kind::EnhancedTexture; boundary.source = nr_post_boundary::Source::Native;
    boundary.list.token = boundary.list.reset = boundary.order = boundary.queue = boundary.submission = wide;
    boundary.before = boundary.after = boundary.sync_before = boundary.sync_after = boundary.access_before = boundary.access_after = wide;
    boundary.flags = word;
    for (auto &value : boundary.subresources) value = word;
    boundary.list.known = boundary.target_known = boundary.reads = boundary.writes = boundary.submitted = true;
    for (auto &value : window.events) value = boundary;
    for (auto &value : window.native.events) value = boundary;
    g_post_boundary_current = {}; g_post_boundary_history[0] = window; g_post_boundary_history_next = 1;
    // Keep the production recovery ledger empty. Its actual unavailable proof
    // must be serialized; no fake SnapshotNrRecoveryDetails override is used.
    g_nr_recovery_snapshot = {};
    g_nr_recovery_snapshot.input.current = {true, 7, 41, 2, 2};
}

void SeedTiming(const nr_timing::Identity &old_origin, bool missing_clock) {
    const auto frequency = NrTimingFrequency();
    for (unsigned i = 0; i < nr_timing::kRecentCount + 25; ++i) {
        nr_timing::Record r;
        r.stage = static_cast<nr_timing::Stage>(i % nr_timing::kStageCount);
        r.identity = NrTimingCurrentIdentity();
        const auto wide = std::numeric_limits<std::uint64_t>::max() - i - 1024;
        auto &id = r.identity;
        id.frame = wide; id.feature = wide - 1; id.config = wide - 2;
        id.device = wide - 3; id.resource = wide - 4; id.resource_generation = wide - 5;
        id.target = wide - 6; id.target_generation = wide - 7; id.target_subresource = 65535;
        id.mode = 2; id.role = 1;
        r.readback_origin = old_origin;
        auto &d = r.detail;
        d.flags = 65535; d.reset_reasons = 127; d.bypass = 123456789;
        d.nr_result = static_cast<std::int32_t>(0x80000001u);
        d.native_result = static_cast<std::int32_t>(0x80000002u);
        d.first_nr_result = static_cast<std::int32_t>(0x80000003u);
        d.first_native_result = static_cast<std::int32_t>(0x80000004u);
        d.nr_calls = d.native_calls = std::numeric_limits<unsigned>::max();
        d.api_result = static_cast<std::int32_t>(0x80000005u);
        d.queue = wide - 8; d.fence_target = wide - 9; d.fence_before = wide - 10; d.fence_after = wide - 11;
        d.wait_method = 3; d.wait_fallback = 5; d.wait_registration = static_cast<std::int32_t>(0x80000006u);
        r.started_ticks = NrTimingCounter();
        r.duration_ticks = frequency ? frequency / 100 + 1 : 0;
        r.clock_known = !missing_clock;
        NrTimingRecord(r);
    }
    // Boundaries are deliberately stressed at full numeric width. These are
    // formatter fixtures, not a measurement of elapsed runtime or GPU work.
    AcquireSRWLockExclusive(&g_nr_timing_lock);
    auto &s = g_nr_timing;
    constexpr auto wide = std::numeric_limits<std::uint64_t>::max();
    s.observed = s.excluded_old_epoch = s.recent_overwritten = wide;
    s.scoped_records = wide - 1; s.unscoped_records = 1;
    s.aggregate_scope_available = s.mixed_scopes = true;
    s.aggregate_scope.config = wide - 2; s.aggregate_scope.feature = wide - 1;
    s.aggregate_scope.mode = 2; s.aggregate_scope.device = wide - 3;
    s.counter_overflow = false;
    if (!missing_clock && !s.epoch_started_ticks) s.epoch_started_ticks = NrTimingCounter();
    for (auto &stats : s.stages) {
        stats.count = wide;
        if (!missing_clock) {
            stats.measured = stats.total_ticks = stats.maximum_ticks = wide;
            for (auto &bucket : stats.histogram) bucket = wide / stats.histogram.size();
            stats.histogram.back() += wide % stats.histogram.size();
        }
    }
    ReleaseSRWLockExclusive(&g_nr_timing_lock);
}

void CheckTiming(const std::string &report, const char *prefix,
                 const nr_timing::Snapshot &expected, bool missing_clock) {
    const auto key = std::string(prefix) + ".timing.";
    Check(Value(report, key + "available") == "1", std::strcmp(prefix, "end") == 0
        ? "FULL_CORE_F8_TIMING_END" : "FULL_CORE_F8_TIMING_START");
    Check(Value(report, key + "probeMode") == U(expected.probe.mode) &&
          Value(report, key + "probePhase") == U(expected.probe.phase) &&
          Value(report, key + "waitMode") == U(expected.wait.mode) &&
          Value(report, key + "waitPhase") == U(expected.wait.phase), "FULL_CORE_F8_POLICY_IDENTITY");
    Check(Value(report, key + "epoch") == U(expected.epoch), "FULL_CORE_F8_TIMING_EPOCH");
    Check(Value(report, key + "clockAvailable") == (missing_clock ? "0" : "1"), "FULL_CORE_F8_CLOCK_AVAILABILITY");
    if (missing_clock)
        Check(Value(report, key + "qpcFrequency") == "0" && Value(report, key + "capturedTicks") == "0",
              "FULL_CORE_F8_MISSING_CLOCK_FIELDS");
    else Check(!Value(report, key + "epochStartedTicks").empty() &&
               Value(report, key + "epochStartedTicks") != "0", "FULL_CORE_F8_EPOCH_START");
    Check(Value(report, key + "aggregateScopeAvailable") == "1" && Value(report, key + "mixedScopes") == "1" &&
          Value(report, key + "scopedRecords") == U(expected.scoped_records) &&
          Value(report, key + "unscopedRecords") == "1", "FULL_CORE_F8_AGGREGATE_SCOPE_FLAGS");
    const auto scope = Value(report, key + "aggregateScope");
    Check(Field(scope, "config", U(expected.aggregate_scope.config)) &&
          Field(scope, "featureGeneration", U(expected.aggregate_scope.feature)) &&
          Field(scope, "mode", "2") && Field(scope, "device", U(expected.aggregate_scope.device)),
          "FULL_CORE_F8_AGGREGATE_SCOPE_IDENTITY");
    Check(Value(report, key + "recentExported") == "16" && Value(report, key + "recentAvailable") == "64",
          "FULL_CORE_F8_BOUNDED_RECENT_COUNT");
    Check(Value(report, key + "observed") == U(expected.observed) &&
          Value(report, key + "excludedOldEpoch") == U(expected.excluded_old_epoch) &&
          Value(report, key + "recentOverwritten") == U(expected.recent_overwritten), "FULL_CORE_F8_LARGE_COUNTERS");
    for (unsigned stage = 0; stage < nr_timing::kStageCount; ++stage) {
        const auto line = Value(report, key + nr_timing::Name(static_cast<nr_timing::Stage>(stage)));
        const auto &stats = expected.stages[stage];
        Check(Field(line, "count", U(stats.count)) && Field(line, "measured", U(stats.measured)) &&
              Field(line, "totalTicks", U(stats.total_ticks)) && Field(line, "maxTicks", U(stats.maximum_ticks)),
              "FULL_CORE_F8_STAGE_COUNTERS");
        const auto percentile = missing_clock ? "unknown" : "above-100000";
        Check(Field(line, "p95UpperUs", percentile) && Field(line, "p99UpperUs", percentile),
              missing_clock ? "FULL_CORE_F8_MISSING_CLOCK_PERCENTILE" : "FULL_CORE_F8_TAIL_PERCENTILE");
        std::string buckets;
        for (auto value : stats.histogram) { if (!buckets.empty()) buckets += ','; buckets += U(value); }
        Check(line.ends_with("histogram:" + buckets), "FULL_CORE_F8_WHOLE_HISTOGRAM_LINE");
    }
    for (unsigned i = 0; i < 16; ++i) {
        const auto &r = expected.recent[expected.recent_count - 16 + i];
        const auto &id = r.identity; const auto &d = r.detail; const auto &origin = r.readback_origin;
        const auto line = Value(report, key + "recent." + U(i));
        Check(Field(line, "sequence", U(r.sequence)) && Field(line, "stage", nr_timing::Name(r.stage)),
              "FULL_CORE_F8_LATEST_SIXTEEN_ORDER");
        Check(Field(line, "epoch", U(id.epoch)) && Field(line, "frame", U(id.frame)) &&
              Field(line, "featureGeneration", U(id.feature)) && Field(line, "config", U(id.config)) &&
              Field(line, "device", U(id.device)) && Field(line, "resource", U(id.resource)) &&
              Field(line, "resourceGeneration", U(id.resource_generation)), "FULL_CORE_F8_RECENT_IDENTITY");
        Check(Field(line, "target", U(id.target)) && Field(line, "targetGeneration", U(id.target_generation)) &&
              Field(line, "targetSubresource", U(id.target_subresource)), "FULL_CORE_F8_TARGET_IDENTITY");
        Check(Field(line, "firstNrResult", "0x80000003") && Field(line, "firstNativeResult", "0x80000004") &&
              Field(line, "nrCalls", U(d.nr_calls)) && Field(line, "nativeCalls", U(d.native_calls)) &&
              Field(line, "apiResult", "0x80000005") && Field(line, "flags", "0xFFFF"), "FULL_CORE_F8_API_RESULTS");
        Check(Field(line, "probe", U(id.probe.mode) + "/" + U(id.probe.phase)) &&
              Field(line, "wait", U(id.wait.mode) + "/" + U(id.wait.phase)), "FULL_CORE_F8_RECENT_POLICY");
        Check(Field(line, "readbackOrigin", U(origin.epoch) + "/" + U(origin.probe.phase) + "/" +
              U(origin.wait.phase) + "/" + U(origin.frame) + "/" + U(origin.feature) + "/" + U(origin.config) + "/" + U(origin.mode)),
              "FULL_CORE_F8_READBACK_ORIGIN");
    }
    Check(Value(report, key + "recent.16").empty(), "FULL_CORE_F8_RECENT_EXPORT_BOUND");
    Check(Value(report, key + "waitMethodKey").find("4:not-waited") != std::string::npos,
          "FULL_CORE_F8_WAIT_METHOD_FOUR_MEANING");
}

void FillTimeline(FeedbackSession &session) {
    const auto limit = sizeof(session.report) - nr_feedback_limits::kTimelineReserveBytes;
    Check(session.report_used < limit, "FULL_CORE_F8_START_FITS_TIMELINE_BUDGET");
    unsigned index = 0;
    while (session.report_used < limit) {
        const auto remaining = limit - session.report_used;
        const auto prefix = "fixture.timeline." + U(index++) + "=";
        if (remaining <= prefix.size()) {
            const std::string blank(remaining, '\n'); FeedbackAppend(session, "%s", blank.c_str());
        } else {
            const auto bytes = std::min<std::size_t>(remaining, 512);
            const auto line = prefix + std::string(bytes - prefix.size() - 1, 'x') + "\n";
            FeedbackAppend(session, "%s", line.c_str());
        }
        if (session.report_truncated) break;
    }
    Check(session.report_used == limit && !session.report_truncated, "FULL_CORE_F8_TIMELINE_BOUNDARY");
}

// Worker metadata is explicitly synthetic capacity data. The actual worker's
// file/module discovery is not executed. Its bounded 4 KiB module inventory,
// artifact identity, four runtime fingerprints and color fields fit within
// this deliberately larger 8 KiB envelope, including whole UTF-8 lines.
std::string BudgetLines(std::string text, std::size_t bytes, const char *label) {
    Check(text.size() <= bytes, "FULL_CORE_F8_BUDGET_FIXTURE_FITS");
    unsigned index = 0;
    while (text.size() < bytes) {
        const auto remaining = bytes - text.size();
        const auto prefix = std::string(label) + "." + U(index++) + "=";
        if (remaining <= prefix.size()) text.append(remaining, '\n');
        else {
            const auto line_bytes = std::min<std::size_t>(remaining, 512);
            text += prefix + std::string(line_bytes - prefix.size() - 1, 'x') + "\n";
        }
    }
    return text;
}

std::string WorkerEquivalent() {
    std::string body = "artifact.sourceCommit=" + std::string(40, 'a') +
        "\nartifact.sourceDirty=1\nartifact.addonSha256=" + std::string(64, 'b') +
        "\nartifact.identityStatus=synthetic-capacity-fixture\n";
    body += BudgetLines("module.0=合成容量测试.addon64\n", 4096, "module.fixture");
    body += "modules.truncated=1\nmodules.reason=name-byte-budget\n"
        "modules.contract=name inventory plus bounded loaded-file fingerprints; no module bytes or process addresses exported\n";
    for (unsigned i = 0; i < 4; ++i) {
        const auto prefix = "runtimeFile." + U(i) + ".";
        body += prefix + "name=synthetic-runtime-capacity.dll\n" + prefix + "loaded=1\n" +
            prefix + "version=65535.65535.65535.65535\n" + prefix + "source=synthetic-fixture\n" +
            prefix + "bytes=18446744073709551615\n" + prefix + "sha256=" + std::string(64, 'c') +
            "\n" + prefix + "status=synthetic-capacity-fixture\n";
    }
    body += "runtimeFiles.hashMeaning=synthetic formatter values; no module file was inspected or hashed\n"
        "runtimeFiles.route=synthetic worker-equivalent body for the complete-TU capacity oracle\n"
        "color.status=synthetic-capacity-fixture\ncolor.requested=-3.40282347e+38\ncolor.effective=-3.40282347e+38\n"
        "color.reason=synthetic-capacity-fixture\ncolor.contract=not a measured picture strength\n";
    return BudgetLines(body, 8192, "fixture.workerPadding");
}

int Run(const std::filesystem::path &directory, bool missing_clock) {
    std::filesystem::create_directories(directory);
    InitializeCriticalSection(&g_chain_cs);
    wcscpy_s(g_ini_path, (directory / L"fixture.ini").c_str());
    wcscpy_s(g_log_path, (directory / L"nr-before-sr.log").c_str());
    Write(g_ini_path, "[NRBeforeSR]\nJitterMode=0\nJitterPolicyVersion=1\n");
    Write(g_log_path, "00:00:00.000 CPU serialization fixture; no NGX, device creation or game execution.\n");
    const auto ini_before = Read(g_ini_path);
    auto session = std::make_unique<nr_core_feedback::CoreSession>();
    auto &activity = session->activity;
    session->snapshot_ok = true;
    wcscpy_s(session->directory, directory.c_str());
    wcscpy_s(session->exe, (directory / L"feedback-timing-fixture.exe").c_str());
    GetLocalTime(&session->local_time); GetSystemTime(&session->utc_time); session->serial = 1;
    activity.started = GetTickCount64();
    const auto start_probe = NrProbePolicySnapshot();
    Check(start_probe.mode == nr_probe_policy::Mode::Normal && start_probe.phase == 1,
          "FULL_CORE_F8_DEFAULT_PROBE_PHASE");
    SeedBoundedEvidence(start_probe);
    auto origin = NrTimingCurrentIdentity();
    origin.frame = 991; origin.feature = 992; origin.config = 993; origin.mode = 1;
    SeedTiming(origin, missing_clock);
    auto start = std::make_unique<nr_timing::Snapshot>(); SnapshotNrTiming(*start);
    FeedbackAppend(activity, "format=full-core-f8-timing-test\nfixture.language=时序诊断，暂停不改写历史。\n");
    FeedbackAppendActivity(activity, "start");
    const auto start_bytes = activity.report_used;
    CheckTiming(activity.report, "start", *start, missing_clock);
    Check(Value(activity.report, "start.store.pre.probePhaseCurrent") == "1" &&
          Value(activity.report, "start.store.post.probePhaseCurrent") == "1", "FULL_CORE_F8_INITIAL_PROBE_PHASE");
    FillTimeline(activity);
    const auto burst = BudgetLines("fixture.fullTimelineBurst=1\n", 4096, "fixture.burst");
    Check(burst.size() == nr_feedback_limits::kTimelineBurstBytes, "FULL_CORE_F8_MAX_TIMELINE_BURST");
    FeedbackAppend(activity, "%s", burst.c_str());
    const auto config = g_config_generation.load(); const auto history = g_history_generation.load();
    SetNrProbePolicy(nr_probe_policy::Mode::Paused);
    SetNrWaitComparison(1);
    const auto end_probe = NrProbePolicySnapshot(); const auto policy = NrTimingPolicySnapshot();
    Check(end_probe.phase > start_probe.phase && end_probe.mode == nr_probe_policy::Mode::Paused &&
          policy.probe.phase == end_probe.phase && policy.probe.mode == static_cast<unsigned>(end_probe.mode),
          "FULL_CORE_F8_UI_AND_TIMING_PHASE_MATCH");
    Check(g_config_generation == config && g_history_generation == history && Read(g_ini_path) == ini_before,
          "FULL_CORE_F8_DIAGNOSTIC_SWITCH_NO_CONFIG_CHANGE");
    SeedTiming(origin, missing_clock);
    auto end = std::make_unique<nr_timing::Snapshot>(); SnapshotNrTiming(*end);
    const auto worker_equivalent = WorkerEquivalent();
    Check(worker_equivalent.size() == 8192 && nr_bundle::ValidUtf8(worker_equivalent.c_str()),
          "FULL_CORE_F8_WORKER_EQUIVALENT_BUDGET");
    const auto append_end = [&](FeedbackSession &target) {
        FeedbackAppendActivity(target, "end");
        FeedbackAppendCommandHistory(target);
        FeedbackAppend(target, "%s", worker_equivalent.c_str());
        FeedbackAppend(target, "collection.elapsedMs=%llu\ncollection.parameterSamples=0\ncollection.cancelled=0\n"
            "collection.metadataTruncated=%d\nfixture.metadataEnd=完整结尾\n",
            GetTickCount64() - activity.started, target.report_truncated ? 1 : 0);
    };
    auto mandatory_tail = std::make_unique<FeedbackSession>();
    append_end(*mandatory_tail);
    Write(directory / "mandatory-tail.txt", std::string(mandatory_tail->report, mandatory_tail->report_used));
    std::printf("FULL_CORE_F8_TAIL startBytes=%zu requiredBytes=%zu configuredReserve=%u tailTruncated=%d\n",
        start_bytes, mandatory_tail->report_used, nr_feedback_limits::kTimelineReserveBytes, mandatory_tail->report_truncated);
    append_end(activity);
    Check(!activity.report_truncated && activity.report_used < sizeof(activity.report) - nr_feedback_limits::kFinalStatusReserveBytes,
          "FULL_CORE_F8_CAPACITY");
    CheckTiming(activity.report, "end", *end, missing_clock);
    Check(Value(activity.report, "end.probeControl.mode") == "paused" &&
          Value(activity.report, "end.probeControl.phase") == U(end_probe.phase), "FULL_CORE_F8_PROBE_CONTROL_END");
    for (const char *route : {"pre", "post"}) {
        const auto key = std::string("end.store.") + route;
        Check(Value(activity.report, key + ".probePhase") == U(start_probe.phase) &&
              Value(activity.report, key + ".probeMode") == "normal" &&
              Value(activity.report, key + ".probePhaseCurrent") == "0", "FULL_CORE_F8_OLD_PROBE_PHASE");
    }
    Check(!Value(activity.report, "end.decision.stage").empty() &&
          !Value(activity.report, "decision.nextAction").empty(), "FULL_CORE_F8_END_DECISION");
    Check(Value(activity.report, "fixture.metadataEnd") == "完整结尾" && nr_bundle::ValidUtf8(activity.report),
          "FULL_CORE_F8_METADATA_UTF8_TAIL");
    Write(directory / "metadata.txt", std::string(activity.report, activity.report_used));
    const auto text = nr_core_feedback::CollectSingleText(*session, !activity.report_truncated);
    Check(text.text && text.complete && text.error == ERROR_SUCCESS && !text.path.empty(), "FULL_CORE_F8_FINAL_TEXT_COMPLETE");
    std::size_t text_bytes = 0;
    if (text.text) {
        text_bytes = text.text->size();
        Check(text.text->ends_with("text.complete=1\nEND DLSS5 FEEDBACK TEXT\n"), "FULL_CORE_F8_FINAL_STATUS_TAIL");
        Check(text.text->find("fixture.metadataEnd=完整结尾\n") != std::string::npos &&
              text.text->find("end.timing.recent.15=") != std::string::npos &&
              nr_bundle::ValidUtf8(text.text->c_str()), "FULL_CORE_F8_SERIALIZED_METADATA_COMPLETE");
        Check(Read(text.path) == *text.text, "FULL_CORE_F8_ATOMIC_TEXT_BYTES");
        const auto path_utf8 = std::filesystem::path(text.path).u8string();
        Write(directory / "final-text-path.txt", std::string(reinterpret_cast<const char *>(path_utf8.data()), path_utf8.size()));
    }
    std::printf("FULL_CORE_F8_TIMING checks=%u failures=%u metadataBytes=%zu metadataCapacity=%zu textBytes=%zu missingClock=%d\n",
                checks, failures, activity.report_used, sizeof(activity.report), text_bytes, missing_clock);
    DeleteCriticalSection(&g_chain_cs);
    return failures ? 41 : 0;
}
} // namespace feedback_timing_test

int wmain(int argc, wchar_t **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    g_feedback_fixture_missing_clock = std::wcscmp(argv[2], L"missing-clock") == 0;
    return feedback_timing_test::Run(argv[1], g_feedback_fixture_missing_clock);
}
