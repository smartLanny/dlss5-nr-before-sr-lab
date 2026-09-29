"""Product post-final private SR; applied after the complete quality generator."""
from pathlib import Path
VERSION = '0.5.2-beta.12'
LOCAL=('src/nr_private_sr_guides.h','src/nr_private_sr_guides.cpp','src/nr_private_sr_status.h','src/nr_private_sr_budget.h',
       'src/nr_private_sr_product.inl','src/nr_private_sr_ui.inl','src/nr_private_sr_artifacts.h',
       'src/nr_private_sr_call_list.inl')
MODULE=('nr_edit_dx12.hpp','nr_edit_dx12.cpp','nr_post_residual_dx12.hpp','nr_post_residual_dx12.cpp',
        'nr_post_private_sr_dx12.hpp','nr_post_private_sr_dx12.cpp','nr_private_sr_runtime.hpp','nr_private_sr_core_adapter.inl')
SHADERS=('nr_rr_edit.hlsl','nr_deferred_edit.hlsl','nr_normalize_guides.hlsl','nr_post_residual.hlsl','nr_edit_scalar.hlsli','nr_edit_carrier.hlsli')
INPUTS=('scripts/patch-private-sr.py','scripts/build-private-sr-module.py')+LOCAL+tuple('experiments/nr-edit/src/'+p for p in MODULE)+tuple('experiments/nr-edit/shaders/'+p for p in SHADERS)+('experiments/nr-edit/tools/compile_edit_shader.cpp','experiments/nr-edit/tools/compile_edit_artifacts.py')

def patch_product_identity(s,path):
    """Only the preceding Beta2 identity is accepted; reject mixed/repeated input."""
    if path not in ('src/nr_before_sr.cpp','scripts/build.ps1','src/nr_before_sr.rc'):
        raise ValueError('private SR unknown identity file: '+path)
    if '0.5.2-beta.2' not in s or VERSION in s:
        raise ValueError('private SR expected preceding Beta 2 identity: '+path)
    s=s.replace('0.5.2-beta.2',VERSION).replace('0.5.2 Beta 2','0.5.2 Beta 12')
    if path.endswith('.rc'):
        for field in ('FILEVERSION','PRODUCTVERSION'):
            old=field+' 0,5,2,2'
            if s.count(old)!=1:raise ValueError('private SR expected one Beta 2 '+field)
            s=s.replace(old,field+' 0,5,2,12')
        for field in ('FileVersion','ProductVersion'):
            if s.count('VALUE "'+field+'", "0.5.2 Beta 12\\0"')!=1:
                raise ValueError('private SR missing exact final '+field)
    return s

def runtime(s,h):
    once=h.once
    def change(sig,fn):
        nonlocal s
        a,b=h.definition(s,sig);s=s[:a]+fn(s[a:b])+s[b:]
    def prepend(body,text):
        a=body.index('{')+1;return body[:a]+'\n'+text+'\n'+body[a:]
    s=once(s,'#include "nr_guide_contract.h"','#include "nr_guide_contract.h"\n#define NR_HAS_PRIVATE_RESIDUAL_SR 1\n#include "nr_private_sr_guides.h"\n#include "private_sr/nr_post_private_sr_dx12.hpp"\n#include "nr_private_sr_status.h"\n#include "nr_private_sr_artifacts.h"','private SR types')
    s=once(s,'struct ChainConfig {','struct ChainConfig {\n    bool private_residual_sr=false;','private SR config')
    s=once(s,'struct NrContext {','namespace nr_post_product { struct Owner; }\nstruct NrContext {\n    nr_post_product::Owner* private_sr=nullptr;\n    nr_private_sr_status::BudgetRetry private_sr_budget_retry{};','private SR context')
    s=once(s,'void DestroyContext(NrContext &context) {','''namespace nr_post_product {
bool PrivateCall();bool Ready(const NrContext&);bool Destroy(NrContext&);void Maintain(NrContext&);
void Submitted(NrContext&,unsigned long long,LifecycleQueue*,unsigned long long);
void Discarded(NrContext&,unsigned long long);void Poison(NrContext&);
}
void DestroyContext(NrContext &context) {''','private SR lifecycle declarations')
    change('void DestroyContext(',lambda p:once(p,'    {\n        // Serialize returned-error handling',
        '    if(!nr_post_product::Destroy(context))return;\n    {\n        // Serialize returned-error handling','private SR release before NR owner'))
    change('bool CanDestroyContextLocked(',lambda p:once(p,'    return CanDestroyBindingsLocked(context);',
        '    return CanDestroyBindingsLocked(context) && nr_post_product::Ready(context);','private SR retirement gate'))
    change('void CollectRetiredContextsLocked(',lambda p:once(p,'        nr_temporal::Maintain(context);',
        '        nr_temporal::Maintain(context);\n        nr_post_product::Maintain(context);','private SR maintain'))
    change('void CompleteSubmissionBatchLocked(',lambda p:once(p,'        --record->in_flight_submissions;',
        '        for(unsigned c=0;c<record->context_count;++c)if(record->contexts[c])\n            nr_post_product::Submitted(*record->contexts[c],record->token,queue_state,batch.fence_value);\n        --record->in_flight_submissions;','private SR successful submit'))
    change('bool QuarantineUnsubmittedLifecycleRecordLocked(',lambda p:once(p,'    Log("Command list %s discarded',
        '    for(unsigned c=0;c<record->context_count;++c)if(record->contexts[c])nr_post_product::Discarded(*record->contexts[c],token);\n    Log("Command list %s discarded','private SR unsubmitted discard'))
    change('void QuarantineUnprovenSubmissionLocked(',lambda p:once(p,'        record->execution_attempted_without_proof = true;',
        '        record->execution_attempted_without_proof = true;\n        for(unsigned c=0;c<record->context_count;++c)if(record->contexts[c]&&record->contexts[c]->private_sr)nr_post_product::Poison(*record->contexts[c]);','private SR failed capture'))
    change('void ProcessSubmittedCommandLists(',lambda p:once(p,'    } else if (captured && batch.quarantine) {',
        '''    } else if(captured&&batch.active) {
        submission_timing.EnterChain(&g_chain_cs);
        for(unsigned c=0;c<batch.context_count;++c)if(batch.contexts[c]&&batch.contexts[c]->private_sr)nr_post_product::Poison(*batch.contexts[c]);
        submission_timing.LeaveChain(&g_chain_cs);
    }
    if (captured && batch.quarantine) {''','private SR failed signal or queue'))
    s=once(s,'#include "nr_temporal_runtime.inl"','#include "nr_temporal_runtime.inl"\n#include "nr_private_sr_product.inl"','private SR product implementation')
    s=once(s,'struct PostSrSubstitution {','struct PostSrSubstitution {\n    unsigned private_sr_capture=0;','private SR capture ticket')
    change('PostSrSubstitution PrepareAfterSr(',lambda p:once(p,'    nr_regions::Dispatch(command_list,*slot,false);',
        '''    if(slot->result_format==DXGI_FORMAT_R16G16B16A16_FLOAT)
        substitution.private_sr_capture=nr_post_product::Capture(command_list,device,*context,sr_handle,sr_params,feature,
            guides,post_sr_color_mode,guide_width,guide_height,work_width,work_height,width,height);
    nr_regions::Dispatch(command_list,*slot,false);''','private SR pre-native guides'))
    change('bool FinishAfterSr(',lambda p:once(p,'    if (!RecordVisibleTransferLocked(command_list, feature.generation, 1))',
        '''    if(!nr_post_product::Finish(command_list,context,slot,feature,substitution.private_sr_capture,
                              width,height,substitution.output_base_x,substitution.output_base_y)){
        substitution.fatal_recording=0xBAD00005u;timing.safe=false;return false;
    }
    if (!RecordVisibleTransferLocked(command_list, feature.generation, 1))''','private SR final controlled candidate'))
    s=once(s,'    next.temporal_backend=GetPrivateProfileIntW(',
        '    next.private_residual_sr=GetPrivateProfileIntW(L"NRBeforeSR",L"PrivateResidualSR",0,g_ini_path)!=0;\n    next.temporal_backend=GetPrivateProfileIntW(','private SR load')
    s=once(s,'    record_write(WriteIniInt(L"ResidualStabilizer",config.temporal_backend));',
        '    record_write(WriteIniInt(L"PrivateResidualSR",config.private_residual_sr?1:0));\n    record_write(WriteIniInt(L"ResidualStabilizer",config.temporal_backend));','private SR save')
    s=once(s,'bool SameConfig(const ChainConfig &a, const ChainConfig &b) {\n    return ',
        'bool SameConfig(const ChainConfig &a, const ChainConfig &b) {\n    return a.private_residual_sr==b.private_residual_sr && ','private SR configuration identity')
    hooks=[('HookCreate','g_original_create','command_list, feature_id, params, out_handle'),('HookCoreCreate','g_original_core_create','command_list, feature_id, params, out_handle'),('HookDlssdCreate','g_original_dlssd_create','command_list, feature_id, params, out_handle'),
           ('HookEvaluate','g_original_evaluate','command_list, handle, params, callback'),('HookCoreEvaluate','g_original_core_evaluate','command_list, handle, params, callback'),('HookDlssdEvaluate','g_original_dlssd_evaluate','command_list, handle, params, callback'),
           ('HookRelease','g_original_release','handle'),('HookCoreRelease','g_original_core_release','handle'),('HookDlssdRelease','g_original_dlssd_release','handle')]
    for name,original,args in hooks:
        change('NgxResult '+name+'(',lambda p,original=original,args=args:prepend(p,f'    if(nr_post_product::PrivateCall())return {original}?{original}({args}):0xBAD00005u;'))
    change('NgxResult DispatchEvaluate(',lambda p:prepend(p,'    if(nr_post_product::PrivateCall())return original(command_list,handle,params,callback);'))
    s=once(s,'#include "nr_private_sr_product.inl"','#include "nr_private_sr_product.inl"\nbool CoverPrivateShutdownDomain();','private SR domain coverage declaration')
    change('NgxResult HookCoreCreate(',lambda p:once(p,
        '    if(nr_post_product::PrivateCall())return g_original_core_create?g_original_core_create(command_list, feature_id, params, out_handle):0xBAD00005u;',
        '    if(nr_post_product::PrivateCall())return g_original_core_create?g_original_core_create(command_list, feature_id, params, out_handle):0xBAD00005u;\n    const bool private_domain_covered=CoverPrivateShutdownDomain();\n    const auto private_domain_ticket=nr_private_sr_core::CaptureDomainTicket();','private SR native-only domain capture'))
    change('NgxResult HookCoreCreate(',lambda p:once(p,'    observed.Finish(result, handle, params);',
        '    if(result==kNgxSuccess&&handle&&private_domain_covered)\n        nr_private_sr_core::ObserveCoreCreate(command_list,feature_id,handle,result,private_domain_ticket);\n    observed.Finish(result, handle, params);','private SR observed initialized Core domain'))
    for name in ('HookCreate','HookCoreCreate','HookDlssdCreate'):
        change('NgxResult '+name+'(',lambda p:once(p,'    observed.Finish(result, handle, params);',
            '    observed.Finish(result, handle, params);\n    nr_private_sr_core::SealCoreCreate(handle,result);','private SR exact native parent generation'))
    for name in ('HookRelease','HookCoreRelease','HookDlssdRelease'):
        change('NgxResult '+name+'(',lambda p:once(p,'    const FeatureRecord feature = FindFeature(handle);',
            '    nr_private_sr_core::ForgetParent(handle);\n    const FeatureRecord feature = FindFeature(handle);','private SR native release parent invalidation'))
    # Observe the exact original initialization domain, including RR-only hosts.
    for name in ('HookLoaderInit','HookLoaderInitExt','HookLoaderInitProject'):
        change('NgxResult '+name+'(',lambda p:prepend(p,'    const auto private_domain_ticket=nr_private_sr_core::CaptureDomainTicket();'))
        change('NgxResult '+name+'(',lambda p:once(p,'    return result;',
            '    nr_private_sr_core::ObserveInit(device,result,nr_private_sr_core::DomainEvidence::OriginalInit,private_domain_ticket);\n    return result;','private SR native Init receipt'))
    s=once(s,'enum class NgxHookOperation { Release, Create, Evaluate, Init };',
        'enum class NgxHookOperation { Release, Create, Evaluate, Init, Shutdown };','private SR Shutdown operation')
    s=once(s,'NgxHookTarget g_ngx_hook_targets[16]', 'NgxHookTarget g_ngx_hook_targets[24]','private SR Shutdown target capacity')
    s=once(s,'void CoverNgxLifecycleAfterInit(const char *entry);',"""using PrivateShutdown0=NgxResult(*)();
using PrivateShutdown1=NgxResult(*)(ID3D12Device*);
PrivateShutdown0 g_private_shutdown0[2]{};
PrivateShutdown1 g_private_shutdown1[2]{};
template<unsigned N> NgxResult HookPrivateShutdown0(){
    {ScopedSrwExclusive lock(&g_nr_api_lock);nr_private_sr_core::shutdown_inflight.fetch_add(1);nr_private_sr_core::InvalidateInit(nullptr);}
    // No private call can enter the invalidated epoch. Forward without the API
    // lock: native Shutdown may itself call a hooked release needing chain locks.
    const auto result=g_private_shutdown0[N]?g_private_shutdown0[N]():0xBAD00005u;
    nr_private_sr_core::shutdown_inflight.fetch_sub(1);return result;
}
template<unsigned N> NgxResult HookPrivateShutdown1(ID3D12Device* device){
    {ScopedSrwExclusive lock(&g_nr_api_lock);nr_private_sr_core::shutdown_inflight.fetch_add(1);nr_private_sr_core::InvalidateInit(device);}
    const auto result=g_private_shutdown1[N]?g_private_shutdown1[N](device):0xBAD00005u;
    nr_private_sr_core::shutdown_inflight.fetch_sub(1);return result;
}
void CoverNgxLifecycleAfterInit(const char *entry);""",'private SR original Shutdown forwarding')
    s=once(s,'bool InstallLoaderInitHooks(HMODULE loader) {',"""bool InstallPrivateShutdownHooks(HMODULE module,unsigned index){
    if(!module||index>1)return false;
    void* a=reinterpret_cast<void*>(GetProcAddress(module,"NVSDK_NGX_D3D12_Shutdown"));
    void* b=reinterpret_cast<void*>(GetProcAddress(module,"NVSDK_NGX_D3D12_Shutdown1"));
    if(!a&&!b)return false;
    AcquireSRWLockExclusive(&g_evaluate_hook_lock);bool queued=false;
    const bool first=!a||QueueLateHook(NgxHookOperation::Shutdown,a,index?
        reinterpret_cast<void*>(&HookPrivateShutdown0<1>):reinterpret_cast<void*>(&HookPrivateShutdown0<0>),
        reinterpret_cast<void**>(&g_private_shutdown0[index]),"private SR Shutdown",queued);
    const bool second=!b||QueueLateHook(NgxHookOperation::Shutdown,b,index?
        reinterpret_cast<void*>(&HookPrivateShutdown1<1>):reinterpret_cast<void*>(&HookPrivateShutdown1<0>),
        reinterpret_cast<void**>(&g_private_shutdown1[index]),"private SR Shutdown1",queued);
    const bool applied=!queued||MH_ApplyQueued()==MH_OK;
    if(applied)MarkNgxHooksPublished(false);
    const bool covered=first&&second&&applied&&(!a||IsNgxHookPublished(NgxHookOperation::Shutdown,a))&&
        (!b||IsNgxHookPublished(NgxHookOperation::Shutdown,b));
    ReleaseSRWLockExclusive(&g_evaluate_hook_lock);return covered;
}
bool CoverPrivateShutdownDomain(){
    const auto loader=GetModuleHandleW(L"nvngx.dll"),core=GetModuleHandleW(L"_nvngx.dll");
    const bool covered=core&&(!loader||InstallPrivateShutdownHooks(loader,0))&&InstallPrivateShutdownHooks(core,1);
    nr_private_sr_core::shutdown_hooks_covered.store(covered);return covered;
}
bool InstallLoaderInitHooks(HMODULE loader) {""",'private SR Shutdown hook coverage')
    change('void CoverNgxLifecycleAfterInit(const char *entry) {',lambda p:once(p,'    Log("[module] %s returned;',
        '    (void)CoverPrivateShutdownDomain();\n    Log("[module] %s returned;','private SR verified Shutdown coverage'))

    return s

def apply(stage:Path,inputs,h):
    root=stage/'core'
    for name in INPUTS:
        if name=='scripts/patch-private-sr.py':continue
        p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(inputs[name])
    # Core fixtures copy src as one unit. Keep identical module declarations
    # beside the consumer while ordinary C++ module sources retain their tree.
    for name in MODULE:
        if name.endswith(('.hpp','.inl')):
            p=root/'src/private_sr'/name;p.parent.mkdir(parents=True,exist_ok=True)
            p.write_bytes(inputs['experiments/nr-edit/src/'+name])
    def update(path,fn):
        p=root/path;p.write_text(fn(p.read_text(encoding='utf-8-sig')),encoding='utf-8',newline='\n')
    update('src/nr_before_sr.cpp',lambda s:runtime(s,h))
    update('src/nr_source_frame_forward.inl',lambda s:h.once(s,'void OnExecute(',
        'bool PrivateSrInputReadState(ID3D12GraphicsCommandList*,ID3D12Resource*,unsigned&);\nvoid OnExecute(','private SR state declaration'))
    update('src/nr_source_frame_runtime.inl',lambda s:h.once(s,'Source CurrentSource(){', '''bool PrivateSrInputReadState(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,unsigned& state){
    if(!list||!resource)return false;
    Lock lock;const auto recording=RecordingOf(list);if(!recording)return false;
    constexpr unsigned read=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    constexpr unsigned allowed=read|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_DEPTH_READ|D3D12_RESOURCE_STATE_COPY_SOURCE;
    if(StateOf(recording,ResourceOf(resource),state))return (state&read)&&!(state&~allowed);
    // Actual NGX input seam only: consume its shader-readable contract without
    // adding a guessed input transition. Observed contradictions above refuse.
    state=read;return true;
}
Source CurrentSource(){''','private SR native state evidence'))
    update('src/nr_feedback_capture.inl',lambda s:h.once(s,'void FeedbackAppendActivity(FeedbackSession &s, const char *prefix) {',r'''void FeedbackAppendActivity(FeedbackSession &s, const char *prefix) {
    const auto private_config=g_config_generation.load();
    const auto private_state=nr_private_sr_status::state.load();
    const bool private_current=nr_private_sr_status::state_configuration.load()==private_config;
    const bool private_budget=private_current&&nr_private_sr_status::budget_configuration.load()==private_config&&
        (private_state==nr_private_sr_status::State::BudgetCap||private_state==nr_private_sr_status::State::BudgetHeadroom||
         private_state==nr_private_sr_status::State::BudgetUnknown||private_state==nr_private_sr_status::State::MemoryInfoUnknown||
         private_state==nr_private_sr_status::State::Budget)&&
        nr_private_sr_status::budget_instance.load()!=0&&nr_private_sr_status::budget_feature_generation.load()!=0;
    FeedbackAppend(s,"%s.privateSR.lastEvent=%s\n%s.privateSR.guideReason=%u\n%s.privateSR.requested=%llu\n%s.privateSR.rejected=%llu\n%s.privateSR.guideCaptures=%llu\n%s.privateSR.createRecordings=%llu\n%s.privateSR.recorded=%llu\n%s.privateSR.composed=%llu\n%s.privateSR.submitted=%llu\n%s.privateSR.adopted=%llu\n%s.privateSR.planeBytes=%llu\n%s.privateSR.peakPlaneBytes=%llu\n%s.privateSR.retired=%llu\n",
        prefix,private_current?nr_private_sr_status::Name(private_state):"not-current-config",prefix,
        private_current&&nr_private_sr_status::guide_configuration.load()==private_config&&
            (private_state==nr_private_sr_status::State::Guides||private_state==nr_private_sr_status::State::Phase)?
            nr_private_sr_status::guide_reason.load():0,
        prefix,nr_private_sr_status::requested.load(),prefix,nr_private_sr_status::rejected.load(),prefix,nr_private_sr_status::captures.load(),
        prefix,nr_private_sr_status::creations.load(),prefix,nr_private_sr_status::recorded.load(),prefix,nr_private_sr_status::composed.load(),
        prefix,nr_private_sr_status::submitted.load(),prefix,nr_private_sr_status::adopted.load(),prefix,nr_private_sr_status::bytes.load(),
        prefix,nr_private_sr_status::peak.load(),prefix,nr_private_sr_status::retired.load());
    FeedbackAppend(s,"%s.privateSR.stateConfig=%u\n%s.privateSR.budgetConfig=%u\n%s.privateSR.budget.snapshotMatched=%u\n%s.privateSR.budget.instance=%llu\n%s.privateSR.budget.featureGeneration=%llu\n%s.privateSR.budget.reason=%s\n%s.privateSR.budget.requiredBytes=%llu\n%s.privateSR.budget.availableBytes=%llu\n",
        prefix,nr_private_sr_status::state_configuration.load(),prefix,nr_private_sr_status::budget_configuration.load(),
        prefix,private_budget?1u:0u,
        prefix,private_budget?nr_private_sr_status::budget_instance.load():0ull,
        prefix,private_budget?nr_private_sr_status::budget_feature_generation.load():0ull,
        prefix,private_budget?nr_private_sr_status::Name(private_state):"none-current-config",
        prefix,private_budget?nr_private_sr_status::budget_required.load():0ull,
        prefix,private_budget?nr_private_sr_status::budget_available.load():0ull);
    FeedbackAppend(s,"%s.privateSR.gpuTimingAvailable=%u\n%s.privateSR.timingConfig=%llu\n%s.privateSR.timingInstance=%llu\n%s.privateSR.gpuSamples=%llu\n%s.privateSR.gpuFirstCreateMs=%.9g\n%s.privateSR.gpuFirstInferenceMs=%.9g\n%s.privateSR.gpuMedianMs=%.9g\n%s.privateSR.gpuP95Ms=%.9g\n",
        prefix,nr_private_sr_status::gpu_timing_available.load(),prefix,nr_private_sr_status::timing_config.load(),
        prefix,nr_private_sr_status::timing_instance.load(),prefix,nr_private_sr_status::timing_samples.load(),
        prefix,nr_private_sr_status::gpu_first_create_ms.load(),prefix,nr_private_sr_status::gpu_first_inference_ms.load(),
        prefix,nr_private_sr_status::gpu_median_ms.load(),prefix,nr_private_sr_status::gpu_p95_ms.load());
    FeedbackAppend(s,"%s.privateSR.gpuTimingScope=exact queue timestamp after GPU completion and recording release; private residual encode/Feature1/compose only; excludes guide conversion, native SR/RR, NR and complete-frame baseline; stable excludes first eight composed frames; not a 3-percent verdict\n",prefix);
    FeedbackAppend(s,"%s.privateSR.contract=post Feature1/13 NativeOutputValue FP16; NGX native jitter lowres guides; no FG; plane bytes exclude opaque NGX; composed/submitted are not picture acceptance\n",prefix);
''','private SR diagnostics'))
    update('config/nr_before_sr.ini',lambda s:s+'\n; Experimental post-SR edit reconstruction, independent and default off.\nPrivateResidualSR=0\n')
    update('src/nr_feedback_picture.inl',lambda s:h.once(s,'    if (!valid) return; // A busy snapshot must not report constructor defaults as facts.',
        '''    if (!valid) return; // A busy snapshot must not report constructor defaults as facts.
    FeedbackAppend(s,"requested.privateResidualSR=%d\\n",config.private_residual_sr?1:0);
    const char* private_gate=!config.private_residual_sr?"off":!config.enabled?"master-off":
        config.temporal_backend?"residual-stabilizer-conflict":
        static_cast<nr_processing::Start>(nr_processing_runtime::effective.load())!=nr_processing::Start::After?"unsupported-current-route":
        (g_last_carrier_feature.load()!=1&&g_last_carrier_feature.load()!=13)?"waiting-native-Feature1-or-13":"consult-last-event-and-counter-deltas";
    FeedbackAppend(s,"requested.privateSR.configurationGate=%s\\n",private_gate);''','private SR requested flag'))
    update('scripts/build.ps1',lambda s:h.once(s,'$CodecCompiler=Join-Path $Build', '''# Compile owned post-SR module and its exact shader artifacts.
& python (Join-Path $PSScriptRoot 'build-private-sr-module.py') --root $ProjectRoot --compiler $CppCompiler --output (Join-Path $Objects 'private-sr')
if($LASTEXITCODE){throw 'Private SR module build failed.'}
$PrivateSrManifest=Get-Content -LiteralPath (Join-Path $Objects 'private-sr/module.json') -Raw | ConvertFrom-Json
if(-not $PrivateSrManifest.passed){throw 'Private SR module receipt not passed.'}
$ObjectFiles += @($PrivateSrManifest.objects)
$Common += "-I$(Join-Path $ProjectRoot 'experiments/nr-edit/src')"
$Common += "-I$(Join-Path $Objects 'private-sr/bytecode')"
$CodecCompiler=Join-Path $Build''','private SR required module build'))
    for path in ('src/nr_before_sr.cpp','scripts/build.ps1','src/nr_before_sr.rc'):
        update(path,lambda s,path=path:patch_product_identity(s,path))
