#!/usr/bin/env python3
"""Final, atomic, opt-in quality1 overlay on the complete unified10 Core.
Own paired decoded edits; no third-party implementation or runtime payload.
"""
from __future__ import annotations
from pathlib import Path

INPUTS=('scripts/patch-reconstruction.py','src/nr_reconstruction_policy.h',
        'src/nr_reconstruction_math.hlsli','src/nr_reconstruction_sampling.hlsli',
        'src/nr_reconstruction_ui.inl')

def patch_runtime(text: str, inputs: dict[str,bytes], helpers) -> str:
    once,edit,definition=helpers.once,helpers.edit,helpers.definition
    def body(s,key):return s[slice(*definition(s,key))]
    unchanged=('void EncodeCS(','void EncodeV2CS(','bool SameHistoryConfig(',
        'struct CodecConstants {','cbuffer Constants : register(b0) {',
        'bool BindNrParameters(','float4 LoadFinalNeuralStoredV2(',
        'float3 SampleProxyLinearV2(','float3 SampleNeuralLinearV2(',
        'ColourLabRgbResult ColourLabRestoreRgb(','ColourLabRgbResult ColourStageRestore(',
        'ColourStagePixel ColourStageReference(')
    saved={k:body(text,k) for k in unchanged}
    api_tokens=('ResourceBarrier(', 'CreateCommittedResource(', 'EvaluateFeature(', 'CreateFeature(')
    counts={token:text.count(token) for token in api_tokens}
    if 'NR_RECONSTRUCTION_MATH_HLSLI' in text:raise ValueError('Reconstruction overlay already applied')
    maths=inputs['src/nr_reconstruction_math.hlsli'].decode('utf-8')
    sampling=inputs['src/nr_reconstruction_sampling.hlsli'].decode('utf-8')
    if ')HLSL"' in maths+sampling:raise ValueError('Raw string delimiter inside shader')
    text=once(text,'float3 ColourLabPreparedReference(',maths+'\nfloat3 ColourLabPreparedReference(','quality1 math')
    text=once(text,'struct ColourStagePixel {',sampling+'\nstruct ColourStagePixel {','quality1 samplers')
    text=once(text,'#include "nr_colour_edge_policy.h"','#include "nr_colour_edge_policy.h"\n#include "nr_reconstruction_policy.h"','quality1 policy')
    text=once(text,'struct ChainConfig {','struct ChainConfig {\n    int reconstruction_mode = 0;\n    bool near_black_chroma_guard = false;','quality1 config')
    text=once(text,'void NormalizeConfig(ChainConfig &config) {','void NormalizeConfig(ChainConfig &config) {\n    config.reconstruction_mode=(config.reconstruction_mode==1 || config.reconstruction_mode==2)?config.reconstruction_mode:0;','quality1 normalize')
    load_anchor='    next.colour_edge_recovery = nr_colour_edge::Normalize(GetPrivateProfileIntW('
    text=once(text,load_anchor,
        '    next.reconstruction_mode=nr_reconstruction::Normalize(GetPrivateProfileIntW(L"NRBeforeSR",L"ReconstructionMode",0,g_ini_path));\n'
        '    next.near_black_chroma_guard=GetPrivateProfileIntW(L"NRBeforeSR",L"NearBlackChromaGuard",0,g_ini_path)!=0;\n'
        +load_anchor,'quality1 load')
    text=once(text,'    record_write(WriteIniInt(L"ProxyCurve", static_cast<int>(config.proxy_curve)));',
        '    record_write(WriteIniInt(L"ReconstructionMode",nr_reconstruction::Normalize(config.reconstruction_mode)));\n'
        '    record_write(WriteIniInt(L"NearBlackChromaGuard",config.near_black_chroma_guard?1:0));\n'
        '    record_write(WriteIniInt(L"ProxyCurve", static_cast<int>(config.proxy_curve)));','quality1 save')
    eq='bool SameConfig(const ChainConfig &a, const ChainConfig &b) {\n    return '
    text=once(text,eq,eq+'a.reconstruction_mode==b.reconstruction_mode && a.near_black_chroma_guard==b.near_black_chroma_guard && ','quality1 comparison')
    def dispatch(s):
        cfg='frame_config' if 'ChainConfig frame_config = g_config;' in s else 'g_config'
        anchor='    const float effective_color_strength = color_policy.effective_color_strength;'
        s=once(s,anchor,anchor+f'''
    const auto reconstruction=nr_reconstruction::Select(
        {cfg}.reconstruction_mode, {cfg}.near_black_chroma_guard, any_combine,
        restore_encoding != nr_colour_restore::Encoding::Unknown,
        {cfg}.colour_restore_mode==2 || {cfg}.colour_neutral_probe,
        v2_combine, edge_scaled, {cfg}.colour_edge_recovery!=0,
        {cfg}.low_grid_edit_field || {cfg}.low_grid_color_edit_field,
        effective_color_strength>0.000001f,
        {cfg}.light_preview!=0 || {cfg}.region_preview!=0 || {cfg}.face_preview);
    color_policy.requested_reconstruction_mode={cfg}.reconstruction_mode;
    color_policy.recorded_reconstruction_mode=reconstruction.mode;
    color_policy.reconstruction_reason=reconstruction.reconstruction;
    color_policy.requested_near_black_guard={cfg}.near_black_chroma_guard;
    color_policy.recorded_near_black_guard=reconstruction.chroma;
    color_policy.near_black_reason=reconstruction.protection;''','quality1 actual eligibility')
        # Publish AFTER existing generic/region assignment and BEFORE captures.
        anchors=['    if(any_combine)nr_replay::recorder.Composition(slot.replay_capture_index,&constants,',
                 '    if(any_combine&&!primary_heap)nr_replay::recorder.Composition(slot.replay_capture_index,&constants,']
        matching=[a for a in anchors if a in s]
        if len(matching)!=1:raise ValueError('Actual capture/primary heap scope changed')
        anchor=matching[0]
        return once(s,anchor,'    constants.region_flags |= reconstruction.flags;\n'+anchor,'quality1 actual captured constants')
    text=edit(text,'void DispatchCodec(',dispatch)
    def combine_v2(s):
        anchor='    [branch] if (ColourEdgeRequested()) {\n        ColourLabPair pair = ColourEdgeSamplePair(center,'
        s=once(s,anchor,'''    [branch] if (NrReconScaled()) {
        NrReconSamplePair(center, ColourLabPreparedReference(storedOriginal.rgb,source),
            proxy,neural,edgePairValid);
    } else if (ColourEdgeRequested()) {
        ColourLabPair pair = ColourEdgeSamplePair(center,''','quality1 paired input in actual v2')
        anchor='    float3 storedResult = EncodeCompositionToStoredV2(result);'
        return once(s,anchor,'''    [branch] if (NrReconChromaRequested() && !lightStylePreview)
        result=NrReconPreserveColour(source, ColourLabPreparedReference(storedOriginal.rgb,source),
            proxy, neural, result, boundedSdrStorage, true);
'''+anchor,'quality1 v2 final colour')
    text=edit(text,'void CombineV2CS(',combine_v2)
    def bind_capture(s):
        split=s.index('    float3 source = DecodeStoredForV2(storedOriginal.rgb);')
        prefix,tail=s[:split],s[split:]
        anchor='    [branch] if((RegionFlags&32u)!=0u'
        tail=once(tail,anchor,'    const float3 capturedProxy=proxy, capturedNeural=neural;\n'+anchor,'capture immutable pre-generic pair')
        import re
        pattern=r'WriteCombinedV2\((.*?)\);'
        # All calls below sampling can use the same stored P/N. Raw branch above
        # remains the exact original, including its synthetic neutral semantics.
        tail,count=re.subn(pattern,lambda m:'WriteCombinedV2('+m[1]+', capturedProxy, capturedNeural, NrReconScaled());',tail,flags=re.S)
        if count!=8:raise ValueError('Review capture call sites: '+str(count))
        return prefix+tail
    text=edit(text,'void CombineV2CS(',bind_capture)
    def capture_writer(s):
        signature='void WriteCombinedV2(uint2 coord, uint2 destinationCoord, float4 result) {'
        s=once(s,signature,'void WriteCombinedV2(uint2 coord, uint2 destinationCoord, float4 result,\n    float3 capturedProxy, float3 capturedNeural, bool paired) {','capture single writer')
        start=s.index('    // Real N is kept')
        end=s.index('    StageCapture.Store4(offset,',start)
        inherited=s[start:end]
        replacement='''    float3 edgeCaptureP,edgeCaptureN;
    [branch] if(paired) {
        edgeCaptureP=(Passthrough&1u)!=0u?capturedProxy:LinearToSRGB(capturedProxy);
        edgeCaptureN=(Passthrough&1u)!=0u?capturedNeural:LinearToSRGB(capturedNeural);
    } else {
'''+inherited.replace('float3 edgeCaptureP =','edgeCaptureP =').replace('float3 edgeCaptureN =','edgeCaptureN =')+'    }\n'
        return s[:start]+replacement+s[end:]
    text=edit(text,'void WriteCombinedV2(',capture_writer)
    wrapper='''void WriteCombinedV2(uint2 coord,uint2 destinationCoord,float4 result) {
    WriteCombinedV2(coord,destinationCoord,result,0.0.xxx,0.0.xxx,false);
}
'''
    _,writer_end=definition(text,'void WriteCombinedV2(')
    text=text[:writer_end]+'\n'+wrapper+text[writer_end:]
    def combine_post(s):
        anchor='    float3 sourceDomainResult =\n'
        return once(s,anchor,'''    [branch] if (NrReconChromaRequested() && !lightStylePreview)
        result=NrReconPreserveColour(source, ColourLabPreparedReference(storedOriginal.rgb,source),
            proxy, neural, result, (Passthrough & (1u|16u|32u))!=0u, true);
'''+anchor,'quality1 post final colour')
    text=edit(text,'void CombineCS(',combine_post)
    for key,value in saved.items():
        if body(text,key)!=value:raise ValueError('Reconstruction changed preserved function: '+key)
    for token,count in counts.items():
        if text.count(token)!=count:raise ValueError('GPU/API ownership changed: '+token)
    return text

def apply(stage: Path, inputs: dict[str,bytes], helpers) -> None:
    def policy(s):
        return helpers.once(s,'struct NrColorPolicy {','''struct NrColorPolicy {
    int requested_reconstruction_mode=0,recorded_reconstruction_mode=0;
    bool requested_near_black_guard=false,recorded_near_black_guard=false;
    nr_reconstruction::Reason reconstruction_reason=nr_reconstruction::Reason::Off;
    nr_reconstruction::Reason near_black_reason=nr_reconstruction::Reason::Off;''','quality1 policy receipt')
    def feedback(s):
        marker='    if (!valid) return; // A busy snapshot must not report constructor defaults as facts.'
        return helpers.once(s,marker,marker+r'''
    FeedbackAppend(s,"requested.reconstructionMode=%d\nrequested.nearBlackChromaGuard=%d\n",
        config.reconstruction_mode,config.near_black_chroma_guard?1:0);
    FeedbackAppend(s,"reconstruction.contract=RegionFlags bits8-9 paired source-guided 4/16; bit10 continuous near-black chroma; same-call encoding; unchanged model/history; preserves chosen luminance; requested or recorded is not GPU/pixel proof; capture P/N uses effective pre-generic pair; optional detail-decision plane retains original detail estimator\n");
''','quality1 F8 scope')
    def activity(s):
        marker='    FeedbackAppendDualSnapshot(s, prefix);'
        return helpers.once(s,marker,r'''
    const auto reconstructionSnapshot=SnapshotColorPolicy();
    if(reconstructionSnapshot.status==NrColorPolicyStatus::Current) {
        const auto& r=reconstructionSnapshot.policy;
        FeedbackAppend(s,"%s.reconstruction.config=%u\n%s.reconstruction.recorded=%d\n%s.reconstruction.reason=%s\n%s.nearBlackChroma.recorded=%d\n%s.nearBlackChroma.reason=%s\n",
            prefix,reconstructionSnapshot.config_generation,
            prefix,r.recorded_reconstruction_mode,prefix,nr_reconstruction::ReasonName(r.reconstruction_reason),
            prefix,r.recorded_near_black_guard?1:0,prefix,nr_reconstruction::ReasonName(r.near_black_reason));
    } else FeedbackAppend(s,"%s.reconstruction.recorded=unknown\n%s.nearBlackChroma.recorded=unknown\n",prefix,prefix);
'''+marker,'quality1 independently scoped start/end evidence')
    def pages(s):
        marker='void DrawUnifiedExperiments(ChainConfig& next,unsigned generation,bool pending,bool& changed,bool& discrete) {'
        return helpers.once(s,marker,'#include "nr_reconstruction_ui.inl"\n'+marker+'\n    DrawReconstructionControls(next,generation,pending,changed,discrete);','quality1 actual UI')
    transforms={'src/nr_before_sr.cpp':lambda s:patch_runtime(s,inputs,helpers),
        'src/nr_color_policy.inl':policy,'src/nr_feedback_picture.inl':feedback,
        'src/nr_feedback_capture.inl':activity,'src/nr_unified_pages.inl':pages}
    # Resolve all anchors before publishing any generated file.
    updates={p:f((stage/'core'/p).read_text(encoding='utf-8')) for p,f in transforms.items()}
    for p,s in updates.items():(stage/'core'/p).write_text(s,encoding='utf-8',newline='\n')
    for p in INPUTS[1:]:(stage/'core'/p).write_bytes(inputs[p])
