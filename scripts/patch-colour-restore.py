#!/usr/bin/env python3
"""Wire the M2/M3a RGB/source-reference and paired-HDR consumers into BOTH real D21 compositors.

Applied after the inspected uniform3/format/post adapters and the M1 overlay.
Every entry point is anchored uniquely. There is no partial publish on drift.
"""
from __future__ import annotations
from pathlib import Path


def once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise ValueError(f'{label}: expected one anchor, got {count}')
    return text.replace(old, new, 1)


def definition(text: str, signature: str) -> tuple[int, int]:
    """Locate one brace-balanced definition, ignoring comments/string braces."""
    if text.count(signature) != 1:
        raise ValueError(f'Unique definition required: {signature}')
    start = text.index(signature)
    opening = text.index('{', start)
    level, i, state, quote = 0, opening, 'code', ''
    while i < len(text):
        c = text[i]
        pair = text[i:i+2]
        if state == 'line':
            if c == '\n': state = 'code'
        elif state == 'block':
            if pair == '*/': state = 'code'; i += 1
        elif state == 'quote':
            if c == '\\': i += 1
            elif c == quote: state = 'code'
        elif pair == '//': state = 'line'; i += 1
        elif pair == '/*': state = 'block'; i += 1
        elif c in ('"', "'"): state = 'quote'; quote = c
        elif c == '{': level += 1
        elif c == '}':
            level -= 1
            if level == 0: return start, i+1
        i += 1
    raise ValueError(f'Unclosed definition: {signature}')


def edit(text: str, signature: str, transform) -> str:
    begin, end = definition(text, signature)
    return text[:begin] + transform(text[begin:end]) + text[end:]


def patch_dispatch(text: str) -> str:
    # Record the actual PSO that wrote this slot's proxy, AFTER its dispatch
    # was recorded. A requested mode or the final composition PSO is not an
    # input-encoding proof. The serial prevents a reused slot's old producer
    # from being mistaken for this frame's input. Existing GPU ownership,
    # binding, lifetime and submission checks stay authoritative.
    cfg = 'frame_config' if 'ChainConfig frame_config = g_config;' in text else 'g_config'
    text = once(text, '    const auto probe_policy = NrProbePolicySnapshot();',
        '    const auto restore_encoding = slot.colour_restore_encoding.For(slot.last_used_serial);\n'
        '    const unsigned colour_restore_flags = nr_colour_restore::Flags(\n'
        f'        {cfg}.colour_restore_mode, any_combine, restore_encoding);\n'
        '    const bool colour_restore = colour_restore_flags != 0u;\n'
        '    const auto probe_policy = NrProbePolicySnapshot();', 'effective RGB consumer')
    text = once(text, '        v2_combine && (independent_chroma || low_grid_color)\n',
        '        (colour_restore || (v2_combine && (independent_chroma || low_grid_color)))\n',
        'actual independent coefficient consumer')
    text = once(text, '    const float effective_color_strength = color_policy.effective_color_strength;',
        f'    color_policy.requested_restore_mode = nr_colour_restore::Normalize({cfg}.colour_restore_mode);\n'
        '    color_policy.recorded_restore_mode = colour_restore ? 1 : 0;\n'
        '    color_policy.restore_post_encoding = colour_restore && restore_encoding == nr_colour_restore::Encoding::Post;\n'
        '    color_policy.restore_encoding_known = restore_encoding != nr_colour_restore::Encoding::Unknown;\n'
        f'    color_policy.requested_input_curve = {cfg}.colour_input_curve;\n'
        '    color_policy.recorded_input_curve = restore_encoding != nr_colour_restore::Encoding::Unknown ? slot.colour_restore_encoding.input_curve : 0;\n'
        '    const float effective_color_strength = color_policy.effective_color_strength;', 'recorded consumer')
    text = once(text, '    const unsigned int base_codec_flags = source_flags |',
        '    const int prepared_input_curve = any_combine\n'
        '        ? color_policy.recorded_input_curve\n'
        f'        : nr_colour_restore::EffectiveInputCurve({cfg}.colour_input_curve, source_flags);\n'
        '    const unsigned int base_codec_flags = source_flags |\n'
        '        colour_restore_flags | (prepared_input_curve == 1 ? nr_colour_restore::kInputCurveFlag : 0u) |',
        'RGB and actual input preparation flags')
    dispatch = ('    ID3D12GraphicsCommandList_Dispatch(\n'
                '        command_list, (width + 7) / 8,\n'
                '        ((pipeline==g_codec.restore_edit_horizontal ? model_height : height)+7)/8, 1);')
    text = once(text, dispatch, dispatch + '\n'
        '    if (pipeline == g_codec.encode)\n'
        '        slot.colour_restore_encoding = {nr_colour_restore::Encoding::Post, slot.last_used_serial, prepared_input_curve};\n'
        '    else if (pipeline == g_codec.encode_v2 || pipeline == g_codec.lanczos_vertical)\n'
        '        slot.colour_restore_encoding = {nr_colour_restore::Encoding::V2, slot.last_used_serial, prepared_input_curve};',
        'record actual proxy producer after dispatch')
    return text


def rgb_block(pre: bool) -> str:
    writer = 'WriteCombinedV2' if pre else 'WriteCombined'
    prepared = 'fullProxy' if pre else 'ColourLabPreparedReference(storedOriginal.rgb, source)'
    raw = 'exp2(rawModelLumaLog) - 1.0' if pre else 'dot(neural - proxy, kLuma) / proxyReference'
    protected = 'exp2(poweredLog) - 1.0' if pre else 'lumaRatio - 1.0'
    confidence = 'confidence' if pre else 'transferConfidence * saturate(FrameConfidence)'
    return f'''    if (ColourLabRgbEnabled()) {{
        // Retain exact signed originals: existing preparation rectifies their
        // negative channels and does not provide a signed recovery contract.
        ColourLabRgbResult restored = ColourLabRestoreRgb(source, {prepared},
            proxy, neural, {raw}, {protected},
            ColorStrength, {confidence}, maxRatio, regionGain.y,
            faceMask, FaceOriginalColor, (FaceOptions & 2u) != 0u,
            (Passthrough & (1u | 16u | 32u)) != 0u);
        if (any(storedOriginal.rgb < 0.0)) restored.valid = false;
        if (!restored.valid || (!lightStylePreview && all(restored.rgb == source))) {{
            {writer}(tid.xy, destinationCoord, storedOriginal);
            RecordDiagnostics(tid.xy, diagnosticOriginal, diagnosticProxy,
                diagnosticNeural, diagnosticOriginal, diagnosticDelta, 0.0, !restored.valid);
            return;
        }}
        result = restored.rgb;
''' + ('        candidateChromaApplied = any(result != source);\n' if pre else '') + '''    } else if (ColorStrength > 0.000001) {'''


def patch_combine(text: str, pre: bool) -> str:
    if pre:
        text = once(text, '    float3 fullProxy = EncodeProxyLinearV2(source);',
            '    float3 fullProxy = ColourLabRgbEnabled()\n'
            '        ? ColourLabPreparedReference(storedOriginal.rgb, source)\n'
            '        : EncodeProxyLinearV2(source);', 'full-source reference uses actual encoder')
    text = once(text, '    if (ColorStrength > 0.000001) {', rgb_block(pre),
                'pre RGB consumer' if pre else 'post RGB consumer')
    return text


def patch_runtime(text: str, shader: str, prepare_shader: str) -> str:
    if 'NR_COLOUR_RESTORE_MATH_BEGIN' in text:
        raise ValueError('RGB overlay already applied')
    invariant_signatures = ('bool SameHistoryConfig(', 'void EncodeCS(', 'void EncodeV2CS(',
                            'struct CodecConstants {', 'cbuffer Constants : register(b0) {')
    invariants = {key: text[slice(*definition(text, key))] for key in invariant_signatures}
    text = once(text, '#include "nr_colour_lab_policy.h"',
        '#include "nr_colour_lab_policy.h"\n#include "nr_colour_restore_policy.h"', 'restore policy include')
    text = once(text, 'struct ChainConfig {',
        'struct ChainConfig {\n    int colour_restore_mode = nr_colour_restore::kDefaultMode; // Output only.\n    int colour_input_curve = nr_colour_restore::kDefaultInputCurve; // History-sensitive.',
        'restore config')
    text = once(text, 'struct NrSlot {',
        'struct NrSlot {\n    nr_colour_restore::EncodingReceipt colour_restore_encoding{};', 'slot-local encoder receipt')
    text = once(text, 'void NormalizeConfig(ChainConfig &config) {',
        'void NormalizeConfig(ChainConfig &config) {\n'
        '    config.colour_restore_mode = nr_colour_restore::Normalize(config.colour_restore_mode);\n'
        '    config.colour_input_curve = nr_colour_restore::Normalize(config.colour_input_curve);',
        'restore normalization')
    text = once(text, '    next.color_strength = ReadIniFloat(L"ColorStrength", 1.0f);',
        '    next.colour_input_curve = nr_colour_restore::Normalize(GetPrivateProfileIntW(\n'
        '        L"NRBeforeSR", L"ColourInputCurve", nr_colour_restore::kDefaultInputCurve, g_ini_path));\n'
        '    next.colour_restore_mode = nr_colour_restore::Normalize(GetPrivateProfileIntW(\n'
        '        L"NRBeforeSR", L"ColourRestoreMode", nr_colour_restore::kDefaultMode, g_ini_path));\n'
        '    next.color_strength = ReadIniFloat(L"ColorStrength", 1.0f);', 'restore load')
    text = once(text, '    record_write(WriteIniInt(L"ProxyCurve", static_cast<int>(config.proxy_curve)));',
        '    record_write(WriteIniInt(L"ColourInputCurve", nr_colour_restore::Normalize(config.colour_input_curve)));\n'
        '    record_write(WriteIniInt(L"ColourRestoreMode", nr_colour_restore::Normalize(config.colour_restore_mode)));\n'
        '    record_write(WriteIniInt(L"ProxyCurve", static_cast<int>(config.proxy_curve)));', 'restore save')
    same = 'bool SameConfig(const ChainConfig &a, const ChainConfig &b) {\n    return '
    text = once(text, same, same+'a.colour_restore_mode == b.colour_restore_mode && a.colour_input_curve == b.colour_input_curve && ', 'restore equality')
    history = 'bool SameHistoryConfig(const ChainConfig &a, const ChainConfig &b) {\n    return '
    text = once(text, history, history+'a.colour_input_curve == b.colour_input_curve && ',
                'input curve requires NR history reset')
    text = once(text, 'float3 HdrSourceToModelLinear(float3 source) {',
        prepare_shader+'\nfloat3 HdrSourceToModelLinear(float3 source) {', 'paired input helper')
    for signature in ('float3 HdrSourceToModelLinear(', 'float3 PqSourceToModelLinear('):
        text = edit(text, signature, lambda s: once(s,
            '    if ((Passthrough & 256u) != 0u) return RationalPeakShoulder(display);',
            '    if (ColourLabN2Requested()) return ColourLabN2Prepare(display);\n'
            '    if ((Passthrough & 256u) != 0u) return RationalPeakShoulder(display);',
            'HDR post input N2'))
    text = edit(text, 'float3 ApplySelectedProxyCurve(', lambda s: once(s,
        '    if (ProxyCurve == 0u) {',
        '    if (ColourLabN2Requested()) return ColourLabN2Prepare(sceneLinear);\n\n'
        '    if (ProxyCurve == 0u) {', 'HDR pre input N2'))
    # Inject after forward definitions and before the consumers. The shader is
    # expanded rather than relying on runtime D3DCompile include resolution.
    text = once(text, '[numthreads(8, 8, 1)]\nvoid CombineCS(',
        shader+'\n[numthreads(8, 8, 1)]\nvoid CombineCS(', 'expanded production RGB math')
    text = edit(text, 'void CombineCS(', lambda s: patch_combine(s, False))
    text = edit(text, 'void CombineV2CS(', lambda s: patch_combine(s, True))
    text = edit(text, 'void DispatchCodec(', patch_dispatch)
    for signature, original in invariants.items():
        expected = original
        if signature == 'bool SameHistoryConfig(':
            expected = original.replace('    return ', '    return a.colour_input_curve == b.colour_input_curve && ', 1)
        if text[slice(*definition(text, signature))] != expected:
            raise ValueError(f'Output-only invariant changed: {signature}')
    return text


def patch_policy(text: str) -> str:
    return once(text, 'struct NrColorPolicy {',
        'struct NrColorPolicy {\n'
        '    int requested_restore_mode = 0, recorded_restore_mode = 0;\n'
        '    bool restore_post_encoding = false, restore_encoding_known = false; // Recorded, not GPU completion.\n'
        '    int requested_input_curve = 0, recorded_input_curve = 0;',
        'restore snapshot fields')


def patch_feedback(text: str) -> str:
    anchor = '    if (!valid) return; // A busy snapshot must not report constructor defaults as facts.'
    return once(text, anchor, anchor + '''
    FeedbackAppend(s, "requested.colourLabMode=%d\\nrequested.colourRestoreMode=%d\\n"
        "requested.colourRestore=%s\\nrequested.colourInputCurve=%d\\nrequested.colourInputCurveName=%s\\nrequested.colourRestoreContract=output-only; source-referenced RGB; actual codec bit30 selects RGB and bit29 marks post EncodeCS; bit28 selects paired N2 HDR; not GPU completion\\n",
        static_cast<int>(nr_colour_lab::Normalize(config.colour_lab_mode)),
        nr_colour_restore::Normalize(config.colour_restore_mode),
        nr_colour_restore::Name(config.colour_restore_mode),
        config.colour_input_curve, nr_colour_restore::CurveName(config.colour_input_curve));''', 'coherent requested feedback')


def apply(stage: Path, inputs: dict[str, bytes]) -> None:
    shader = inputs['src/nr_colour_restore.hlsli'].decode('utf-8')
    prepare_shader = inputs['src/nr_colour_prepare.hlsli'].decode('utf-8')
    if ')HLSL"' in shader or ')HLSL"' in prepare_shader:
        raise ValueError('RGB shader contains the enclosing raw-string delimiter')
    transforms = {'src/nr_before_sr.cpp': lambda s: patch_runtime(s, shader, prepare_shader),
                  'src/nr_color_policy.inl': patch_policy,
                  'src/nr_feedback_picture.inl': patch_feedback}
    updates = {path: transform((stage/'core'/path).read_text(encoding='utf-8'))
               for path, transform in transforms.items()}
    for path, text in updates.items():
        (stage/'core'/path).write_text(text, encoding='utf-8', newline='\n')
    for path in ('src/nr_colour_restore.hlsli', 'src/nr_colour_restore_policy.h', 'src/nr_colour_prepare.hlsli'):
        (stage/'core'/path).write_bytes(inputs[path])
