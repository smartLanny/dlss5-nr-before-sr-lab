"""Session-only guided comparison, applied after temporal and native O capture."""
from pathlib import Path
INPUTS=('scripts/patch-acceptance.py','src/nr_acceptance_policy.h','src/nr_acceptance_runtime.inl','src/nr_acceptance_screenshot_export.inl','src/nr_acceptance_gpu_timing.inl',
        'tests/acceptance_policy_test.cpp','tests/acceptance_core_harness.inl','scripts/validate-acceptance.py')

def apply(stage:Path,inputs,helpers):
    root=stage/'core'
    for path in ('src/nr_acceptance_policy.h','src/nr_acceptance_runtime.inl','src/nr_acceptance_screenshot_export.inl','src/nr_acceptance_gpu_timing.inl'):
        (root/path).write_bytes(inputs[path])
    p=root/'src/nr_before_sr.cpp';s=p.read_text(encoding='utf-8')
    s=helpers.once(s,'#include "interop/nr_rr_post_runtime.inl"',
        '#define NR_HAS_ACCEPTANCE 1\n#include "interop/nr_rr_post_runtime.inl"',
        'acceptance enabled before real RR route include')
    s=helpers.once(s,'#include "nr_f8_sequence.inl"',
        'namespace nr_acceptance { bool Active(); bool PerformanceActive(); }\n#include "nr_f8_sequence.inl"\n#include "nr_acceptance_gpu_timing.inl"\n#include "nr_acceptance_runtime.inl"','guided controller ordering')
    s=helpers.once(s,'    ID3D12QueryHeap* dual_time_heap=nullptr;', '''    ID3D12QueryHeap* acceptance_time_heap=nullptr;
    ID3D12Resource* acceptance_time_readback=nullptr;
    LifecycleQueue* acceptance_time_queue=nullptr;
    unsigned long long acceptance_time_token=0,acceptance_time_fence=0;
    unsigned acceptance_time_mask=0,acceptance_time_generation=0,acceptance_time_history=0;
    unsigned acceptance_time_work_width=0,acceptance_time_work_height=0,acceptance_time_output_width=0,acceptance_time_output_height=0;
    nr_source_frame::Source acceptance_time_source{};
    NrDiagnosticIdentity acceptance_time_identity{};
    bool acceptance_time_prepared=false,acceptance_time_active=false,acceptance_time_pending=false,acceptance_time_visible=false,acceptance_time_private_composed=false,acceptance_time_temporal_recorded=false;
    ID3D12QueryHeap* dual_time_heap=nullptr;''','acceptance timestamp slot state')
    s=helpers.once(s,'    ReleaseCom(slot.dual_time_heap);ReleaseCom(slot.dual_time_readback);',
        '    ReleaseCom(slot.acceptance_time_heap);ReleaseCom(slot.acceptance_time_readback);\n    ReleaseCom(slot.dual_time_heap);ReleaseCom(slot.dual_time_readback);','acceptance timestamp retirement')
    s=helpers.once(s,'    if (slot.delta_stats_capture_armed)\n        slot.diagnostic_identity = identity;',
        '    if(slot.acceptance_time_active||slot.acceptance_time_pending)slot.acceptance_time_identity=identity;\n    if (slot.delta_stats_capture_armed)\n        slot.diagnostic_identity = identity;','freeze acceptance source identity')
    s=helpers.once(s,'namespace nr_dual_runtime {void CollectTiming(NrSlot&);}',
        '''namespace nr_acceptance {bool PerformanceActive();}
namespace nr_acceptance_gpu {
enum class EntryRoute : unsigned { Sr, Rr };
enum class PreReason : unsigned { Route, Lifecycle, Owner, Unavailable };
struct EntryScope {
    EntryRoute route;
    PreReason reason=PreReason::Route;
    EntryScope* previous=nullptr;
    unsigned config=0;
    bool active=false,reached=false;
    explicit EntryScope(EntryRoute,bool enabled=true);
    ~EntryScope();
    void Decline(PreReason value){reason=value;}
    void ReachPrepare();
};
void ReachPrepare();
bool Prepare(ID3D12Device*,NrSlot&,ID3D12GraphicsCommandList*,unsigned,unsigned,unsigned,unsigned);
bool Bind(ID3D12GraphicsCommandList*,NrSlot&);
void DiscardPrepared(NrSlot&);
void Abort(NrSlot&,const char*);
void Stamp(ID3D12GraphicsCommandList*,NrSlot&,unsigned);
void Finish(ID3D12GraphicsCommandList*,NrSlot&,bool);
void Collect(NrSlot&);
void DiscardUnsubmitted(unsigned long long);
}
namespace nr_dual_runtime {void CollectTiming(NrSlot&);}''','acceptance timestamp declarations')
    s=helpers.once(s,'    nr_dual_runtime::CollectTiming(slot);',
        '    nr_dual_runtime::CollectTiming(slot);\n    nr_acceptance_gpu::Collect(slot);','asynchronous timestamp collection')
    s=helpers.once(s,'        if (QuarantineUnsubmittedLifecycleRecordLocked(\n                token, "native Reset")) {',
        '        nr_acceptance_gpu::DiscardUnsubmitted(token);\n        if (QuarantineUnsubmittedLifecycleRecordLocked(\n                token, "native Reset")) {','successful Reset discards only unsubmitted timestamp recording')
    s=helpers.once(s,'        if(slot->dual_time_pending)for(unsigned r=0;r<batch.record_count;++r) {',
        '''        if(slot->acceptance_time_pending)for(unsigned r=0;r<batch.record_count;++r) {
            const auto* record=batch.records[r];
            if(record&&record->token==slot->acceptance_time_token){
                slot->acceptance_time_queue=queue_state;slot->acceptance_time_fence=batch.fence_value;break;
            }
        }
        if(slot->dual_time_pending)for(unsigned r=0;r<batch.record_count;++r) {''','exact acceptance submit receipt')
    s=helpers.once(s,'    nr_regions::Seal(device,command_list,*context,*slot);\n    if (!RecordNrUseLocked(command_list, *context, *slot)) {',
        '    nr_acceptance_gpu::ReachPrepare();\n    nr_acceptance_gpu::Prepare(device,*slot,command_list,context->width,context->height,width,height);\n    nr_regions::Seal(device,command_list,*context,*slot);\n    if (!RecordNrUseLocked(command_list, *context, *slot)) {\n        nr_acceptance_gpu::DiscardPrepared(*slot);','prepare timestamp before lifecycle recording')
    s=helpers.once(s,'    // A disabled add-on must be indistinguishable from an unloaded add-on on',
        '    nr_acceptance_gpu::EntryScope acceptance_sr_entry(nr_acceptance_gpu::EntryRoute::Sr,\n        nr_acceptance::PerformanceActive() && IsUpscalingCarrierFeature(FindFeature(handle).feature_id));\n    // A disabled add-on must be indistinguishable from an unloaded add-on on',
        'session-only real SR entry before global owner and lifecycle guards')
    s=helpers.once(s,'    if (feature.pending || feature.retiring) {\n        SetBypassReason(BypassReason::LifecycleIsolation);',
        '    if (feature.pending || feature.retiring) {\n        acceptance_sr_entry.Decline(nr_acceptance_gpu::PreReason::Lifecycle);\n        SetBypassReason(BypassReason::LifecycleIsolation);',
        'classified SR pending lifecycle refusal')
    s=helpers.once(s,'        g_nr_failed_create_process_latched.load(std::memory_order_acquire)) {\n        if (!g_enabled_fast.load(std::memory_order_relaxed))',
        '        g_nr_failed_create_process_latched.load(std::memory_order_acquire)) {\n        acceptance_sr_entry.Decline(\n            nr_external_provider_feedback::owner_provider_id.load(std::memory_order_acquire)!=0 || OtherNrOwnerActive()\n                ?nr_acceptance_gpu::PreReason::Owner:\n            g_module_rescan_in_progress.load(std::memory_order_acquire) ||\n            g_evaluate_target_overflow.load(std::memory_order_acquire) ||\n            g_lifecycle_tracking_uncertain.load(std::memory_order_acquire) ||\n            g_nr_failed_create_process_latched.load(std::memory_order_acquire)\n                ?nr_acceptance_gpu::PreReason::Lifecycle:nr_acceptance_gpu::PreReason::Route);\n        if (!g_enabled_fast.load(std::memory_order_relaxed))',
        'SR global owner and lifecycle guard')
    s=helpers.once(s,'    const unsigned long long all = ++g_all_evaluations;\n    if (g_feature_tracking_uncertain.load(std::memory_order_acquire)) {',
        '    const unsigned long long all = ++g_all_evaluations;\n    if (g_feature_tracking_uncertain.load(std::memory_order_acquire)) {\n        acceptance_sr_entry.Decline(nr_acceptance_gpu::PreReason::Lifecycle);',
        'SR feature observer uncertainty before Prepare')
    s=helpers.once(s,'    ScopedRrOwnershipGate rr_transaction(true, true, true);\n    if (rr_transaction.acquisition_failed()) {',
        '    ScopedRrOwnershipGate rr_transaction(true, true, true);\n    if (rr_transaction.acquisition_failed()) {\n        acceptance_sr_entry.Decline(nr_acceptance_gpu::PreReason::Owner);',
        'SR ownership refusal before timestamp preparation')
    s=helpers.once(s,'    const unsigned int live_rr_handles =\n        g_live_rr_handles.load(std::memory_order_acquire);\n    if (ShouldGuardFeature1ForRayReconstruction()) {',
        '    const unsigned int live_rr_handles =\n        g_live_rr_handles.load(std::memory_order_acquire);\n    if (ShouldGuardFeature1ForRayReconstruction()) {\n        acceptance_sr_entry.Decline(nr_acceptance_gpu::PreReason::Owner);',
        'SR active RR guard before ownership gate')
    s=helpers.once(s,'    if (g_feature_tracking_uncertain.load(std::memory_order_acquire)) {\n        SetBypassReason(BypassReason::LifecycleIsolation);\n        rr_transaction.Release();',
        '    if (g_feature_tracking_uncertain.load(std::memory_order_acquire)) {\n        acceptance_sr_entry.Decline(nr_acceptance_gpu::PreReason::Lifecycle);\n        SetBypassReason(BypassReason::LifecycleIsolation);\n        rr_transaction.Release();',
        'SR lifecycle changed after ownership gate')
    s=helpers.once(s,'    if (ShouldGuardFeature1ForRayReconstruction()) {\n        SetBypassReason(BypassReason::RayReconstructionGuard);\n        g_last_carrier_feature.store(13, std::memory_order_relaxed);\n        g_rr_guard_was_active.store(true, std::memory_order_relaxed);\n        rr_transaction.Release();',
        '    if (ShouldGuardFeature1ForRayReconstruction()) {\n        acceptance_sr_entry.Decline(nr_acceptance_gpu::PreReason::Owner);\n        SetBypassReason(BypassReason::RayReconstructionGuard);\n        g_last_carrier_feature.store(13, std::memory_order_relaxed);\n        g_rr_guard_was_active.store(true, std::memory_order_relaxed);\n        rr_transaction.Release();',
        'SR RR guard changed after ownership gate')
    s=helpers.once(s,'    NativeCommandStateScope before_state(command_list);\n    if (!before_state.active) {',
        '    NativeCommandStateScope before_state(command_list);\n    if (!before_state.active) {\n        acceptance_sr_entry.Decline(nr_acceptance_gpu::PreReason::Lifecycle);',
        'SR command-list lifecycle refusal before preparation')
    s=helpers.once(s,'        after_sr=PrepareAfterSr(before_state.command_list,handle,params,feature,compatibility_fallback);',
        '        acceptance_sr_entry.Decline(nr_acceptance_gpu::PreReason::Unavailable);\n        after_sr=PrepareAfterSr(before_state.command_list,handle,params,feature,compatibility_fallback);',
        'SR entered post route but could not reach timestamp preparation')
    s=helpers.once(s,'    nr_regions::Dispatch(command_list,*slot,false);\n    ReleaseCom(device);\n\n    substitution.active = true;',
        '    nr_acceptance_gpu::Bind(command_list,*slot);\n    nr_acceptance_gpu::Stamp(command_list,*slot,0);\n    nr_regions::Dispatch(command_list,*slot,false);\n    ReleaseCom(device);\n\n    substitution.active = true;','start post route GPU sample before private guide capture')
    s=helpers.once(s,'    if(timed){if(after_sr.active)nr_dual_runtime::Stamp(before_state.command_list,*timed,10);',
        '    if(after_sr.active&&timed)nr_acceptance_gpu::Stamp(before_state.command_list,*timed,1);\n    if(timed){if(after_sr.active)nr_dual_runtime::Stamp(before_state.command_list,*timed,10);','end pre-native plugin work')
    s=helpers.once(s,'    if(timed){nr_dual_runtime::Stamp(before_state.command_list,*timed,9);',
        '    if(after_sr.active&&timed)nr_acceptance_gpu::Stamp(before_state.command_list,*timed,2);\n    if(timed){nr_dual_runtime::Stamp(before_state.command_list,*timed,9);','start post-native plugin work')
    s=helpers.once(s,'        ~TimingEnd(){if(safe)nr_dual_runtime::ResolveTiming(list,slot,visible);}',
        '        ~TimingEnd(){if(safe){nr_acceptance_gpu::Finish(list,slot,visible);nr_dual_runtime::ResolveTiming(list,slot,visible);}}','finish successful post route GPU sample')
    s=helpers.once(s,'        Log("Modified Feature 1 transaction aborted after an RR ownership ',
        '        if(used_after_sr)nr_acceptance_gpu::Abort(*after_sr.slot,"native-SR-transaction-aborted");\n'
        '        Log("Modified Feature 1 transaction aborted after an RR ownership ',
        'SR transaction abort retires unfinished query attempt')
    s=helpers.once(s,'                if(after_sr.fatal_recording){\n                    after_state.AbandonAfterOpaqueFailure();',
        '                if(after_sr.fatal_recording){\n                    nr_acceptance_gpu::Abort(*after_sr.slot,"post-NR-fatal-before-finish");\n                    after_state.AbandonAfterOpaqueFailure();',
        'fatal post-NR route has no completed timestamp sample')
    s=helpers.once(s,'    if (rr_transaction.transaction_aborted() ||\n        g_feature_tracking_uncertain.load(std::memory_order_acquire)) {\n        Log("RR ownership changed while finalizing ',
        '    if(used_after_sr)nr_acceptance_gpu::Abort(*after_sr.slot,\n        result!=kNgxSuccess?"native-SR-failed-before-finish":"post-NR-finish-unavailable");\n'
        '    if (rr_transaction.transaction_aborted() ||\n        g_feature_tracking_uncertain.load(std::memory_order_acquire)) {\n        Log("RR ownership changed while finalizing ',
        'SR success without post finish or native failure is diagnostic only')
    s=helpers.once(s,'#include "nr_feedback_capture.inl"',
        'namespace nr_acceptance { void Screenshot(reshade::api::effect_runtime*,const char*); }\n#include "nr_feedback_capture.inl"',
        'screenshot callback forward declaration before feedback include')
    s=helpers.once(s,'#include "nr_compat_present_runtime.inl"',
        '''#include "nr_compat_present_runtime.inl"
namespace nr_acceptance {
PresentCounters ReadPresentLocked() {
    // The acceptance controller already holds g_chain_cs. Do not call Snapshot(),
    // which acquires the same lock again.
    const auto& o=nr_compat_present_runtime::observation;
    return {nr_compat_present_runtime::stable_config,o.generation,o.completed_source,
        o.submitted,o.completed,o.evaluated[0],o.attempted[0],o.timing_source,
        o.reset_each_frame,o.timing_valid,o.total_ms};
}
}''','Present baseline reads exact owner counters under existing chain lock')
    s=helpers.once(s,'    nr_f8::Present(runtime);','    nr_f8::Present(runtime);\n    nr_acceptance::Present(runtime);','guided Present tick')
    s=helpers.once(s,'any_combine && nr_f8::g_config_hold.load() && ReadLifecycleToken(command_list,f8_token)',
        'any_combine && nr_f8::IsActive() && ReadLifecycleToken(command_list,f8_token)',
        'configuration hold alone must not enter per-frame image capture preparation')
    p.write_text(s,encoding='utf-8',newline='\n')
    p=root/'src/nr_f8_sequence.inl';s=p.read_text(encoding='utf-8')
    s=helpers.once(s,'bool RequestCore() {','bool RequestCore() {\n    if(nr_acceptance::Active())return false;','single config owner report')
    s=helpers.once(s,'    const bool accepted = !IsActive() && g_status.load()!=Status::Packaging &&',
        '    const bool accepted = !nr_acceptance::Active() && !IsActive() && g_status.load()!=Status::Packaging &&',
        'recheck single config owner under F8 control lock')
    s=helpers.once(s,'bool RequestWithOptions(const Options &options) {',
        'bool RequestWithOptions(const Options &options) {\n    if(nr_acceptance::Active())return false;','single config owner images')
    p.write_text(s,encoding='utf-8',newline='\n')
    p=root/'src/nr_feedback_capture.inl';s=p.read_text(encoding='utf-8')
    s=helpers.once(s,'    nr_f8::Screenshot(runtime, path);',
        '    nr_f8::Screenshot(runtime, path);\n    nr_acceptance::Screenshot(runtime, path);',
        'guided screenshot callback')
    p.write_text(s,encoding='utf-8',newline='\n')
    p=root/'src/nr_settings_ui.inl';s=p.read_text(encoding='utf-8')
    s=helpers.once(s,'    nr_f8::RuntimeDestroyed(runtime);','    nr_acceptance::RuntimeDestroyed(runtime);\n    nr_f8::RuntimeDestroyed(runtime);','guided runtime shutdown')
    p.write_text(s,encoding='utf-8',newline='\n')
