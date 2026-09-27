"""Focused CPU/WARP guide validation. No NGX inference, game, or full Core build."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / 'experiments/nr-edit'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    compiler = args.compiler.resolve(strict=True)
    out = args.output.resolve()
    if out.exists():
        parser.error('Use a fresh output directory; retain earlier failures.')
    out.mkdir(parents=True)
    report = {'scope': 'NGX viewport jitter / HDR75 / SR and RR guide admission; CPU and WARP only',
              'commands': [], 'result': 'running', 'game': 'not-run', 'model': 'not-run'}
    def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
    sources = [ROOT/'src/nr_private_sr_guides.h', ROOT/'src/nr_private_sr_guides.cpp',
               ROOT/'tests/nr_private_sr_guides_cpu.cpp', ROOT/'tests/nr_private_sr_guides_warp.cpp',
               PACKAGE/'src/nr_edit_dx12.cpp', PACKAGE/'shaders/nr_normalize_guides.hlsl']
    report['source_hashes'] = {str(p.relative_to(ROOT)): sha(p) for p in sources}
    def save(): (out/'validation.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    def run(name, command):
        command = list(map(str, command))
        p = subprocess.run(command, cwd=out, capture_output=True, timeout=180,
                           creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        (out/(name+'.log')).write_bytes(p.stdout+p.stderr)
        report['commands'].append({'name': name, 'command': command, 'exit_code': p.returncode})
        save(); print(name, p.returncode, (p.stdout+p.stderr).decode('utf-8', 'replace')[-2000:], flush=True)
        if p.returncode: raise RuntimeError(name+' failed')
    try:
        common = [compiler, '-std=c++20', '-O2', '-static']
        run('build-cpu', [*common, ROOT/'tests/nr_private_sr_guides_cpu.cpp', '-o', out/'cpu.exe'])
        run('cpu', [out/'cpu.exe'])
        run('build-shader-compiler', [*common, '-municode', PACKAGE/'tools/compile_edit_shader.cpp',
                                    '-ld3dcompiler', '-o', out/'shader-compiler.exe'])
        run('compile-shaders', [sys.executable, PACKAGE/'tools/compile_edit_artifacts.py',
                               '--compiler', out/'shader-compiler.exe', '--source', PACKAGE/'shaders',
                               '--output', out/'shaders'])
        run('build-warp', [*common, '-municode', '-I'+str(PACKAGE/'src'),
                          ROOT/'tests/nr_private_sr_guides_warp.cpp', ROOT/'src/nr_private_sr_guides.cpp',
                          PACKAGE/'src/nr_edit_dx12.cpp', '-ld3d12', '-ldxgi', '-ldxguid', '-ld3dcompiler',
                          '-lole32', '-luuid', '-o', out/'warp.exe'])
        run('warp', [out/'warp.exe', out/'shaders'])
        if any(sha(ROOT/p)!=digest for p,digest in report['source_hashes'].items()):
            raise RuntimeError('Sources changed during validation')
        report['result'] = 'passed'
    except Exception as error:
        report['result'] = 'failed'; report['error'] = str(error)
    save()
    return 0 if report['result']=='passed' else 1

if __name__ == '__main__': raise SystemExit(main())
