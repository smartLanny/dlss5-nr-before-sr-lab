#!/usr/bin/env python3
"""Exercise complete production queue admission/submit code with actual WARP."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
from temporal_test_objects import objects_for
import subprocess
import re
import time

ROOT = Path(__file__).resolve().parents[1]

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--reshade', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True,
                        help='Canonical staged Core source, including the integration adapters.')
    parser.add_argument('--old-policy-negative', action='store_true')
    parser.add_argument('--compile-only', action='store_true',
                        help='Compile the production queue host without running WARP cases.')
    args = parser.parse_args()
    out = args.build_dir.resolve()
    if out.exists():
        raise ValueError('Preserve existing queue evidence; use a fresh build directory.')
    out.mkdir(parents=True)
    compiler = args.compiler.resolve(strict=True)
    c_compiler = compiler.with_name(compiler.name.replace('clang++', 'clang'))
    source = args.source.resolve(strict=True)
    fixture = ROOT/'tests/queue_retirement_harness.cpp'
    fixture_inputs = {}
    def bind_fixture(path):
        if path in fixture_inputs: return
        data=path.read_bytes()
        fixture_inputs[path]=hashlib.sha256(data).hexdigest()
        for name in re.findall(r'^#include "([^"]+)"',data.decode('utf-8-sig'),re.M):
            dependency=path.parent/name
            if dependency.is_file(): bind_fixture(dependency)
    bind_fixture(fixture)
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
    objects = list(map(str,objects_for(source)))
    # nr_before_sr.cpp exposes the dual spatial API through its public header;
    # the D3D12 implementation remains a separate production translation unit.
    # Compile the same sibling source as the staged Core when it is present.
    spatial_source = source.parent / 'nr_dual_spatial_d3d12.cpp'
    if spatial_source.is_file():
        spatial_object = out / 'nr_dual_spatial_d3d12.o'
        run([str(compiler), *common, '-std=c++20', '-c', str(spatial_source), '-o', str(spatial_object)],
            'compile-nr-dual-spatial')
        objects.append(str(spatial_object))
    for rel in ('buffer.c', 'hook.c', 'trampoline.c', 'hde/hde64.c'):
        obj = out/(Path(rel).stem+'.o')
        run([str(c_compiler), *common, '-std=c17', '-c',
             str(ROOT/'third_party/minhook/src'/rel), '-o', str(obj)], 'compile-'+obj.stem)
        objects.append(str(obj))
    binary = out/'queue-retirement.exe'
    run([str(compiler), *common, '-std=c++20', '-static', '-Wl,--gc-sections',
         str(wrapper), *objects, '-o', str(binary), '-lkernel32', '-luser32',
         '-ld3d12', '-ldxgi', '-ldxguid', '-lole32', '-luuid', '-ld3dcompiler', '-lshell32'], 'compile-core')
    if args.compile_only:
        evidence = {'source': str(source), 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                    'fixture_sha256': hashlib.sha256(fixture.read_bytes()).hexdigest(),
                    'fixture_inputs_sha256': {str(path.relative_to(ROOT)): digest for path, digest in fixture_inputs.items()},
                    'old_policy_negative': args.old_policy_negative, 'cases': [],
                    'production_translation_unit': True, 'compile_only': True,
                    'warp': False, 'nvidia_nr': False}
        (out/'result.json').write_text(json.dumps(evidence, indent=2)+'\n', encoding='utf-8')
        print('PASS production queue retirement compile-only; WARP cases not run')
        return
    cases = ['one-shot'] if args.old_policy_negative else [
        'one-shot', 'guards', 'pending-context', 'pending-slot',
        'migration', 'one-observation-migration', 'replay', 'discard',
        'submitted-retirement', 'submitted-retirement-polling', 'retirement-timeout', 'retirement-guards', 'post-output', 'boundary-callbacks']
    results = []
    for case in cases:
        # post-output contains 27 complete GPU/readback scenes plus cold shader
        # setup. Both the old and current Core approach the former 30s budget.
        # Keep a bounded correctness timeout without changing any assertion;
        # the smaller queue cases retain their original 30s deadline.
        timeout = 90 if case == 'post-output' else 30
        started = time.perf_counter()
        result = run([str(binary), case], case, timeout)
        elapsed = time.perf_counter()-started
        print(result.stdout.strip())
        expected = (result.returncode == 41 and 'FAIL ONE_SHOT_RETIRED_ADMISSION' in result.stdout
                    if args.old_policy_negative else result.returncode == 0)
        results.append({'case': case, 'exit_code': result.returncode, 'expected_result': expected,
                        'elapsed_seconds': elapsed, 'timeout_seconds': timeout})
        if not expected:
            raise RuntimeError(f'Queue retirement case failed: {case}, exit={result.returncode}')
    if any(hashlib.sha256(path.read_bytes()).hexdigest()!=digest for path,digest in fixture_inputs.items()):
        raise RuntimeError('Fixture inputs changed during validation; do not promote this run.')
    evidence = {'source': str(source), 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                'fixture_sha256': hashlib.sha256(fixture.read_bytes()).hexdigest(),
                'fixture_inputs_sha256': {str(path.relative_to(ROOT)):digest for path,digest in fixture_inputs.items()},
                'old_policy_negative': args.old_policy_negative, 'cases': results,
                'production_translation_unit': True, 'warp': True, 'nvidia_nr': False}
    (out/'result.json').write_text(json.dumps(evidence, indent=2)+'\n', encoding='utf-8')
    print('PASS old queue policy rejected one-shot carrier as expected' if args.old_policy_negative
          else 'PASS production queue retirement matrix')

if __name__ == '__main__':
    main()
