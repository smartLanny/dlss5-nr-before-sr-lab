#!/usr/bin/env python3
"""Colour4 bounded early-reference consumer and a session-only N=P instrument.

Apply after the complete colour3 overlay inside the canonical staging transaction.
Do not modify the signed model, queues, root ABI, descriptors or actual captures.
"""
from __future__ import annotations
from pathlib import Path

VERSION = 'beta0.5-dline21-colour4'
INPUTS = ('scripts/patch-colour-stage.py', 'src/nr_colour_stage_policy.h',
          'src/nr_colour_stage_reference.hlsli')


def reference_sampler(text: str, helpers) -> str:
    sampler = text[slice(*helpers.definition(text, 'float3 SampleNeuralLinearV2('))]
    sampler = helpers.once(sampler, 'SampleNeuralLinearV2(', 'ColourStageSampleNeural(', 'reference sampler')
    if sampler.count('LoadNeuralLinearV2(') != 4:
        raise ValueError('Production neural bilinear footprint changed; review before porting')
    return sampler.replace('LoadNeuralLinearV2(', 'ColourStageLoadNeural(')


def patch_runtime(text: str, shader: str, helpers) -> str:
    once, edit, definition = helpers.once, helpers.edit, helpers.definition
    if 'NR_COLOUR_STAGE_REFERENCE_HLSLI' in text:
        raise ValueError('Colour4 stage overlay already applied')
    def body(value, sig): return value[slice(*definition(value, sig))]
    # Exact invariants: never replace the model's final multilayer accessor,
    # normal P/N samplers, encode, capture writes, or the GPU constants ABI.
    keep = ('void EncodeCS(', 'void EncodeV2CS(', 'float4 LoadFinalNeuralStoredV2(',
            'float3 LoadNeuralLinearV2(', 'float3 SampleNeuralLinearV2(',
            'float3 SampleProxyLinearV2(', 'bool SameHistoryConfig(',
            'struct CodecConstants {', 'cbuffer Constants : register(b0) {',
            'void WriteCombined(', 'void WriteCombinedV2(')
    saved = {sig: body(text, sig) for sig in keep}
    sampler = reference_sampler(text, helpers)
    shader = once(shader, '// NR_COLOUR_STAGE_NEURAL_SAMPLER', sampler, 'production sampling expression')
    if ')HLSL"' in shader:
        raise ValueError('Invalid enclosing raw-string delimiter')
    text = once(text, '[numthreads(8, 8, 1)]\nvoid CombineCS(',
                shader+'\n[numthreads(8, 8, 1)]\nvoid CombineCS(', 'early reference helpers')
    for sig, mapped, writer in (('void CombineCS(', 'false', 'WriteCombined'),
                               ('void CombineV2CS(', 'true', 'WriteCombinedV2')):
        anchor = ('    float4 storedOriginal = LoadSourceStoredV2(int2(tid.xy));' if mapped == 'true'
                  else '    float4 storedOriginal = OriginalColor.Load(int3(sourceCoord, 0));')
        block = f'''{anchor}
    // Colour4: read the actual model result before Generic/face/region/preview
    // can alter or discard it. The native resource/queue guards ran on the CPU.
    if (ColourLabRgbEnabled() && ColourLabRawEditRequested()) {{
        ColourStagePixel reference = ColourStageReference(tid.xy, storedOriginal, {mapped});
        {writer}(tid.xy, destinationCoord, reference.stored);
        if (reference.invalid)
            RecordDiagnostics(tid.xy, 0.0.xxx, 0.0.xxx, 0.0.xxx, 0.0.xxx, 0.0, 0.0, true);
        else
            RecordDiagnostics(tid.xy, SourceToDiagnosticLinear(storedOriginal.rgb),
                reference.proxy, reference.neural, SourceToDiagnosticLinear(reference.stored.rgb),
                ColourLabAbsPeak(reference.neural - reference.proxy),
                reference.relativeDelta, false);
        return;
    }}'''
        text = edit(text, sig, lambda s: once(s, anchor, block, 'early reference '+sig))
    text = once(text, '#include "nr_colour_restore_policy.h"',
                '#include "nr_colour_restore_policy.h"\n#include "nr_colour_stage_policy.h"', 'stage policy')
    text = once(text, 'struct ChainConfig {',
                'struct ChainConfig {\n    bool colour_neutral_probe = false; // Session only: never persisted.', 'session probe')
    text = once(text, 'bool SameConfig(const ChainConfig &a, const ChainConfig &b) {\n    return ',
                'bool SameConfig(const ChainConfig &a, const ChainConfig &b) {\n    return a.colour_neutral_probe == b.colour_neutral_probe && ', 'probe transaction')
    # Follow existing transient preview handling: periodic INI reload must
    # preserve the live instrument, and toggling only it must not write INI.
    text = edit(text, 'void ReloadConfig(', lambda s: once(s,
        '    ChainConfig next = {};',
        '    ChainConfig next = {};\n    next.colour_neutral_probe = g_config.colour_neutral_probe;', 'probe survives polling'))
    def transient_apply(s):
        s = once(s, '    ChainConfig persisted_next=next;',
            '    ChainConfig persisted_next=next;\n    persisted_next.colour_neutral_probe=g_config.colour_neutral_probe;', 'transient comparison')
        return once(s, 'const bool preview_only=(',
            'const bool preview_only=(next.colour_neutral_probe!=g_config.colour_neutral_probe || ', 'probe does not save INI')
    text = edit(text, 'bool ApplyConfig(', transient_apply)
    def dispatch(s):
        cfg = 'frame_config' if 'ChainConfig frame_config = g_config;' in s else 'g_config'
        s = once(s, '    const unsigned colour_restore_flags = nr_colour_restore::Flags(',
            '    const unsigned colour_stage_flags = nr_colour_stage::Flags(\n'
            f'        {cfg}.colour_neutral_probe, any_combine, restore_encoding);\n'
            '    const unsigned colour_restore_flags = colour_stage_flags | nr_colour_restore::Flags(', 'probe flags')
        s = once(s, '    const float effective_color_strength = color_policy.effective_color_strength;',
            f'    color_policy.requested_neutral_probe = {cfg}.colour_neutral_probe;\n'
            '    color_policy.recorded_neutral_probe = colour_stage_flags != 0u;\n'
            '    if (colour_stage_flags != 0u) color_policy.recorded_restore_mode = 2;\n'
            '    const float effective_color_strength = color_policy.effective_color_strength;', 'probe receipt')
        return s
    text = edit(text, 'void DispatchCodec(', dispatch)
    for name in ('kAddonVersion', 'kBuildIdentity'):
        text = once(text, f'constexpr const char *{name} = "beta0.5-dline21-colour3";',
                    f'constexpr const char *{name} = "{VERSION}";', 'colour4 identity')
    for sig, original in saved.items():
        if body(text, sig) != original:
            raise ValueError('Colour4 changed invariant: '+sig)
    if 'colour_neutral_probe' in body(text, 'bool SaveConfig('):
        raise ValueError('Neutral instrument must not persist')
    return text


def patch_ui(text: str, helpers) -> str:
    once = helpers.once
    anchor = '    int recovery = nr_colour_restore::Normalize(next.colour_restore_mode);'
    block = '''    bool neutralProbe = next.colour_neutral_probe;
    if (ImGui::Checkbox(ui("Zero-edit recovery test [session only]###colour-neutral",
                           "零修改恢复对照 [仅本次运行]###colour-neutral"), &neutralProbe)) {
        next.colour_neutral_probe = neutralProbe;
        changed = true; apply_discrete_edit = true;
    }
    if (neutralProbe) {
        SettingsNote(ui("TEST ONLY: NR still runs, but the early reference consumer samples P as N. No model/history texture is modified. Output should match the original; this is NOT an inference or performance test.",
            "仅测试：NR 仍在运行，早期恢复器把 P 作为 N 取样；不修改模型输出或历史纹理。结果应等于原图；不作为推理或性能测试。"));
        SettingsNote(ui("F7/F8 S/P/N/E still captures the REAL model output N; only E uses the synthetic pair. Inspect the recorded bit25 flag. Disabled/warmup/bypass frames are not a valid zero-edit test.",
            "F7/F8 的 S/P/N/E 仍保存真实模型输出 N，仅 E 使用模拟零修改配对。核对已录制 bit25；关闭、预热或旁路帧不算零修改验证。"));
    }
    SettingsNote(ui("Colour4 raw RGB now precedes Generic, full-face keep and previews. Master disable, finite/gamut/max-ratio and resource safety remain. F7 captures SR-input E on NR→SR, NOT the final SR output; use F8 ROI for pixel detail. On bypass/flicker/halos, collect F8 and submit a BUG.",
        "colour4 原始 RGB 已移到通用残差、整脸保留和预览之前；总开关、有限值/色域/亮度上限和资源安全仍在。NR→SR 的 F7 捕获 SR 输入 E，不是 SR 最终画面；像素细节使用 F8 区域捕获。旁路、闪烁或光晕请 F8 收集并提交 BUG。"));
'''+anchor
    text = once(text, anchor, block, 'probe UI transaction')
    capture = r'''    const auto captureSample = SnapshotColorPolicy();
    const bool captureReady = !changed && !has_pending &&
        captureSample.status == NrColorPolicyStatus::Current &&
        captureSample.config_generation == generation && StageCaptureStatus() == 0u;
    ImGui::BeginDisabled(!captureReady);
    if (ImGui::Button(ui("Capture stages (F7)###colour-stage-capture", "捕获处理阶段（F7）###colour-stage-capture"))) {
        if (!RequestStageCapture())
            SettingsNote(ui("Capture not accepted (busy or feedback active); no completed capture claimed.",
                "捕获未受理（忙碌或 F8 占用），并非已经完成。"));
    }
    ImGui::EndDisabled();
    SettingsNote(ui("F7 recreates and warms NR, then captures through the existing fence-owned path. Whole-frame capture is sampled; use F8 ROI for texture detail.",
        "F7 会重建并预热 NR，随后走既有 fence 保护的捕获；全图为抽样，细纹理请使用 F8 区域捕获。"));
'''
    text = once(text, '    const auto sample = SnapshotColorPolicy();', capture+'    const auto sample = SnapshotColorPolicy();', 'existing capture entry')

    text = once(text, '        next.colour_restore_mode = 0;',
                '        next.colour_neutral_probe = false;\n        next.colour_restore_mode = 0;', 'reset probe')
    anchor = '        ImGui::TextWrapped("%s: %s", ui("Recorded recovery", "已录制恢复算法"),'
    text = once(text, anchor,
        '        ImGui::TextWrapped("%s: %s", ui("Recorded consumer", "已录制消费者"),\n'
        '            nr_colour_stage::Name(sample.policy.recorded_neutral_probe));\n'+anchor, 'probe UI receipt')
    text = once(text, 'any existing low-grid luminance filtering remains active.',
        'only protected mode keeps low-grid luminance filtering; raw mode bypasses it.', 'raw filter description')
    text = once(text, '已有低网格明暗过滤仍会生效。',
        '仅受保护模式保留低网格明暗过滤，原始参考绕过它。', 'raw filter description Chinese')
    text = once(text,
        '(sample.policy.requested_restore_mode == 1 || sample.policy.requested_input_curve == 1)',
        '(sample.policy.requested_restore_mode != 0 || sample.policy.requested_neutral_probe || sample.policy.requested_input_curve == 1)',
        'raw and neutral missing-encoder feedback')
    return text


def apply(stage: Path, inputs: dict[str, bytes], helpers) -> None:
    core = stage/'core'
    shader = inputs['src/nr_colour_stage_reference.hlsli'].decode('utf-8')
    def policy(s):
        return helpers.once(s, 'struct NrColorPolicy {',
            'struct NrColorPolicy {\n    bool requested_neutral_probe = false, recorded_neutral_probe = false;', 'probe policy')
    def feedback(s):
        anchor = '    if (!valid) return; // A busy snapshot must not report constructor defaults as facts.'
        return helpers.once(s, anchor, anchor+r'''
    FeedbackAppend(s, "requested.colourNeutralProbe=%d\ncolourStage.contract=bit25 synthetic consumer N=P; real captured N unchanged; detail-decision plane is legacy, not the early consumer; output-only, session-local, not GPU completion\n",
        config.colour_neutral_probe ? 1 : 0);''', 'probe feedback')
    transforms = {'src/nr_before_sr.cpp': lambda s: patch_runtime(s, shader, helpers),
                  'src/nr_color_policy.inl': policy,
                  'src/nr_feedback_picture.inl': feedback,
                  'src/nr_colour_lab_ui.inl': lambda s: patch_ui(s, helpers)}
    updates = {p: f((core/p).read_text(encoding='utf-8')) for p, f in transforms.items()}
    for p, text in updates.items():
        (core/p).write_text(text, encoding='utf-8', newline='\n')
    for p in INPUTS[1:]:
        (core/p).write_bytes(inputs[p])
