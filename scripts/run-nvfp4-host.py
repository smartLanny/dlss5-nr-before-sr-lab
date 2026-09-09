#!/usr/bin/env python3
"""Run sequential owned Feature18 experiments with immutable input receipts.

This runner never installs an addon, changes games, or qualifies renderer Auto.
GPU time remains Feature18-only; ambient desktop activity is recorded separately.
"""
import argparse
import hashlib
import json
import statistics
import subprocess
import time
from pathlib import Path


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('host', 'runtime', 'bridge', 'assets', 'output'):
        p.add_argument('--' + key, type=Path, required=True)
    p.add_argument('--build', type=Path)
    p.add_argument('--expansion-build', type=Path)
    p.add_argument('--expansion-variant', type=int, choices=range(3), default=0)
    p.add_argument('--modes', default='passthrough,fused')
    p.add_argument('--width', type=int, default=2560)
    p.add_argument('--height', type=int, default=1440)
    p.add_argument('--accepted-m', default='640,960')
    p.add_argument('--variant', type=int, choices=range(4), default=0)
    p.add_argument('--frames', type=int, default=600)
    p.add_argument('--warmup', type=int, default=128)
    p.add_argument('--blocks', type=int, default=5)
    a = p.parse_args()
    modes = a.modes.split(',')
    if len(set(modes)) != len(modes) or any(x not in ('fp8', 'passthrough', 'reference', 'optimized', 'fused', 'two') for x in modes):
        p.error('Choose distinct known experiment modes')
    if not 1 <= a.blocks <= 20 or a.frames < 1 or a.warmup < 1:
        p.error('Invalid sampling count')
    a.output = a.output.resolve()
    a.output.mkdir(parents=True, exist_ok=False)
    accepted = [int(x) for x in a.accepted_m.split(',')]
    receipts = {}
    if any(x in modes for x in ('optimized', 'fused', 'two')):
        if not a.build:
            p.error('Candidate experiments require --build')
        a.build = a.build.resolve()
        receipt = json.loads((a.build / 'kernel-build.json').read_text(encoding='utf-8-sig'))
        artifacts = {x['file']: x['sha256'] for x in receipt['artifacts']}
        for name, expected in artifacts.items():
            if sha(a.build / name) != expected:
                raise ValueError('Artifact does not match its build receipt: ' + name)
        ids = ['128x128x128', '128x64x128', '128x64x256', '128x64x128_pingpong']
        for mode in modes:
            if mode not in ('optimized', 'fused', 'two'):
                continue
            if mode in ('fused', 'two') and a.variant:
                raise ValueError('The fused experiment currently implements variant 0 only')
            cubin = 'nr_nvfp4_contract_fused_sm120.cubin' if mode in ('fused', 'two') else 'nr_nvfp4_contract_sm120.cubin'
            kernel = 'nr_nvfp4_contract_publish_v1' if mode in ('fused', 'two') else 'nr_nvfp4_contract_v1_' + ids[a.variant]
            path = a.output / (mode + '-experiment-bindings.txt')
            path.write_text('\n'.join([str(a.build), artifacts[cubin], artifacts['nr_nvfp4_params.dll'],
                                      receipt['abi_fingerprint'], kernel, str(a.variant), *map(str, accepted)]) + '\n', encoding='utf-8')
            receipts[mode] = path
        (a.output / 'kernel-build.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    if 'two' in modes:
        if not a.expansion_build:
            p.error('The two-launch candidate needs --expansion-build')
        a.expansion_build = a.expansion_build.resolve()
        expansion = json.loads((a.expansion_build / 'expansion-build.json').read_text(encoding='utf-8-sig'))
        artifacts = {x['file']: x['sha256'] for x in expansion['artifacts']}
        for name, expected in artifacts.items():
            if sha(a.expansion_build / name) != expected:
                raise ValueError('Expansion build receipt mismatch: ' + name)
        name = ['nr_pack_expand_v1_m64n128', 'nr_pack_expand_v1_m32n128', 'nr_pack_expand_v1_m128n128'][a.expansion_variant]
        path = a.output / 'expansion-experiment-bindings.txt'
        path.write_text('\n'.join([str(a.expansion_build), artifacts['nr_nvfp4_pack_expansion_sm120.cubin'],
                                  artifacts['nr_nvfp4_expansion_params.dll'], expansion['abi_fingerprint'],
                                  name, str(a.expansion_variant)]) + '\n', encoding='utf-8')
        receipts['expansion'] = path
        (a.output / 'expansion-build.json').write_text(json.dumps(expansion, indent=2) + '\n', encoding='utf-8')
    smi = subprocess.run(['nvidia-smi'], capture_output=True, text=True, check=True).stdout
    (a.output / 'gpu-before.txt').write_text(smi, encoding='utf-8')
    source = Path(__file__).resolve().parents[1]
    host_receipt_path = a.host.resolve().parent / 'host-build.json'
    host_receipt = None
    if host_receipt_path.exists():
        host_receipt = json.loads(host_receipt_path.read_text(encoding='utf-8-sig'))
        if host_receipt['artifact']['file'] != a.host.name or host_receipt['artifact']['sha256'] != sha(a.host):
            raise ValueError('Host binary does not match its build receipt')
        (a.output / 'host-build.json').write_text(json.dumps(host_receipt, indent=2) + '\n', encoding='utf-8')
    meta = {'scope': 'owned signed Feature18 synthetic static input', 'game': False, 'sr': False,
            'quality_qualified': False, 'auto_qualified': False, 'gpu_exclusive_proven': False,
            'width': a.width, 'height': a.height, 'frames': a.frames, 'warmup': a.warmup,
            'blocks': a.blocks, 'modes': modes, 'created_unix': time.time(),
            'source_commit': subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip(),
            'source_dirty': bool(subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain'], text=True).strip()),
            'host_build_receipt': host_receipt,
            'host_source_provenance': 'recorded compilation inputs' if host_receipt else 'legacy host; preserve earlier build logs separately',
            'inputs': {str(x.resolve()): sha(x) for x in [a.host, a.runtime, a.bridge] if x.is_file()},
            'runs': [], 'completed': False}
    def save():
        (a.output / 'experiment.json').write_text(json.dumps(meta, indent=2) + '\n', encoding='utf-8')
    save()
    for block in range(a.blocks):
        # Alternate A/B ordering to reduce drift from warmup and laptop thermals.
        order = modes if block % 2 == 0 else list(reversed(modes))
        for mode in order:
            out = a.output / f'block-{block:02d}-{mode}'
            command = [str(a.host.resolve()), str(a.runtime.resolve()), str(a.bridge.resolve()), str(a.assets.resolve()),
                       str(out), str(a.width), str(a.height), str(a.frames), mode, str(a.warmup)]
            if mode in receipts:
                command.append(str(receipts[mode]))
            if mode == 'two':
                command.append(str(receipts['expansion']))
            with (a.output / (out.name + '-gpu.csv')).open('w', encoding='utf-8') as telemetry:
                monitor = subprocess.Popen(['nvidia-smi', '--query-gpu=timestamp,utilization.gpu,memory.used,temperature.gpu,power.draw,clocks.sm',
                                            '--format=csv', '-lms', '500'], stdout=telemetry, stderr=subprocess.STDOUT,
                                           creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
                try:
                    with (a.output / (out.name + '.log')).open('w', encoding='utf-8') as log:
                        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=max(120, (a.frames + a.warmup) * .2))
                finally:
                    monitor.terminate()
                    monitor.wait(timeout=10)
            if result.returncode:
                meta['failure'] = {'run': out.name, 'exit_code': result.returncode}
                save()
                raise RuntimeError('Host failed; preserve log: ' + out.name)
            host = json.loads((out / 'host.json').read_text(encoding='utf-8'))
            rows = [r for line in (out / 'timing.jsonl').read_text(encoding='utf-8').splitlines() if not (r := json.loads(line))['warmup']]
            if host['frames'] != a.frames or len(rows) != a.frames or not all(r['gpu_complete'] for r in rows):
                raise RuntimeError('Incomplete host samples')
            candidate = mode in ('reference', 'optimized', 'fused', 'two')
            if candidate and any(r['replaced_pairs'] != 8 for r in rows):
                raise RuntimeError('Candidate was not ready for every measured frame; increase warmup')
            expected_launches = 16 if mode == 'two' else 24 if mode == 'fused' else 32
            if candidate and any(r['replacement_launches'] != expected_launches for r in rows):
                raise RuntimeError('Unexpected number of replacement launches')
            run = {'block': block, 'mode': mode, 'directory': out.name, 'samples': len(rows),
                   'M': sorted(set(r['M'] for r in rows)), 'feature18_median_ms': statistics.median(r['feature18_ms'] for r in rows),
                   'replaced_pairs': sorted(set(r['replaced_pairs'] for r in rows)),
                   'replacement_launches': sorted(set(r['replacement_launches'] for r in rows)),
                   'output_sha256': sha(out / 'nr_output.f16')}
            if all('ffn_ms' in r for r in rows):
                run['ffn_median_ms'] = statistics.median(r['ffn_ms'] for r in rows)
            meta['runs'].append(run)
            save()
            print(json.dumps(run), flush=True)
    meta['completed'] = True
    save()
    (a.output / 'gpu-after.txt').write_text(subprocess.check_output(['nvidia-smi'], text=True), encoding='utf-8')


if __name__ == '__main__':
    main()
