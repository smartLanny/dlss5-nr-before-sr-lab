#!/usr/bin/env python3
"""Extract private E_R AFTER the product's own generator. Never edit its checkout."""
from __future__ import annotations
import re

def once(s:str,old:str,new:str)->str:
    if s.count(old)!=1:raise ValueError('Anchor changed or ambiguous: '+old[:140])
    return s.replace(old,new,1)
def end_bracket(s:str,start:int,opening:str,closing:str)->int:
    depth=0;state='code';i=start
    while i<len(s):
        c=s[i];n=s[i:i+2]
        if state=='line':
            if c=='\n':state='code'
        elif state=='comment':
            if n=='*/':state='code';i+=1
        elif state in ('"',"'"):
            if c=='\\':i+=1
            elif c==state:state='code'
        else:
            if n=='//':state='line';i+=1
            elif n=='/*':state='comment';i+=1
            elif c in ('"',"'"):state=c
            elif c==opening:depth+=1
            elif c==closing:
                depth-=1
                if depth==0:return i
        i+=1
    raise ValueError('Unbalanced source at '+str(start))
def function(s:str,signature:str):
    candidates=[];at=0
    while True:
        start=s.find(signature,at)
        if start<0:break
        at=start+len(signature)
        if signature.rstrip().endswith('{'):body=start+signature.rfind('{')
        else:
            op=s.find('(',start)
            if op<0:continue
            close=end_bracket(s,op,'(',')');body=s.find('{',close+1);semi=s.find(';',close+1)
            if body<0 or (semi>=0 and semi<body):continue
        candidates.append((start,end_bracket(s,body,'{','}')+1))
    if len(candidates)!=1:raise ValueError('Expected exactly one definition: '+signature)
    return candidates[0]
def span(s,start,opening,closing):return end_bracket(s,start,opening,closing)+1
def definition(s,signature):
    a,b=function(s,signature);return a,b,s[a:b]
def arguments(s:str)->list[str]:
    out=[];last=0;level=0;quote=None;i=0
    while i<len(s):
        c=s[i]
        if quote:
            if c=='\\':i+=1
            elif c==quote:quote=None
        elif c in ('"',"'"):quote=c
        elif c in '([{':level+=1
        elif c in ')]}':level-=1
        elif c==',' and level==0:out.append(s[last:i]);last=i+1
        i+=1
    out.append(s[last:]);return out
def codec_mode(s:str)->str:
    positions=[];at=0
    while True:
        at=s.find('DispatchCodec(',at)
        if at<0:break
        op=s.index('(',at);end=end_bracket(s,op,'(',')');args=arguments(s[op+1:end])
        if len(args) not in (16,17):raise ValueError('Unreviewed pre-SR codec argument count: '+str(len(args)))
        positions.append((end,(', 1.0f' if len(args)==16 else '')+', private_image ? 2 : 0'));at=end+1
    if not positions:raise ValueError('No actual codec calls found')
    for end,text in reversed(positions):s=s[:end]+text+s[end:]
    return s
def patch(text:str)->str:
    if 'NrPrivateImageRequest' in text:raise ValueError('Private image already patched')
    if 'int effective_mode = 0' not in text or 'ProcessRrPost(' not in text:
        raise ValueError('Input is not the generated product; existing RR/format transforms must run first')
    start,end=function(text,'Substitution PrepareBeforeSr(');old=text[start:end]
    new=once(old,'Substitution PrepareBeforeSr(','Substitution PrepareNrPrivateOrBefore(')
    new=once(new,'const FeatureRecord &feature) {',
        'const FeatureRecord &feature, const NrPrivateImageRequest* request = nullptr) {\n'
        '    const bool private_image=request!=nullptr;\n'
        '    if(private_image&&!NrPrivateImageRequestMatches(*request,command_list,sr_handle,feature))return {};')
    new=once(new,'if(nr_processing_runtime::Effective()!=nr_processing::Start::Before || !nr_compat_present_runtime::InlineDrainReady())',
        'if((!private_image&&nr_processing_runtime::Effective()!=nr_processing::Start::Before) || !nr_compat_present_runtime::InlineDrainReady())')
    new=once(new,'if (!g_config.enabled || g_config.mode != 2 || feature.feature_id != 1 ||',
        'if (!g_config.enabled || (!private_image&&(g_config.mode != 2 || feature.feature_id != 1)) ||')
    anchor='    NrContext *context = FindOrCreateContext('
    new=once(new,anchor,'    if(private_image&&!ResolveRrLinearDepth(sr_params,feature,guides)) {\n        ReleaseCom(device);return substitution;\n    }\n'+anchor)
    anchor='    nr_regions::Prepare(device,*slot,sr_params,guides,'
    new=once(new,anchor,'    if(private_image&&!EnsureRrDepthSlot(device,*slot,guides)) {\n        ReleaseCom(device);return substitution;\n    }\n'+anchor)
    new=once(new,'const bool edit_field_requested = g_config.mode == 2 &&','const bool edit_field_requested = (private_image || g_config.mode == 2) &&')
    anchor='    nr_dual_runtime::Stamp(command_list,*slot,10);'
    new=once(new,anchor,'    if(private_image&&!DispatchRrDepth(command_list,*slot,guides)) {\n        ReleaseCom(device);return substitution;\n    }\n'+anchor)
    anchor='    substitution.active = true;'
    new=once(new,anchor,'''    if(private_image) {
        auto& image=substitution.private_image;
        image.context=context;image.slot=slot;image.original=color;image.edited=slot->result;
        image.guides=guides;image.color=color_contract;
        image.render_width=render_width;image.render_height=render_height;
        image.work_width=width;image.work_height=height;image.origin_x=color_x;image.origin_y=color_y;
        image.source_serial=request->source_serial;image.recording_token=request->recording_token;
        image.ready=true;substitution.timing_slot=slot;
        return substitution; // Before any Reset/Color/subrect mutation of native params.
    }
'''+anchor)
    new=codec_mode(new)
    wrapper='''
Substitution PrepareBeforeSr(ID3D12GraphicsCommandList *command_list,
                             const NgxHandle *sr_handle,
                             const NgxParams *sr_params,
                             const FeatureRecord &feature) {
    return PrepareNrPrivateOrBefore(command_list,sr_handle,sr_params,feature,nullptr);
}
'''
    text=text[:start]+new+wrapper+text[end:]
    return once(text,'struct Substitution {','#include "nr_private_image_core.inl"\n\nstruct Substitution {\n    NrPrivateImage private_image{};')
