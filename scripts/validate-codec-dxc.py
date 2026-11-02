#!/usr/bin/env python3
"""Compile the exact embedded codec with DXC on a CPU host.

This optional SM6 syntax check does not replace the canonical Windows SM5
compiler, WARP, actual NGX inference, or game validation. It does not download
a compiler and its DXIL files must never be packaged as the Core's SM5 cache.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import re
import subprocess


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--dxc', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--workers', type=int, default=2)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    source_bytes = args.source.read_bytes()
    text = source_bytes.decode('utf-8')
    marker = 'const char kCodecShader[] = R"HLSL('
    if text.count(marker) != 1:
        raise ValueError('Expected one exact production codec string')
    body = text.split(marker, 1)[1].split(')HLSL";', 1)[0]
    entries = re.findall(r'\[numthreads\([^)]*\)\]\s*void\s+(\w+)\s*\(', body)
    if len(entries) != len(set(entries)) or not {'CombineCS', 'CombineV2CS'} <= set(entries):
        raise ValueError('Invalid production shader entry inventory')
    hlsl = args.output / 'actual-codec.hlsl'
    hlsl.write_bytes(body.encode('utf-8'))
    cases = [(entry, False, -1) for entry in entries]
    cases += [(entry, True, -1) for entry in ('CombineCS', 'CombineV2CS')]
    reduced = ('CombineCS', 'CombineV2CS', 'BuildEditFieldCS', 'BuildColorEditFieldCS')
    cases += [(entry, False, domain) for domain in (0, 1) for entry in reduced]
    cases += [(entry, True, domain) for domain in (0, 1) for entry in ('CombineCS', 'CombineV2CS')]
    compiler = args.dxc.resolve()
    version = subprocess.run([str(compiler), '--version'], capture_output=True, text=True, check=True)

    def compile_one(case: tuple[str, bool, int]) -> dict:
        entry, face, domain = case
        tag = f'{entry}-face{int(face)}-domain{domain}'
        binary = args.output / (tag + '.dxil')
        command = [str(compiler), '-T', 'cs_6_0', '-E', entry, '-HV', '2018', '-O3',
                   '-D', f'NR_FACE_SHADER_ENABLE={int(face)}',
                   '-D', f'NR_DUAL_REDUCED={int(domain >= 0)}',
                   '-D', f'NR_DUAL_DOMAIN={int(domain == 1)}',
                   '-Fo', str(binary.resolve()), str(hlsl.resolve())]
        result = subprocess.run(command, capture_output=True, timeout=180)
        (args.output / (tag + '.log')).write_bytes(result.stdout + result.stderr)
        info = {'entry': entry, 'face': face, 'domain': domain, 'exit_code': result.returncode}
        if result.returncode == 0:
            data = binary.read_bytes()
            if data[:4] != b'DXBC':
                raise ValueError(f'{tag}: compiler did not produce a DXIL container')
            info.update(bytes=len(data), sha256=digest(data))
        return info

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(args.workers, 3))) as pool:
        results = list(pool.map(compile_one, cases))
    failed = [x for x in results if x['exit_code']]
    receipt = {'schema': 'nr-codec-dxc-cpu-v1', 'target': 'cs_6_0', 'hlsl_version': '2018',
               'source_sha256': digest(source_bytes), 'shader_sha256': digest(body.encode()),
               'compiler_sha256': digest(compiler.read_bytes()),
               'compiler_version': version.stdout.strip(), 'cases': results,
               'passed': len(results) - len(failed), 'failed': len(failed),
               'canonical_windows_sm5_verified': False, 'gpu_verified': False,
               'ngx_verified': False, 'game_verified': False}
    (args.output / 'validation.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(f'DXC SM6 CPU compile: {len(results) - len(failed)} passed / {len(failed)} failed')
    print('Canonical Windows SM5, GPU, NGX and game checks remain unrun.')
    return int(bool(failed))


if __name__ == '__main__':
    raise SystemExit(main())
