#!/usr/bin/env python3
"""Extract the actual staged post codec for WARP. Never substitute test shader math."""
from __future__ import annotations
import argparse
import hashlib
import json
import re
from pathlib import Path


def unique(text: str, pattern: str, label: str) -> str:
    matches = list(re.finditer(pattern, text, re.S))
    if len(matches) != 1:
        raise ValueError(f'{label}: expected exactly one production definition, got {len(matches)}')
    return matches[0].group(0)


def function(text: str, signature: str) -> str:
    if text.count(signature) != 1:
        raise ValueError(f'Non-unique production function: {signature}')
    start = text.index(signature)
    i = text.index('{', start)
    depth, state = 0, 'code'
    while i < len(text):
        c, pair = text[i], text[i:i + 2]
        if state == 'line':
            if c == '\n': state = 'code'
        elif state == 'comment':
            if pair == '*/': state = 'code'; i += 1
        elif state in ('"', "'"):
            if c == '\\': i += 1
            elif c == state: state = 'code'
        elif pair == '//': state = 'line'; i += 1
        elif pair == '/*': state = 'comment'; i += 1
        elif pair == 'R"': raise ValueError('Raw string in extracted function; review parser')
        elif c in ('"', "'"): state = c
        elif c == '{': depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0: return text[start:i + 1]
        i += 1
    raise ValueError(f'Unterminated production function: {signature}')


def extract(source: str, descriptors: str, motion: str, template: str, exposure: str | None = None) -> str:
    # Normalise line endings, not source tokens or formulas.
    source = source.replace('\r\n', '\n')
    if 'float4 LoadPostArea' not in source or 'const bool scaled = ModelWidth != Width' not in source:
        raise ValueError('Expected staged post-scaling codec, not the untransformed core')
    if 'source_srv_format' not in descriptors or 'NrMotionReadFormat' not in descriptors:
        raise ValueError('Expected production typed-view descriptor writer')
    parts = {
        'DESCRIPTOR_CONSTANTS': unique(source,
            r'constexpr UINT kCodecSrvCount\b.*?constexpr UINT kCodecRootConstantCount = \d+;',
            'codec descriptor constants'),
        'SHADER': unique(source, r'const char kCodecShader\[\] = R"HLSL\(.*?\)HLSL";', 'codec HLSL'),
        'CONSTANTS': unique(source, r'struct CodecConstants \{.*?\n\};', 'codec constants ABI'),
        'MOTION_READ_FORMAT': function(motion, 'DXGI_FORMAT NrMotionReadFormat('),
        'ALIGN_WORK': function(source, 'unsigned int AlignWorkExtent(') + '\n\n' +
                      function(source, 'void SelectAlignedWorkExtent('),
        'DESCRIPTOR_WRITER': unique(source,
            r'constexpr unsigned int kDiagnosticStatsUintCount = \d+;', 'stats UAV extent') +
            '\n' + descriptors,
    }
    if 'NrExposureReadFormat' in descriptors:
        if exposure is None:
            raise ValueError('Missing actual exposure view helper for staged descriptor writer')
        parts['DESCRIPTOR_WRITER'] = function(exposure, 'DXGI_FORMAT NrExposureReadFormat(') + '\n' + parts['DESCRIPTOR_WRITER']
    result = template
    for name, value in parts.items():
        marker = '// @PRODUCTION_' + name + '@'
        if result.count(marker) != 1:
            raise ValueError(f'Expected exactly one harness marker {marker}')
        result = result.replace(marker, value, 1)
    if '@PRODUCTION_' in result:
        raise ValueError('Unknown/unexpanded production marker')
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--template', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = args.source_root.resolve() / 'src'
    inputs = [root / 'nr_before_sr.cpp', root / 'nr_codec_descriptors.inl',
              root / 'interop/nr_motion_access_d3d12.inl', args.template.resolve()]
    exposure_path = root / 'interop/nr_exposure_access_d3d12.inl'
    output = args.output.resolve()
    helper = Path(__file__).resolve().parents[1] / 'tests/ci_warp_runtime.h'
    helper_bytes = helper.read_bytes()
    if output.name in (helper.name, 'ci-warp-runtime-input.json'):
        raise ValueError('Generated harness must not replace its frozen helper or input receipt')
    if output in [p.resolve() for p in inputs + [exposure_path, helper]]:
        raise ValueError('Output must not replace a production input or the harness template')
    exposure = exposure_path.read_text(encoding='utf-8-sig') if exposure_path.is_file() else None
    result = extract(*(p.read_text(encoding='utf-8-sig') for p in inputs), exposure=exposure)
    output.parent.mkdir(parents=True, exist_ok=True)
    frozen_helper = output.parent / helper.name
    if frozen_helper != helper:
        frozen_helper.write_bytes(helper_bytes)
    (output.parent / 'ci-warp-runtime-input.json').write_text(json.dumps({
        'schema': 1, 'helper_sha256': hashlib.sha256(helper_bytes).hexdigest(),
        'generated_harness_sha256': hashlib.sha256(result.encode('utf-8')).hexdigest(),
    }, indent=2) + '\n', encoding='utf-8')
    output.write_text(result, encoding='utf-8', newline='\n')


if __name__ == '__main__':
    try: main()
    except (ValueError, OSError) as exc: raise SystemExit(str(exc)) from exc
