"""Product post-final private SR; applied after the complete quality generator."""
from pathlib import Path
VERSION = '0.5.2-beta.3'
LOCAL=('src/nr_private_sr_guides.h','src/nr_private_sr_guides.cpp','src/nr_private_sr_status.h',
       'src/nr_private_sr_product.inl','src/nr_private_sr_ui.inl','src/nr_private_sr_artifacts.h')
MODULE=('nr_edit_dx12.hpp','nr_edit_dx12.cpp','nr_post_residual_dx12.hpp','nr_post_residual_dx12.cpp',
        'nr_post_private_sr_dx12.hpp','nr_post_private_sr_dx12.cpp','nr_private_sr_runtime.hpp','nr_private_sr_core_adapter.inl')
SHADERS=('nr_rr_edit.hlsl','nr_deferred_edit.hlsl','nr_normalize_guides.hlsl','nr_post_residual.hlsl','nr_edit_scalar.hlsli','nr_edit_carrier.hlsli')
INPUTS=('scripts/patch-private-sr.py','scripts/build-private-sr-module.py')+LOCAL+tuple('experiments/nr-edit/src/'+p for p in MODULE)+tuple('experiments/nr-edit/shaders/'+p for p in SHADERS)+('experiments/nr-edit/tools/compile_edit_shader.cpp','experiments/nr-edit/tools/compile_edit_artifacts.py')

def runtime(s,h):
    once=h.once
    def change(sig,fn):
        nonlocal s
        a,b=h.definition(s,sig);s=s[:a]+fn(s[a:b])+s[b:]
    def prepend(body,text):
        a=body.index('{')+1;return body[:a]+'\n'+text+'\n'+body[a:]
    s=once(s,'#include "nr_guide_contract.h"','#include "nr_guide_contract.h"\n#define NR_HAS_PRIVATE_RESIDUAL_SR 1\n#include "nr_private_sr_guides.h"\n#include "private_sr/nr_post_private_sr_dx12.hpp"\n#include "nr_private_sr_status.h"\n#include "nr_private_sr_artifacts.h"','private SR types')
    s=once(s,'struct ChainConfig {','struct ChainConfig {\n    bool private_residual_sr=false;','private SR config')
    s=once(s,'struct NrContext {','namespace nr_post_product { struct Owner; }\nstruct NrContext {\n    nr_post_product::Owner* private_sr=nullptr;','private SR context')
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
    FeedbackAppend(s,"%s.privateSR.lastEvent=%s\n%s.privateSR.guideReason=%u\n%s.privateSR.requested=%llu\n%s.privateSR.rejected=%llu\n%s.privateSR.guideCaptures=%llu\n%s.privateSR.createRecordings=%llu\n%s.privateSR.recorded=%llu\n%s.privateSR.composed=%llu\n%s.privateSR.submitted=%llu\n%s.privateSR.adopted=%llu\n%s.privateSR.planeBytes=%llu\n%s.privateSR.peakPlaneBytes=%llu\n%s.privateSR.retired=%llu\n",
        prefix,nr_private_sr_status::Name(nr_private_sr_status::state.load()),prefix,nr_private_sr_status::guide_reason.load(),
        prefix,nr_private_sr_status::requested.load(),prefix,nr_private_sr_status::rejected.load(),prefix,nr_private_sr_status::captures.load(),
        prefix,nr_private_sr_status::creations.load(),prefix,nr_private_sr_status::recorded.load(),prefix,nr_private_sr_status::composed.load(),
        prefix,nr_private_sr_status::submitted.load(),prefix,nr_private_sr_status::adopted.load(),prefix,nr_private_sr_status::bytes.load(),
        prefix,nr_private_sr_status::peak.load(),prefix,nr_private_sr_status::retired.load());
    FeedbackAppend(s,"%s.privateSR.contract=post Feature1 FP16 SDR; zero proven native jitter; no FG; plane bytes exclude opaque NGX; composed/submitted are not picture acceptance\n",prefix);
''','private SR diagnostics'))
    update('config/nr_before_sr.ini',lambda s:s+'\n; Experimental post-SR edit reconstruction, independent and default off.\nPrivateResidualSR=0\n')
    update('src/nr_feedback_picture.inl',lambda s:h.once(s,'    if (!valid) return; // A busy snapshot must not report constructor defaults as facts.',
        '''    if (!valid) return; // A busy snapshot must not report constructor defaults as facts.
    FeedbackAppend(s,"requested.privateResidualSR=%d\\n",config.private_residual_sr?1:0);
    const char* private_gate=!config.private_residual_sr?"off":!config.enabled?"master-off":
        config.temporal_backend?"residual-stabilizer-conflict":
        static_cast<nr_processing::Start>(nr_processing_runtime::effective.load())!=nr_processing::Start::After?"unsupported-current-route":
        g_last_carrier_feature.load()!=1?"waiting-native-Feature1":"consult-last-event-and-counter-deltas";
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
    # Final candidate identity is distinct from the frozen intermediate overlays.
    import re
    for path in ('src/nr_before_sr.cpp','scripts/build.ps1'):
        update(path,lambda s:s.replace('beta0.5-dline21-unified10-reconstruction1-ui1',VERSION))
    def resource(s):
        s=s.replace('FILEVERSION 0,5,1,26','FILEVERSION 0,5,2,3').replace('PRODUCTVERSION 0,5,1,26','PRODUCTVERSION 0,5,2,3')
        return re.sub(r'(VALUE "(?:FileVersion|ProductVersion)", )"[^"\n]+"',lambda m:m[1]+'"'+VERSION+'\\0"',s)
    update('src/nr_before_sr.rc',resource)
