#!/usr/bin/env python3
"""Compile the complete assembled Core and verify F8 timing/probe text on CPU.

No source-function extraction, serializer mocks, devices, NGX or game execution.
The only API seams are missing or fixed-width Win32 performance clocks, selected
in a fresh process before the production timing frequency cache initializes.
"""
from pathlib import Path
from temporal_test_objects import objects_for
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

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
    parser.add_argument('--negative-control', choices=('omit-end-timing', 'old-probe-current', 'old-tail-reserve', 'old-metadata-limit', 'old-text-limit',
                        'redact-timing-numbers', 'bypass-timing-privacy', 'untyped-decimal-exemption',
                        'omit-end-ui-receipt', 'redact-ui-decimals', 'omit-end-retirement',
                        'redact-boundary-numbers', 'bypass-boundary-privacy', 'borrow-historical-boundary'))
    parser.add_argument('--compile-only', action='store_true')
    parser.add_argument('--descriptor-profile', action='store_true', help='Compile the real descriptor timing formatter used by the measurement variant.')
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
        mutation = ('src/nr_feedback_limits.h', 'kMandatoryTailBytes = 185u * 1024u',
                    'kMandatoryTailBytes = 80u * 1024u', 'FULL_CORE_F8_CAPACITY')
    elif args.negative_control == 'old-metadata-limit':
        mutation = ('src/nr_feedback_limits.h', 'kMetadataBytes = 288u * 1024u',
                    'kMetadataBytes = 192u * 1024u', 'FULL_CORE_F8_CAPACITY')
    elif args.negative_control == 'old-text-limit':
        mutation = ('src/nr_feedback_single_text.h', 'kTextLimit = 480u * 1024u',
                    'kTextLimit = 384u * 1024u', 'FULL_CORE_F8_ALL_SOURCES_CAPACITY')
    elif args.negative_control == 'redact-timing-numbers':
        mutation = ('src/nr_feedback_text.h', 'line = RedactSensitiveLine(std::move(line), true);',
                    'line = RedactSensitiveLine(std::move(line), false);', 'FULL_CORE_F8_TYPED_METADATA_PRIVACY')
    elif args.negative_control == 'bypass-timing-privacy':
        anchor = 'inline std::string RedactSensitiveLine(std::string line, bool typed_metadata = false) {'
        mutation = ('src/nr_feedback_text.h', anchor, anchor +
                    '\n    if (typed_metadata && (line.find("start.timing.") == 0 || line.find("end.timing.") == 0)) return line;',
                    'FULL_CORE_F8_TYPED_METADATA_PRIVACY')
    elif args.negative_control == 'untyped-decimal-exemption':
        mutation = ('src/nr_feedback_text.h', 'if (typed_metadata && TimingMetadataNumber(line, i, end))',
                    "if (std::all_of(line.begin() + i, line.begin() + end, [](char c) { return c >= '0' && c <= '9'; }))",
                    'FULL_CORE_F8_TYPED_METADATA_PRIVACY')
    elif args.negative_control == 'omit-end-ui-receipt':
        mutation = ('src/nr_feedback_capture.inl', '    FeedbackAppendUiActivity(s,prefix);',
                    '    if (std::strcmp(prefix,"end") != 0) FeedbackAppendUiActivity(s,prefix);',
                    'FULL_CORE_F8_UI_RECORD')
    elif args.negative_control == 'redact-ui-decimals':
        mutation = ('src/nr_feedback_text.h', 'if (typed_metadata && UiActivityMetadataNumber(line, i, end))',
                    'if (false)', 'FULL_CORE_F8_UI_RECORD')
    elif args.negative_control == 'omit-end-retirement':
        mutation = ('src/nr_feedback_capture.inl', 'if(std::strcmp(prefix,"end")==0)FeedbackAppendRetirement(s,prefix);',
                    '(void)prefix;', 'FULL_CORE_F8_RETIREMENT_END')
    elif args.negative_control == 'redact-boundary-numbers':
        mutation = ('src/nr_feedback_text.h', 'if (typed_metadata && BoundaryMetadataNumber(line, i, end))',
                    'if (false && BoundaryMetadataNumber(line, i, end))', 'FULL_CORE_F8_READONLY_IDENTITY_PRESERVED')
    elif args.negative_control == 'bypass-boundary-privacy':
        anchor = 'inline std::string RedactSensitiveLine(std::string line, bool typed_metadata = false) {'
        mutation = ('src/nr_feedback_text.h', anchor, anchor +
                    '\n    if (typed_metadata && (line.find("start.post.boundary.") == 0 || line.find("end.post.boundary.") == 0)) return line;',
                    'FULL_CORE_F8_BOUNDARY_TYPED_PRIVACY')
    elif args.negative_control == 'borrow-historical-boundary':
        mutation = ('src/nr_post_boundary_runtime.inl', '} else if(latest) {', '} else if(false && latest) {',
                    'FULL_CORE_F8_CURRENT_READONLY_OLD_STORE_DISTINCT')
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
unsigned g_feedback_fixture_digits = 0;
unsigned long long g_feedback_fixture_clock_ticks = 0;
BOOL WINAPI FeedbackFixtureFrequency(LARGE_INTEGER* value) {
    if (g_feedback_fixture_missing_clock) { if(value)value->QuadPart=0; return FALSE; }
    if (g_feedback_fixture_digits) { if(value)value->QuadPart=10000000; return TRUE; }
    return ::QueryPerformanceFrequency(value);
}
BOOL WINAPI FeedbackFixtureCounter(LARGE_INTEGER* value) {
    if (g_feedback_fixture_missing_clock) { if(value)value->QuadPart=0; return FALSE; }
    if (g_feedback_fixture_digits) { if(value)value->QuadPart=static_cast<LONGLONG>(g_feedback_fixture_clock_ticks); return TRUE; }
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
              '-DNR_DESCRIPTOR_PROFILE=' + str(int(args.descriptor_profile)),
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

    objects = list(map(str,objects_for(source/'src/nr_before_sr.cpp')))
    # The complete Core references the dual spatial API from nr_before_sr.cpp,
    # while its D3D12 implementation is deliberately a separate production
    # translation unit.  Link that captured implementation when present so
    # this full-TU timing check exercises the same source closure as the Core.
    spatial_source = snapshot / 'src/nr_dual_spatial_d3d12.cpp'
    if spatial_source.is_file():
        spatial_object = out / 'nr_dual_spatial_d3d12.o'
        run([str(compiler), *common, '-std=c++20', '-c', str(spatial_source), '-o', str(spatial_object)],
            'compile-nr-dual-spatial')
        objects.append(str(spatial_object))
    for rel in ('buffer.c', 'hook.c', 'trampoline.c', 'hde/hde64.c'):
        obj = out / (Path(rel).stem + '.o')
        run([str(c_compiler), *common, '-std=c17', '-c', str(snapshot / 'third_party/minhook/src' / rel), '-o', str(obj)],
            'compile-' + obj.stem)
        objects.append(str(obj))
    binary = out / 'feedback-timing-full.exe'
    run([str(compiler), *common, '-std=c++20', '-static', '-municode', '-Wl,--gc-sections',
         str(wrapper), *objects, '-o', str(binary), '-lkernel32', '-luser32', '-ld3d12', '-ldxgi',
         '-ldxguid', '-lole32', '-luuid', '-ld3dcompiler', '-lshell32', '-lbcrypt'], 'compile-core')
    cases = []
    case_names = ('known-clock', 'missing-clock', 'qpc-12', 'qpc-13', 'qpc-14', 'qpc-15', 'qpc-16')
    if not args.compile_only:
        for case in case_names:
            result = run([str(binary), str(out / case), case], case, 30)
            print(result.stdout, end='', flush=True)
            matched = re.search(r'FULL_CORE_F8_TIMING checks=(\d+) failures=(\d+) metadataBytes=(\d+) metadataCapacity=(\d+) textBytes=(\d+) missingClock=(\d+)', result.stdout)
            tail = re.search(r'FULL_CORE_F8_TAIL startBytes=(\d+) requiredBytes=(\d+) configuredReserve=(\d+) tailTruncated=(\d+)', result.stdout)
            expected = (result.returncode == 41 and ('FAIL ' + mutation[3]) in result.stdout
                        if mutation else result.returncode == 0 and matched is not None)
            summary_info = None
            text_path_file = out / case / 'final-text-path.txt'
            if text_path_file.is_file():
                text_path = Path(text_path_file.read_text(encoding='utf-8'))
                summary_path = out / case / 'timing-summary.json'
                summary_result = run([sys.executable, str(ROOT / 'scripts/summarize-runtime-timing.py'),
                                      str(text_path), '--output', str(summary_path)],
                                     case + '-summary', 30)
                summary_info = {'exit_code': summary_result.returncode, 'input_text_sha256': sha(text_path),
                                'output': str(summary_path) if summary_path.is_file() else None}
                if summary_result.returncode == 0:
                    summarized = json.loads(summary_path.read_text(encoding='utf-8'))[0]
                    raw = dict(line.split('=', 1) for line in (out / case / 'metadata.txt').read_text(
                        encoding='utf-8').splitlines() if '=' in line)
                    frequency = int(raw['end.timing.qpcFrequency'])
                    clock = raw['end.timing.clockAvailable'] == '1' and frequency > 0
                    expected_seconds = ((int(raw['end.timing.capturedTicks']) -
                                         int(raw['end.timing.epochStartedTicks'])) / frequency if clock else None)
                    same_numbers = summarized['segment_seconds'] == expected_seconds
                    for stage, reported in summarized['stages'].items():
                        values = dict(item.split(':', 1) for item in raw['end.timing.' + stage].split(
                            ',histogram:', 1)[0].split(','))
                        count, measured = int(values['count']), int(values['measured'])
                        mean = int(values['totalTicks']) * 1000 / frequency / measured if clock and measured else None
                        maximum = int(values['maxTicks']) * 1000 / frequency if clock and measured else None
                        same_numbers = same_numbers and (reported['calls'], reported['measured_calls'],
                            reported['mean_cpu_ms'], reported['segment_max_cpu_ms']) == (count, measured, mean, maximum)
                    if case.startswith('qpc-'):
                        same_numbers = same_numbers and summarized['segment_seconds'] == 31.0
                    summary_info['matches_raw_writer_numbers'] = same_numbers
                if not mutation:
                    expected = expected and summary_result.returncode == 0 and summary_info.get('matches_raw_writer_numbers', False)
                elif args.negative_control == 'redact-timing-numbers':
                    # A real unavailable clock still has numeric zero fields.
                    # Fixed/real known clocks must reproduce the actual parser failure.
                    if case == 'missing-clock':
                        expected = expected and summary_result.returncode == 0
                    else:
                        expected = expected and summary_result.returncode != 0 and (
                            'Missing or invalid end.timing.epochStartedTicks' in summary_result.stderr)
            elif not mutation or args.negative_control == 'redact-timing-numbers':
                expected = False
            cases.append({'case': case, 'expected_result': expected, 'exit_code': result.returncode,
                          'summary': summary_info,
                          'tail': dict(zip(('start_bytes', 'required_bytes', 'configured_reserve', 'truncated'), map(int, tail.groups()))) if tail else None,
                          'measurements': dict(zip(('checks', 'failures', 'metadata_bytes', 'metadata_capacity', 'text_bytes', 'missing_clock'),
                                                   map(int, matched.groups()))) if matched else None})
            if not expected:
                print(result.stderr)
    final_inputs = {str(path.relative_to(snapshot)): sha(path)
                    for path in sorted(snapshot.rglob('*')) if path.is_file()}
    artifacts = [{'path': str(path.relative_to(out)), 'bytes': path.stat().st_size, 'sha256': sha(path)}
                 for case in case_names for path in sorted((out / case).rglob('*'))
                 if path.is_file() and path.suffix in ('.txt', '.json')]
    receipt = {'gate': 'assembled Core F8 timing/probe serialization CPU',
               'descriptor_profile': int(args.descriptor_profile),
               'passed': not args.compile_only and len(cases) == len(case_names) and all(case['expected_result'] for case in cases),
               'production_translation_unit': True, 'source_function_extraction': False,
               'source_root': str(source), 'original_source_sha256': inputs, 'compiled_source_sha256': final_inputs,
               'source_commit': source_commit, 'source_manifest': str(manifest_path),
               'source_manifest_sha256': sha(manifest_path), 'source_manifest_verified': True,
               'negative_control': args.negative_control, 'compile_only': args.compile_only,
               'fixture_sha256': sha(fixture), 'runner_sha256': sha(Path(__file__)), 'binary_sha256': sha(binary),
               'summary_script_sha256': sha(ROOT / 'scripts/summarize-runtime-timing.py'),
               'compiler_sha256': sha(compiler), 'reshade_headers': str(reshade),
               'cases': cases, 'logs': logs, 'artifacts': artifacts,
               'clock_fault_injection': 'Win32 QPF/QPC only; real, unavailable, or deterministic 12-16 digit clocks; fresh process per case',
               'gpu_executed': False, 'ngx_executed': False, 'game_verified': False,
               'worker_equivalent_bytes': 8192, 'timeline_burst_bytes': 4096,
               'private_counter_failure_width_allowance_per_snapshot': 2048,
               'retirement_scope': 'when NR_HAS_RETIREMENT_EVIDENCE: synthetic published POD, 32 full-width legal context masks plus full fixed detail tables; actual Activity writer, 24 KiB retirement text with explicit omissions, final semantic IDs preserved and addresses redacted; CPU only, no native owner, fence wait or real GPU work',
               'scope': 'metadata capacity including full route and descriptor/boundary records, real three-carrier recovery proof, actual 8192-slot/six-format descriptor inventory, full timeline burst, 8 KiB worker-equivalent body and 2 KiB private-counter/failure width allowance per snapshot; conditional retirement writer adds 32 full-width context rows/full fixed detail tables and a 24 KiB bounded tail with typed ID/address roundtrip; raw-to-final-to-disk timing field equality and actual Python summary; precise typed-number/privacy counterexamples through the actual single-text collector; no worker module enumeration or GPU execution'}
    (out / 'validation.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print('receipt:', out / 'validation.json')
    if not args.compile_only and not receipt['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
