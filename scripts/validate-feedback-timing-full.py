#!/usr/bin/env python3
"""Compile the complete assembled Core and verify F8 timing/probe text on CPU.

No source-function extraction, serializer mocks, devices, NGX or game execution.
The only API fault injection is a missing Win32 performance clock, selected in
a fresh process before the production timing frequency cache initializes.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
HIDDEN = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--reshade', type=Path, required=True)
    parser.add_argument('--source-root', type=Path, required=True,
                        help='Frozen assembled Core directory containing src/nr_before_sr.cpp.')
    parser.add_argument('--source-manifest', type=Path,
                        help='Defaults to source-root/../source-manifest.json; all compiled Core inputs must match it.')
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--negative-control', choices=('omit-end-timing', 'old-probe-current', 'old-tail-reserve'))
    parser.add_argument('--compile-only', action='store_true')
    args = parser.parse_args()
    source = args.source_root.resolve(strict=True)
    out = args.output_dir.resolve()
    if out.exists():
        raise SystemExit('Preserve existing full-Core evidence; use a fresh output directory.')
    if not (source / 'src/nr_before_sr.cpp').is_file():
        raise SystemExit('--source-root must contain the actual assembled src/nr_before_sr.cpp.')
    manifest_path = (args.source_manifest or source.parent / 'source-manifest.json').resolve(strict=True)
    manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    source_commit = manifest.get('source_commit', '')
    if not re.fullmatch('[0-9a-f]{40}', source_commit):
        raise SystemExit('Frozen source manifest must identify its full source commit.')
    for path in sorted((source / 'src').rglob('*')):
        if path.is_file():
            key = 'core/' + path.relative_to(source).as_posix()
            if manifest.get('files', {}).get(key) != sha(path):
                raise SystemExit('Frozen Core input does not match source manifest: ' + key)
    out.mkdir(parents=True)
    snapshot = out / 'source/core'
    shutil.copytree(source / 'src', snapshot / 'src')
    minhook = source / 'third_party/minhook'
    if not minhook.is_dir():
        raise SystemExit('Frozen assembled Core must include its MinHook sources.')
    for path in sorted(minhook.rglob('*')):
        if path.is_file():
            key = 'core/' + path.relative_to(source).as_posix()
            if manifest.get('files', {}).get(key) != sha(path):
                raise SystemExit('Frozen MinHook input does not match source manifest: ' + key)
    shutil.copytree(minhook, snapshot / 'third_party/minhook')
    inputs = {str(path.relative_to(snapshot)): sha(path)
              for path in sorted(snapshot.rglob('*')) if path.is_file()}
    mutation = None
    if args.negative_control == 'omit-end-timing':
        mutation = ('src/nr_feedback_capture.inl', '    FeedbackAppendTiming(s,prefix);',
                    '    if (std::strcmp(prefix,"end") != 0) FeedbackAppendTiming(s,prefix);', 'FULL_CORE_F8_TIMING_END')
    elif args.negative_control == 'old-probe-current':
        mutation = ('src/nr_feedback_capture.inl',
                    'available && sample.probe_policy.phase==probe_policy.phase &&\n                sample.probe_policy.mode==probe_policy.mode',
                    'available', 'FULL_CORE_F8_OLD_PROBE_PHASE')
    elif args.negative_control == 'old-tail-reserve':
        mutation = ('src/nr_feedback_limits.h', 'kMandatoryTailBytes = 80u * 1024u',
                    'kMandatoryTailBytes = 64u * 1024u', 'FULL_CORE_F8_CAPACITY')
    if mutation:
        path = snapshot / mutation[0]
        text = path.read_text(encoding='utf-8-sig')
        if text.count(mutation[1]) != 1:
            raise SystemExit('Negative-control production anchor changed: ' + args.negative_control)
        text = text.replace(mutation[1], mutation[2], 1)
        path.write_text(text, encoding='utf-8')
    fixture = ROOT / 'tests/feedback_timing_full.inl'
    shutil.copy2(fixture, out / 'fixture.inl')
    wrapper = out / 'feedback-timing-core.cpp'
    wrapper.write_text('''#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define CINTERFACE
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
bool g_feedback_fixture_missing_clock = false;
BOOL WINAPI FeedbackFixtureFrequency(LARGE_INTEGER* value) {
    if (g_feedback_fixture_missing_clock) { if(value)value->QuadPart=0; return FALSE; }
    return ::QueryPerformanceFrequency(value);
}
BOOL WINAPI FeedbackFixtureCounter(LARGE_INTEGER* value) {
    if (g_feedback_fixture_missing_clock) { if(value)value->QuadPart=0; return FALSE; }
    return ::QueryPerformanceCounter(value);
}
#define QueryPerformanceFrequency FeedbackFixtureFrequency
#define QueryPerformanceCounter FeedbackFixtureCounter
#include "''' + (snapshot / 'src/nr_before_sr.cpp').as_posix() + '''"
#undef QueryPerformanceFrequency
#undef QueryPerformanceCounter
#include "fixture.inl"
''', encoding='utf-8')
    compiler = args.compiler.resolve(strict=True)
    c_compiler = compiler.with_name(compiler.name.replace('clang++', 'clang'))
    reshade = args.reshade.resolve(strict=True)
    common = ['-O1', '-DNDEBUG', '-DUNICODE', '-D_UNICODE', '-D_WIN64',
              '-DNR_SOURCE_COMMIT="' + source_commit + '"',
              '-ffunction-sections', '-fdata-sections',
              '-I' + str(snapshot / 'third_party/minhook/include'),
              '-I' + str(reshade / 'include'), '-I' + str(reshade / 'deps/imgui')]
    logs = []

    def run(command, name, timeout=180):
        result = subprocess.run(command, cwd=out, capture_output=True, text=True,
                                encoding='utf-8', errors='replace', timeout=timeout, creationflags=HIDDEN)
        path = out / (name + '.log')
        path.write_text(result.stdout + result.stderr, encoding='utf-8')
        logs.append({'name': name, 'command': command, 'exit_code': result.returncode,
                     'log': str(path), 'log_sha256': sha(path)})
        if result.returncode and name.startswith('compile'):
            print((result.stdout + result.stderr)[-16000:])
            raise RuntimeError(name + ' failed')
        return result

    objects = []
    for rel in ('buffer.c', 'hook.c', 'trampoline.c', 'hde/hde64.c'):
        obj = out / (Path(rel).stem + '.o')
        run([str(c_compiler), *common, '-std=c17', '-c', str(snapshot / 'third_party/minhook/src' / rel), '-o', str(obj)],
            'compile-' + obj.stem)
        objects.append(str(obj))
    binary = out / 'feedback-timing-full.exe'
    run([str(compiler), *common, '-std=c++20', '-static', '-municode', '-Wl,--gc-sections',
         str(wrapper), *objects, '-o', str(binary), '-lkernel32', '-luser32', '-ld3d12', '-ldxgi',
         '-ldxguid', '-lole32', '-luuid', '-ld3dcompiler', '-lshell32'], 'compile-core')
    cases = []
    if not args.compile_only:
        for case in ('known-clock', 'missing-clock'):
            result = run([str(binary), str(out / case), case], case, 30)
            print(result.stdout, end='', flush=True)
            matched = re.search(r'FULL_CORE_F8_TIMING checks=(\d+) failures=(\d+) metadataBytes=(\d+) metadataCapacity=(\d+) textBytes=(\d+) missingClock=(\d+)', result.stdout)
            tail = re.search(r'FULL_CORE_F8_TAIL startBytes=(\d+) requiredBytes=(\d+) configuredReserve=(\d+) tailTruncated=(\d+)', result.stdout)
            expected = (result.returncode == 41 and ('FAIL ' + mutation[3]) in result.stdout
                        if mutation else result.returncode == 0 and matched is not None)
            cases.append({'case': case, 'expected_result': expected, 'exit_code': result.returncode,
                          'tail': dict(zip(('start_bytes', 'required_bytes', 'configured_reserve', 'truncated'), map(int, tail.groups()))) if tail else None,
                          'measurements': dict(zip(('checks', 'failures', 'metadata_bytes', 'metadata_capacity', 'text_bytes', 'missing_clock'),
                                                   map(int, matched.groups()))) if matched else None})
            if not expected:
                print(result.stderr)
    final_inputs = {str(path.relative_to(snapshot)): sha(path)
                    for path in sorted(snapshot.rglob('*')) if path.is_file()}
    artifacts = [{'path': str(path.relative_to(out)), 'bytes': path.stat().st_size, 'sha256': sha(path)}
                 for path in sorted(out.glob('*/*.txt')) if path.is_file()]
    receipt = {'gate': 'assembled Core F8 timing/probe serialization CPU',
               'passed': not args.compile_only and len(cases) == 2 and all(case['expected_result'] for case in cases),
               'production_translation_unit': True, 'source_function_extraction': False,
               'source_root': str(source), 'original_source_sha256': inputs, 'compiled_source_sha256': final_inputs,
               'source_commit': source_commit, 'source_manifest': str(manifest_path),
               'source_manifest_sha256': sha(manifest_path), 'source_manifest_verified': True,
               'negative_control': args.negative_control, 'compile_only': args.compile_only,
               'fixture_sha256': sha(fixture), 'runner_sha256': sha(Path(__file__)), 'binary_sha256': sha(binary),
               'compiler_sha256': sha(compiler), 'reshade_headers': str(reshade),
               'cases': cases, 'logs': logs, 'artifacts': artifacts,
               'clock_fault_injection': 'Win32 QueryPerformanceFrequency/Counter only; fresh process per case',
               'gpu_executed': False, 'ngx_executed': False, 'game_verified': False,
               'worker_equivalent_bytes': 8192, 'timeline_burst_bytes': 4096,
               'scope': 'metadata capacity including a full timeline burst and synthetic 8 KiB worker-equivalent body, timing identities, policy phases, unknown clocks and final actual single text; no worker module enumeration or GPU execution'}
    (out / 'validation.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print('receipt:', out / 'validation.json')
    if not args.compile_only and not receipt['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
