"""Opt-in same-frame B/Z spatial experiments, after private SR source patch."""
from pathlib import Path

LOCAL=('src/nr_spatial_experiment_policy.h','src/nr_spatial_experiment_shader.hlsli',
       'src/nr_spatial_experiment_runtime.inl','src/nr_spatial_experiment_ui.inl')
INPUTS=('scripts/patch-spatial-experiment.py',)+LOCAL

def apply(stage:Path,inputs,h):
    root=stage/'core'
    for path in LOCAL:
        target=root/path;target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(inputs[path])
    def update(path,fn):
        target=root/path
        target.write_text(fn(target.read_text(encoding='utf-8-sig')),encoding='utf-8',newline='\n')
    def source(s):
        once=h.once
        shader=inputs['src/nr_spatial_experiment_shader.hlsli'].decode('utf-8')
        s=once(s,')HLSL";',shader+'\n)HLSL";','spatial shader entries in compiled codec')
        s=once(s,'struct ChainConfig {',
            'struct ChainConfig {\n    unsigned spatial_experiment_mode=0;','spatial saved option')
        s=once(s,'    config.style = std::min(config.style, 2u);',
            '    config.spatial_experiment_mode=nr_spatial_experiment::Normalize(config.spatial_experiment_mode);\n    config.style = std::min(config.style, 2u);',
            'spatial option normalization')
        s=once(s,'namespace {\n\n#include "nr_source_frame_forward.inl"',
            'namespace {\n\n#include "nr_spatial_experiment_policy.h"\n#include "nr_source_frame_forward.inl"',
            'spatial policy inside Core private namespace before config')
        s=once(s,'    next.private_residual_sr=GetPrivateProfileIntW(',
            '    next.spatial_experiment_mode=static_cast<unsigned>(GetPrivateProfileIntW(L"NRBeforeSR",L"SpatialExperimentMode",0,g_ini_path));\n    next.private_residual_sr=GetPrivateProfileIntW(',
            'spatial setting load')
        s=once(s,'    record_write(WriteIniInt(L"PrivateResidualSR",config.private_residual_sr?1:0));',
            '    record_write(WriteIniInt(L"SpatialExperimentMode",config.spatial_experiment_mode));\n    record_write(WriteIniInt(L"PrivateResidualSR",config.private_residual_sr?1:0));',
            'spatial setting save')
        s=once(s,'    return a.private_residual_sr==b.private_residual_sr && ',
            '    return a.spatial_experiment_mode==b.spatial_experiment_mode && a.private_residual_sr==b.private_residual_sr && ',
            'spatial setting config identity without history change')
        s=once(s,'    ID3D12Resource *result = nullptr;',
            '''    ID3D12DescriptorHeap* spatial_heap=nullptr;
    ID3D12PipelineState *spatial_area=nullptr,*spatial_guided=nullptr,*spatial_local=nullptr;
    ID3D12Resource *spatial_scratch=nullptr,*spatial_low_b=nullptr,*spatial_low_delta=nullptr;
    ID3D12Resource *spatial_bound_source=nullptr,*spatial_bound_result=nullptr;
    D3D12_RESOURCE_STATES spatial_scratch_state=D3D12_RESOURCE_STATE_COMMON,
        spatial_low_b_state=D3D12_RESOURCE_STATE_COMMON,spatial_low_delta_state=D3D12_RESOURCE_STATE_COMMON;
    unsigned spatial_width=0,spatial_height=0,spatial_low_width=0,spatial_low_height=0;
    unsigned spatial_configuration=0,spatial_retry_config=0;
    unsigned long long spatial_token=0,spatial_bytes=0,spatial_retry_after_frame=0;
    bool spatial_prepared=false;
    unsigned spatial_prepared_config=0,spatial_prepared_mode=0;
    unsigned spatial_prepared_width=0,spatial_prepared_height=0,
        spatial_prepared_base_x=0,spatial_prepared_base_y=0;
    unsigned long long spatial_prepared_feature=0;
    ID3D12Resource *spatial_prepared_source=nullptr,*spatial_prepared_result=nullptr;
    ID3D12Resource *result = nullptr;''','spatial owned slot resources')
        s=once(s,'void DestroySlot(NrSlot &slot) {',
            '''namespace nr_spatial_experiment {
void ReleaseSlot(NrSlot&);
void PrepareForPost(NrContext&,NrSlot&,unsigned,unsigned,unsigned,unsigned);
void SweepIdle();
void Submitted(NrContext&,unsigned long long);
void Discarded(NrContext&,unsigned long long);
}
void DestroySlot(NrSlot &slot) {
    nr_spatial_experiment::ReleaseSlot(slot);''','spatial exact slot retirement')
        s=once(s,'#include "nr_private_sr_product.inl"',
            '#include "nr_private_sr_product.inl"\n#include "nr_spatial_experiment_runtime.inl"',
            'spatial runtime after private product and codec helpers')
        s=once(s,'    slot->edit_field_recorded=false;slot->edit_horizontal_recorded=false;',
            '''    nr_spatial_experiment::PrepareForPost(*context,*slot,width,height,output_x,output_y);
    slot->edit_field_recorded=false;slot->edit_horizontal_recorded=false;''',
            'spatial mutable-slot preflight before RecordNrUse')
        begin,end=h.definition(s,'void CollectRetiredContextsLocked(')
        section=s[begin:end]
        if not section.endswith('\n}'):raise ValueError('spatial retirement sweep anchor changed')
        s=s[:begin]+section[:-2]+'\n    nr_spatial_experiment::SweepIdle();\n}'+s[end:]
        s=once(s,'    if(!nr_post_product::Finish(command_list,context,slot,feature,substitution.private_sr_capture,',
            '''    nr_spatial_experiment::Finish(command_list,context,slot,feature,width,height,
        substitution.output_base_x,substitution.output_base_y);
    if(!nr_post_product::Finish(command_list,context,slot,feature,substitution.private_sr_capture,''',
            'spatial post-final same-frame B/Z recording')
        s=once(s,'            nr_post_product::Submitted(*record->contexts[c],record->token,queue_state,batch.fence_value);',
            '''            {nr_post_product::Submitted(*record->contexts[c],record->token,queue_state,batch.fence_value);
             nr_spatial_experiment::Submitted(*record->contexts[c],record->token);}''',
            'spatial exact successful submit')
        s=once(s,'nr_post_product::Discarded(*record->contexts[c],token);',
            '{nr_spatial_experiment::Discarded(*record->contexts[c],token);nr_post_product::Discarded(*record->contexts[c],token);}',
            'spatial exact unsubmitted discard')
        return s
    update('src/nr_before_sr.cpp',source)
    update('src/nr_unified_pages.inl',lambda s:h.once(s,
        '#include "nr_private_sr_ui.inl"',
        '#include "nr_private_sr_ui.inl"\n#include "nr_spatial_experiment_ui.inl"',
        'spatial UI include'))
    update('src/nr_unified_pages.inl',lambda s:h.once(s,
        '    SettingsSection(NrUi("Colour comparisons","颜色对照"));',
        '    DrawSpatialExperiment(next,changed,discrete);\n    SettingsSection(NrUi("Colour comparisons","颜色对照"));',
        'spatial experiment control'))
    update('config/nr_before_sr.ini',lambda s:s+'\n; Same-frame post-native NR edit experiments; default off.\nSpatialExperimentMode=0\n')
    update('src/nr_feedback_picture.inl',lambda s:h.once(s,
        '    FeedbackAppend(s,"requested.privateResidualSR=%d\\n",config.private_residual_sr?1:0);',
        '''    FeedbackAppend(s,"requested.spatialExperimentMode=%u\\n",config.spatial_experiment_mode);
    FeedbackAppend(s,"requested.privateResidualSR=%d\\n",config.private_residual_sr?1:0);''',
        'spatial saved request distinct from attempt and submit'))
    update('src/nr_acceptance_runtime.inl',lambda s:h.once(s,
        '    if(s.program==Program::Screenshots){\n        g.requested.enabled=s.index!=0;',
        '    if(s.program==Program::Screenshots){\n        g.requested.spatial_experiment_mode=0; // Keep all nine old baselines independent.\n        g.requested.enabled=s.index!=0;',
        'nine acceptance groups force spatial off; original config restores'))
    update('src/nr_feedback_capture.inl',lambda s:h.once(s,
        'void FeedbackAppendActivity(FeedbackSession &s, const char *prefix) {',
        '''void FeedbackAppendActivity(FeedbackSession &s, const char *prefix) {
    const auto& spatial=nr_spatial_experiment::status;
    const unsigned spatial_config=g_config_generation.load();
    const bool spatial_current=spatial.configuration.load()==spatial_config;
    const auto spatial_reason=static_cast<nr_spatial_experiment::Reason>(spatial.reason.load());
    const bool spatial_budget=spatial_current&&(spatial_reason==nr_spatial_experiment::Reason::BudgetCap||
        spatial_reason==nr_spatial_experiment::Reason::BudgetHeadroom||
        spatial_reason==nr_spatial_experiment::Reason::MemoryUnknown||
        spatial_reason==nr_spatial_experiment::Reason::Allocation);
    FeedbackAppend(s,"%s.spatial.mode=%u\\n%s.spatial.state=%s\\n%s.spatial.config=%u\\n%s.spatial.current=%u\\n%s.spatial.requested=%llu\\n%s.spatial.recorded=%llu\\n%s.spatial.submitted=%llu\\n%s.spatial.discarded=%llu\\n%s.spatial.token=%llu\\n%s.spatial.instance=%llu\\n%s.spatial.low=%ux%u\\n%s.spatial.requiredBytes=%llu\\n%s.spatial.threeSlotBytes=%llu\\n%s.spatial.availableBytes=%llu\\n%s.spatial.residentBytes=%llu\\n",
        prefix,spatial.mode.load(),prefix,spatial_current?nr_spatial_experiment::Name(static_cast<nr_spatial_experiment::Reason>(spatial.reason.load())):"not-current-config",
        prefix,spatial.configuration.load(),prefix,spatial_current?1u:0u,prefix,spatial.requested.load(),
        prefix,spatial.recorded.load(),prefix,spatial.submitted.load(),prefix,spatial.discarded.load(),
        prefix,spatial_current?spatial.token.load():0ull,prefix,spatial_current?spatial.instance.load():0ull,
        prefix,spatial_current?spatial.low_width.load():0u,spatial_current?spatial.low_height.load():0u,
        prefix,spatial_budget?spatial.required_bytes.load():0ull,prefix,spatial_budget?spatial.three_slot_bytes.load():0ull,
        prefix,spatial_budget?spatial.available_bytes.load():0ull,
        prefix,spatial.resident_bytes.load());
    FeedbackAppend(s,"%s.spatial.contract=same-frame post-native B/Z FP16, default off; submitted is not picture quality or final display proof\\n",prefix);
    const unsigned spatial_mode=spatial.mode.load();
    FeedbackAppend(s,"%s.spatial.algorithm=%s\\n%s.spatial.countersScope=process-cumulative, not current-config adoption\\n",
        prefix,spatial_mode==1?"Beta8-area-delta-B-guided-four-tap":
            spatial_mode==2?"full-resolution-B-guided-3x3-delta-mean":"off",
        prefix);
''','spatial F8 receipt'))
