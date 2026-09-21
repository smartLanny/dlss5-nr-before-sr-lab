"""Final additive temporal seam; never replace cumulative NR/Core files."""
from pathlib import Path

LOCAL=('src/nr_temporal_status.h','src/nr_temporal_runtime.inl','src/nr_temporal_ui.inl')
TEMPORAL=('temporal_controls.hpp','history_contract.hpp','amd_of_producer.hpp','amd_of_producer.cpp',
 'amd_of_shader.hpp','residual_accumulator.hpp','residual_accumulator_shader.hpp','recorded_temporal.hpp','recorded_temporal.cpp')
INPUTS=('scripts/patch-temporal-runtime.py','scripts/build-temporal-module.py')+LOCAL+tuple('experiments/final_residual/'+n for n in TEMPORAL)

def runtime(s,h):
    once=h.once
    s=once(s,'#include "nr_colour_preferences.h"','#include "nr_colour_preferences.h"\n#define NR_HAS_TEMPORAL_RUNTIME 1\n#include "nr_temporal_status.h"\n#include "nr_process_memory.h"\n#include "temporal/recorded_temporal.hpp"','temporal module includes')
    s=once(s,'struct ChainConfig {','struct ChainConfig {\n    unsigned temporal_backend=0,temporal_scope=0;\n    bool temporal_legacy=false;','temporal default off')
    s=once(s,'void NormalizeConfig(ChainConfig &config) {','void NormalizeConfig(ChainConfig &config) {\n    config.temporal_backend=std::min(config.temporal_backend,2u);\n    config.temporal_scope=std::min(config.temporal_scope,1u);','temporal normalization')
    s=once(s,'bool SameConfig(const ChainConfig &a, const ChainConfig &b) {\n    return ',
        'bool SameConfig(const ChainConfig &a, const ChainConfig &b) {\n    return a.temporal_backend==b.temporal_backend && a.temporal_scope==b.temporal_scope && a.temporal_legacy==b.temporal_legacy && ','temporal same config')
    s=once(s,'    next.color_strength = ReadIniFloat(L"ColorStrength", 1.0f);','', 'obsolete colour read') if '    next.color_strength = ReadIniFloat(L"ColorStrength", 1.0f);' in s else s
    marker='    next.master_virtual_key=GetPrivateProfileIntW'
    start=s.index(marker)
    s=s[:start]+'''    next.temporal_backend=GetPrivateProfileIntW(L"NRBeforeSR",L"ResidualStabilizer",0,g_ini_path);
    next.temporal_scope=GetPrivateProfileIntW(L"NRBeforeSR",L"ResidualStabilizerScope",0,g_ini_path);
    next.temporal_legacy=GetPrivateProfileIntW(L"NRBeforeSR",L"ResidualLegacyWeight",0,g_ini_path)!=0;
'''+s[start:]
    marker='    record_write(WriteIniFloat(L"ColourPriorityStrength",config.colour_priority_strength));'
    s=once(s,marker,marker+'''
    record_write(WriteIniInt(L"ResidualStabilizer",config.temporal_backend));
    record_write(WriteIniInt(L"ResidualStabilizerScope",config.temporal_scope));
    record_write(WriteIniInt(L"ResidualLegacyWeight",config.temporal_legacy?1:0));''','temporal persistence')
    s=once(s,'struct NrContext {','''struct NrContext {
    nr::final_residual::RecordedTemporal* temporal=nullptr;
    unsigned temporal_key=0,temporal_width=0,temporal_height=0;
    unsigned long long temporal_queue=0,temporal_token=0,temporal_retry_tick=0;
    unsigned temporal_attempt_generation=0;''','temporal context owner')
    s=once(s,'    for(auto& guide:context.dual_create_guides)ReleaseCom(guide);','''    if(context.temporal){
        nr_temporal_status::bytes.fetch_sub(context.temporal->stats().bytes);
        delete context.temporal;context.temporal=nullptr;
    }
    for(auto& guide:context.dual_create_guides)ReleaseCom(guide);''','temporal safe context destruction')
    # These callbacks retain the existing native Reset, Execute and Signal proofs.
    a,b=h.definition(s,'void DetachLifecycleCommandListTokenLocked(')
    part=s[a:b]
    part=once(part,'        --context->recording_command_lists;','''        if(context->temporal)context->temporal->end_recording(token);
        --context->recording_command_lists;''','temporal recording end')
    s=s[:a]+part+s[b:]
    a,b=h.definition(s,'void CompleteSubmissionBatchLocked(');part=s[a:b]
    part=once(part,'        context->awaiting_fence_signal = false;','''        context->awaiting_fence_signal = false;
        if(context->temporal){
            const auto just_recorded=context->temporal_token;
            for(unsigned r=0;r<batch.record_count;++r)if(batch.records[r] && batch.records[r]->token!=just_recorded){
                if(!context->temporal->resubmitted(batch.records[r]->token,queue_state->fence,batch.fence_value)){
                    context->lifecycle_dependencies_quarantined=true;
                    MarkLifecycleUncertain("temporal replay completion receipt rejected");nr_temporal_status::state.store(9);
                }
            }
            bool included=false;
            for(unsigned r=0;r<batch.record_count;++r)included|=batch.records[r] && batch.records[r]->token==context->temporal_token;
            if(included){
                if(context->temporal->submitted(queue_state->fence,batch.fence_value,context->temporal_token)){
                    context->temporal_token=0;nr_temporal_status::submitted.fetch_add(1);
                    nr_temporal_status::last_tick.store(GetTickCount64());nr_temporal_status::state.store(8);
                }else{
                    context->lifecycle_dependencies_quarantined=true;
                    MarkLifecycleUncertain("temporal last consumer submission receipt rejected");nr_temporal_status::state.store(9);
                }
            }
        }''','temporal actual adoption')
    s=s[:a]+part+s[b:]
    # One compositor implementation, with an explicit primary-output table.
    # Suppress diagnostic captures for P1; its nonlinear colour/light constants
    # still come from exactly the same current consumer as final Z.
    a,b=h.definition(s,'void DispatchCodec(');part=s[a:b]
    part=once(part,'float frame_confidence = 1.0f, int effective_mode = 0) {',
        'float frame_confidence = 1.0f, int effective_mode = 0, ID3D12DescriptorHeap* primary_heap=nullptr) {','primary composition override')
    part=once(part,'const bool checkpoint_ready = any_combine &&','const bool checkpoint_ready = !primary_heap && any_combine &&','primary no statistics capture')
    part=once(part,'if(any_combine && nr_f8::g_config_hold.load()','if(!primary_heap && any_combine && nr_f8::g_config_hold.load()','primary no F8 capture')
    marker='    ID3D12DescriptorHeap *selected_heap='
    end=part.index('\n',part.index(marker))
    part=part[:end]+'\n    if(primary_heap)selected_heap=primary_heap;'+part[end:]
    part=once(part,'    if (any_combine) {\n        // Recovery follows',
        '    if (any_combine && !primary_heap) {\n        // Recovery follows','primary no descriptor adoption claim')
    part=once(part,'const bool capture_stages = !f8_armed && any_combine &&',
        'const bool capture_stages = !primary_heap && !f8_armed && any_combine &&','primary no stage capture')
    part=part.replace('if(slot.dual_selected)', 'if(!primary_heap&&slot.dual_selected)').replace('if(slot.dual_selected&&','if(!primary_heap&&slot.dual_selected&&')
    for namespace in ('nr_replay::recorder','nr_dual_capture','nr_fg_capture'):
        part=once(part,'if(any_combine)'+namespace+'::Composition' if namespace!='nr_replay::recorder' else 'if(any_combine)nr_replay::recorder.Composition',
            'if(any_combine&&!primary_heap)'+namespace+'::Composition' if namespace!='nr_replay::recorder' else 'if(any_combine&&!primary_heap)nr_replay::recorder.Composition','primary composition capture '+namespace)
    marker='    if (pipeline == g_codec.encode)\n'
    part=once(part,marker,'    if(primary_heap)return; // P1 never updates final-output diagnostics.\n'+marker,'primary no final side effects')
    s=s[:a]+part+s[b:]
    # Split the existing post fields into a helper, then reuse it for P1 and Z.
    assert s.count('    const bool post_color_field=g_config.low_grid_color_edit_field;')==2
    start=s.index('    const bool post_color_field=g_config.low_grid_color_edit_field;',
        s.index('void RestorePostSrParameters('))
    end=s.index('    DispatchCodec(command_list, slot, g_codec.combine, width, height,',start)
    fields=s[start:end]
    fields=fields.replace('context.width,context.height,1.0f,1);','context.width,context.height,1.0f,1,primary_heap);')
    fields=fields.replace('nr_regions::Dispatch(command_list,slot,true);','nr_regions::Dispatch(command_list,slot,true,primary_heap!=nullptr);')
    helper='''
#include "nr_temporal_runtime.inl"
void RecordPostNrFields(ID3D12GraphicsCommandList* command_list,NrContext& context,NrSlot& slot,
    const FeatureRecord& feature,const ColorContract& color_contract,unsigned width,unsigned height,
    ID3D12DescriptorHeap* primary_heap=nullptr){
'''+fields+'}\n'
    s=s[:start]+'''    auto temporal=nr_temporal::Prepare(command_list,context,slot,substitution.guides,feature,
        width,height,substitution.output_base_x,substitution.output_base_y);
    if(temporal && temporal.primary_heap){
        RecordPostNrFields(command_list,context,slot,feature,color_contract,width,height,temporal.primary_heap);
        DispatchCodec(command_list,slot,g_codec.combine,width,height,color_contract,false,
            0,0,0,0,slot.codec_color_mode,width,height,work_width,work_height,1.0f,1,temporal.primary_heap);
        UavBarrier(command_list,temporal.runtime->anchor());
        temporal.inputs.anchor_source=temporal.inputs.frame.source;
    }
    RecordPostNrFields(command_list,context,slot,feature,color_contract,width,height);
'''+s[end:]
    s=once(s,'void RestorePostSrParameters(PostSrSubstitution &substitution) {',helper+'\nvoid RestorePostSrParameters(PostSrSubstitution &substitution) {','post temporal helper definition')
    marker='    if (!RecordVisibleTransferLocked(command_list, feature.generation, 1))'
    s=once(s,marker,'''    if(!nr_temporal::Record(command_list,context,slot,temporal)){
        substitution.fatal_recording=0xBAD00005u;timing.safe=false;return false;
    }
'''+marker,'post final residual once')
    return s

def apply(stage:Path,inputs,h):
    root=stage/'core'
    for name in LOCAL:(root/name).write_bytes(inputs[name])
    target=root/'src/temporal';target.mkdir(exist_ok=True)
    for name in TEMPORAL:(target/name).write_bytes(inputs['experiments/final_residual/'+name])
    (root/'scripts/build-temporal-module.py').write_bytes(inputs['scripts/build-temporal-module.py'])
    def update(path,fn):
        p=root/path;p.write_text(fn(p.read_text(encoding='utf-8')),encoding='utf-8',newline='\n')
    update('src/nr_before_sr.cpp',lambda s:runtime(s,h))
    update('src/nr_source_frame_forward.inl',lambda s:h.once(s,'unsigned long long CurrentRetryToken();','nr_source_frame::Source CurrentSource();\nunsigned long long CurrentRetryToken();','source accessor'))
    update('src/nr_region_runtime.inl',lambda s:s.replace('NrSlot& slot,bool field){','NrSlot& slot,bool field,bool primary=false){').replace('auto* packet=field&&slot.dual_selected?', 'auto* packet=field&&!primary&&slot.dual_selected?'))
    update('src/nr_feedback_capture.inl',lambda s:h.once(s,'void FeedbackAppendActivity(FeedbackSession &s, const char *prefix) {',
        '''void FeedbackAppendActivity(FeedbackSession &s, const char *prefix) {
    FeedbackAppend(s,"%s.temporal.state=%s\\n%s.temporal.recorded=%llu\\n%s.temporal.submitted=%llu\\n%s.temporal.declined=%llu\\n%s.temporal.accountedBytes=%llu\\n%s.temporal.peakBytes=%llu\\n%s.temporal.lastSubmitTick=%llu\\n%s.temporal.scope=Core-owned textures plus SDK report; excludes NGX/driver; recorded is not adopted or completed; game quality unverified\\n",
        prefix,nr_temporal_status::Name(nr_temporal_status::state.load()),prefix,nr_temporal_status::recorded.load(),
        prefix,nr_temporal_status::submitted.load(),prefix,nr_temporal_status::rejected.load(),prefix,nr_temporal_status::bytes.load(),
        prefix,nr_temporal_status::peak.load(),prefix,nr_temporal_status::last_tick.load(),prefix);''','temporal F8 receipt'))
    update('config/nr_before_sr.ini',lambda s:s+'\n; 0 off, 1 proven native MV, 2 AMD OF. Scope: 0 whole, 1 later layers.\nResidualStabilizer=0\nResidualStabilizerScope=0\nResidualLegacyWeight=0\n')
    update('scripts/build.ps1',lambda s:h.once(s,'$CodecCompiler=Join-Path $Build', '''# Required compiled production temporal/AMD backend; external SDK cache stays private.
& python (Join-Path $PSScriptRoot 'build-temporal-module.py') --source (Join-Path $ProjectRoot 'src/temporal') --compiler $CppCompiler --sdk $env:DLSS5_FFX_SDK_ROOT --sdk-cache $env:DLSS5_FFX_CACHE_ROOT --output (Join-Path $Objects 'temporal')
if($LASTEXITCODE){throw 'Temporal module or pinned AMD SDK validation failed.'}
$TemporalManifest=Get-Content -LiteralPath (Join-Path $Objects 'temporal/module.json') -Raw | ConvertFrom-Json
$ObjectFiles += @($TemporalManifest.objects)
$CodecCompiler=Join-Path $Build''','required temporal build'))
