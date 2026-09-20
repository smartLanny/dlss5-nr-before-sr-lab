#!/usr/bin/env python3
"""Add paired edge reconstruction AFTER the reviewed colour4 stage overlay.

A separate opt-in. The inherited raw, surrogate, model, source, history and
lifetime code are not replaced. Validate all output before writing the stage.
"""
from __future__ import annotations
from pathlib import Path

VERSION = 'beta0.5-dline21-unified4'
INPUTS = ('scripts/patch-colour-edge.py', 'src/nr_colour_edge.hlsli', 'src/nr_colour_edge_policy.h',
          'src/nr_quality_recipe.h', 'src/nr_quality_ui.inl','src/nr_hotkeys.h','src/nr_colour_preferences.h',
          'src/nr_unified_settings.inl', 'src/nr_unified_pages.inl')


def patch_runtime(text: str, shader: str, helpers) -> str:
    once, edit, definition = helpers.once, helpers.edit, helpers.definition
    def body(s,k): return s[slice(*definition(s,k))]
    keep = ('void EncodeCS(', 'void EncodeV2CS(', 'bool SameHistoryConfig(',
            'struct CodecConstants {','cbuffer Constants : register(b0) {',
            'bool BindNrParameters(', 'float4 LoadFinalNeuralStoredV2(',
            'float3 SampleProxyLinearV2(', 'float3 SampleNeuralLinearV2(',
            'ColourLabRgbResult ColourStageRestore(', 'void CombineCS(', 'void WriteCombined(')
    saved={k:body(text,k) for k in keep}
    if 'NR_COLOUR_EDGE_HLSLI' in text or ')HLSL"' in shader: raise ValueError('Invalid/repeated edge overlay')
    text=once(text, 'struct ColourStagePixel {', shader+'\nstruct ColourStagePixel {', 'edge helper placement')
    text=once(text, '#include "nr_colour_stage_policy.h"',
        '#include "nr_colour_stage_policy.h"\n#include "nr_colour_edge_policy.h"\n#include "nr_quality_recipe.h"', 'edge policy include')
    text=once(text, 'struct ChainConfig {',
        'struct ChainConfig {\n    int colour_edge_recovery = 0; // Output-only opt-in.', 'edge configuration')
    text=once(text, 'void NormalizeConfig(ChainConfig &config) {',
        'void NormalizeConfig(ChainConfig &config) {\n    config.colour_edge_recovery = nr_colour_edge::Normalize(config.colour_edge_recovery);', 'edge normalize')
    text=once(text, '    next.color_strength = ReadIniFloat(L"ColorStrength", 1.0f);',
        '    next.colour_edge_recovery = nr_colour_edge::Normalize(GetPrivateProfileIntW(\n'
        '        L"NRBeforeSR", L"ColourEdgeRecovery", 0, g_ini_path));\n'
        '    next.color_strength = ReadIniFloat(L"ColorStrength", 1.0f);', 'edge INI load')
    text=once(text, '    record_write(WriteIniInt(L"ProxyCurve", static_cast<int>(config.proxy_curve)));',
        '    record_write(WriteIniInt(L"ColourEdgeRecovery", config.colour_edge_recovery));\n'
        '    record_write(WriteIniInt(L"ProxyCurve", static_cast<int>(config.proxy_curve)));', 'edge INI save')
    same='bool SameConfig(const ChainConfig &a, const ChainConfig &b) {\n    return '
    text=once(text,same,same+'a.colour_edge_recovery == b.colour_edge_recovery && ', 'edge transaction')

    def dispatch(s):
        cfg='frame_config' if 'ChainConfig frame_config = g_config;' in s else 'g_config'
        anchor='    const bool colour_restore = colour_restore_flags != 0u;'
        s=once(s,anchor,anchor+'\n'
            '    const bool edge_scaled = (model_width && model_width != width) || (model_height && model_height != height);\n'
            '    const unsigned edge_flags = nr_colour_edge::Flags(\n'
            f'        {cfg}.colour_edge_recovery, any_combine, colour_restore,\n'
            '        restore_encoding != nr_colour_restore::Encoding::Unknown, v2_combine, edge_scaled);', 'actual edge eligibility')
        s=once(s,'    const float effective_color_strength = color_policy.effective_color_strength;',
            f'    color_policy.requested_edge_recovery = {cfg}.colour_edge_recovery;\n'
            '    color_policy.recorded_edge_recovery = edge_flags != 0u;\n'
            '    const float effective_color_strength = color_policy.effective_color_strength;', 'edge recorded status')
        return once(s,'    const unsigned int base_codec_flags = source_flags |',
            '    const unsigned int base_codec_flags = source_flags | edge_flags |','edge codec flag')
    text=edit(text,'void DispatchCodec(',dispatch)

    def raw(s):
        a='    answer.proxy = mapped ? SampleProxyLinearV2(center) : LoadProxyLinearV2(int2(pixel));\n    answer.neural = mapped ? ColourStageSampleNeural(center) : ColourStageLoadNeural(int2(pixel));'
        s=once(s,a,'''    bool pairValid = true;
    [branch] if (mapped && ColourEdgeRequested()) {
        ColourLabPair pair = ColourEdgeSamplePair(center, prepared, ColourStageNeutral());
        answer.proxy = pair.proxy;
        answer.neural = pair.neural;
        pairValid = pair.valid;
    } else {
'''+a+'''
    }''', 'raw uses paired weights, inherited surrogate remains separate')
        return once(s,'    valid = valid && IsFiniteModelStoredV2(original.rgb) &&',
            '    valid = valid && pairValid && IsFiniteModelStoredV2(original.rgb) &&','raw pair validity')
    text=edit(text,'ColourStagePixel ColourStageReference(',raw)

    def protected(s):
        a='    float3 proxy = SampleProxyLinearV2(center);\n    float3 neural = SampleNeuralLinearV2(center);'
        s=once(s,a,'''    float3 proxy, neural;
    bool edgePairValid = true;
    [branch] if (ColourEdgeRequested()) {
        ColourLabPair pair = ColourEdgeSamplePair(center,
            ColourLabPreparedReference(storedOriginal.rgb, source), false);
        proxy = pair.proxy;
        neural = pair.neural;
        edgePairValid = pair.valid;
    } else {
        proxy = SampleProxyLinearV2(center);
        neural = SampleNeuralLinearV2(center);
    }''','protected paired weights')
        return once(s,'    bool finiteInputs = ModelNeighborhoodFiniteV2(center) &&',
            '    bool finiteInputs = edgePairValid && ModelNeighborhoodFiniteV2(center) &&','protected pair validity')
    text=edit(text,'void CombineV2CS(',protected)

    def capture(s):
        anchor='    StageCapture.Store4(offset,\n        asuint(LoadSourceStoredV2(int2(coord))));'
        s=once(s,anchor,'''    // Real N is kept even during the inherited synthetic N=P instrument.
    float3 edgeCaptureP = SampleProxyStoredV2(float2(coord)+0.5);
    float3 edgeCaptureN = SampleNeuralStoredV2(float2(coord)+0.5);
    [branch] if (ColourEdgeRequested()) {
        float3 fullSource = DecodeStoredForV2(LoadSourceStoredV2(int2(coord)).rgb);
        ColourLabPair pair = ColourEdgeSamplePair(float2(coord)+0.5,
            ColourLabPreparedReference(LoadSourceStoredV2(int2(coord)).rgb,fullSource), false);
        edgeCaptureP = (Passthrough & 1u) != 0u ? pair.proxy : LinearToSRGB(pair.proxy);
        edgeCaptureN = (Passthrough & 1u) != 0u ? pair.neural : LinearToSRGB(pair.neural);
    }
'''+anchor,'capture actual sampling pairs')
        s=once(s,'asuint(float4(SampleProxyStoredV2(float2(coord) + 0.5), 1.0))',
            'asuint(float4(edgeCaptureP, 1.0))','capture P')
        return once(s,'asuint(float4(SampleNeuralStoredV2(float2(coord) + 0.5), 1.0))',
            'asuint(float4(edgeCaptureN, 1.0))','capture N')
    text=edit(text,'void WriteCombinedV2(',capture)
    for name in ('kAddonVersion','kBuildIdentity'):
        text=once(text,f'constexpr const char *{name} = "beta0.5-dline21-colour4";',
            f'constexpr const char *{name} = "{VERSION}";','edge identity')
    for k,v in saved.items():
        if body(text,k)!=v: raise ValueError('Edge changed invariant: '+k)
    return text


def patch_ui(text, helpers, replacement):
    # Validate the previous stage's complete UI before replacing presentation.
    # No new preference shadow state or GPU setting is introduced here.
    anchors = ('void DrawColourLabControls(', 'next.colour_neutral_probe',
               'sample.policy.recorded_neutral_probe', 'RequestStageCapture()')
    if any(anchor not in text for anchor in anchors):
        raise ValueError('Expected the complete colour4 UI before consolidation')
    return replacement


def patch_light_group(text, helpers):
    anchor = '    DrawColourLabControls(next, generation, has_pending, changed, apply_discrete_edit);'
    return helpers.once(text, anchor, r'''
    if (next.light_preview != 0u) {
        SettingsNote(ui("Lighting diagnostic preview is active, not the final picture.",
            "光影诊断预览仍在运行，当前不是最终画面。"));
        if (ImGui::Button(ui("Exit lighting preview###quality-preview-off", "退出光影预览###quality-preview-off"))) {
            next.light_preview = 0u; changed = true; apply_discrete_edit = true;
        }
    }
    if (!ImGui::CollapsingHeader(ui("Advanced lighting controls###quality-lighting",
                                  "进阶光影调节（保留原设置）###quality-lighting"))) return;''',
        'collapse secondary artistic controls without changing preferences')


def apply(stage: Path, inputs: dict[str,bytes], helpers) -> None:
    shader=inputs['src/nr_colour_edge.hlsli'].decode('utf-8')
    def policy(s):
        return helpers.once(s,'struct NrColorPolicy {','struct NrColorPolicy {\n'
            '    int requested_edge_recovery = 0, recorded_edge_recovery = 0;','edge policy')
    def feedback(s):
        anchor='    if (!valid) return; // A busy snapshot must not report constructor defaults as facts.'
        return helpers.once(s,anchor,anchor+r'''
    FeedbackAppend(s, "requested.colourEdgeRecovery=%d\ncolourEdge.contract=bit24 paired positive weights; actual captured N kept; recorded not GPU completion\n",
        config.colour_edge_recovery);''','edge feedback')
    transforms={'src/nr_before_sr.cpp':lambda s:patch_runtime(s,shader,helpers),
        'src/nr_colour_lab_ui.inl':lambda s:patch_ui(s,helpers,inputs['src/nr_quality_ui.inl'].decode('utf-8')),
        'src/nr_light_style_ui.inl':lambda s:patch_light_group(s,helpers),
        'src/nr_settings_ui.inl':lambda s:helpers.once(s, '    DrawColorControls(next, has_pending, changed);', '    DrawColorControls(next, has_pending, changed);\n    DrawColourLabControls(next, current.generation, has_pending, changed, apply_discrete_edit);', 'common colour recipe'),
        'src/nr_color_policy.inl':policy,'src/nr_feedback_picture.inl':feedback}
    updates={p:f((stage/'core'/p).read_text(encoding='utf-8')) for p,f in transforms.items()}
    # Historic route/colour adapters operate on their reviewed anchor layout.
    # Publish exactly one final DrawSettings with the successor's flat pages;
    # retain the shared transaction, activity and capture helpers above it.
    ui_path='src/nr_settings_ui.inl'
    first,last=helpers.definition(updates[ui_path],'void DrawSettings(')
    updates[ui_path]=updates[ui_path][:first]+'#include "nr_unified_settings.inl"\n'+updates[ui_path][last:]
    # Freeze the same identity in the overlay, PE resources and payload names.
    # Intermediate overlays intentionally retain their historical identities.
    for path in ('src/nr_before_sr.rc','scripts/build.ps1'):
        text=(stage/'core'/path).read_text(encoding='utf-8-sig')
        if 'dline21-uniform3' not in text: raise ValueError('Unexpected staged identity: '+path)
        updates[path]=text.replace('dline21-uniform3',VERSION.removeprefix('beta0.5-'))
    for p,s in updates.items(): (stage/'core'/p).write_text(s,encoding='utf-8',newline='\n')
    for p in INPUTS[1:]: (stage/'core'/p).write_bytes(inputs[p])
