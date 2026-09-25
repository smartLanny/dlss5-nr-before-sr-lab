#!/usr/bin/env python3
"""Observe near-black sensitivity in the actual colour-restore math.

This is a synthetic, CPU-only diagnostic, not a game image, denoiser, regression
fix or FPS benchmark. No runtime DLL is loaded. Production settings are unchanged.
The existing float3 adapter is reused; the arithmetic comes from production HLSL.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=os.environ.get('CXX'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    compiler = args.compiler or shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise SystemExit('No C++20 compiler available; no probe performed.')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)  # Preserve previous evidence.
    shader_path = ROOT / 'src/nr_colour_restore.hlsli'
    source = shader_path.read_text(encoding='utf-8-sig')
    begin, end = '// NR_COLOUR_RESTORE_MATH_BEGIN', '// NR_COLOUR_RESTORE_MATH_END'
    if source.count(begin) != 1 or source.count(end) != 1:
        raise RuntimeError('Production math boundaries changed.')
    math = source.split(begin, 1)[1].split(end, 1)[0].replace('[unroll]', '')
    adapter_path = ROOT / 'tests/colour_restore_math_test.cpp'
    adapter = adapter_path.read_text(encoding='utf-8-sig').split('static unsigned checks =', 1)[0]
    adapter = adapter.replace('#include "../src/nr_colour_restore_policy.h"', '')
    if adapter.count('#include "colour_restore_math.generated.hpp"') != 1:
        raise RuntimeError('Existing float3 test adapter boundary changed.')
    (output / 'colour_restore_math.generated.hpp').write_text(math, encoding='utf-8')
    program = adapter + r'''
int main() {
    const float3 s{.4f,.3f,.2f};
    for (float level : {.0001f,.001f,.01f,.1f}) {
        const float3 n0{level,level,level};
        const float3 n1{level,level+.00005f,level};
        for (float colour : {0.0f,0.5f,1.0f}) {
            // A bright source and almost-black model result, with the existing
            // 2x luminance safety floor. No face ROI, temporal history or image.
            const float raw0=dot(n0-s,kLuma)/dot(s,kLuma);
            const float raw1=dot(n1-s,kLuma)/dot(s,kLuma);
            auto a=ColourLabRestoreRgb(s,s,s,n0,raw0,-.5f,colour,1,2,1,0,0,true,false,false);
            auto b=ColourLabRestoreRgb(s,s,s,n1,raw1,-.5f,colour,1,2,1,0,0,true,false,false);
            if(!a.valid||!b.valid||!all(isfinite(a.rgb))||!all(isfinite(b.rgb)))return 2;
            std::printf("%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n",
                level,colour,a.rgb.x,a.rgb.y,a.rgb.z,b.rgb.x,b.rgb.y,b.rgb.z,
                ColourLabAbsPeak(b.rgb-a.rgb),dot(b.rgb-a.rgb,kLuma));
        }
    }
    return 0;
}
'''
    cpp = output / 'probe.cpp'
    cpp.write_text(program, encoding='utf-8')
    exe = output / ('probe.exe' if os.name == 'nt' else 'probe')
    cmd = [compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
           '-ffp-contract=off', '-I', str(output), str(cpp), '-o', str(exe)]
    if os.name == 'nt':
        cmd.append('-static')
    compilation = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    (output / 'compile.log').write_text(compilation.stdout + compilation.stderr, encoding='utf-8')
    compilation.check_returncode()
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    (output / 'probe.csv').write_text('model_level,colour,r0,g0,b0,r1,g1,b1,max_rgb_difference,luma_difference\n' + run.stdout, encoding='utf-8')
    run.check_returncode()
    rows = []
    for line in run.stdout.splitlines():
        v = list(map(float, line.split(',')))
        rows.append({'model_level': v[0], 'colour': v[1], 'before': v[2:5],
                     'after': v[5:8], 'max_rgb_difference': v[8], 'luma_difference': v[9]})
    assert len(rows) == 12 and all(x['max_rgb_difference'] < 1e-6 for x in rows if x['colour'] == 0)
    result = {
        'scope': 'Synthetic CPU math only; not evidence that the model emits these values in any game.',
        'source_rgb': [.4, .3, .2], 'model_green_perturbation': .00005,
        'protected_relative_luma': -.5, 'max_luma_ratio': 2,
        'actual_production_shader_sha256': hashlib.sha256(shader_path.read_bytes()).hexdigest(),
        'existing_adapter_sha256': hashlib.sha256(adapter_path.read_bytes()).hexdigest(),
        'syntax_adaptation': 'Remove HLSL [unroll] only; existing float3 adapter.',
        'runtime_modified': False, 'gpu_executed': False, 'game_verified': False,
        'rows': rows,
    }
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
