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
NATIVE_EVENT_NEGATIVES = {
    'wrapper': ('native-wrapper-retention', 'NATIVE_WRAPPER_DESTROY_MUST_RETAIN_NR_RECORDING'),
    'reset-success': ('native-reset-success', 'NATIVE_SUCCESSFUL_RESET_MUST_CONSUME_OLD_NR_TOKEN'),
    'reset-failure': ('native-reset-failure', 'NATIVE_FAILED_RESET_MUST_RETAIN_NR_TOKEN'),
    'pending-fence': ('native-death-pending-fence', 'NATIVE_DEATH_PENDING_FENCE_MUST_RETAIN_OWNER'),
    'callback-consumption': ('native-final-destruction', 'FINAL_NATIVE_RELEASE_MUST_ONLY_PUBLISH_DEATH'),
    'marker-wrapper': ('native-marker-wrapper-retention', 'NATIVE_WRAPPER_DESTROY_MUST_RETAIN_NR_RECORDING'),
    'unsubmitted': ('native-unsubmitted-death', 'NATIVE_UNSUBMITTED_DEATH_MUST_RETAIN_QUARANTINED_CLOSURE'),
    'unsubmitted-post-ready': ('native-unsubmitted-death', 'NATIVE_UNSUBMITTED_COLLECTOR_MUST_STOP_BEFORE_POST_READY'),
    'notice-query': ('native-notifier-unavailable', 'NATIVE_UNSUPPORTED_NOTICE_MUST_REFUSE_NR_WITHOUT_HALF_PUBLICATION'),
    'notice-register': ('native-notifier-register-failure', 'NATIVE_UNSUPPORTED_NOTICE_MUST_REFUSE_NR_WITHOUT_HALF_PUBLICATION'),
}

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--reshade', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True,
                        help='Canonical staged Core source, including the integration adapters.')
    parser.add_argument('--old-policy-negative', action='store_true')
    parser.add_argument('--native-destruction-negative', action='store_true',
                        help='Run the native-final-destruction regression against an unfixed Core; require its exact runtime failure.')
    parser.add_argument('--native-event-negative', choices=NATIVE_EVENT_NEGATIVES,
                        help='Run one precise event-contract negative against a supplied fix-removal Core; no source is mutated by this runner.')
    parser.add_argument('--compile-only', action='store_true',
                        help='Compile the production queue host without running WARP cases.')
    args = parser.parse_args()
    if sum((args.old_policy_negative, args.native_destruction_negative,
            bool(args.native_event_negative))) > 1:
        parser.error('Choose one causal negative control per evidence directory.')
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
    evidence = {'source': str(source), 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                'compiler': str(compiler), 'compiler_sha256': hashlib.sha256(compiler.read_bytes()).hexdigest(),
                'fixture_sha256': hashlib.sha256(fixture.read_bytes()).hexdigest(),
                'fixture_inputs_sha256': {str(path.relative_to(ROOT)): digest for path, digest in fixture_inputs.items()},
                'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                'old_policy_negative': args.old_policy_negative,
                'native_destruction_negative': args.native_destruction_negative,
                'native_event_negative': args.native_event_negative,
                'production_translation_unit': True, 'compile_only': args.compile_only,
                'warp': False, 'nvidia_nr': False, 'passed': False,
                'status': 'compiling', 'commands': [], 'cases': []}
    def save():
        (out/'result.json').write_text(json.dumps(evidence, indent=2)+'\n', encoding='utf-8')
    save()
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
        try:
            result = subprocess.run(command, cwd=out, capture_output=True, text=True,
                                    encoding='utf-8', errors='replace', timeout=timeout)
        except subprocess.TimeoutExpired as error:
            def decoded(value):
                return value.decode('utf-8', errors='replace') if isinstance(value, bytes) else value or ''
            (out/(name+'.log')).write_text(decoded(error.stdout)+decoded(error.stderr), encoding='utf-8')
            evidence['commands'].append({'name': name, 'command': command, 'timeout_seconds': timeout,
                                         'timed_out': True})
            evidence['status'] = 'timed-out'
            save()
            raise
        (out/(name+'.log')).write_text(result.stdout+result.stderr, encoding='utf-8')
        evidence['commands'].append({'name': name, 'command': command, 'exit_code': result.returncode,
                                     'timeout_seconds': timeout})
        save()
        if result.returncode and name.startswith('compile'):
            evidence['status'] = 'compile-failed'
            save()
            print((result.stdout+result.stderr)[-10000:])
            raise RuntimeError(f'{name} failed: {result.returncode}')
        return result
    objects = list(map(str,objects_for(source)))
    evidence['verified_module_objects_sha256'] = {
        path: hashlib.sha256(Path(path).read_bytes()).hexdigest() for path in objects}
    save()
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
         '-ld3d12', '-ldxgi', '-ldxguid', '-lole32', '-luuid', '-ld3dcompiler', '-lshell32', '-lbcrypt'], 'compile-core')
    evidence['binary_sha256'] = hashlib.sha256(binary.read_bytes()).hexdigest()
    evidence['linked_objects_sha256'] = {
        path: hashlib.sha256(Path(path).read_bytes()).hexdigest() for path in objects}
    if args.compile_only:
        evidence['status'] = 'compiled-only'
        save()
        print('PASS production queue retirement compile-only; WARP cases not run')
        return
    cases = (['one-shot'] if args.old_policy_negative else
             ['native-final-destruction', 'native-marker-replacement'] if args.native_destruction_negative else [
                 NATIVE_EVENT_NEGATIVES[args.native_event_negative][0]] if args.native_event_negative else [
        'one-shot', 'guards', 'pending-context', 'pending-slot',
        'migration', 'one-observation-migration', 'replay', 'discard',
        'submitted-retirement', 'submitted-retirement-polling', 'retirement-timeout', 'retirement-guards', 'post-output', 'boundary-callbacks',
        'native-final-destruction', 'native-marker-replacement',
        'native-wrapper-retention', 'native-reset-success', 'native-reset-failure', 'native-death-pending-fence',
        'native-marker-wrapper-retention', 'native-unsubmitted-death',
        'native-notifier-unavailable', 'native-notifier-register-failure'])
    results = evidence['cases']
    evidence['warp'] = True
    evidence['status'] = 'running'
    evidence['planned_cases'] = cases
    save()
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
        expected_failure = ('ONE_SHOT_RETIRED_ADMISSION' if args.old_policy_negative else
                            'NATIVE_FINAL_DESTROY_MUST_RETIRE_NR_RECORDING' if args.native_destruction_negative else
                            NATIVE_EVENT_NEGATIVES[args.native_event_negative][1] if args.native_event_negative else None)
        expected = (result.returncode == 41 and ('FAIL '+expected_failure) in result.stdout.splitlines()
                    if expected_failure else result.returncode == 0)
        results.append({'case': case, 'exit_code': result.returncode, 'expected_result': expected,
                        'expected_failure': expected_failure,
                        'elapsed_seconds': elapsed, 'timeout_seconds': timeout})
        save()
        if not expected:
            evidence['status'] = 'case-failed'
            save()
            raise RuntimeError(f'Queue retirement case failed: {case}, exit={result.returncode}')
    if any(hashlib.sha256(path.read_bytes()).hexdigest()!=digest for path,digest in fixture_inputs.items()):
        evidence['status'] = 'fixture-inputs-changed'
        save()
        raise RuntimeError('Fixture inputs changed during validation; do not promote this run.')
    evidence['status'] = 'passed'
    evidence['passed'] = True
    save()
    print('PASS native destruction negative reproduced the exact NR recording leak' if args.native_destruction_negative else
          'PASS old queue policy rejected one-shot carrier as expected' if args.old_policy_negative
          else 'PASS precise native event negative: '+args.native_event_negative if args.native_event_negative
          else 'PASS production queue retirement matrix')

if __name__ == '__main__':
    main()
