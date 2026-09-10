// One explicitly requested session. Rendering only publishes small snapshots;
// file reads, hashing and ZIP writing run on a process-pinned worker.
#include "nr_feedback_limits.h"
std::atomic<bool> g_feedback_requested{false};
std::atomic<unsigned int> g_feedback_status{0}; // idle, collecting, packaging, complete, partial, failed
SRWLOCK g_feedback_lock = SRWLOCK_INIT;
struct FeedbackSession {
    ULONGLONG started = 0;
    unsigned long long stage_request = 0;
    unsigned int shots_requested = 0;
    bool stage_finished = false;
    bool stage_ok = false;
    char stage_reason[128] = {};
    char postfix[64] = {};
    wchar_t screenshots[3][1024] = {};
    ULONGLONG screenshot_ticks[3] = {};
    wchar_t stage_directory[1024] = {};
    char report[nr_feedback_limits::kMetadataBytes] = {};
    std::size_t report_used = 0;
    bool report_truncated = false;
    unsigned int observations = 0;
    unsigned int timeline_omitted = 0;
    ULONGLONG last_observation = 0;
    reshade::api::effect_runtime *runtime_identity = nullptr; // comparison only on worker
};
FeedbackSession *g_feedback_session = nullptr; // protected by g_feedback_lock
wchar_t g_feedback_last_zip[1024] = {};
std::atomic<unsigned int> g_feedback_swap_color_space{0};
std::atomic<unsigned int> g_feedback_swap_format{0};

void FeedbackAppend(FeedbackSession &s, const char *format, ...) {
    // Reserve a final status record. Reject an overflowing append as a whole:
    // partial UTF-8 metadata would make the ZIP job itself invalid.
    if (s.report_used >= sizeof(s.report) - nr_feedback_limits::kFinalStatusReserveBytes) {
        s.report_truncated = true; return;
    }
    const std::size_t available = sizeof(s.report) - s.report_used - nr_feedback_limits::kFinalStatusReserveBytes;
    va_list args;
    va_start(args, format);
    const int n = std::vsnprintf(s.report + s.report_used,
        available, format, args);
    va_end(args);
    if (n < 0 || static_cast<std::size_t>(n) >= available) {
        s.report[s.report_used] = 0;
        s.report_truncated = true;
    } else s.report_used += static_cast<std::size_t>(n);
}

#include "nr_compatibility_verdict_feedback.inl"
#if NR_HAS_TIMING
#include "nr_timing_feedback.inl"
#endif

const char *FeedbackStageBlockReason(const ChainConfig &config) {
    if (!config.enabled) return "NR disabled; current-chain screenshots and logs collected without enabling NR";
    if (ShouldGuardFeature1ForRayReconstruction())
        return "An active Ray Reconstruction transaction/create currently excludes legacy stage capture; cached handles alone do not block NR";
    if (OtherNrOwnerActive())
        return g_generic_renodx_conflict.load(std::memory_order_acquire)
            ? "renodx-dlss5.addon64 is also loaded; remove it and restart before capturing this addon's NR stages"
            : "another NR addon owns the path; this addon's NR stages are unavailable";
    return nullptr;
}

#if defined(NR_HAS_COMMAND_CREATION_EVIDENCE)
void FeedbackAppendCommandHistory(FeedbackSession &s) {
    FeedbackAppend(s, "commandHistory.schema=1\ncommandHistory.scope=historical distinct carrier samples; not a fresh snapshot or frame identity\n"
        "commandLifetime.createCalls=%llu\ncommandLifetime.createSuccess=%llu\ncommandLifetime.create1Calls=%llu\ncommandLifetime.create1Success=%llu\n"
        "commandLifetime.receiptFailures=%llu\ncommandLifetime.publicationFailures=%llu\n"
        "commandLifetime.initEvents=%llu\ncommandLifetime.destroyEvents=%llu\n"
        "commandLifetime.nativeResetEvents=%llu\ncommandLifetime.nativeResetFailures=%llu\n"
        "commandLifetime.resetWithoutRecord=%llu\ncommandLifetime.resetIdentityMismatch=%llu\n",
        g_command_create_calls.load(), g_command_create_successes.load(), g_command_create1_calls.load(), g_command_create1_successes.load(),
        g_command_create_receipt_failures.load(), g_command_creation_publication_failures.load(),
        g_command_init_events.load(), g_command_destroy_events.load(), g_command_native_reset_events.load(), g_command_native_reset_failures.load(),
        g_command_reset_without_record.load(), g_command_reset_identity_mismatches.load());
    auto history = std::unique_ptr<CommandEvidenceHistory>(new (std::nothrow) CommandEvidenceHistory);
    if (!history || !TryAcquireSRWLockShared(&g_command_history_lock)) {
        FeedbackAppend(s, "commandHistory.available=0\ncommandHistory.reason=allocation-or-snapshot-busy; counters-above-are-independent-atomics\n"); return;
    }
    *history = g_command_history;
    ReleaseSRWLockShared(&g_command_history_lock);
    const auto now = GetTickCount64();
    FeedbackAppend(s, "commandHistory.available=1\ncommandHistory.count=%u\ncommandHistory.dropped=%llu\n", history->count, g_command_history_dropped.load());
    for (unsigned i = 0; i < history->count; ++i) {
        const auto &e = history->entries[i];
        const char *next = e.captured ? "admitted-at-sample; inspect-runtime-and-submit-next" :
            e.creation_method == 1 && e.native_resets == 0 && e.genuine_init_event && e.pipeline_writes > 0 &&
            std::strcmp(e.reason, "awaiting-native-reset") == 0 ? "first-recording-candidate; prove-native-Create-baseline-before-admission" :
            e.native_resets && !e.reset_successes ? "inspect-native-Reset-result; failed-Reset-is-not-a-baseline" :
            std::strcmp(e.reason, "awaiting-native-reset") == 0 ? "inspect-create-receipt-and-native-reset-identity; do-not-remove-guard" :
            "inspect-recorded-reason-and-current-runtime-stage";
        FeedbackAppend(s, "commandHistory.%u=token:%llu,ageMs:%llu,captured:%d,reason:%s,baseline:%s,resetSerial:%llu,psoWrites:%llu,"
            "createMethod:%u,createSerial:%llu,createTick:%llu,registeredTick:%llu,initEvent:%d,initialPso:%d,"
            "resetNotifications:%llu,nativeResets:%llu,resetSuccess:%llu,captures:%llu,route:%s,dynamic:0x%llX,dirty:0x%X\n"
            "commandHistory.%u.next=%s\n",
            i, e.token, now >= e.sampled_at ? now - e.sampled_at : 0, e.captured ? 1 : 0, e.reason, e.baseline,
            e.reset_serial, e.pipeline_writes, e.creation_method, e.creation_serial, e.creation_tick, e.registered_tick,
            e.genuine_init_event ? 1 : 0, e.initial_pso ? 1 : 0, e.reset_notifications, e.native_resets, e.reset_successes,
            e.captures, e.route, e.dynamic, e.dirty, i, next);
        FeedbackAppend(s, "commandHistory.%u.firstRecordingObserved=%d\n", i, e.first_recording_observed ? 1 : 0);
        FeedbackAppend(s, "commandHistory.%u.blendObserved=%d\n", i, e.blend_observed ? 1 : 0);
        if (e.blend_observed)
            FeedbackAppend(s, "commandHistory.%u.blend=%.9g,%.9g,%.9g,%.9g\n",
                i, e.blend[0], e.blend[1], e.blend[2], e.blend[3]);
        else FeedbackAppend(s, "commandHistory.%u.blend=unobserved\n", i);
    }
    FeedbackAppend(s, "commandHistory.methodLegend=0:unknown,1:CreateCommandList-initially-open,2:CreateCommandList1-initially-closed\n"
        "commandHistory.warning=ReShade-reset-notification-is-not-proof-of-native-Reset; next-steps-are-hypotheses-not-proven-fixes\ncommandLifetime.coverage=optional observers attempt each implementation once; missing receipts and earlier unobserved calls remain unknown\n");
}
#endif

void FeedbackAppendNgxLifecycle(FeedbackSession &s) {
#if defined(NR_HAS_NGX_LIFECYCLE_EVIDENCE)
    const auto evidence = SnapshotNgxLifecycleEvidence();
    FeedbackAppend(s, "ngxLifecycle.schema=1\nngxLifecycle.externalHeld=%u\n"
        "ngxLifecycle.externalCreating=%u\nngxLifecycle.externalEvaluations=%llu\n"
        "ngxLifecycle.events=%llu\nngxLifecycle.firstFaultFrozen=%d\n"
        "ngxLifecycle.layers=0:unspecified,1:dlss,2:core,3:dlssd,4:private-bridge\n"
        "ngxLifecycle.owners=0:game,1:private-NR,2:external-NR,3:uncertain\n"
        "ngxLifecycle.operations=1:create-success,2:proven-nested-alias,3:create-failed,4:first-evaluate-or-unknown,5:released,6:release-entry,7:create-entry\n"
        "ngxLifecycle.contract=bounded process-local tokens; no addresses; absent Create is unknown, not proof of pre-observer timing\n",
        g_external_nr_handles.load(), g_external_nr_creates.load(),
        g_external_nr_evaluations.load(), evidence.events, evidence.frozen ? 1 : 0);
    for (unsigned int group = 0; group != 2; ++group) {
        const char *label = group ? "recent" : "firstFault";
        const auto *events = group ? evidence.recent : evidence.first_fault;
        const unsigned int count = group ? evidence.recent_count : evidence.fault_count;
        FeedbackAppend(s, "ngxLifecycle.%s.count=%u\n", label, count);
        for (unsigned int i = 0; i < count; ++i) {
            const auto &e = events[i];
            FeedbackAppend(s, "ngxLifecycle.%s.%u=seq:%llu,call:%llu,parent:%llu,handle:%llu,generation:%llu,epoch:%llu,layer:%u,owner:%u,op:%u,feature:%d,result:0x%X\n",
                label, i, e.sequence, e.call, e.parent, e.handle_token,
                e.generation, e.epoch, e.layer, e.owner, e.operation, e.feature, e.result);
        }
    }
#else
    (void)s;
#endif
}

void FeedbackAppendActivity(FeedbackSession &s, const char *prefix) {
    const auto decision = SnapshotNrCompatibilityVerdict();
    FeedbackAppendCompatibilityVerdict(s, prefix, decision);
#if defined(NR_HAS_RESOURCE_RECOVERY)
    const auto receipt = SnapshotNrRecoveryDetails();
    const auto &current = receipt.current;
    const auto &key = current.key;
    const auto &proof = receipt.proof;
    const auto &failure = proof.first_failure;
    const auto &failure_key = failure.token.key;
    const auto number = [](std::uint64_t value, bool known = true) {
        return known ? std::to_string(value) : std::string("unknown");
    };
    const auto identity = [&](std::uint64_t value) { return number(value, value != 0); };
    char failure_result[16] = "unknown";
    if (failure.present) std::snprintf(failure_result, sizeof(failure_result), "0x%08X",
                                      static_cast<unsigned>(failure.result));
    // This independently locked receipt may follow the decision snapshot by a
    // frame. Keep both scopes explicit; none of these raw fields change Resolve.
    FeedbackAppend(s, "%s.recovery.receipt=scopeAvailable:%d,scopeMatchesDecision:%d,"
        "config:%s,featureGeneration:%s,requestedMode:%s,effectiveMode:%s,"
        "keyConfig:%s,keyFeature:%s,keyFeatureGeneration:%s,keyMode:%s,"
        "device:%s,deviceGeneration:%s,resource:%s,resourceGeneration:%s,subresource:%s,role:%s,carrierFrame:%s,"
        "srv:%s,uav:%s,interpretationContract:%s,interpretationVersion:%s,state:%s,attemptId:%s,proofAttemptId:%s,"
        "proofFrame:%s,proofAge:%s,proofOrdinal:%s,proofTarget:%s,proofTargetGeneration:%s,proofTargetSubresource:%s,"
        "correlatedWindow:%d,windowFirstFrame:%s,windowLastFrame:%s,windowFirstOrdinal:%s,windowLastOrdinal:%s,"
        "failurePresent:%d,failureHistorical:%d,failureStage:%s,failureResult:%s,failureKind:%s,"
        "failureFrame:%s,failureOrdinal:%s,failureAttempt:%s,failureConfig:%s,failureFeatureGeneration:%s,"
        "failureDevice:%s,failureResource:%s,failureRole:%s,capacityUnknown:%d,frameLost:%d,receiptOverflow:%d\n",
        prefix, current.input.current.available,
        nr_compatibility_verdict::SameScope(current.input.current, decision.current),
        number(current.input.current.config, current.input.current.available && current.input.current.config).c_str(),
        number(current.input.current.feature_generation, current.input.current.available && current.input.current.feature_generation).c_str(),
        number(current.input.current.requested_mode, current.input.current.available && current.input.current.requested_mode).c_str(),
        number(current.input.current.effective_mode, current.input.current.available && current.input.current.effective_mode).c_str(),
        identity(key.scope.config_generation).c_str(), identity(key.scope.feature).c_str(),
        identity(key.scope.feature_generation).c_str(), identity(key.scope.mode).c_str(),
        identity(key.scope.device).c_str(), identity(key.scope.device_generation).c_str(),
        identity(key.resource).c_str(), identity(key.resource_generation).c_str(),
        number(key.subresource, key.resource != 0).c_str(), number(key.role, key.resource != 0).c_str(),
        identity(current.frame).c_str(), identity(current.signature.srv).c_str(), identity(current.signature.uav).c_str(),
        identity(current.signature.provenance).c_str(), identity(current.signature.evidence_id).c_str(),
        nr_resource_recovery::StateName(proof.state), identity(proof.attempt_id).c_str(), identity(proof.proof_attempt_id).c_str(),
        identity(proof.proof_frame_id).c_str(), number(proof.proof_age, proof.proof_frame_id != 0).c_str(),
        identity(proof.proof_carrier_ordinal).c_str(), identity(proof.proof_target.resource).c_str(),
        identity(proof.proof_target.resource_generation).c_str(),
        number(proof.proof_target.subresource, proof.proof_target.resource != 0).c_str(),
        proof.correlated_window,
        number(proof.window_first_frame_id, proof.correlated_window && proof.window_first_frame_id).c_str(),
        number(proof.window_last_frame_id, proof.correlated_window && proof.window_last_frame_id).c_str(),
        number(proof.window_first_ordinal, proof.correlated_window && proof.window_first_ordinal).c_str(),
        number(proof.window_last_ordinal, proof.correlated_window && proof.window_last_ordinal).c_str(),
        failure.present, proof.first_failure_historical,
        failure.present ? nr_resource_recovery::StageName(failure.stage) : "none", failure_result,
        number(static_cast<unsigned>(failure.kind), failure.present).c_str(),
        number(failure.token.carrier.frame_id, failure.present && failure.token.carrier.frame_id).c_str(),
        number(failure.token.carrier.ordinal, failure.present && failure.token.carrier.ordinal).c_str(),
        number(failure.token.attempt_id, failure.present && failure.token.attempt_id).c_str(),
        number(failure_key.scope.config_generation, failure.present && failure_key.scope.config_generation).c_str(),
        number(failure_key.scope.feature_generation, failure.present && failure_key.scope.feature_generation).c_str(),
        number(failure_key.scope.device, failure.present && failure_key.scope.device).c_str(),
        number(failure_key.resource, failure.present && failure_key.resource).c_str(),
        number(failure_key.role, failure.present && failure_key.resource).c_str(),
        proof.capacity_unknown, proof.frame_evidence_lost, current.receipt_overflow);
    FeedbackAppend(s, "%s.recovery.receiptMeaning=interpretationContract is the semantic contract, not observation source; "
        "frameLost and receiptOverflow are historical flags; current proof must follow the most recent interruption\n", prefix);
#endif
#if defined(NR_HAS_EXPOSURE_SEMANTIC_EVIDENCE)
    const auto exposure_semantic = SnapshotNrExposureSemantic();
    FeedbackAppend(s, "%s.semantic.Exposure=available:%d,present:%d,required:%d,physical:%u,flags:0x%X,"
        "shape:%ux%u,mips:%u,samples:%u,selected:%u,source:%s,contract:final-exposure-scale-red,"
        "config:%u,feature:%llu,deviceId:%llu,resourceId:%llu,mode:%d,queryAttempted:%d,queryResult:0x%08X,"
        "support1:0x%X,support2:0x%X,verdict:%s,reason:%s,missingEvidence:%s,nextAction:%s\n",
        prefix, exposure_semantic.available, exposure_semantic.present, exposure_semantic.required,
        exposure_semantic.physical, exposure_semantic.flags, exposure_semantic.width, exposure_semantic.height,
        exposure_semantic.mips, exposure_semantic.samples, static_cast<unsigned>(exposure_semantic.access.view),
        nr_resource_semantics::SourceName(exposure_semantic.semantic.source), exposure_semantic.config,
        exposure_semantic.feature, exposure_semantic.device, exposure_semantic.resource, exposure_semantic.mode,
        exposure_semantic.access.query_attempted, static_cast<unsigned>(exposure_semantic.access.query_result),
        exposure_semantic.access.support1, exposure_semantic.access.support2,
        nr_resource_semantics::VerdictName(exposure_semantic.semantic.verdict), exposure_semantic.semantic.reason,
        exposure_semantic.semantic.missing_evidence, exposure_semantic.semantic.next_action);
#endif
#if defined(NR_HAS_NATIVE_FORMAT_OBSERVER)
    const auto formats = SnapshotNrFormatDiagnostics();
    for (unsigned role = 0; role != 2; ++role) {
        for (unsigned first = 0; first != 2; ++first) {
            const auto& d = first ? formats.first_unknown[role] : formats.latest[role];
            FeedbackAppend(s, "%s.format.%s.%s=available:%d,physical:%u,srv:%u,uav:%u,"
                "config:%u,feature:%llu,deviceId:%llu,resourceId:%llu,observedCall:%llu,"
                "viewStatus:%s,provenance:%u,hookCoverage:%d,conflict:%d,nativeFailed:%d\n",
                prefix, role ? "output" : "color", first ? "firstUnknown" : "latest",
                d.available, d.physical, d.srv, d.uav, d.config, d.feature, d.device,
                d.resource, d.observed_call, nr_format_access::StatusName(d.status),
                static_cast<unsigned>(d.provenance), d.coverage, d.conflict, d.native_failed);
            if (!first && d.available) {
                const auto& semantic = d.semantic;
                FeedbackAppend(s, "%s.semantic.%s=storage:%u,flags:0x%X,shape:%ux%u,array:%u,mips:%u,samples:%u,"
                    "selected:%u,write:%u,source:%s,colorContract:%s,lifecycleMatch:%d,verdict:%s,"
                    "reason:%s,definitive:%d,missingEvidence:%s,nextAction:%s\n",
                    prefix, role ? "Output" : "ColorInput", d.physical, d.flags, d.width, d.height,
                    d.arrays, d.mips, d.samples, semantic.access.srv, semantic.access.uav,
                    nr_resource_semantics::SourceName(semantic.source),
                    nr_resource_semantics::ContractName(semantic.contract),
                    d.device && d.resource && d.feature && !d.conflict && !d.native_failed,
                    nr_resource_semantics::VerdictName(semantic.verdict), semantic.reason,
                    semantic.definitive, semantic.missing_evidence, semantic.next_action);
                FeedbackAppend(s, "%s.semantic.%s.observedViews=", prefix, role ? "Output" : "ColorInput");
                for (unsigned i = 0; i != d.observed.count; ++i)
                    FeedbackAppend(s, "%s%u", i ? "," : "", d.observed.formats[i]);
                FeedbackAppend(s, "; owner=%s; call=%llu\n",
                    d.conflict ? "conflict" : d.native_failed ? "stale" : d.observed.format ? "current" : "none",
                    d.observed_call);
                for (unsigned i = 0; i != semantic.candidate_count; ++i) {
                    const auto& c = semantic.candidates[i];
                    FeedbackAppend(s, "%s.semantic.%s.candidate.%u=view:%u,write:%u,legal:%d,contractPossible:%d,"
                        "codecImplemented:%d,queryAttempted:%d,queryResult:0x%08X,queryKnown:%d,texture2D:%d,srv:%d,uav:%d\n",
                        prefix, role ? "Output" : "ColorInput", i, c.view, c.write_view, c.layout_legal,
                        c.contract_possible, c.codec_implemented, c.capability.queried,
                        static_cast<unsigned>(c.capability.result), c.capability.known(),
                        c.capability.texture2d, c.capability.load, c.capability.store);
                }
            }
        }
    }
    FeedbackAppend(s, "%s.format.coverage=admission observer: native Feature1 synchronous view creation only; "
        "pre-existing/worker-thread views unobserved; no descriptor-address reuse; "
        "viewReady is not color/NR/game admission; provenance=0:none,1:typed,2:native-view,3:float-contract\n", prefix);
#endif
#if defined(NR_HAS_DESCRIPTOR_PROVENANCE)
    const auto descriptor_use=SnapshotNrDescriptorUse();
    const auto descriptor_counts=g_nr_descriptors.SnapshotCounters();
    const auto descriptor_inventory=g_nr_descriptors.ResourceInventory(descriptor_use.resource);
    const char *descriptor_reason=!descriptor_use.call?"no-native-call-observed":
        !descriptor_use.hooks_ready?"descriptor-observer-hook-incomplete":
        !descriptor_use.identity_known?"current-output-or-list-lifetime-unknown":
        !descriptor_use.clear_calls?"no-matching-explicit-UAV-clear-observed":nr_descriptor::ReasonName(descriptor_use.reason);
    FeedbackAppend(s,"%s.descriptor.nativeUse=call:%llu,config:%llu,currentConfig:%d,feature:%llu,carrier:%llu,device:%llu,resource:%llu,semanticResource:%llu,list:%llu,reset:%llu,hooks:%d,identity:%d,finished:%d,nativeSuccess:%d,OutputClearCalls:%u,paired:%u,unknown:%u,otherResources:%u,otherLists:%u,observedTyped:%u,conflict:%d,overflow:%d,reason:%s\n",
        prefix,descriptor_use.call,descriptor_use.config,descriptor_use.config==g_config_generation.load(),
        descriptor_use.feature,descriptor_use.carrier,descriptor_use.device,descriptor_use.resource,descriptor_use.semantic_resource,
        descriptor_use.list,descriptor_use.reset,descriptor_use.hooks_ready,descriptor_use.identity_known,
        descriptor_use.finished,descriptor_use.native_success,descriptor_use.clear_calls,descriptor_use.matches,descriptor_use.unknown,
        descriptor_use.other_resources,descriptor_use.other_lists,descriptor_use.observed_typed,descriptor_use.conflict,descriptor_use.overflow,descriptor_reason);
    FeedbackAppend(s,"%s.descriptor.revisions=afterNativeValid:%d,executeEntryObserved:%d,atExecuteEntryValid:%d,executeReturned:%d,queue:%llu,submission:%llu,arrayIndex:%u,arrayCount:%u,replayObserved:%d,current:%d\n",
        prefix,descriptor_use.after_native_valid,descriptor_use.submission_seen,descriptor_use.at_submit_valid,descriptor_use.execute_returned,
        descriptor_use.queue,descriptor_use.submission,descriptor_use.submission_index,descriptor_use.submission_count,
        descriptor_use.replayed,descriptor_use.RevisionsCurrent(g_nr_descriptors));
    FeedbackAppend(s,"%s.descriptor.registry=writes:%llu,copies:%llu,unknownWrites:%llu,overlaps:%llu,evictions:%llu,invalidations:%llu,resourceRetirements:%llu,heapRetirements:%llu,lifetimeFailures:%llu\n",
        prefix,descriptor_counts.writes,descriptor_counts.copies,descriptor_counts.unknown_writes,descriptor_counts.overlaps,
        descriptor_counts.evictions,descriptor_counts.invalidations,descriptor_counts.resources_retired,descriptor_counts.heaps_retired,
        g_nr_descriptor_lifetime_failures.load(std::memory_order_relaxed));
    FeedbackAppend(s,"%s.descriptor.outputCache=resource:%llu,device:%llu,lifetimeKnown:%d,observedSlots:%u,srv:%u,uav:%u,rtv:%u,unknownShapeOrType:%u,formatGroups:%u,truncated:%d; cached-only-not-use; kind/format/count=",
        prefix,descriptor_inventory.resource,descriptor_inventory.device,descriptor_inventory.lifetime_known,
        descriptor_inventory.slots,descriptor_inventory.srv,descriptor_inventory.uav,descriptor_inventory.rtv,
        descriptor_inventory.unknown_shapes,descriptor_inventory.count,descriptor_inventory.overflow);
    for(unsigned i=0;i<descriptor_inventory.count;++i)
        FeedbackAppend(s,"%s%u/%u/%u",i?",":"",static_cast<unsigned>(descriptor_inventory.formats[i].kind),
            descriptor_inventory.formats[i].format,descriptor_inventory.formats[i].count);
    FeedbackAppend(s,"; kind1:SRV,2:UAV,3:RTV; only current tracked slots, absence does not prove no native descriptor\n");
    if(std::strcmp(prefix,"end")==0)for(unsigned i=0;i<descriptor_use.count;++i) {
        const auto &pair=descriptor_use.pairs[i];
        FeedbackAppend(s,"%s.descriptor.nativePair.%u=format:%u,mip:%u,plane:%u,cpuHeap:%llu,cpuSlot:%u,cpuRevision:%llu,gpuHeap:%llu,gpuSlot:%u,gpuRevision:%llu,current:%d\n",
            prefix,i,pair.cpu.view.format,pair.cpu.view.mip,pair.cpu.view.plane,
            pair.cpu.stamp.heap,pair.cpu.stamp.index,pair.cpu.stamp.revision,
            pair.gpu.stamp.heap,pair.gpu.stamp.index,pair.gpu.stamp.revision,g_nr_descriptors.Current(pair));
    }
    FeedbackAppend(s,"%s.descriptor.coverage=read-only cached descriptor provenance; tracked creation/copy/overwrite and object lifetimes; explicit native Output UAV clear requires matching CPU/nonvisible and GPU/current-bound-heap views; no root-table/bindless shader-use decoding; observedTyped never enters selectedTyped or the admission ledger; Execute entry/return is not GPU execution or final pixels\n",prefix);
#endif
#if defined(NR_HAS_RUNTIME_LOAD_EVIDENCE)
    const auto load = SnapshotRuntimeLoadEvidence();
    FeedbackAppend(s, "%s.runtime.available=%d\n%s.runtime.stage=%s\n%s.runtime.ageMs=%llu\n"
        "%s.runtime.bridgeLoaded=%d\n%s.runtime.bridgeWin32=%lu\n%s.runtime.signedLoaded=%d\n%s.runtime.signedWin32=%lu\n"
        "%s.runtime.coreLoaded=%d\n%s.runtime.coreWin32=%lu\n%s.runtime.missingExportMask=0x%X\n%s.runtime.initResult=0x%X\n"
        "%s.runtime.bridgePresent=%d\n%s.runtime.signedPresent=%d\n%s.runtime.corePresent=%d\n"
        "%s.runtime.bridgeClass=%s\n%s.runtime.signedClass=%s\n%s.runtime.coreClass=%s\n%s.runtime.search=%s\n",
        prefix, load.available ? 1 : 0, prefix, load.stage,
        prefix, load.available ? GetTickCount64() - load.sampled_at : 0,
        prefix, load.loaded[0] ? 1 : 0, prefix, load.errors[0], prefix, load.loaded[1] ? 1 : 0, prefix, load.errors[1],
        prefix, load.loaded[2] ? 1 : 0, prefix, load.errors[2], prefix, load.missing_exports, prefix, load.init_result,
        prefix, load.present[0] ? 1 : 0, prefix, load.present[1] ? 1 : 0, prefix, load.present[2] ? 1 : 0,
        prefix, RuntimeLoadClassName(load.classes[0]), prefix, RuntimeLoadClassName(load.classes[1]),
        prefix, RuntimeLoadClassName(load.classes[2]), prefix, load.search);
    FeedbackAppend(s, "%s.scope.config=%u\n%s.scope.epoch=%llu\n%s.scope.featureGeneration=%llu\n%s.scope.mode=%d\n"
        "%s.scope.nrSuccess=%llu\n%s.scope.transferSubmitted=%llu\n%s.queue.admitted=%llu\n%s.queue.unknownBypasses=%llu\n"
        "%s.queue.predictionBypasses=%llu\n%s.queue.actualMismatches=%llu\n%s.queue.markerFailures=%llu\n%s.queue.retiredAdmitted=%llu\n"
        "%s.scope.note=individual atomic observations; only compare matching config/epoch, never infer FPS\n",
        prefix, g_config_generation.load(), prefix, g_pipeline_stats_epoch.load(), prefix, g_pipeline_stats_generation.load(),
        prefix, g_pipeline_stats_mode.load(), prefix, g_pipeline_feature18_success.load(), prefix, g_pipeline_transfer_submitted.load(),
        prefix, g_queue_affinity_admitted_frames.load(), prefix, g_queue_affinity_unknown_bypass_frames.load(),
        prefix, g_queue_affinity_prediction_bypass_frames.load(), prefix, g_queue_affinity_actual_mismatches.load(),
        prefix, g_queue_affinity_marker_failures.load(), prefix, g_queue_retired_admitted_frames.load(), prefix);
    FeedbackAppend(s, "%s.queue.retirementWaits=%llu\n%s.queue.retirementWaitCompleted=%llu\n"
        "%s.queue.retirementWaitTimeouts=%llu\n%s.queue.retirementWaitTotalMs=%llu\n%s.queue.retirementWaitMaxMs=%llu\n",
        prefix,g_queue_retirement_waits.load(),prefix,g_queue_retirement_wait_completed.load(),
        prefix,g_queue_retirement_wait_timeouts.load(),prefix,g_queue_retirement_wait_ms.load(),
        prefix,g_queue_retirement_wait_max_ms.load());
#if NR_HAS_PROBE_POLICY
    const auto probe_policy=NrProbePolicySnapshot();
    FeedbackAppend(s,"%s.probeControl.phase=%llu\n%s.probeControl.mode=%s\n"
        "%s.probeControl.persisted=0\n%s.probeControl.historyResetOnSwitch=0\n"
        "%s.probeControl.note=automatic GPU samples including recovery probes only; explicit image captures remain independent; pending samples keep original phase; mode change is not a config generation or model reset\n",
        prefix,static_cast<unsigned long long>(probe_policy.phase),
        prefix,nr_probe_policy::Name(probe_policy.mode),prefix,prefix,prefix);
#endif
    const auto stored=SnapshotPostStoreEvidence();
#if NR_HAS_TIMING
    FeedbackAppendTiming(s,prefix);
#endif
    const auto pre_stored=SnapshotPreStoreEvidence();
    const auto append_store=[&](const char *route,const NrPostStoreEvidence &sample) {
        const bool available=sample.samples!=0;
#if NR_HAS_PROBE_POLICY
        FeedbackAppend(s,"%s.store.%s.probePhase=%llu\n%s.store.%s.probeMode=%s\n"
            "%s.store.%s.probePhaseCurrent=%d\n",
            prefix,route,static_cast<unsigned long long>(available?sample.probe_policy.phase:0),
            prefix,route,available?nr_probe_policy::Name(sample.probe_policy.mode):"unknown",
            prefix,route,available && sample.probe_policy.phase==probe_policy.phase &&
                sample.probe_policy.mode==probe_policy.mode);
#endif
        const auto &token=sample.recovery_token;
        const auto &scope=token.key.scope;
        const bool token_available=static_cast<bool>(token);
        const bool scope_known=available && sample.config && sample.feature_generation &&
            (sample.mode==1 || sample.mode==2) && token_available && token.key.resource && scope.device &&
            scope.config_generation && scope.feature_generation && (scope.mode==1 || scope.mode==2) &&
            decision.current.available;
        const bool scope_matches=scope_known && sample.config==decision.current.config &&
            sample.feature_generation==decision.current.feature_generation &&
            static_cast<unsigned>(sample.mode)==decision.current.effective_mode &&
            scope.config_generation==sample.config && scope.feature_generation==sample.feature_generation &&
            scope.mode==static_cast<unsigned>(sample.mode);
        const bool attempt_known=scope_known && decision.recovery.available &&
            decision.recovery.attempted && decision.recovery.attempt_id;
        const bool attempt_matches=scope_matches &&
            nr_compatibility_verdict::SameScope(decision.recovery.scope,decision.current) &&
            token.attempt_id==decision.recovery.attempt_id;
        const auto value=[](std::uint64_t number,bool known) {
            return known?std::to_string(number):std::string("unknown");
        };
        const auto id=[&](std::uint64_t number) { return value(number,number!=0); };
        const auto decimal=[](float number,bool known) {
            char text[64]="unknown";
            if(known)std::snprintf(text,sizeof(text),"%.7f",number);
            return std::string(text);
        };
        const auto hash=[](unsigned number,bool known) {
            char text[16]="unknown";
            if(known)std::snprintf(text,sizeof(text),"%08X",number);
            return std::string(text);
        };
        const bool expected_available=available && sample.expected_samples!=0;
        FeedbackAppend(s,"%s.store.%s=available:%d,source:frozen-recording-and-readback-sample,"
            "mode:%s,config:%s,feature:%s,frame:%s,ageMs:%s,"
            "tokenAvailable:%d,attempt:%s,carrierFrame:%s,carrierOrdinal:%s,"
            "tokenConfig:%s,tokenFeature:%s,tokenFeatureGeneration:%s,tokenMode:%s,tokenDevice:%s,"
            "scopeMatchesDecision:%s,attemptMatchesDecision:%s,scopeCompared:config-feature-effectiveMode,"
            "actualTarget:%s,actualTargetGeneration:%s,actualTargetSubresource:%s,"
            "sourcePhysical:%s,sourceView:%s,resultPhysical:%s,resultView:%s,resultUav:%s,"
            "samples:%s,expected:%s,invalid:%s,actualNonfinite:%s,referenceNonfinite:%s,known:%s,match:%s,"
            "changed:%s,expectedChanged:%s,beforeHash:%s,expectedHash:%s,actualHash:%s,"
            "expectedMean:%s,actualMean:%s,expectedMax:%s,actualMax:%s\n",
            prefix,route,available,
            value(static_cast<unsigned>(sample.mode),available && (sample.mode==1 || sample.mode==2)).c_str(),
            value(sample.config,available && sample.config).c_str(),
            value(sample.feature_generation,available && sample.feature_generation).c_str(),
            value(sample.frame,available && sample.frame).c_str(),
            value(sample.tick?GetTickCount64()-sample.tick:0,available && sample.tick).c_str(),
            token_available,id(token.attempt_id).c_str(),id(token.carrier.frame_id).c_str(),id(token.carrier.ordinal).c_str(),
            id(scope.config_generation).c_str(),id(scope.feature).c_str(),id(scope.feature_generation).c_str(),
            id(scope.mode).c_str(),id(scope.device).c_str(),value(scope_matches,scope_known).c_str(),
            value(attempt_matches,attempt_known).c_str(),id(sample.recovery_target.resource).c_str(),
            id(sample.recovery_target.resource_generation).c_str(),
            value(sample.recovery_target.subresource,sample.recovery_target.resource!=0).c_str(),
            value(sample.source_physical,available && sample.source_physical).c_str(),
            value(sample.source_view,available && sample.source_view).c_str(),
            value(sample.result_physical,available && sample.result_physical).c_str(),
            value(sample.result_view,available && sample.result_view).c_str(),
            value(sample.result_uav,available && sample.result_uav).c_str(),
            value(sample.samples,available).c_str(),value(sample.expected_samples,available).c_str(),
            value(sample.invalid,available).c_str(),value(sample.actual_nonfinite,available).c_str(),
            value(sample.reference_nonfinite,available).c_str(),value(sample.comparison_known,available).c_str(),
            value(sample.matches_expected,available && sample.comparison_known).c_str(),
            value(sample.changed,available).c_str(),value(sample.expected_changed,expected_available).c_str(),
            hash(sample.before_hash,available).c_str(),hash(sample.expected_hash,expected_available).c_str(),
            hash(sample.actual_hash,available).c_str(),decimal(sample.expected_mean,expected_available).c_str(),
            decimal(sample.actual_mean,available).c_str(),decimal(sample.expected_max,expected_available).c_str(),
            decimal(sample.actual_max,available).c_str());
    };
    append_store("pre",pre_stored);
    append_store("post",stored);
    FeedbackAppend(s,"%s.post.outputProbe.available=%d\n%s.post.outputProbe.frame=%llu\n"
        "%s.post.outputProbe.config=%u\n%s.post.outputProbe.featureGeneration=%llu\n%s.post.outputProbe.ageMs=%llu\n"
        "%s.post.outputProbe.samples=%u\n%s.post.outputProbe.expectedSamples=%u\n"
        "%s.post.outputProbe.changed=%u\n%s.post.outputProbe.expectedChanged=%u\n%s.post.outputProbe.invalid=%u\n"
        "%s.post.outputProbe.meanStoredDelta=%.7f\n%s.post.outputProbe.expectedMeanStoredDelta=%.7f\n"
        "%s.post.outputProbe.maxStoredDelta=%.7f\n%s.post.outputProbe.expectedMaxStoredDelta=%.7f\n"
        "%s.post.outputProbe.beforeHash=%08X\n%s.post.outputProbe.expectedHash=%08X\n%s.post.outputProbe.actualHash=%08X\n"
        "%s.post.outputProbe.note=sparse RGB read after external Output store; expected uses the frozen actual typed UAV storage conversion and matching canonical read normalization; non-unique or nonfinite comparison is unknown; mean delta capped at 1; not Present or same-frame fullscreen evidence\n",
        prefix,stored.samples!=0,prefix,stored.frame,prefix,stored.config,prefix,stored.feature_generation,
        prefix,stored.tick?GetTickCount64()-stored.tick:0,prefix,stored.samples,prefix,stored.expected_samples,
        prefix,stored.changed,prefix,stored.expected_changed,prefix,stored.invalid,
        prefix,stored.actual_mean,prefix,stored.expected_mean,prefix,stored.actual_max,prefix,stored.expected_max,
        prefix,stored.before_hash,prefix,stored.expected_hash,prefix,stored.actual_hash,prefix);
#if defined(NR_HAS_POST_BOUNDARY_TRACE)
    const auto boundary=stored.boundary_trace_id?SnapshotPostBoundary(stored.boundary_trace_id,
        stored.frame,stored.config,stored.feature_generation):nr_post_boundary::Window{};
    FeedbackAppend(s,"%s.post.boundary.frame=%llu\n%s.post.boundary.config=%u\n%s.post.boundary.featureGeneration=%llu\n"
        "%s.post.boundary.ageMs=%llu\n%s.post.boundary.nativeEventEntries=%u\n%s.post.boundary.nativeMatches=%u\n"
        "%s.post.boundary.nativeOtherListMatches=%u\n%s.post.boundary.nativeFirstBefore=0x%X\n%s.post.boundary.nativeLastAfter=0x%X\n"
        "%s.post.boundary.followingBarriers=%u\n%s.post.boundary.firstBefore=0x%X\n%s.post.boundary.firstAfter=0x%X\n"
        "%s.post.boundary.lastAfter=0x%X\n%s.post.boundary.readBarriers=%u\n%s.post.boundary.writeBarriers=%u\n"
        "%s.post.boundary.copyReads=%u\n%s.post.boundary.copyWrites=%u\n%s.post.boundary.renderTargetBinds=%u\n"
        "%s.post.boundary.note=immutable sampled-frame recording window; ReShade counters omit split/subresources; exact native events below; neither path proves GPU order or consumed pixels; zero matches means unobserved\n",
        prefix,boundary.frame,prefix,boundary.config,prefix,boundary.feature,
        prefix,boundary.tick?GetTickCount64()-boundary.tick:0,prefix,boundary.native.callbacks,prefix,boundary.native.matches,
        prefix,boundary.native.other_lists,prefix,boundary.native.first_before,prefix,boundary.native.last_after,
        prefix,boundary.barriers,prefix,boundary.first_before,prefix,boundary.first_after,prefix,boundary.last_after,
        prefix,boundary.read_barriers,prefix,boundary.write_barriers,prefix,boundary.copy_reads,prefix,boundary.copy_writes,
        prefix,boundary.render_target_binds,prefix);
    FeedbackAppend(s,"%s.post.boundary.trace=%llu\n%s.post.boundary.pairedWithProbe=%d\n"
        "%s.post.boundary.currentConfig=%d\n%s.post.boundary.close=%s\n"
        "%s.post.boundary.nativeExactMatches=%u\n%s.post.boundary.nativeExactOtherLists=%u\n"
        "%s.post.boundary.nativeGlobals=%u\n%s.post.boundary.nativeUnknownOperations=%u\n"
        "%s.post.boundary.nativeCoverage=0x%X\n%s.post.boundary.coverage=0x%X\n"
        "%s.post.boundary.exactFollowing=%u\n%s.post.boundary.unknownOperations=%u\n"
        "%s.post.boundary.nativeDropped=%u\n%s.post.boundary.followingDropped=%u\n"
        "%s.post.boundary.originSubmission=%d,%llu,%llu\n"
        "%s.post.boundary.originArrayPosition=%u,%u\n"
        "%s.post.boundary.coverageKey=1:legacy,2:enhanced,4:enhanced-unavailable,8:base-operations,16:hook-missing,32:interface-unknown,64:list-identity-unknown,128:event-loss,256:opaque,512:target-unknown,1024:submission-unknown\n",
        prefix,boundary.trace,prefix,boundary.trace!=0,prefix,boundary.frame&&boundary.config==g_config_generation.load(),
        prefix,nr_post_boundary::CloseName(boundary.close),prefix,boundary.native.exact_matches,prefix,boundary.native.exact_other_lists,
        prefix,boundary.native.exact_global,prefix,boundary.native.unknown_operations,prefix,boundary.native.coverage,prefix,boundary.coverage,
        prefix,boundary.exact_following,prefix,boundary.unknown_operations,prefix,boundary.native.dropped,prefix,boundary.dropped,
        prefix,boundary.origin_submitted,boundary.origin_queue,boundary.origin_submission,
        prefix,boundary.origin_submission_index,boundary.origin_submission_count,prefix);
    FeedbackAppend(s,"%s.post.boundary.knownOtherRtv=nativeBind:%u,nativeClear:%u,followingBind:%u,followingClear:%u; scope=observed native/origin/following lists; excluded from Output events\n",
        prefix,boundary.native.other_rtv_binds,boundary.native.other_rtv_clears,boundary.other_rtv_binds,boundary.other_rtv_clears);
    const auto append_boundary_event=[&](const char *phase,unsigned index,const nr_post_boundary::Event &e) {
        FeedbackAppend(s,"%s.post.boundary.%s.%u=k:%u,src:%u,list:%llu,reset:%llu,id:%d,seq:%llu,target:%d,rw:%d/%d,ba:%llX/%llX,f:%X,sub:%u/%u/%u/%u/%u/%u,sync:%llX/%llX,access:%llX/%llX,submit:%d/%llu/%llu,position:%u/%u,descriptor:%llu/%llu/%u,clear:%u/%u/%d/%d/%d/%d\n",
            prefix,phase,index,static_cast<unsigned>(e.kind),static_cast<unsigned>(e.source),e.list.token,e.list.reset,e.list.known,
            e.order,e.target_known,e.reads,e.writes,e.before,e.after,e.flags,
            e.subresources[0],e.subresources[1],e.subresources[2],e.subresources[3],e.subresources[4],e.subresources[5],
            e.sync_before,e.sync_after,e.access_before,e.access_after,e.submitted,e.queue,e.submission,
            e.submission_index,e.submission_count,e.descriptor_resource,e.descriptor_revision,e.descriptor_format,
            e.clear_kind,e.clear_rect_count,e.clear_bounds[0],e.clear_bounds[1],e.clear_bounds[2],e.clear_bounds[3]);
    };
    // Keep room for the rest of the F8 metadata. Start carries coherent summary
    // identities; the final matched window carries the full bounded event list.
    const bool export_events=std::strcmp(prefix,"end")==0;
    if(export_events) {
        for(unsigned i=0;i<boundary.native.count;++i)append_boundary_event("native",i,boundary.native.events[i]);
        for(unsigned i=0;i<boundary.count;++i)append_boundary_event("following",i,boundary.events[i]);
    }
    FeedbackAppend(s,"%s.post.boundary.eventExport=%s\n",prefix,export_events?"final-paired-window":"summary-only; full events in end window");
    if(export_events)FeedbackAppend(s,"%s.post.boundary.eventKey=1:transition,2:UAV,3:aliasing,4:enhanced-texture,5:enhanced-global,6:enhanced-buffer,7:copy,8:copy-region,9:UAV-clear,10:RT-clear,11:draw,12:indexed-draw,13:dispatch,14:RT-bind,15:opaque,16:resolve,17:discard; src1:native,src2:ReShade; target=0 unknown; position=index/count in one actual Execute array; descriptor=resource-lifetime/revision/format; clear=kind/count/left/top/right/bottom, kind0:unknown,1:full-view,2:one-rect,3:rect-envelope-not-union; submit is CPU Execute return, cross-queue order and consumed pixels unknown; ray/mesh/render-pass/descriptor-indirect targets not decoded\n",prefix);
#endif
#endif
#if defined(NR_HAS_COMMAND_STATE_EVIDENCE)
    const auto evidence = SnapshotCommandStateEvidence();
    FeedbackAppend(s, "%s.commandState.available=%d\n%s.commandState.snapshotReason=%s\n",
        prefix, evidence.available ? 1 : 0, prefix, evidence.available ? "available" : evidence.reason);
    FeedbackAppend(s, "%s.commandState.blendObserved=%d\n", prefix,
        evidence.available && evidence.blend_observed ? 1 : 0);
    if (evidence.available && evidence.blend_observed)
        FeedbackAppend(s, "%s.commandState.blend=%.9g,%.9g,%.9g,%.9g\n",
            prefix, evidence.blend[0], evidence.blend[1], evidence.blend[2], evidence.blend[3]);
    else FeedbackAppend(s, "%s.commandState.blend=%s\n", prefix,
        evidence.available ? "unobserved" : "unavailable");
    if (evidence.available) {
        FeedbackAppend(s, "%s.commandState.schema=1\n%s.commandState.ageMs=%llu\n"
            "%s.commandState.sequence=%llu\n%s.commandState.token=%llu\n"
            "%s.commandState.reason=%s\n%s.commandState.route=%s\n"
            "%s.commandState.registered=%d\n%s.commandState.hooksReady=%d\n"
            "%s.commandState.guardRequested=%d\n%s.commandState.guardCovered=%d\n"
            "%s.commandState.freshReset=%d\n%s.commandState.tainted=%d\n",
            prefix, prefix, GetTickCount64() - evidence.sampled_at,
            prefix, evidence.sequence, prefix, evidence.token, prefix, evidence.reason, prefix, evidence.route,
            prefix, evidence.registered ? 1 : 0, prefix, evidence.hooks_ready ? 1 : 0,
            prefix, evidence.guard_requested ? 1 : 0, prefix, evidence.guard_covered ? 1 : 0,
            prefix, evidence.fresh_reset ? 1 : 0, prefix, evidence.tainted ? 1 : 0);
        FeedbackAppend(s, "%s.commandState.firstRecordingObserved=%d\n", prefix, evidence.first_recording_observed ? 1 : 0);
        FeedbackAppend(s, "%s.commandState.baseline=%s\n%s.commandState.resetSerial=%llu\n"
            "%s.commandState.pipelineWrites=%llu\n%s.commandState.nullBaseline=%d\n"
            "%s.commandState.hasPso=%d\n%s.commandState.hasDxrPso=%d\n"
            "%s.commandState.dirty=0x%X\n%s.commandState.dynamic=0x%llX\n"
            "%s.commandState.vertexMask=0x%X\n%s.commandState.streamMask=0x%X\n"
            "%s.commandState.metaKnown=%d\n%s.commandState.metaInit=0x%X\n%s.commandState.metaExecute=0x%X\n"
            "%s.commandState.rootHeapPresence=%d,%d,%d,%d\n%s.commandState.rootArgumentCounts=%u,%u\n",
            prefix, evidence.baseline, prefix, evidence.reset_serial,
            prefix, evidence.pipeline_writes, prefix, evidence.null_baseline ? 1 : 0,
            prefix, evidence.has_pipeline ? 1 : 0, prefix, evidence.has_ray_pipeline ? 1 : 0,
            prefix, evidence.dirty, prefix, evidence.dynamic, prefix, evidence.vertex_mask, prefix, evidence.stream_mask,
            prefix, evidence.meta_known ? 1 : 0, prefix, evidence.meta_init, prefix, evidence.meta_execute,
            prefix, evidence.roots[0] ? 1 : 0, evidence.roots[1] ? 1 : 0, evidence.heaps[0] ? 1 : 0, evidence.heaps[1] ? 1 : 0,
            prefix, evidence.arguments[0], evidence.arguments[1]);
    }
#endif
#if defined(NR_HAS_NGX_HOOK_EVIDENCE)
    const auto hook_failure = SnapshotNgxHookFailure();
    FeedbackAppend(s, "%s.ngxHookFailure.available=%d\n", prefix, hook_failure.available ? 1 : 0);
    if (hook_failure.available)
        FeedbackAppend(s, "%s.ngxHookFailure.ageMs=%llu\n%s.ngxHookFailure.operation=%s\n"
            "%s.ngxHookFailure.status=%d\n%s.ngxHookFailure.entry=%s\n",
            prefix, GetTickCount64() - hook_failure.sampled_at, prefix, hook_failure.operation,
            prefix, hook_failure.status, prefix, hook_failure.entry);
#endif

    FeedbackAppend(s, "%s.nrSuccess=%llu\n%s.transferSubmitted=%llu\n%s.bypass=%s\n"
        "%s.srCarrierEvaluations=%llu\n%s.lastCarrierFeature=%d\n%s.rrLiveHandles=%u\n"
        "%s.rrCreatesInProgress=%u\n%s.rrEvaluations=%llu\n%s.otherNrOwner=%d\n"
        "%s.genericRenoDxConflict=%d\n%s.moduleObserverFault=%s\n"
        "%s.feature1Preflight=%llu/%llu\n%s.preflightCoreLoaded=%d\n"
        "%s.preflightDlssdLoaded=%d\n%s.commandSignatureHook=%d\n"
        "%s.commandSignatureDeferredStart=%d\n%s.commandSignatureObserved=%llu\n"
        "%s.commandSignatureUnknownUses=%llu\n",
        prefix, g_nr_successful_evaluations.load(), prefix, g_nr_transfer_submitted_frames.load(),
        prefix, BypassReasonName(static_cast<BypassReason>(g_last_bypass_reason.load())),
        prefix, g_carrier_evaluations.load(), prefix, g_last_carrier_feature.load(),
        prefix, g_live_rr_handles.load(), prefix, g_rr_create_in_progress.load(),
        prefix, g_rr_passthrough_evaluations.load(), prefix,
        OtherNrOwnerActive() ? 1 : 0,
        prefix, g_generic_renodx_conflict.load() ? 1 : 0,
        prefix, ModuleObserverFaultName(static_cast<ModuleObserverFault>(
            g_module_observer_fault.load())),
        prefix, g_feature1_preflight_covered.load(),
        g_feature1_preflight_attempts.load(),
        prefix, g_preflight_core_loaded.load() ? 1 : 0,
        prefix, g_preflight_dlssd_loaded.load() ? 1 : 0,
        prefix, g_command_signature_hook_published.load() ? 1 : 0,
        prefix, g_command_signature_start_deferred.load() ? 1 : 0,
        prefix, g_command_signature_create_calls.load(),
        prefix, g_command_signature_unknown_uses.load());
    if (std::strcmp(prefix, "end") == 0) FeedbackAppendNgxLifecycle(s);
}

namespace nr_f8 { bool RequestCore(); void DrawControls(); }

bool RequestFeedbackBundle() {
    // Technical/legacy button and keyboard F8 share the same session initializer.
    return nr_f8::RequestCore();
}

void FeedbackStageExported(unsigned long long request, const wchar_t *directory, bool ok) {
    AcquireSRWLockExclusive(&g_feedback_lock);
    auto *s = g_feedback_session;
    if (s != nullptr && s->stage_request == request) {
        s->stage_finished = true;
        s->stage_ok = ok;
        if (directory != nullptr && wcslen(directory) < std::size(s->stage_directory))
            wcscpy_s(s->stage_directory, directory);
        strcpy_s(s->stage_reason, ok ? "exported after exact GPU fence" : "stage export failed; inspect NR log");
    }
    ReleaseSRWLockExclusive(&g_feedback_lock);
}

void OnFeedbackScreenshot(reshade::api::effect_runtime *runtime, const char *path) {
    if (path == nullptr) return;
    AcquireSRWLockExclusive(&g_feedback_lock);
    auto *s = g_feedback_session;
    // The event fires only after ReShade closes its image file. Never search a
    // screenshot directory or accidentally attach an unrelated user's image.
    if (s != nullptr && s->runtime_identity == runtime &&
        g_feedback_status.load(std::memory_order_relaxed) == 1) {
        const char *basename = path;
        for (const char *p = path; *p; ++p) if (*p == '/' || *p == '\\') basename = p + 1;
        for (unsigned int i = 0; i < s->shots_requested && i < 3; ++i) {
            char tag[80] = {};
            std::snprintf(tag, sizeof(tag), "%s_%u", s->postfix, i);
            const char *match = std::strstr(basename, tag);
            if (match != nullptr && match[std::strlen(tag)] == '.' && s->screenshots[i][0] == 0) {
                if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                        s->screenshots[i], static_cast<int>(std::size(s->screenshots[i]))) == 0)
                    s->screenshots[i][0] = 0;
                else {
                    // ReShade retains '.\\' in its absolute saved path. Resolve
                    // that syntax before the ZIP allowlist validates components.
                    wchar_t canonical[1024] = {};
                    const DWORD n = GetFullPathNameW(s->screenshots[i],
                        static_cast<DWORD>(std::size(canonical)), canonical, nullptr);
                    if (n != 0 && n < std::size(canonical)) {
                        wcscpy_s(s->screenshots[i], canonical);
                        s->screenshot_ticks[i] = GetTickCount64();
                    } else s->screenshots[i][0] = 0;
                }
            }
        }
    }
    ReleaseSRWLockExclusive(&g_feedback_lock);
    nr_f8::Screenshot(runtime, path);
}

void FeedbackObserveEvaluate(const NgxParams *params, const FeatureRecord &feature,
                             unsigned long long carrier) {
    if (g_feedback_status.load(std::memory_order_relaxed) != 1) return;
    if (!TryAcquireSRWLockExclusive(&g_feedback_lock)) return;
    auto *s = g_feedback_session;
    const ULONGLONG now = GetTickCount64();
    if (s != nullptr && s->observations + s->timeline_omitted < 120 &&
        now - s->last_observation >= 100) {
        s->last_observation = now;
        // Leave space for the stage outcome, policy, counters and screenshot
        // identity even when paths/numbers make the optional timeline longer.
        if (s->report_used >= sizeof(s->report) - nr_feedback_limits::kTimelineReserveBytes) {
            ++s->timeline_omitted;
            s->report_truncated = true;
            ReleaseSRWLockExclusive(&g_feedback_lock);
            return;
        }
        ++s->observations;
        float jitter_x = NAN, jitter_y = NAN, mv_x = NAN, mv_y = NAN;
        float pre = NAN, exposure = NAN;
        int reset = -1;
        GetFloat(params, "Jitter.Offset.X", &jitter_x);
        GetFloat(params, "Jitter.Offset.Y", &jitter_y);
        GetFloat(params, "MV.Scale.X", &mv_x);
        GetFloat(params, "MV.Scale.Y", &mv_y);
        GetFloat(params, "DLSS.Pre.Exposure", &pre);
        GetFloat(params, "DLSS.Exposure.Scale", &exposure);
        GetInt(params, "Reset", &reset);
        FeedbackAppend(*s, "frame,%llu,%llu,%llu,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%s\n",
            now - s->started, carrier, feature.generation, reset,
            jitter_x, jitter_y, mv_x, mv_y, pre, exposure,
            BypassReasonName(static_cast<BypassReason>(g_last_bypass_reason.load())));
        if (s->observations == 1 || s->observations == 60) {
            FeedbackAppend(*s, "ngx.contract.sample=%u featureId=%d generation=%llu\n", s->observations, feature.feature_id, feature.generation);
            for (const char *key : {"DLSS.Render.Subrect.Dimensions.Width", "DLSS.Render.Subrect.Dimensions.Height",
                    "DLSS.Input.Color.Subrect.Base.X", "DLSS.Input.Color.Subrect.Base.Y",
                    "DLSS.Input.Depth.Subrect.Base.X", "DLSS.Input.Depth.Subrect.Base.Y",
                    "DLSS.Input.MV.Subrect.Base.X", "DLSS.Input.MV.Subrect.Base.Y",
                    "DLSS.Output.Subrect.Base.X", "DLSS.Output.Subrect.Base.Y"}) {
                unsigned value = 0;
                if (GetUInt(params, key, &value)) FeedbackAppend(*s, "ngx.%s=%u\n", key, value);
                else FeedbackAppend(*s, "ngx.%s=not-supplied\n", key);
            }
            FeedbackAppend(*s, "feature.flags=%d feature.create=%ux%u->%ux%u\n",
                feature.create_flags, feature.width, feature.height, feature.out_width, feature.out_height);
            for (const char *key : {"Color", "Output", "Depth", "MotionVectors", "ExposureTexture"}) {
                ID3D12Resource *resource = nullptr;
                if (GetResource(params, key, &resource)) {
                    const auto d = ID3D12Resource_GetDesc(resource);
                    FeedbackAppend(*s, "resource.%s=dim:%u size:%llux%u array:%u mip:%u format:%u samples:%u flags:%u\n",
                        key, static_cast<unsigned int>(d.Dimension), d.Width, d.Height,
                        d.DepthOrArraySize, d.MipLevels, static_cast<unsigned int>(d.Format),
                        d.SampleDesc.Count, static_cast<unsigned int>(d.Flags));
                } else FeedbackAppend(*s, "resource.%s=absent\n", key);
            }
        }
    }
    ReleaseSRWLockExclusive(&g_feedback_lock);
}

bool FeedbackCreateDirectory(wchar_t (&directory)[1024]) {
    for (unsigned int attempt = 0; attempt < 2; ++attempt) {
        wchar_t parent[MAX_PATH] = {};
        if (attempt == 0) {
            if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, parent))) continue;
        } else {
            const DWORD n = GetTempPathW(MAX_PATH, parent);
            if (n == 0 || n >= MAX_PATH) continue;
        }
        wchar_t root[1024] = {};
        if (swprintf_s(root, L"%ls\\DLSS5-Feedback", parent) < 0) continue;
        if (!CreateDirectoryW(root, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) continue;
        SYSTEMTIME t = {}; GetSystemTime(&t);
        if (swprintf_s(directory, L"%ls\\%04u%02u%02u-%02u%02u%02u-%lu-%llu", root,
                t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                GetCurrentProcessId(), GetTickCount64()) < 0) continue;
        if (CreateDirectoryW(directory, nullptr)) return true;
    }
    directory[0] = 0;
    return false;
}

bool FeedbackModuleRelevant(const wchar_t *name) {
    wchar_t lower[MAX_PATH] = {};
    wcsncpy_s(lower, name, _TRUNCATE);
    CharLowerBuffW(lower, static_cast<DWORD>(wcslen(lower)));
    return wcsstr(lower, L".addon") != nullptr || wcsstr(lower, L"ngx") != nullptr ||
        wcsstr(lower, L"dlss") != nullptr || wcsstr(lower, L"reshade") != nullptr ||
        wcscmp(lower, L"dxgi.dll") == 0 || wcscmp(lower, L"d3d12.dll") == 0 ||
        wcscmp(lower, L"d3dcompiler_47.dll") == 0 || wcscmp(lower, L"nvwgf2umx.dll") == 0;
}

void FeedbackAddModules(nr_bundle::Job &job, FeedbackSession &s) {
    (void)s;
    HMODULE own = nullptr;
    wchar_t own_path[1024] = {};
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&OnFeedbackScreenshot), &own) &&
        GetModuleFileNameW(own, own_path, static_cast<DWORD>(std::size(own_path))))
        nr_bundle::AddFile(job, own_path, "modules/current-addon", nr_bundle::FileKind::HashOnly, true);
    else nr_bundle::AddMissing(job, "modules/current-addon", "cannot identify loaded addon path", true);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) {
        nr_bundle::AddMissing(job, "modules", "module snapshot failed", true); return;
    }
    MODULEENTRY32W entry = {}; entry.dwSize = sizeof(entry);
    unsigned int count = 0, relevant = 0;
    for (BOOL more = Module32FirstW(snapshot, &entry); more; more = Module32NextW(snapshot, &entry)) {
        if (entry.hModule == own || !FeedbackModuleRelevant(entry.szModule)) continue;
        ++relevant;
        if (count >= 32) continue;
        char leaf[1024] = {}; WideToUtf8(entry.szModule, leaf, sizeof(leaf));
        char name[1100] = {};
        std::snprintf(name, sizeof(name), "modules/%02u-%s", count++, leaf);
        nr_bundle::AddFile(job, entry.szExePath, name, nr_bundle::FileKind::HashOnly, true);
    }
    CloseHandle(snapshot);
    if (relevant > count) nr_bundle::AddMissing(job, "modules/remaining", "module inventory capped at 32 entries", false);
}

void FeedbackAddLogBeside(nr_bundle::Job &job, const wchar_t *module_path,
                         const wchar_t *leaf, const char *name, bool required) {
    wchar_t path[1024] = {};
    if (wcslen(module_path) >= std::size(path)) return;
    wcscpy_s(path, module_path);
    auto slash = wcsrchr(path, L'\\');
    if (slash == nullptr) return;
    slash[1] = 0;
    if (wcscat_s(path, leaf) == 0)
        nr_bundle::AddFile(job, path, name, nr_bundle::FileKind::LogTail, required);
}

DWORD WINAPI FeedbackWorker(void *parameter) {
    const auto &module_identity = EnsureRuntimeIdentityOnWorker();
    auto *s = static_cast<FeedbackSession *>(parameter);
    bool pending_cancelled = false;
    // A timeout freezes the file list; late GPU exports remain in the ordinary
    // stage directory and can never be mistaken for a different request.
    for (;;) {
        Sleep(100);
        // Config takes the chain lock, so sample it before the feedback lock.
        // A game can replace Feature1 with RR after this session was queued.
        const char *blocked = FeedbackStageBlockReason(SnapshotConfig());
        unsigned long long cancel_request = 0;
        if (blocked != nullptr) {
            AcquireSRWLockExclusive(&g_feedback_lock);
            if (!s->stage_finished) {
                s->stage_finished = true;
                strcpy_s(s->stage_reason, blocked);
                cancel_request = s->stage_request;
            }
            ReleaseSRWLockExclusive(&g_feedback_lock);
        }
        if (cancel_request != 0)
            pending_cancelled = CancelPendingStageCapture(cancel_request) || pending_cancelled;
        AcquireSRWLockShared(&g_feedback_lock);
        bool ready = s->stage_finished;
        for (const auto &shot : s->screenshots) ready = ready && shot[0] != 0;
        ReleaseSRWLockShared(&g_feedback_lock);
        if ((ready && GetTickCount64() - s->started >= 5000) ||
            GetTickCount64() - s->started >= 30000) break;
    }
    AcquireSRWLockExclusive(&g_feedback_lock);
    g_feedback_status.store(2, std::memory_order_release);
    g_feedback_session = nullptr;
    ReleaseSRWLockExclusive(&g_feedback_lock);
    // Only this session's unarmed request can be withdrawn. Recorded GPU work
    // keeps its exact-fence lifetime and may export later outside this ZIP.
    if (!s->stage_ok && s->stage_request != 0)
        pending_cancelled = CancelPendingStageCapture(s->stage_request) || pending_cancelled;
    if (!s->stage_finished)
        strcpy_s(s->stage_reason, pending_cancelled ?
            "no NR stage was armed within 30 seconds; waiting request cancelled so the next F8 can retry" :
            "no exact-fence stage export within 30 seconds; any GPU work remains protected and may export later");
    FeedbackAppend(*s, "collection.elapsedMs=%llu\ncollection.parameterSamples=%u\n",
        GetTickCount64() - s->started, s->observations);
    FeedbackAppend(*s, "stage.request=%llu\nstage.completed=%d\nstage.pendingRequestCancelled=%d\nstage.reason=%s\n",
        s->stage_request, s->stage_ok ? 1 : 0, pending_cancelled ? 1 : 0, s->stage_reason);
    for (unsigned int i = 0; i < 3; ++i)
        FeedbackAppend(*s, "screenshot.%u.savedElapsedMs=%llu\n", i,
            s->screenshot_ticks[i] ? s->screenshot_ticks[i] - s->started : 0);
    const auto color = SnapshotColorPolicy();
    FeedbackAppend(*s, "colorPolicy.valuesMeaning=last recorded Combine; current only when status is recent-recorded-combine; not display completion\n"
        "colorPolicy.status=%s\ncolorPolicy.frame=%llu\ncolorPolicy.config=%u\n"
        "colorPolicy.requested=%.9g\ncolorPolicy.effective=%.9g\ncolorPolicy.reason=%s\n"
        "lightingPolicy.requested=%d\nlightingPolicy.effective=%d\nlightingPolicy.reason=%s\n",
        NrColorPolicyStatusName(color.status), color.frame_id, color.config_generation,
        color.policy.requested_color_strength, color.policy.effective_color_strength,
        NrColorReasonName(color.policy.color_reason), color.policy.requested_preserve_lighting ? 1 : 0,
        color.policy.effective_preserve_lighting ? 1 : 0, NrLightingReasonName(color.policy.lighting_reason));
    FeedbackAppend(*s, "artifact.sourceCommit=%s\nartifact.sourceDirty=%d\n"
        "artifact.addonSha256=%s\nartifact.identityStatus=%s\n"
        "artifact.hashMeasurement=loaded-path-file-once-worker\n",
        kSourceCommit, kSourceDirty ? 1 : 0,
        module_identity.sha256, module_identity.status);
    FeedbackAppend(*s, "competingAddon.module=%s\ncompetingAddon.latched=%d\n"
        "competingAddon.action=%s\n",
        g_generic_renodx_conflict.load(std::memory_order_acquire)
            ? "renodx-dlss5.addon64" : "none",
        g_generic_renodx_conflict.load(std::memory_order_relaxed) ? 1 : 0,
        g_generic_renodx_conflict.load(std::memory_order_relaxed)
            ? "remove it and restart" : "none");
    FeedbackAppendActivity(*s, "end");
    // FeedbackAppend always preserves these final 256 bytes, even on overflow.
    const int summary_bytes = std::snprintf(s->report + s->report_used,
        sizeof(s->report) - s->report_used,
        "collection.metadataTruncated=%d\ncollection.timelineRowsOmitted=%u\n",
        s->report_truncated ? 1 : 0, s->timeline_omitted);
    if (summary_bytes > 0 && static_cast<std::size_t>(summary_bytes) <
            sizeof(s->report) - s->report_used)
        s->report_used += static_cast<std::size_t>(summary_bytes);
    wchar_t directory[1024] = {};
    nr_bundle::Result result = {};
    result.status = nr_bundle::Status::Failed;
    if (FeedbackCreateDirectory(directory)) {
        auto *job = nr_bundle::CreateJob(directory, s->report);
        if (job != nullptr) {
            nr_bundle::AddFile(*job, g_log_path, "logs/nr-current.log", nr_bundle::FileKind::LogTail, true);
            FeedbackAddLogBeside(*job, g_log_path, L"nr-before-sr.previous.log", "logs/nr-previous.log", false);
            FeedbackAddLogBeside(*job, g_log_path, L"ReShade.log", "logs/reshade-addon-dir.log", false);
            wchar_t exe[1024] = {};
            GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
            FeedbackAddLogBeside(*job, exe, L"ReShade.log", "logs/reshade-game-dir.log", false);
            wchar_t reshade_path[1024] = {};
            if (GetModuleFileNameW(reshade::internal::get_reshade_module_handle(), reshade_path,
                    static_cast<DWORD>(std::size(reshade_path))))
                FeedbackAddLogBeside(*job, reshade_path, L"ReShade.log", "logs/reshade-module-dir.log", true);
            nr_bundle::AddFile(*job, g_ini_path, "settings/nr_before_sr.ini", nr_bundle::FileKind::Copy, true);
            nr_bundle::AddFile(*job, g_reshade_ini_path, "settings/ReShade.ini", nr_bundle::FileKind::Copy, false);
            FeedbackAddModules(*job, *s);
            for (unsigned int i = 0; i < 3; ++i) {
                char name[96] = {};
                const wchar_t *extension = wcsrchr(s->screenshots[i], L'.');
                char ext[16] = {};
                if (extension != nullptr && wcslen(extension) < 12) WideToUtf8(extension, ext, sizeof(ext));
                std::snprintf(name, sizeof(name), "screenshots/current-%u%s", i, ext);
                if (s->screenshots[i][0])
                    nr_bundle::AddFile(*job, s->screenshots[i], name, nr_bundle::FileKind::Copy, true, 128ull * 1024 * 1024);
                else nr_bundle::AddMissing(*job, name, "ReShade did not deliver this request's saved image before timeout", true);
            }
            if (s->stage_finished && s->stage_ok) {
                for (const wchar_t *leaf : {L"original.pfm", L"proxy.pfm", L"neural.pfm", L"composited.pfm", L"metadata.txt", L"stages.rgba32f"}) {
                    wchar_t path[1200] = {};
                    swprintf_s(path, L"%ls\\%ls", s->stage_directory, leaf);
                    char name[128] = {}; char utf8[64] = {};
                    WideToUtf8(leaf, utf8, sizeof(utf8));
                    std::snprintf(name, sizeof(name), "stages/%s", utf8);
                    nr_bundle::AddFile(*job, path, name, nr_bundle::FileKind::Copy, true);
                }
            } else nr_bundle::AddMissing(*job, "stages", s->stage_reason[0] ? s->stage_reason :
                "no completed exact-fence NR capture within 30 seconds; see bypass and NGX timeline", true);
            nr_bundle::AddMissing(*job, "guides/depth-motion-raw", "resource layouts/scales/jitter are recorded; raw guide textures not read without a verified state contract", false);
            nr_bundle::AddMissing(*job, "native-sr-same-frame", "display screenshots and NR stages have independent frame identities; native SR output is not an extra readback", false);
            result = nr_bundle::Run(*job);
            nr_bundle::DestroyJob(job);
        }
    }
    AcquireSRWLockExclusive(&g_feedback_lock);
    wcscpy_s(g_feedback_last_zip, result.zip_path);
    ReleaseSRWLockExclusive(&g_feedback_lock);
    Log("[feedback] result=%u zip=%ls included=%u missing=%u requiredComplete=%d win32=%lu",
        static_cast<unsigned int>(result.status), result.zip_path,
        result.included_files, result.missing_files, result.required_complete ? 1 : 0, result.win32_error);
    g_feedback_status.store(result.status == nr_bundle::Status::Failed ? 5 :
        (result.required_complete ? 3 : 4), std::memory_order_release);
    delete s;
    return 0;
}

void FeedbackPresent(reshade::api::effect_runtime *runtime) {
    bool begin_session = false;
    if (g_feedback_requested.exchange(false, std::memory_order_acq_rel)) {
        unsigned int status = g_feedback_status.load(std::memory_order_acquire);
        while (status != 1 && status != 2) {
            // Claim before calling runtime APIs or requesting GPU stages. Two
            // Present threads must never publish competing sessions/workers.
            if (g_feedback_status.compare_exchange_weak(status, 1,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                begin_session = true;
                break;
            }
        }
    }
    if (begin_session) {
        AcquireSRWLockExclusive(&g_feedback_lock);
        g_feedback_last_zip[0] = 0;
        ReleaseSRWLockExclusive(&g_feedback_lock);
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&FeedbackWorker), &pinned)) {
            g_feedback_status.store(5); return;
        }
        auto *s = new (std::nothrow) FeedbackSession{};
        if (s == nullptr) { g_feedback_status.store(5); return; }
        s->started = GetTickCount64(); s->runtime_identity = runtime;
        std::snprintf(s->postfix, sizeof(s->postfix), "DLSS5_FB_%lu_%llu", GetCurrentProcessId(), s->started);
        const ChainConfig config = SnapshotConfig();
        const auto device = runtime->get_device();
        char gpu[256] = {}; uint32_t driver = 0, vendor = 0;
        const bool gpu_known = device->get_property(reshade::api::device_properties::description, gpu);
        const bool driver_known = device->get_property(reshade::api::device_properties::driver_version, &driver);
        device->get_property(reshade::api::device_properties::vendor_id, &vendor);
        uint32_t width = 0, height = 0; runtime->get_screenshot_width_and_height(&width, &height);
        // ReShade resource handles are C++ return-by-value wrappers. MinGW and
        // MSVC disagree on their return ABI: get_current_back_buffer crashed
        // the F8 CP repro by passing index=2 as an output pointer. Use the
        // native COM GetDesc out-parameter snapshot published by Present.
        wchar_t exe[1024] = {}; GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
        char exe_utf8[4096] = {}; WideToUtf8(exe, exe_utf8, sizeof(exe_utf8));
        NrDiagnosticIdentity applied = {};
        EnterCriticalSection(&g_chain_cs); applied = g_applied_nr_identity; LeaveCriticalSection(&g_chain_cs);
        FeedbackAppend(*s, "format=DLSS5Feedback-v1\nbuild=%s\nsession=%s\nexe=%s\n"
            "gpu=%s\ndriver.raw=%u\ndriver.known=%d\nvendor=%u\napi=%u\n"
            "display.extent=%ux%u\ndisplay.storageFormat=%u\ndisplay.colorSpace=%u\n"
            "requested.enabled=%d\nrequested.mode=%d\nrequested.config=%u\nrequested.intensity=%.9g\nrequested.style=%u\n"
            "requested.blend=%.9g\nrequested.color=%.9g\nrequested.lighting=%d\n"
            "applied.instance=%llu\napplied.frame=%llu\napplied.config=%u\napplied.intensity=%.9g\napplied.style=%u\n"
            "capture.automaticSettingChanges=none\ncapture.stageRequestRecreatesNRHistory=1\n"
            "capture.activityCounters=cumulative process totals; compare start/end; applied settings can describe historical NR\n"
            "capture.displayFrames=three current-chain images, independent from NR stages, not controlled A/B\n"
            "capture.rawStageDomain=see stages/metadata.txt; before destination quantization\n"
            "capture.crashDump=not generated; game may supply crash report separately\n"
            "timeline.columns=tag,elapsedMs,SRcarrier,featureGeneration,reset,jitterX,jitterY,mvScaleX,mvScaleY,preExposure,exposureScale,previousBypass\n",
            kBuildIdentity, s->postfix, exe_utf8, gpu_known ? gpu : "unknown", driver, driver_known ? 1 : 0,
            vendor, static_cast<unsigned int>(device->get_api()), width, height,
            g_feedback_swap_format.load(), g_feedback_swap_color_space.load(),
            config.enabled ? 1 : 0, config.mode, g_config_generation.load(), config.intensity, config.style,
            config.mode == 2 ? config.transfer_strength : config.post_transfer_strength,
            config.color_strength, config.preserve_lighting ? 1 : 0,
            applied.instance_id, applied.frame_id, applied.config_generation, applied.tuning.intensity,
            applied.tuning.style);
        FeedbackAppendActivity(*s, "start");
        const char *stage_block = FeedbackStageBlockReason(config);
        // RequestStageCapture uses atomics and Log only, never g_chain_cs.
        // Keep session publication and request identity in this one lock so
        // an export completing on another worker cannot race past registration.
        AcquireSRWLockExclusive(&g_feedback_lock);
        g_feedback_session = s;
        if (stage_block != nullptr) {
            s->stage_finished = true; strcpy_s(s->stage_reason, stage_block);
        } else if (g_nr_successful_evaluations.load() == 0) {
            s->stage_finished = true; strcpy_s(s->stage_reason, "no successful NR frame yet; current-chain screenshots and logs collected without enabling NR");
        } else if (RequestStageCapture()) {
            s->stage_request = g_stage_capture_request_id.load();
        } else {
            s->stage_finished = true; strcpy_s(s->stage_reason, "another stage request was already in progress; it is not attached to this session");
        }
        g_feedback_status.store(1, std::memory_order_release);
        ReleaseSRWLockExclusive(&g_feedback_lock);
        if (!QueueUserWorkItem(&FeedbackWorker, s, WT_EXECUTEDEFAULT)) {
            AcquireSRWLockExclusive(&g_feedback_lock); g_feedback_session = nullptr; ReleaseSRWLockExclusive(&g_feedback_lock);
            if (s->stage_request != 0) CancelPendingStageCapture(s->stage_request);
            delete s; g_feedback_status.store(5); return;
        }
        Log("[feedback] one-click session requested; current settings retained, 3 display images, exact-fence stages, bounded 30-second wait.");
    }
    char tag[80] = {};
    AcquireSRWLockExclusive(&g_feedback_lock);
    auto *s = g_feedback_session;
    if (s != nullptr && s->runtime_identity == runtime && s->shots_requested < 3 &&
        GetTickCount64() - s->started >= s->shots_requested * 2000ull) {
        std::snprintf(tag, sizeof(tag), "%s_%u", s->postfix, s->shots_requested++);
    }
    ReleaseSRWLockExclusive(&g_feedback_lock);
    if (tag[0]) runtime->save_screenshot(tag);
}

void DrawFeedbackControls() { nr_f8::DrawControls(); }
