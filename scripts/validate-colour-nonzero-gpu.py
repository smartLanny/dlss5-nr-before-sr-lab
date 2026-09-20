#!/usr/bin/env python3
"""Run exact generated pre-codec on FP16 D3D12 planes, with controlled nonzero N."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import time


def digest(path: Path) -> str:
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--toolchain', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--hardware', action='store_true')
    parser.add_argument('--actual-controls', action='store_true',
                        help='PreExposure 1.6, SourceToCanonical .625, LightingLock .2, EdgeGuard .25; HDR flat fixture.')
    parser.add_argument('--codec-artifacts', type=Path)
    parser.add_argument('--include-inherited', action='store_true')
    args = parser.parse_args()
    if os.name != 'nt':
        raise SystemExit('This gate requires real Windows D3D12; no host-only fallback.')
    repo = Path(__file__).resolve().parents[1]
    source = args.source_root.resolve()
    manifest_path = source.parent / 'source-manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    for name, expected in manifest['files'].items():
        file = manifest_path.parent / name
        if not file.resolve().is_relative_to(manifest_path.parent):
            raise ValueError('Generated manifest path escapes source root')
        if digest(file) != expected:
            raise ValueError(f'Generated source manifest mismatch: {name}')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    artifacts = None
    artifact_records = {}
    if args.codec_artifacts:
        artifacts = args.codec_artifacts.resolve()
        text = (source / 'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
        body = re.findall(r'const char kCodecShader\[\] = R"HLSL\((.*?)\)HLSL";', text, re.S)
        if len(body) != 1 or (artifacts / 'actual-codec.hlsl').read_bytes() != body[0].encode('utf-8'):
            raise ValueError('Artifact HLSL differs from exact generated shader')
        entries = ['EncodeV2CS', 'CombineV2CS', 'CopyCS']
        if args.include_inherited:
            entries += ['EncodeCS', 'CombineCS']
        for entry in entries:
            metadata = json.loads((artifacts / (entry+'-face0-domain-1.json')).read_text(encoding='utf-8-sig'))
            cso = artifacts / (entry+'-face0-domain-1.cso')
            if metadata['entry'] != entry or metadata['face'] or metadata['domain'] != -1 or \
               metadata['shader_sha256'] != digest(artifacts / 'actual-codec.hlsl') or \
               metadata['sha256'] != digest(cso) or metadata['bytes'] != cso.stat().st_size:
                raise ValueError('Production CSO receipt mismatch: '+entry)
            artifact_records[entry] = metadata
    generated = output / 'post_scaled_work_gpu.cpp'
    subprocess.run([sys.executable, str(repo / 'scripts/extract-post-gpu.py'),
                    '--source-root', str(source), '--template',
                    str(repo / 'tests/post_scaled_work_gpu.cpp.in'),
                    '--output', str(generated)], check=True)
    compiler = args.toolchain.resolve() / 'bin/x86_64-w64-mingw32-clang++.exe'
    exe = output / 'colour_nonzero_gpu.exe'
    command = [str(compiler), '-std=c++17', '-O2', '-static', '-Wall', '-Wextra', '-Werror',
               str(repo / 'tests/colour_nonzero_gpu.cpp'), '-o', str(exe),
               '-I'+str(output), '-I'+str(source / 'src/interop'),
               '-ld3d12', '-ldxgi', '-ld3dcompiler', '-ldxguid', '-lole32']
    with (output / 'compile.txt').open('w', encoding='utf-8') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    records = []
    failures = []
    cases = [('positive', [], 0), ('wrong-source-origin', ['--negative-source-origin'], 1)]
    if args.include_inherited:
        cases += [('inherited-post-WARP', ['--inherited-post'], 0)]
    for label, extra, expected in cases:
        cmd = [str(exe)] + (['--hardware'] if args.hardware else []) + \
              (['--actual-controls'] if args.actual_controls and label != 'inherited-post-WARP' else []) + \
              (['--artifacts='+str(artifacts)] if artifacts else []) + extra
        start = time.monotonic()
        with (output / (label+'.stdout.txt')).open('w', encoding='utf-8') as stdout, \
             (output / (label+'.stderr.txt')).open('w', encoding='utf-8') as stderr:
            result = subprocess.run(cmd, stdout=stdout, stderr=stderr, timeout=1800)
        records.append(dict(name=label, exit_code=result.returncode,
                            seconds=time.monotonic()-start,
                            adapter='WARP' if label == 'inherited-post-WARP' else
                                    ('NVIDIA hardware' if args.hardware else 'WARP')))
        records[-1]['expected_exit_code'] = expected
        records[-1]['passed'] = result.returncode == expected
        if result.returncode != expected:
            failures.append(f'{label}: exit {result.returncode}, expected {expected}')
        if expected and 'production EncodeV2 FP16/source-origin/area oracle mismatch' not in \
                (output / (label+'.stderr.txt')).read_text(encoding='utf-8'):
            records[-1]['passed'] = False
            failures.append('Negative control failed for an unrelated reason')
        print(f'{label}: exit {result.returncode}, expected {expected}', flush=True)
    adapter_lines = [line for line in (output / 'positive.stdout.txt').read_text(encoding='utf-8').splitlines()
                     if line.startswith('ADAPTER,')]
    if len(adapter_lines) != 1:
        raise RuntimeError('Missing unambiguous actual adapter identity')
    receipt = dict(schema=1, source_commit=manifest['source_commit'],
                   manifest_sha256=digest(manifest_path),
                   manifest_files_verified=len(manifest['files']),
                   staged_core_sha256=digest(source / 'src/nr_before_sr.cpp'),
                   template_sha256=digest(repo / 'tests/post_scaled_work_gpu.cpp.in'),
                   test_sha256=digest(repo / 'tests/colour_nonzero_gpu.cpp'),
                   generated_harness_sha256=digest(generated),
                   executable_sha256=digest(exe),
                   adapter='NVIDIA hardware' if args.hardware else 'WARP',
                   adapter_identity=adapter_lines[0],
                   fp16_model_planes=True, actual_generated_pre_codec=True,
                   control_profile=dict(pre_exposure=1.6 if args.actual_controls else 1.,
                       source_to_canonical=.625 if args.actual_controls else 1.,
                       lighting_lock=.2 if args.actual_controls else 0.,
                       edge_guard=.25 if args.actual_controls else 0.),
                   production_cso_receipts=artifact_records,
                   nvidia_feature18_verified=False, native_sr_output_O_verified=False,
                   game_runtime_verified=False, passed=not failures,
                   failures=failures, results=records)
    for file in output.glob('*.txt'):
        receipt[file.name+'_sha256'] = digest(file)
    (output / 'colour-nonzero-result.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
    if failures:
        raise RuntimeError('; '.join(failures)+f'; preserve logs and failed receipt in {output}')
    print('PASS actual pre-codec FP16 nonzero recovery; controlled N, no inference/game claim.', flush=True)


if __name__ == '__main__':
    main()
