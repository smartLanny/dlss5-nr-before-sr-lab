#!/usr/bin/env python3
"""Add paired edge reconstruction AFTER the reviewed colour4 stage overlay.

A separate opt-in. The inherited raw, surrogate, model, source, history and
lifetime code are not replaced. Validate all output before writing the stage.
"""
from __future__ import annotations
from pathlib import Path

VERSION = 'beta0.5-dline21-colour4-edge1'
INPUTS = ('scripts/patch-colour-edge.py', 'src/nr_colour_edge.hlsli', 'src/nr_colour_edge_policy.h')


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
        '#include "nr_colour_stage_policy.h"\n#include "nr_colour_edge_policy.h"', 'edge policy include')
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


def patch_ui(s,helpers):
    anchor='    int inputCurve = nr_colour_restore::Normalize(next.colour_input_curve);'
    s=helpers.once(s,anchor,'''    int edge = nr_colour_edge::Normalize(next.colour_edge_recovery);
    if (ImGui::Combo(ui("Low-resolution edge recovery###colour-edge", "低分辨率轮廓恢复###colour-edge"), &edge,
        ui("D21 bilinear [baseline]\\0Paired edge matching [experimental]\\0",
           "D21 双线性 [基准]\\0配对边缘匹配 [实验]\\0"))) {
        next.colour_edge_recovery = edge;
        changed = true; apply_discrete_edit = true;
    }
    SettingsTooltip(ui("Scaled V2 RGB only. P and N use identical positive, source-guided weights. No added NR or pass; ALU cost and game benefit are unmeasured. May suppress genuine edits or flicker. 100% is unchanged.",
        "仅低分 V2 RGB：P/N 使用完全相同、由原图引导的正权重，尝试减少跨轮廓串色。不增加 NR 或 pass；尚未测 GPU 开销及游戏收益，也可能压弱真实修改或闪烁。100% 不改变。"));
    SettingsNote(ui("Next A/B: same 99%, protected RGB, original input, only edge OFF/ON. Disable the neutral probe for quality/performance. Capture S/P/N/E with F7 or F8 ROI; captured N remains real. The optional detail-decision plane describes legacy filtering, not the edge/raw consumer.",
        "下一轮保持 99%、受保护 RGB、原输入，仅切轮廓恢复。画质/性能对比关闭零修改测试。F7/F8 区域捕获 S/P/N/E，其中 N 始终是真实模型输出；额外细节决策平面仍描述旧过滤，不代表边缘/原始消费者。"));
'''+anchor,'edge UI')
    s=helpers.once(s,'        next.colour_restore_mode = 0;',
        '        next.colour_edge_recovery = 0;\n        next.colour_restore_mode = 0;','reset edge')
    anchor='        ImGui::TextWrapped("%s: %s", ui("Recorded input curve", "已录制输入曲线"),'
    s=helpers.once(s,anchor,'''        ImGui::TextWrapped("%s: %s", ui("Recorded edge recovery", "已录制轮廓恢复"),
            nr_colour_edge::Name(sample.policy.recorded_edge_recovery != 0));
        if (sample.policy.requested_edge_recovery && !sample.policy.recorded_edge_recovery)
            SettingsNote(ui("Edge recovery not applied: needs scaled V2 RGB and a matching input receipt. Baseline kept. Unexpected bypass: collect F8 and report a BUG.",
                "边缘恢复未采用：需要低分 V2 RGB 与匹配输入记录，已保留基准；意外旁路请 F8 收集并反馈 BUG。"));
'''+anchor,'edge UI recorded state')
    return s


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
        'src/nr_colour_lab_ui.inl':lambda s:patch_ui(s,helpers),
        'src/nr_color_policy.inl':policy,'src/nr_feedback_picture.inl':feedback}
    updates={p:f((stage/'core'/p).read_text(encoding='utf-8')) for p,f in transforms.items()}
    for p,s in updates.items(): (stage/'core'/p).write_text(s,encoding='utf-8',newline='\n')
    for p in INPUTS[1:]: (stage/'core'/p).write_bytes(inputs[p])
