// Appended to the actual core collector harness by validate-feedback-combined.py.
// Every serializer/snapshot body is production code. These records deliberately
// stress storage capacities and numeric widths; they are not GPU observations.
void PopulateCombinedEvidence() {
    constexpr auto wide = std::numeric_limits<unsigned long long>::max();
    constexpr auto word = std::numeric_limits<unsigned int>::max();
    g_config_generation = 7;
    g_pipeline_stats_generation = g_pipeline_stats_epoch = wide;
    g_pipeline_stats_mode = 2;
    g_pipeline_feature18_success = g_pipeline_transfer_submitted = wide;
    g_queue_affinity_admitted_frames = g_queue_affinity_unknown_bypass_frames = wide;
    g_queue_affinity_prediction_bypass_frames = g_queue_affinity_actual_mismatches = wide;
    g_queue_affinity_marker_failures = g_queue_retired_admitted_frames = wide;
    g_queue_retirement_waits = g_queue_retirement_wait_completed = wide;
    g_queue_retirement_wait_timeouts = g_queue_retirement_wait_ms = g_queue_retirement_wait_max_ms = wide;
    g_command_create_calls = g_command_create_successes = wide;
    g_command_create1_calls = g_command_create1_successes = wide;
    g_command_create_receipt_failures = g_command_creation_publication_failures = wide;
    g_command_init_events = g_command_destroy_events = wide;
    g_command_native_reset_events = g_command_native_reset_failures = wide;
    g_command_reset_without_record = g_command_reset_identity_mismatches = wide;
    g_command_history_dropped = wide;
    g_external_nr_handles = g_external_nr_creates = word;
    g_external_nr_evaluations = wide;

    g_runtime_load_evidence = {};
    auto &load = g_runtime_load_evidence;
    load.available = true;
    load.stage = "private-runtime-initialization-evidence-capacity-fixture";
    load.search = "addon-directory-restricted-dependency-search";
    for (unsigned i = 0; i != 3; ++i) {
        load.loaded[i] = load.present[i] = true;
        load.errors[i] = word;
        load.classes[i] = 3;
    }
    load.missing_exports = load.init_result = word;

    g_nr_format_diagnostics = {};
    for (unsigned role = 0; role != 2; ++role) {
        auto &format = g_nr_format_diagnostics.latest[role];
        format.available = format.coverage = true;
        format.physical = format.srv = format.uav = format.flags = word;
        format.width = format.height = format.arrays = format.mips = format.samples = word;
        format.config = word;
        format.feature = format.device = format.resource = format.observed_call = wide;
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
        // Exercise every storage slot, including the reserve slot beyond today's
        // largest five-view family. This tests the actual serializer's full bound.
        semantic.candidate_count = static_cast<unsigned>(semantic.candidates.size());
        for (auto &candidate : semantic.candidates) {
            candidate.view = candidate.write_view = word;
            candidate.contract_possible = candidate.layout_legal = candidate.codec_implemented = true;
            candidate.capability = {true, std::numeric_limits<std::int32_t>::min(), true, true, true};
        }
        g_nr_format_diagnostics.first_unknown[role] = format;
    }

    auto &exposure = g_exposure_semantic_evidence;
    exposure = {};
    exposure.available = exposure.present = exposure.required = true;
    exposure.physical = exposure.flags = exposure.width = exposure.height = word;
    exposure.mips = exposure.samples = exposure.config = word;
    exposure.mode = std::numeric_limits<int>::min();
    exposure.device = exposure.resource = exposure.feature = wide;
    exposure.access.view = static_cast<DXGI_FORMAT>(word);
    exposure.access.query_attempted = true;
    exposure.access.query_result = E_INVALIDARG;
    exposure.access.support1 = exposure.access.support2 = word;
    exposure.semantic = g_nr_format_diagnostics.latest[0].semantic;
    exposure.semantic.contract = nr_resource_semantics::Contract::ExposureScale;

    auto &state = g_command_state_evidence;
    state = {};
    state.available = state.captured = state.registered = state.hooks_ready = true;
    state.guard_requested = state.guard_covered = state.fresh_reset = state.tainted = true;
    state.meta_known = state.has_pipeline = state.has_ray_pipeline = state.null_baseline = true;
    state.roots[0] = state.roots[1] = state.heaps[0] = state.heaps[1] = true;
    state.reason = "command-state-unverified-after-native-recording-baseline";
    state.baseline = "Create-first-recording";
    state.route = "native-carrier-transaction-restore";
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

    auto &hook = g_ngx_hook_failure;
    hook = {};
    hook.available = true;
    std::memset(hook.operation, 'x', sizeof(hook.operation) - 1);
    hook.status = std::numeric_limits<int>::min();
    hook.entry = "existing-short-hotpatch-with-unsupported-trampoline-shape";

    auto &life = g_ngx_lifecycle_evidence;
    life = {};
    life.frozen = true;
    life.events = wide;
    life.recent_count = static_cast<unsigned>(std::size(life.recent));
    life.fault_count = static_cast<unsigned>(std::size(life.first_fault));
    NgxLifecycleEvent event;
    event.sequence = event.call = event.parent = event.handle_token = event.generation = event.epoch = wide;
    event.layer = event.owner = event.operation = event.result = word;
    event.feature = std::numeric_limits<int>::min();
    for (auto &value : life.recent) value = event;
    for (auto &value : life.first_fault) value = event;

    auto &stored = g_post_store_evidence;
    stored = {};
    stored.mode = 1;
    stored.frame = stored.feature_generation = wide;
    stored.boundary_trace_id = wide;
    stored.config = 7;
    stored.samples = stored.expected_samples = stored.changed = stored.expected_changed = stored.invalid = word;
    stored.before_hash = stored.expected_hash = stored.actual_hash = word;
    stored.expected_mean = stored.actual_mean = stored.expected_max = stored.actual_max = -std::numeric_limits<float>::max();
    stored.source_physical = stored.source_view = stored.result_physical = stored.result_view = stored.result_uav = word;
    stored.actual_nonfinite = stored.reference_nonfinite = true;
    stored.comparison_known = stored.matches_expected = false;
    nr_post_boundary::Window window;
    window.trace = stored.boundary_trace_id;
    window.frame = stored.frame;
    window.feature = stored.feature_generation;
    window.config = stored.config;
    window.origin_queue = window.origin_submission = wide;
    window.origin_submitted = true;
    window.close = nr_post_boundary::Close::Present;
    window.barriers = window.read_barriers = window.write_barriers = word;
    window.copy_reads = window.copy_writes = window.render_target_binds = word;
    window.first_before = window.first_after = window.last_after = word;
    window.coverage = window.exact_following = window.unknown_operations = word;
    window.count = nr_post_boundary::kFollowingEvents;
    window.native.count = nr_post_boundary::kNativeEvents;
    window.native.callbacks = window.native.matches = window.native.other_lists = word;
    window.native.first_before = window.native.last_after = word;
    window.native.exact_matches = window.native.exact_other_lists = window.native.exact_global = word;
    window.native.unknown_operations = window.native.coverage = word;
    nr_post_boundary::Event full;
    full.kind = nr_post_boundary::Kind::EnhancedTexture;
    full.source = nr_post_boundary::Source::Native;
    full.list.token = full.list.reset = full.order = full.queue = full.submission = wide;
    full.before = full.after = full.sync_before = full.sync_after = full.access_before = full.access_after = wide;
    full.flags = word;
    for (auto &value : full.subresources) value = word;
    full.list.known = full.target_known = full.reads = full.writes = full.submitted = true;
    for (auto &value : window.events) value = full;
    for (auto &value : window.native.events) value = full;
    g_post_boundary_current = {};
    g_post_boundary_history[0] = window;
    g_post_boundary_history_next = 1;

    using namespace nr_compatibility_verdict;
    fixture_decision = {};
    auto &decision = fixture_decision;
    decision.current = {true, 7, wide, 2, 2};
    decision.first_block = {true, true, decision.current, "未知资源语义：等待当前载体证明", false};
    decision.resource.available = decision.resource.definitive = true;
    decision.resource.scope = decision.current;
    decision.resource.state = ResourceState::UnknownSemantic;
    decision.resource.reason = "unknown-resource-semantic";
    decision.resource.missing_evidence = "current-resource-native-SRV-or-UAV-format";
    decision.recovery.available = decision.recovery.attempted = true;
    decision.recovery.scope = decision.current;
    decision.recovery.state = RecoveryState::Succeeded; // Caller label alone cannot prove recovery.
    decision.recovery.attempt_id = decision.recovery.proof_attempt_id = wide;
    decision.recovery.correlated_window = decision.recovery.descriptor_verified = true;
    decision.recovery.nr_success_streak = decision.recovery.native_success_streak = word;
    decision.recovery.submitted_count = word;
    decision.recovery.writeback_verified = true;
    decision.recovery.first_failure = "unsupported-input-format";
    decision.recovery.first_failure_definitive = true;
    static const std::string invalid_reason = std::string("reported\ndecision.evidenceComplete=1") +
        std::string("\xC0\xAF\0", 3);
    decision.recovery.failure_reason = invalid_reason;
    decision.observer = {FactState::Incomplete, decision.current, "module-observer-uncertain", true};
    decision.command = {FactState::Incomplete, decision.current, "command-state-unverified", true};
    decision.queue = {FactState::Incomplete, decision.current, "queue-proof-unavailable", true};
    decision.visual = {FactState::Unobserved, {}, {}, false, 0};

    auto &details = fixture_recovery_details;
    details = {};
    details.current.input = decision;
    // A different receipt scope is intentional; it must be labelled explicitly
    // and must not supplement the independently captured decision with success.
    details.current.input.current.config = 9;
    details.current.key = {{wide, wide, 1, wide, 9, 2}, wide, wide, word, word};
    details.current.signature = {word, word, word, wide};
    details.current.frame = wide;
    details.current.receipt_overflow = true;
    auto &proof = details.proof;
    proof.state = nr_recovery::State::Latched;
    proof.semantic = nr_recovery::SemanticState::Unique;
    proof.attempt_id = proof.proof_attempt_id = proof.proof_frame_id = wide;
    proof.proof_carrier_ordinal = proof.proof_age = wide;
    proof.window_first_ordinal = proof.window_last_ordinal = wide;
    proof.window_first_frame_id = proof.window_last_frame_id = wide;
    proof.was_waiting = proof.correlated_window = proof.descriptor_verified = proof.writeback_verified = true;
    proof.capacity_unknown = proof.frame_evidence_lost = proof.first_failure_historical = true;
    proof.nr_success_streak = proof.native_success_streak = proof.submitted_count = word;
    proof.proof_target = {wide, wide, word};
    proof.first_failure.present = true;
    proof.first_failure.stage = nr_recovery::Stage::ActualStorageRead;
    proof.first_failure.result = static_cast<std::int32_t>(0x8007000Eu);
    proof.first_failure.kind = nr_recovery::FailureKind::WritebackMismatch;
    proof.first_failure.token = {details.current.key, details.current.signature, wide, {wide, wide}};
    proof.first_failure.token.key.scope.config_generation = 6;
    stored.recovery_token = {details.current.key, details.current.signature, wide, {wide, wide}};
    stored.recovery_token.key.scope.mode = 1;
    stored.recovery_token.key.role = 1;
    stored.recovery_target = {wide, wide, word};
    g_post_boundary_history[0].observation={stored.recovery_token.key.scope.device,stored.feature_generation,
        stored.recovery_target.resource,stored.probe_policy.phase,stored.config,1,
        static_cast<unsigned>(stored.probe_policy.mode),stored.recovery_target.subresource};
    g_post_boundary_history[0].provenance=nr_post_boundary::Provenance::StoreProbe;
    g_pre_store_evidence = stored;
    g_pre_store_evidence.mode = 2;
    g_pre_store_evidence.frame = wide - 1;
    g_pre_store_evidence.recovery_token.key.scope.mode = 2;
    g_pre_store_evidence.recovery_token.key.role = 0;
}

void CheckMetadataPrivacy() {
    using nr_feedback_text::SanitizeMetadata;
    const std::string chinese = "项目=中文元数据🙂";
    check(SanitizeMetadata(chinese) == chinese, "METADATA_UTF8_REQUIRED");
    const auto path = SanitizeMetadata("owner=C:\\Users\\Private Tester\\Game\\metadata.txt | 备注=中文");
    check(path.find("Private Tester") == std::string::npos && path.find("[local-path]") != std::string::npos &&
          path.find("备注=中文") != std::string::npos, "METADATA_PRIVACY_REQUIRED");
    const auto unc = SanitizeMetadata("share=\\\\private-server\\private-share\\data.txt | 备注=中文");
    check(unc.find("private-server") == std::string::npos && unc.find("[local-path]") != std::string::npos,
          "UTF-8 metadata retains UNC path redaction");
    check(SanitizeMetadata("状态=中文 password=fixture-secret") == "[sensitive log line omitted]",
          "UTF-8 metadata retains secret-line suppression");
    const auto address = SanitizeMetadata("token=0x123456789ABCDEF0 | HRESULT=0x8007000E | 备注=中文");
    check(address.find("123456789ABCDEF0") == std::string::npos &&
          address.find("HRESULT=0x8007000E") != std::string::npos,
          "metadata hides pointers while preserving observed HRESULTs");
    std::string invalid = "one=中\r\ntwo=文\t";
    invalid += std::string("\0\xC0\xAF\xED\xA0\x80", 6);
    invalid += "\xE2\x80\xA8\xC2\x85";
    const auto safe = SanitizeMetadata(invalid);
    check(safe.find('\n') == std::string::npos && safe.find('\r') == std::string::npos &&
          safe.find('\0') == std::string::npos && nr_bundle::ValidUtf8(safe.c_str()),
          "metadata controls, surrogates and invalid UTF-8 cannot produce additional lines");
    std::string oversized;
    for (unsigned n = 0; n != 2000; ++n) oversized += "边界🙂";
    const auto bounded = SanitizeMetadata(oversized);
    check(bounded.size() <= nr_feedback_text::kLineLimit && nr_bundle::ValidUtf8(bounded.c_str()) &&
          bounded.find("[truncated]") != std::string::npos,
          "metadata limit truncates only at complete UTF-8 scalar boundaries");
    std::string many_paths;
    for (unsigned n = 0; n != 1000; ++n) many_paths += "C:/ | 中文 ";
    const auto expanded = SanitizeMetadata(many_paths);
    check(expanded.size() <= nr_feedback_text::kLineLimit && nr_bundle::ValidUtf8(expanded.c_str()) &&
          expanded.find("C:/") == std::string::npos && expanded.find("[truncated]") != std::string::npos,
          "privacy marker expansion keeps the final metadata line within its UTF-8 byte limit");
    const auto abort = SanitizeMetadata("Another ReShade instance C:/Aborting initialization | " + oversized);
    check(abort.size() <= nr_feedback_text::kLineLimit && nr_bundle::ValidUtf8(abort.c_str()) &&
          abort.find("Aborting initialization") != std::string::npos,
          "retained abort markers also truncate Unicode metadata at whole scalar boundaries");
    check(nr_feedback_text::Sanitize("日志") == "??????",
          "raw game log ASCII normalization remains unchanged");
    const auto lines = nr_core_feedback::nr_feedback_report::SafeLines(
        "stage=中文\npath=C:\\Users\\Private Tester\\Game\\metadata.txt | end=中文\n");
    check(lines.find("stage=中文\n") != std::string::npos && lines.find("Private Tester") == std::string::npos,
          "the actual shared TXT metadata path preserves Unicode and privacy together");
}

void RunCombined(reshade::api::effect_runtime &runtime, bool archive) {
    PopulateCombinedEvidence();
    fixture_decision_snapshots = 0;
    fixture_recovery_detail_snapshots = 0;
    begin(runtime, archive);
    check(fixture_decision_snapshots == 1, "start Activity samples the Verdict exactly once");
    check(g_feedback_session != nullptr, "actual core Present publishes its live FeedbackSession");
    auto &session = *g_feedback_session;
    const auto capacity = sizeof(session.report);
    const auto start_bytes = session.report_used;
    // Fill the optional-metadata area up to the actual production timeline gate.
    // The payload is labelled capacity input, not fabricated runtime observations.
    const auto optional_gate = sizeof(session.report) - nr_feedback_limits::kTimelineReserveBytes;
    unsigned row = 0;
    while (session.report_used + 256 < optional_gate) {
        const std::string value = "容量输入：保留 UTF-8 完整行；" + std::string(155, 'x');
        FeedbackAppend(session, "fixture.optionalCapacity.%u=%s\n", row++, value.c_str());
    }
    const auto before_end = session.report_used;
    check(!session.report_truncated, "COMBINED_CAPACITY_TOO_SMALL: start snapshot and labelled optional capacity rows remain whole");
    // A configuration change during collection keeps the historical proof's
    // original scope. The main decision must come from the actual end snapshot.
    fixture_decision.current.config = 8;
    fixture_decision.first_block.scope = fixture_decision.current;
    drain();
    check(fixture_decision_snapshots == 2, "end Activity samples the Verdict exactly once");
    check(fixture_recovery_detail_snapshots == 2, "each Activity takes exactly one independently labelled recovery receipt");
    check(g_feedback_session == nullptr && !nr_core_feedback::Busy(), "actual worker retires the live session");
    check(nr_core_feedback::g_full_report != nullptr, "actual collector publishes a standalone UTF-8 TXT");
    const auto &report = *nr_core_feedback::g_full_report;
    check(report.find("collection.metadataTruncated=0\n") != std::string::npos,
          "COMBINED_CAPACITY_TOO_SMALL");
    check(report.find("end.decision.stage=unknown\n") != std::string::npos &&
          report.find("decision.scope.config=8\n") != std::string::npos,
          "COMBINED_END_VERDICT_REQUIRED");
    check(g_feedback_status == 3 && nr_core_feedback::g_report_complete,
          "all combined mandatory evidence fits and the actual report is complete");
    check(nr_bundle::ValidUtf8(report.c_str()), "actual report writer retains valid Chinese UTF-8");
    check(report.find("decision.recoveryResult=unavailable\n") != std::string::npos &&
          report.find("decision.evidenceComplete=0\n") != std::string::npos,
          "old scoped counters and caller success label cannot create a successful decision");
    check(report.find("end.recovery.receipt=scopeAvailable:1,scopeMatchesDecision:0,config:9,") != std::string::npos &&
          report.find("failureStage:actual-storage-read,failureResult:0x8007000E,failureKind:4,") != std::string::npos,
          "receipt scope and actual failure result survive without being merged into the decision");
    check(report.find("end.semantic.Output.candidate.5=") != std::string::npos &&
          report.find("end.semantic.ColorInput.candidate.5=") != std::string::npos,
          "both native format roles serialize every candidate storage slot");
    check(report.find("end.post.boundary.native.5=") != std::string::npos &&
          report.find("end.post.boundary.following.11=") != std::string::npos,
          "full native and following boundary arrays survive the combined collector");
    check(report.find("commandHistory.11.blend=") != std::string::npos &&
          report.find("ngxLifecycle.firstFault.15=") != std::string::npos &&
          report.find("ngxLifecycle.recent.15=") != std::string::npos,
          "full command and lifecycle histories survive after the end decision");
    check(report.find("artifact.identityStatus=") != std::string::npos &&
          report.find("modules.contract=") != std::string::npos &&
          report.find("color.contract=") != std::string::npos,
          "actual mandatory artifact, module and color tail follows all evidence writers");
    check(report.find("text.complete=1\nEND DLSS5 FEEDBACK TEXT\n") != std::string::npos,
          "the final status record and text terminator survive");
    std::ifstream disk(std::filesystem::path(nr_core_feedback::g_report_path), std::ios::binary);
    check(std::string((std::istreambuf_iterator<char>(disk)), {}) == report,
          "actual atomic on-disk TXT equals the published report bytes");
    const auto target = std::filesystem::path(output_root) / (archive ? "combined-paired.txt" : "combined-single.txt");
    std::filesystem::copy_file(nr_core_feedback::g_report_path, target);
    if (archive) save("combined.zip");
    else check(!g_feedback_last_zip[0], "single F8 produces its TXT without creating a ZIP");
    std::printf("Combined %s: live-capacity=%zu, start=%zu, before-end=%zu, text=%zu bytes; snapshots=%u\n",
                archive ? "paired-ZIP" : "single-TXT", capacity, start_bytes, before_end,
                report.size(), fixture_decision_snapshots);
}

void CheckUnknownReceipt() {
    fixture_decision = {};
    fixture_recovery_details = {};
    auto session = std::make_unique<FeedbackSession>();
    FeedbackAppendActivity(*session, "end");
    const std::string report = session->report;
    check(report.find("end.recovery.receipt=scopeAvailable:0,scopeMatchesDecision:0,config:unknown,") != std::string::npos,
          "unavailable recovery receipt has explicit unknown scope instead of zero");
    check(report.find("resource:unknown,resourceGeneration:unknown,subresource:unknown,role:unknown,carrierFrame:unknown,") != std::string::npos,
          "absent resource identity cannot masquerade as observed subresource zero");
    check(report.find("failureStage:none,failureResult:unknown,failureKind:unknown,") != std::string::npos,
          "unobserved failure never creates a successful zero HRESULT");
    check(report.find("proofFrame:unknown,proofAge:unknown,proofOrdinal:unknown,") != std::string::npos &&
          report.find("correlatedWindow:0,windowFirstFrame:unknown,") != std::string::npos,
          "missing receipt proof and correlation window remain unknown");
    check(report.find("decision.recoveryResult=unavailable\n") != std::string::npos && !session->report_truncated,
          "raw receipt serialization cannot promote an unavailable decision");
}

void CheckLatestStoreVersusHistoricalProof(reshade::api::effect_runtime &runtime) {
    PopulateCombinedEvidence();
    using namespace nr_compatibility_verdict;
    fixture_decision = {};
    auto &decision = fixture_decision;
    decision.current = {true, 7, 41, 2, 2};
    decision.resource.available = decision.resource.admitted = decision.resource.definitive = true;
    decision.resource.scope = decision.current;
    decision.resource.state = ResourceState::Ready;
    decision.resource.reason = "admitted";
    decision.resource.next_action = NextAction::None;
    decision.observer = {FactState::Complete, decision.current, {}, true};
    decision.command = decision.queue = decision.observer;
    auto &recovery = decision.recovery;
    recovery.available = recovery.attempted = recovery.correlated_window = true;
    recovery.scope = decision.current;
    recovery.state = RecoveryState::Succeeded;
    recovery.attempt_id = recovery.proof_attempt_id = 17;
    recovery.descriptor_verified = recovery.writeback_verified = true;
    recovery.nr_success_streak = recovery.native_success_streak = recovery.submitted_count = 3;
    recovery.first_failure = "unsupported-input-format";
    recovery.first_failure_definitive = true;

    auto &pre = g_pre_store_evidence;
    pre = {};
    pre.mode = 2; pre.config = 7; pre.feature_generation = 41; pre.frame = 102;
    pre.source_physical = pre.source_view = pre.result_physical = pre.result_view = pre.result_uav = 10;
    pre.samples = 2040; pre.expected_samples = 0; pre.invalid = 1;
    pre.reference_nonfinite = true;
    pre.recovery_token = {{{31, 1, 1, 41, 7, 2}, 701, 701, 0, 0}, {10, 10, 1, 1}, 17, {502, 4}};
    pre.recovery_target = {1001, 1001, 0};
    pre.before_hash = 0x13579BDFu; pre.actual_hash = 0x2468ACE0u;
    auto &post = g_post_store_evidence;
    post = pre;
    post.mode = 1; post.config = 6; post.feature_generation = 40; post.frame = 103;
    post.recovery_token = {{{31, 1, 1, 40, 6, 1}, 2001, 2001, 0, 1}, {10, 10, 1, 1}, 18, {602, 9}};
    post.recovery_target = {2001, 2001, 0};

    auto &details = fixture_recovery_details;
    details = {};
    details.current.input = decision;
    details.current.key = pre.recovery_token.key;
    details.current.signature = pre.recovery_token.signature;
    details.current.frame = 502;
    details.proof.state = nr_recovery::State::RecoverySucceeded;
    details.proof.attempt_id = details.proof.proof_attempt_id = 17;
    details.proof.proof_frame_id = 501; details.proof.proof_age = 1;
    details.proof.proof_carrier_ordinal = 3;
    details.proof.window_first_frame_id = 499; details.proof.window_last_frame_id = 501;
    details.proof.window_first_ordinal = 1; details.proof.window_last_ordinal = 3;
    details.proof.correlated_window = details.proof.descriptor_verified = details.proof.writeback_verified = true;
    details.proof.nr_success_streak = details.proof.native_success_streak = details.proof.submitted_count = 3;
    details.proof.proof_target = pre.recovery_target;
    fixture_decision_snapshots = fixture_recovery_detail_snapshots = 0;
    begin(runtime, false);
    drain();
    check(fixture_decision_snapshots == 2 && fixture_recovery_detail_snapshots == 2,
          "latest-store capture still samples each independent decision/receipt once per boundary");
    check(nr_core_feedback::g_full_report != nullptr && g_feedback_status == 3,
          "latest unknown storage evidence is a complete collection, not a collection failure");
    const auto &report = *nr_core_feedback::g_full_report;
    const auto line = [&](const char *prefix) {
        const auto start = report.find(prefix);
        check(start != std::string::npos, "latest store role is explicitly serialized");
        return report.substr(start, report.find('\n', start) - start);
    };
    const auto pre_line = line("end.store.pre=");
    const auto post_line = line("end.store.post=");
    check(pre_line.find("frame:102,") != std::string::npos && pre_line.find("carrierFrame:502,") != std::string::npos &&
          pre_line.find("samples:2040,expected:0,invalid:1,actualNonfinite:0,referenceNonfinite:1,known:0,match:unknown,") != std::string::npos,
          "LATEST_UNKNOWN_MUST_REMAIN_UNKNOWN");
    check(pre_line.find("scopeMatchesDecision:1,attemptMatchesDecision:1,") != std::string::npos &&
          pre_line.find("actualTarget:1001,") != std::string::npos &&
          pre_line.find("expectedHash:unknown,") != std::string::npos && pre_line.find("expectedMean:unknown,") != std::string::npos,
          "PRE retains its frozen scope/target and does not invent a missing expected comparison");
    check(post_line.find("frame:103,") != std::string::npos && post_line.find("carrierFrame:602,") != std::string::npos &&
          post_line.find("scopeMatchesDecision:0,attemptMatchesDecision:0,") != std::string::npos &&
          post_line.find("actualTarget:2001,") != std::string::npos && post_line.find("known:0,match:unknown,") != std::string::npos,
          "POST unknown sample keeps its separate historical configuration and actual target");
    check(report.find("proofFrame:501,proofAge:1,proofOrdinal:3,proofTarget:1001,") != std::string::npos &&
          report.find("state:recovery-succeeded,attemptId:17,proofAttemptId:17,") != std::string::npos,
          "historical proof keeps frame 501 while latest actual PRE sample has diagnostic frame 102 and carrier 502");
    check(report.find("decision.evidenceComplete=1\n") != std::string::npos &&
          report.find("decision.nextAction=needs-visual-F7\n") != std::string::npos &&
          report.find("decision.evidenceCompleteMeaning=enough evidence for the decision, not final visual acceptance\n") != std::string::npos,
          "enough evidence for the next action is explicitly distinct from final visual acceptance");
    std::filesystem::copy_file(nr_core_feedback::g_report_path, std::filesystem::path(output_root) / "latest-store.txt");

    g_post_store_evidence = {};
    auto absent = std::make_unique<FeedbackSession>();
    FeedbackAppendActivity(*absent, "end");
    const std::string missing = absent->report;
    check(missing.find("end.store.pre=available:1,") != std::string::npos &&
          missing.find("end.store.post=available:0,source:frozen-recording-and-readback-sample,mode:unknown,config:unknown,feature:unknown,frame:unknown,") != std::string::npos,
          "an absent POST sample does not hide the separately available PRE evidence");
    check(missing.find("samples:unknown,expected:unknown,invalid:unknown,actualNonfinite:unknown,referenceNonfinite:unknown,known:unknown,match:unknown,") != std::string::npos,
          "an absent sample emits unknown rather than zero statistics or mismatch");

    pre.frame = 104; pre.expected_samples = pre.samples; pre.invalid = 0;
    pre.actual_nonfinite = pre.reference_nonfinite = false;
    pre.comparison_known = pre.matches_expected = true;
    pre.expected_hash = pre.actual_hash;
    post = pre; post.mode = 1; post.frame = 105;
    post.matches_expected = false; post.actual_hash ^= 1u;
    auto comparable = std::make_unique<FeedbackSession>();
    FeedbackAppendActivity(*comparable, "end");
    const std::string known = comparable->report;
    const auto pre_start = known.find("end.store.pre=");
    const auto post_start = known.find("end.store.post=");
    check(pre_start != std::string::npos && post_start != std::string::npos,
          "both comparable frozen store roles remain explicit");
    check(known.substr(pre_start, known.find('\n', pre_start) - pre_start).find("known:1,match:1,") != std::string::npos &&
          known.substr(post_start, known.find('\n', post_start) - post_start).find("known:1,match:0,") != std::string::npos,
          "known comparison is distinct from whether the frozen sample actually matched");

    // DS1-shaped R10 collector values: complete current execution and 2040
    // actual samples, but only 1079 comparable expectations. No GPU result is
    // inferred: these are explicit inputs to the actual complete F8 pipeline.
    decision.current = {true, 7, 41, 1, 1};
    decision.resource.scope = decision.observer.scope = decision.command.scope =
        decision.queue.scope = recovery.scope = decision.current;
    recovery.state = RecoveryState::Pending;
    recovery.proof_attempt_id = 0; recovery.writeback_verified = false;
    post = {};
    post.mode = 1; post.config = 7; post.feature_generation = 41; post.frame = 106;
    post.source_physical = post.source_view = post.result_physical = post.result_view = post.result_uav = 24;
    post.samples = 2040; post.expected_samples = 1079; post.changed = 1893;
    post.recovery_token = {{{31, 1, 1, 41, 7, 1}, 2001, 2001, 0, 1}, {24, 24, 1, 1}, 17, {606, 12}};
    post.recovery_target = {2001, 2001, 0};
    details = {};
    details.current.input = decision;
    details.current.key = post.recovery_token.key;
    details.current.signature = post.recovery_token.signature;
    details.current.frame = 606;
    details.proof.state = nr_recovery::State::Pending;
    details.proof.attempt_id = 17;
    details.proof.correlated_window = details.proof.descriptor_verified = true;
    details.proof.nr_success_streak = details.proof.native_success_streak = details.proof.submitted_count = 3;
    begin(runtime, false); drain();
    check(nr_core_feedback::g_full_report != nullptr && g_feedback_status == 3,
          "partial comparison does not prevent complete F8 collection");
    const auto &partial = *nr_core_feedback::g_full_report;
    const auto partial_start = partial.find("end.store.post=");
    check(partial_start != std::string::npos &&
          partial.substr(partial_start, partial.find('\n', partial_start) - partial_start).find(
              "samples:2040,expected:1079,invalid:0,actualNonfinite:0,referenceNonfinite:0,known:0,match:unknown,") != std::string::npos,
          "partial R10 sample comparison is explicitly unknown despite changed actual samples");
    check(partial.find("end.decision.recovery.nrSuccessStreak=3\n") != std::string::npos &&
          partial.find("end.decision.recovery.nativeSuccessStreak=3\n") != std::string::npos &&
          partial.find("end.decision.recovery.submittedCount=3\n") != std::string::npos &&
          partial.find("end.decision.recovery.storageComparison=unknown\n") != std::string::npos &&
          partial.find("decision.reason=storage-comparison-unverified\n") != std::string::npos &&
          partial.find("decision.recoveryResult=pending\n") != std::string::npos &&
          partial.find("decision.nextAction=needs-visual-F7\n") == std::string::npos,
          "complete F8 preserves current execution and leaves storage/visual acceptance unproved");
    check(partial.find("final-consumer-and-present-verification") != std::string::npos &&
          partial.find("same-attempt-correlated-streak-and-submission-window") == std::string::npos,
          "remaining storage/consumer gap never hides already available execution");
    std::filesystem::copy_file(nr_core_feedback::g_report_path, std::filesystem::path(output_root) / "partial-storage.txt");
}

int main(int argc, char **argv) {
    check(argc == 2, "output directory supplied");
    output_root = std::filesystem::absolute(argv[1]).wstring();
    std::filesystem::create_directories(output_root);
    InitializeCriticalSection(&g_chain_cs);
    CheckMetadataPrivacy();
    const auto log = std::filesystem::path(output_root) / "nr-before-sr.log";
    wcscpy_s(g_log_path, log.c_str());
    put(log, "build=fixture\nNR bypass: reason=unknown-resource-semantic\n"
             "中文反馈：此处仅验证完整采集，不代表游戏或 GPU 恢复成功。\n");
    reshade::api::effect_runtime runtime;
    RunCombined(runtime, false);
    RunCombined(runtime, true);
    CheckLatestStoreVersusHistoricalProof(runtime);
    CheckUnknownReceipt();
    DeleteCriticalSection(&g_chain_cs);
    std::printf("PASS combined actual F8 pipeline: %u checks; snapshot storage inputs only; no GPU/provider/game calls\n", checks);
}
