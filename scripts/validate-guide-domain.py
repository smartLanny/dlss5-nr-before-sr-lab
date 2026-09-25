#!/usr/bin/env python3
"""Run actual guide-binding and additive capture-JSON regressions on Windows.

No model or live game is called. Negative controls alter isolated source copies,
never the checkout. Both the raw base and the fully generated Core must be tested.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def definition(text: str, marker: str, semicolon: bool = False) -> str:
    start = text.index(marker)
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end + int(semicolon)]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source_path = (args.source or root/'src/nr_before_sr.cpp').resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    text = source_path.read_text(encoding='utf-8-sig')
    # Test the exact staged serializer/header when testing a generated Core.
    types_path = source_path.parent/'nr_f8_types.h'
    stream_path = source_path.parent/'nr_f8_stream.inl'
    stream = stream_path.read_text(encoding='utf-8-sig')
    for header in ('nr_f8_types.h', 'nr_guide_contract.h'):
        if (source_path.parent/header).read_text(encoding='utf-8-sig') != (root/'src'/header).read_text(encoding='utf-8-sig'):
            raise ValueError('Generated guide test header does not match this source revision: '+header)
    binding = definition(text, 'bool ResolveNrGuideBinding(')
    generated = '#include "interop/nr_rr_post_runtime.inl"' in text
    rows = []

    def run_binding(name: str, source: Path, expected_failure: str | None = None):
        command = [os.sys.executable, str(root/'scripts/validate-guide-history.py'),
                   '--compiler', args.compiler, '--source', str(source)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=90)
        log = result.stdout + result.stderr
        (output/(name+'.txt')).write_text(log, encoding='utf-8')
        passed = (result.returncode == 1 and 'FAIL: '+expected_failure in log
                  if expected_failure else result.returncode == 0 and 'PASS (0 failures)' in log)
        rows.append({'name': name, 'passed': passed, 'exit_code': result.returncode,
                     'expected_failure': expected_failure})
        if not passed:
            raise RuntimeError(name+' failed: '+log[-6000:])

    run_binding('production', source_path)
    mutations = {
        'missing-empty-grid-guard': (
            'if (!width || !height || !guide_width || !guide_height)', 'if (false)',
            'empty grid rejects before any resource lookup'),
        'missing-converted-scale-guard': (
            'if (!std::isfinite(s.motion_scale_x) || !std::isfinite(s.motion_scale_y) ||\n'
            '        s.motion_scale_x == 0.0f || s.motion_scale_y == 0.0f)', 'if (false)',
            'non-finite converted motion scale rejects before opaque NGX'),
        'storage-confused-with-measurement-grid': (
            'static_cast<float>(width) / guide_width',
            'static_cast<float>(width) / s.motion_source_width',
            'MV.Scale remains render-referenced even for full-resolution storage'),
    }
    for name, (old, new, expected) in mutations.items():
        if binding.count(old) != 1:
            raise ValueError('Negative-control anchor drift: '+name)
        copy = output/(name+'.cpp')
        copy.write_text(text.replace(binding, binding.replace(old, new), 1), encoding='utf-8')
        run_binding(name, copy, expected)

    serializer = definition(stream, 'struct JsonBuffer {', True) + '\n' + definition(stream, 'void GuideDomainJson(')
    frame = definition(stream, 'JsonBuffer FrameJson(')
    if frame.count('GuideDomainJson(b,p);') != 1:
        raise ValueError('The production frame writer no longer includes guide domains')
    test = '''#include <cmath>
#include <cstdarg>
#include <cstdio>
#include "nr_f8_types.h"
''' + serializer + r'''
int main() {
    NrCaptureParams p{};
    JsonBuffer unknown; unknown.Append("{\"fixture\":1"); GuideDomainJson(unknown,p); unknown.Append("}");
    if(!unknown.ok) return 2;
    std::puts(unknown.data);
    p.guide_domain_known=true; p.render_width=1920; p.render_height=1080;
    p.work_width=1344; p.work_height=756;
    p.mv_source_width=3840; p.mv_source_height=2160; p.mv_source_x=13; p.mv_source_y=7;
    p.mv_to_work_x=p.mv_to_work_y=.7f;
    JsonBuffer known; known.Append("{\"fixture\":1"); GuideDomainJson(known,p); known.Append("}");
    if(!known.ok) return 3;
    std::puts(known.data);
    p.mv_fallback_x=true; p.mv_to_work_y=NAN;
    JsonBuffer invalid; invalid.Append("{\"fixture\":1"); GuideDomainJson(invalid,p); invalid.Append("}");
    if(!invalid.ok) return 4;
    std::puts(invalid.data);
    return 0;
}
'''
    cpp, exe = output/'capture-domain.cpp', output/'capture-domain.exe'
    cpp.write_text(test, encoding='utf-8')
    build = subprocess.run([args.compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                            *(['-static'] if os.name == 'nt' else []),
                            '-I'+str(types_path.parent), str(cpp), '-o', str(exe)],
                           capture_output=True, text=True, timeout=90)
    (output/'capture-compile.txt').write_text(build.stdout+build.stderr, encoding='utf-8')
    if build.returncode:
        raise RuntimeError(build.stderr)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15, check=True)
    # Independent Python JSON parser verifies real C++ serialization, not a mirror.
    unknown, known, invalid = [json.loads(line)['guideDomain'] for line in result.stdout.splitlines()]
    assert unknown == {'schema': 1, 'known': 0, 'boundary': 'ngx-evaluate', 'nativeProvenanceVerified': False}
    assert known['renderWidth'] == 1920 and known['workWidth'] == 1344 and known['mvSourceWidth'] == 3840
    assert known['mvSourceX'] == 13 and known['mvSourceY'] == 7
    assert abs(known['mvToWorkX']-.7) < 1e-6 and known['gameMvLowResolution'] == 0
    assert not known['nativeProvenanceVerified'] and invalid['mvFallbackX'] == 1 and invalid['mvToWorkY'] is None
    (output/'capture-json.txt').write_text(result.stdout, encoding='utf-8')
    receipt = {'success': True, 'generated_core': generated, 'source_compile_verified': True,
               'gpu_verified': False, 'ngx_verified': False, 'game_verified': False,
               'source_sha256': hashlib.sha256(source_path.read_bytes()).hexdigest(),
               'capture_types_sha256': hashlib.sha256(types_path.read_bytes()).hexdigest(),
               'capture_writer_sha256': hashlib.sha256(stream_path.read_bytes()).hexdigest(),
               'actual_function_and_negative_controls': rows, 'actual_capture_json_verified': True,
               'limitations': ['NGX/resources/context services are CPU seams.',
                              'Matching raster sizes does not prove Feature18 guide use or temporal quality.',
                              'NGX input provenance is not inferred to be game-native.',
                              'Valid mapped values keep the established float operations and R reference.']}
    (output/'validation.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
