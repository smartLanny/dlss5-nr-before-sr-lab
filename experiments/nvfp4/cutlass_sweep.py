"""Run a locally built CUTLASS 79a SM120 example on FFN-like shapes.
This is GEMM-only and synthetic: no model weights, no game, no NVAPI hook.
Some CUTLASS examples exit 0 when the GPU/toolkit is unsupported: require BOTH
Disposition: Passed and a positive Avg runtime, never treat exit code 0 as proof.
"""
from __future__ import annotations
import argparse
import json
import re
import subprocess
from pathlib import Path


def parse_output(returncode, text):
    verified = re.search(r'Disposition:\s*Passed\b', text)
    runtime = re.search(r'Avg runtime:\s*([0-9.eE+-]+)\s*ms', text)
    if returncode or not verified or not runtime:
        raise ValueError('benchmark did not execute and verify; no-op exits are not passes')
    value = float(runtime.group(1))
    if not (0 < value < float('inf')):
        raise ValueError('invalid GPU time')
    return value


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('executable', type=Path, help='locally built official 79a NVFP4 example')
    ap.add_argument('--m', nargs='+', type=int, default=[960, 2160, 3840])
    ap.add_argument('--iterations', type=int, default=100)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    if not args.executable.is_file() or args.iterations < 1 or any(x <= 0 for x in args.m):
        ap.error('executable must exist and all counts must be positive')
    results = []
    for m in args.m:
        for name,n,k in [('expansion',4096,1024),('contraction',1024,4096)]:
            command = [str(args.executable.resolve()), f'--m={m}', f'--n={n}', f'--k={k}', f'--iterations={args.iterations}']
            try:
                p = subprocess.run(command, capture_output=True, text=True, timeout=300, check=False)
                ms = parse_output(p.returncode, p.stdout+'\n'+p.stderr)
                results.append(dict(m=m,n=n,k=k,operation=name,status='synthetic-gemm-verified',gpu_ms=ms))
            except (ValueError, OSError, subprocess.TimeoutExpired) as e:
                results.append(dict(m=m,n=n,k=k,operation=name,status='not-verified',error=str(e)))
    report = dict(scope='synthetic NVFP4 GEMM only; not quantization accuracy, FFN, NR, or game validation',results=results)
    args.output.write_text(json.dumps(report, indent=2)+'\n',encoding='utf-8')
    return 0 if all(r['status']=='synthetic-gemm-verified' for r in results) else 2


if __name__ == '__main__': raise SystemExit(main())
