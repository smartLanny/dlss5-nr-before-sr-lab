#!/usr/bin/env python3
"""Record focused production format/exposure checks without starting a game.

CPU policy/observer tests are reused. Optional WARP tests consume an explicitly
prepared canonical source image; artificial neural planes are not NVIDIA NR.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--staged-source', type=Path, help='Canonical staged core/src/nr_before_sr.cpp')
    parser.add_argument('--reshade', type=Path)
    parser.add_argument('--powershell', default='pwsh')
    parser.add_argument('--run-warp', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        raise ValueError('Output already exists; keep previous focused evidence')
    source = args.source_root.resolve(strict=True)
    compiler = args.compiler.resolve(strict=True)
    if args.run_warp and (not args.staged_source or not args.reshade):
        raise ValueError('WARP requires explicit --staged-source and --reshade')
    output.mkdir(parents=True)
    record = {'schema': 'nr-format-exposure-focused-v1', 'success': False,
              'cpu_verified': False, 'warp_verified': False,
              'nvidia_feature18_verified': False, 'game_verified': False,
              'commands': [], 'input_sha256': {}, 'failure': None}
    env = os.environ.copy()
    env['NR_VALIDATION_WARP_ONLY'] = '1'

    def run(command: list[str], label: str, timeout: int = 180) -> None:
        print(label, flush=True)
        result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True,
                                text=True, encoding='utf-8', errors='replace', timeout=timeout)
        log = output / (label + '.log')
        log.write_text(result.stdout + result.stderr, encoding='utf-8')
        record['commands'].append({'label': label, 'argv': command, 'exit_code': result.returncode,
                                   'log_sha256': sha(log)})
        print(result.stdout[-3500:], end='', flush=True)
        if result.returncode:
            print(result.stderr[-5000:], flush=True)
            raise RuntimeError(f'{label} failed: {result.returncode}')

    try:
        run([sys.executable, str(ROOT / 'scripts/validate-resource-semantics.py'),
             '--compiler', str(compiler), '--source-root', str(source),
             '--output', str(output / 'semantic-cpu')], 'resource-semantics')
        run([sys.executable, str(ROOT / 'scripts/validate-native-format-observer.py'),
             '--compiler', str(compiler), '--source-root', str(source),
             '--output', str(output / 'native-format-cpu')], 'native-format-observer')
        exposure = output / 'exposure-cpu'
        (exposure / 'src/interop').mkdir(parents=True)
        for original, relative in ((source / 'src/interop/nr_exposure_access_d3d12.inl',
                                    'src/interop/nr_exposure_access_d3d12.inl'),
                                   (ROOT / 'tests/multi_api/exposure_access.cpp', 'exposure_access.cpp')):
            data = original.read_bytes()
            (exposure / relative).write_bytes(data)
            if original.read_bytes() != data:
                raise RuntimeError('Exposure source changed while freezing')
            record['input_sha256'][relative] = hashlib.sha256(data).hexdigest()
        binary = exposure / 'exposure_access.exe'
        run([str(compiler), '-std=c++20', '-O2', '-static', '-Wall', '-Wextra', '-Werror',
             '-I' + str(exposure), str(exposure / 'exposure_access.cpp'), '-o', str(binary)],
            'compile-exposure')
        run([str(binary)], 'exposure-access')
        record['cpu_verified'] = True
        if args.run_warp:
            staged = args.staged_source.resolve(strict=True)
            record['staged_source_sha256'] = sha(staged)
            run([sys.executable, str(ROOT / 'scripts/validate-native-color-view.py'),
                 '--compiler', str(compiler), '--reshade', str(args.reshade.resolve(strict=True)),
                 '--source', str(staged), '--output', str(output / 'native-color-warp')],
                'native-color-warp', 600)
            powershell = shutil.which(args.powershell)
            if not powershell:
                raise FileNotFoundError('Provide an available --powershell executable')
            run([powershell, '-NoProfile', '-File', str(ROOT / 'scripts/validate-codec-gpu.ps1'),
                 '-ToolchainRoot', str(compiler.parent.parent), '-SourceRoot', str(staged.parent.parent),
                 '-ExposureTyped', '-OutputRoot', str(output / 'typed-exposure-warp')],
                'typed-exposure-warp', 600)
            record['warp_verified'] = True
        record['success'] = True
    except Exception as exc:
        record['failure'] = str(exc)
        raise
    finally:
        (output / 'validation.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print('PASS focused production format/exposure; no NVIDIA/game claim')


if __name__ == '__main__':
    main()
