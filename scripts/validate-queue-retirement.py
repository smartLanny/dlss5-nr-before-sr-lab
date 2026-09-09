#!/usr/bin/env python3
"""Exercise complete production queue admission/submit code with actual WARP."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--reshade', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True,
                        help='Canonical staged Core source, including the integration adapters.')
    parser.add_argument('--old-policy-negative', action='store_true')
    args = parser.parse_args()
    out = args.build_dir.resolve()
    if out.exists():
        raise ValueError('Preserve existing queue evidence; use a fresh build directory.')
    out.mkdir(parents=True)
    compiler = args.compiler.resolve(strict=True)
    c_compiler = compiler.with_name(compiler.name.replace('clang++', 'clang'))
    source = args.source.resolve(strict=True)
    fixture = ROOT/'tests/queue_retirement_harness.cpp'
    wrapper = out/'queue-core.cpp'
    wrapper.write_text(f'#include "{source.as_posix()}"\n#include "{fixture.as_posix()}"\n', encoding='utf-8')
    common = ['-O1', '-DNDEBUG', '-DUNICODE', '-D_UNICODE', '-D_WIN64',
              '-ffunction-sections', '-fdata-sections',
              '-I'+str(ROOT/'third_party/minhook/include'),
              '-I'+str(args.reshade.resolve()/'include'),
              '-I'+str(args.reshade.resolve()/'deps/imgui')]
    if args.old_policy_negative:
        common.append('-DNR_QUEUE_OLD_POLICY_NEGATIVE')
    def run(command: list[str], name: str, timeout: int = 180) -> subprocess.CompletedProcess:
        result = subprocess.run(command, cwd=out, capture_output=True, text=True,
                                encoding='utf-8', errors='replace', timeout=timeout)
        (out/(name+'.log')).write_text(result.stdout+result.stderr, encoding='utf-8')
        if result.returncode and name.startswith('compile'):
            print((result.stdout+result.stderr)[-10000:])
            raise RuntimeError(f'{name} failed: {result.returncode}')
        return result
    objects = []
    for rel in ('buffer.c', 'hook.c', 'trampoline.c', 'hde/hde64.c'):
        obj = out/(Path(rel).stem+'.o')
        run([str(c_compiler), *common, '-std=c17', '-c',
             str(ROOT/'third_party/minhook/src'/rel), '-o', str(obj)], 'compile-'+obj.stem)
        objects.append(str(obj))
    binary = out/'queue-retirement.exe'
    run([str(compiler), *common, '-std=c++20', '-static', '-Wl,--gc-sections',
         str(wrapper), *objects, '-o', str(binary), '-lkernel32', '-luser32',
         '-ld3d12', '-ldxgi', '-ldxguid', '-lole32', '-luuid', '-ld3dcompiler', '-lshell32'], 'compile-core')
    cases = ['one-shot'] if args.old_policy_negative else [
        'one-shot', 'guards', 'pending-context', 'pending-slot',
        'migration', 'one-observation-migration', 'replay', 'discard',
        'submitted-retirement', 'retirement-timeout', 'retirement-guards', 'post-output']
    results = []
    for case in cases:
        result = run([str(binary), case], case, 30)
        print(result.stdout.strip())
        expected = (result.returncode == 41 and 'FAIL ONE_SHOT_RETIRED_ADMISSION' in result.stdout
                    if args.old_policy_negative else result.returncode == 0)
        results.append({'case': case, 'exit_code': result.returncode, 'expected_result': expected})
        if not expected:
            raise RuntimeError(f'Queue retirement case failed: {case}, exit={result.returncode}')
    evidence = {'source': str(source), 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                'fixture_sha256': hashlib.sha256(fixture.read_bytes()).hexdigest(),
                'old_policy_negative': args.old_policy_negative, 'cases': results,
                'production_translation_unit': True, 'warp': True, 'nvidia_nr': False}
    (out/'result.json').write_text(json.dumps(evidence, indent=2)+'\n', encoding='utf-8')
    print('PASS old queue policy rejected one-shot carrier as expected' if args.old_policy_negative
          else 'PASS production queue retirement matrix')

if __name__ == '__main__':
    main()
