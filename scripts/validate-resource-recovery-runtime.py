#!/usr/bin/env python3
"""Freeze/build the full production Core recovery fixture; WARP run is explicit.

Default operation compiles only. --prepare-only freezes inputs. --run-warp is
the only mode which starts a GPU device. --snapshot-cpu runs the actual mapper
with synthetic value facts plus inverse history/signature controls, without a
device or GPU. A pending fixture never reports a test pass.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PRE_CASES = ('cold-auto-recover', 'missing-descriptor', 'nr-failure', 'native-failure', 'record-only', 'wrong-target', 'delayed-collector-failure')
POST_CASES = ('post-r11-recover', 'post-actual-nonfinite', 'post-reference-nonfinite')
CASES = PRE_CASES + POST_CASES


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def stable_tree(folder: Path) -> dict[str, bytes]:
    paths = {p.relative_to(folder).as_posix(): p for p in sorted(folder.rglob('*')) if p.is_file()}
    contents = {name: path.read_bytes() for name, path in paths.items()}
    if paths.keys() != {p.relative_to(folder).as_posix() for p in folder.rglob('*') if p.is_file()} or any(
            path.read_bytes() != contents[name] for name, path in paths.items()):
        raise RuntimeError('Source changed while freezing; preserve this attempt and retry with a fresh output.')
    return contents


def prefix(data: bytes, anchor: str, namespace: str, required: str) -> str:
    text = data.decode('utf-8-sig')
    if text.count(anchor) != 1:
        raise RuntimeError('Borrowed helper boundary changed: ' + anchor)
    part = text.split(anchor, 1)[0]
    if required not in part:
        raise RuntimeError('Borrowed helper is missing its actual production fixture: ' + required)
    return part + '\n} // namespace ' + namespace + '\n'


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='Complete transformed src/nr_before_sr.cpp and its sibling production modules.')
    parser.add_argument('--helpers-root', type=Path, default=ROOT, help='Repository containing the established full-Core fixtures and MinHook.')
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--reshade', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--prepare-only', action='store_true')
    mode.add_argument('--run-warp', action='store_true')
    mode.add_argument('--snapshot-cpu', action='store_true')
    parser.add_argument('--case', choices=('all', 'post', *CASES), default='all')
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        raise SystemExit('Preserve prior evidence; choose a fresh --output.')
    source = args.source.resolve(strict=True)
    helpers_root = args.helpers_root.resolve(strict=True)
    compiler = args.compiler.resolve(strict=True)
    reshade = args.reshade.resolve(strict=True)
    source_contents = stable_tree(source.parent)
    stage_receipt_path = source.parents[2] / 'receipt.json'
    stage_receipt = stage_receipt_path.read_bytes() if stage_receipt_path.is_file() else None
    stage_metadata = json.loads(stage_receipt) if stage_receipt else None
    if stage_metadata and stage_metadata.get('files'):
        expected = stage_metadata['files']
        if any(expected.get('core/src/' + name) != sha(data) for name, data in source_contents.items()):
            raise RuntimeError('Complete source snapshot disagrees with its transformation receipt.')
    fixture_name = 'resource_recovery_snapshot_harness.inl' if args.snapshot_cpu else 'resource_recovery_runtime_harness.inl'
    fixture = (ROOT / 'tests' / fixture_name).read_bytes()
    helper_names = () if args.snapshot_cpu else ('queue_retirement_harness.cpp', 'post_output_harness.inl',
                    'native_color_view_harness.inl', 'pre_store_probe_harness.inl', 'store_quantization_harness.inl',
                    'store_interval_shader.h')
    helpers = {name: (helpers_root / 'tests' / name).read_bytes() for name in helper_names}
    minhook = stable_tree(helpers_root / 'third_party/minhook')
    if any((helpers_root / 'tests' / name).read_bytes() != data for name, data in helpers.items()):
        raise RuntimeError('Borrowed helpers changed during snapshot; retry when their edits settle.')
    borrowed = '' if args.snapshot_cpu else ''.join((
        prefix(helpers['queue_retirement_harness.cpp'], 'void OneShot() {', 'queue_test', 'struct Fixture'),
        prefix(helpers['post_output_harness.inl'], 'std::vector<unsigned char> Case(queue_test::Fixture', 'post_output_test', 'ReadTexture('),
        prefix(helpers['native_color_view_harness.inl'], 'float Half(', 'native_color_test', 'void ResetContext('),
        prefix(helpers['pre_store_probe_harness.inl'], 'void Case(queue_test::Fixture', 'pre_store_test', 'void MakeSource('),
        prefix(helpers['store_quantization_harness.inl'], 'ID3D12Resource *MakeBuffer(', 'store_quantization_test', 'void MakeTexture('),
    ))
    output.mkdir(parents=True)
    snapshot = output / 'core/src'
    for name, data in source_contents.items():
        path = snapshot / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    for name, data in helpers.items():
        path = output / 'original-helpers' / name
        path.parent.mkdir(exist_ok=True)
        path.write_bytes(data)
    minhook_snapshot = output / 'minhook'
    for name, data in minhook.items():
        path = minhook_snapshot / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    (output / 'borrowed-helpers.inl').write_text(borrowed, encoding='utf-8')
    (output / 'runtime-fixture.inl').write_bytes(fixture)
    if stage_receipt:
        (output / 'input-stage-receipt.json').write_bytes(stage_receipt)
    wrapper = output / 'resource-recovery-runtime.cpp'
    test_namespace = 'resource_recovery_snapshot_test' if args.snapshot_cpu else 'resource_recovery_runtime_test'
    wrapper.write_text('#include "' + (snapshot / source.name).as_posix() + '"\n'
                       '#include "borrowed-helpers.inl"\n#include "runtime-fixture.inl"\n'
                       'int main(int argc,char** argv){std::setvbuf(stdout,nullptr,_IONBF,0);'
                       'return ' + test_namespace + '::Run(argc,argv);}\n', encoding='utf-8')
    api_marker = b'#define NR_RECOVERY_SNAPSHOT_FIXTURE_API_READY 1' if args.snapshot_cpu else b'#define NR_RECOVERY_RUNTIME_FIXTURE_API_READY 1'
    api_ready = api_marker in fixture
    runtime_included = b'"nr_resource_recovery_runtime.inl"' in source_contents[source.name]
    summary = {
        'schema': 1, 'status': 'prepared', 'fixture_api_ready': api_ready,
        'source_path': str(source), 'helpers_root': str(helpers_root),
        'source_stage_receipt_sha256': sha(stage_receipt) if stage_receipt else None,
        'source_stage_scope': stage_metadata.get('scope') if stage_metadata else None,
        'runtime_included_in_complete_core': runtime_included,
        'source_inputs_sha256': {name: sha(data) for name, data in source_contents.items()},
        'fixture_sha256': sha(fixture), 'borrowed_helpers_sha256': {name: sha(data) for name, data in helpers.items()},
        'minhook_inputs_sha256': {name: sha(data) for name, data in minhook.items()},
        'compiler_sha256': sha(compiler.read_bytes()), 'commands': [],
        'production_translation_unit': True, 'production_policy_replaced': False,
        'planned_cases': ['receipt-overflow', 'frame-history', 'failed-and-unavailable', 'typed-native-cache-churn',
                          'typeless-native-cache-churn'] if args.snapshot_cpu else list(CASES),
        'case': 'snapshot-cpu' if args.snapshot_cpu else args.case, 'compile_exit': None, 'run_exit': None,
        'gpu_executed': False, 'nvidia_nr': False, 'game_verified': False,
        'vendor_boundary': 'synthetic CPU value facts; production snapshot mapper and overflow adapter' if args.snapshot_cpu else
                           'scripted NGX callback; actual Core, descriptors, shaders, queue and readback',
        'callback_result_wiring_only': not args.snapshot_cpu, 'synthetic_value_facts': args.snapshot_cpu,
    }

    def save() -> None:
        (output / 'result.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')

    if args.prepare_only:
        summary['status'] = 'prepared-api-pending' if not api_ready or not runtime_included else 'prepared-not-run'
        save()
        print(summary['status'] + ': frozen complete-Core inputs; no compilation, GPU or test pass')
        return
    if not api_ready or not runtime_included:
        summary['status'] = 'api-pending-not-tested'
        save()
        raise SystemExit('RuntimeRecovery fixture wiring or full-Core include is pending; use --prepare-only until ready.')

    c_compiler = compiler.with_name(compiler.name.replace('clang++', 'clang'))
    common = ['-O1', '-DNDEBUG', '-DUNICODE', '-D_UNICODE', '-D_WIN64', '-ffunction-sections', '-fdata-sections',
              '-I' + str(minhook_snapshot / 'include'), '-I' + str(output / 'original-helpers'), '-I' + str(reshade / 'include'),
              '-I' + str(reshade / 'deps/imgui')]

    def run(command: list[str], label: str, timeout: int = 180, warp: bool = False) -> subprocess.CompletedProcess:
        summary['commands'].append(command)
        environment = dict(os.environ)
        if warp:
            environment['NR_VALIDATION_WARP_ONLY'] = '1'
        result = subprocess.run(command, cwd=output, capture_output=True, text=True, encoding='utf-8',
                                errors='replace', timeout=timeout, env=environment)
        (output / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        if result.returncode:
            print((result.stdout + result.stderr)[-16000:])
        return result

    objects = []
    # nr_before_sr.cpp includes the public dual spatial header, but the D3D12
    # implementation is a separate production translation unit.  Include the
    # captured implementation whenever the frozen source tree contains it so
    # snapshot and WARP recovery links use the complete Core closure.
    spatial_source = output / 'core/src/nr_dual_spatial_d3d12.cpp'
    if spatial_source.is_file():
        spatial_object = output / 'nr_dual_spatial_d3d12.o'
        spatial_compile = run([str(compiler), *common, '-std=c++20', '-c', str(spatial_source), '-o', str(spatial_object)],
                              'compile-nr-dual-spatial')
        if spatial_compile.returncode:
            summary.update(status='compile-failed', compile_exit=spatial_compile.returncode)
            save()
            raise SystemExit(spatial_compile.returncode)
        objects.append(str(spatial_object))
    for name in ('buffer.c', 'hook.c', 'trampoline.c', 'hde/hde64.c'):
        obj = output / (Path(name).stem + '.o')
        result = run([str(c_compiler), *common, '-std=c17', '-c', str(minhook_snapshot / 'src' / name), '-o', str(obj)],
                     'compile-' + obj.stem)
        if result.returncode:
            summary.update(status='compile-failed', compile_exit=result.returncode)
            save()
            raise SystemExit(result.returncode)
        objects.append(str(obj))
    binary = output / ('resource-recovery-snapshot-cpu.exe' if args.snapshot_cpu else 'resource-recovery-runtime.exe')
    result = run([str(compiler), *common, '-std=c++20', '-static', '-Wl,--gc-sections', str(wrapper), *objects,
                  '-o', str(binary), '-lkernel32', '-luser32', '-ld3d12', '-ldxgi', '-ldxguid', '-lole32',
                  '-luuid', '-ld3dcompiler', '-lshell32'], 'compile-core')
    summary.update(status='compile-failed' if result.returncode else 'compiled-not-run', compile_exit=result.returncode)
    if not result.returncode:
        summary['binary_sha256'] = sha(binary.read_bytes())
        if args.snapshot_cpu:
            positive = run([str(binary)], 'snapshot-cpu', 30)
            print(positive.stdout)
            summary.update(run_exit=positive.returncode, status='failed' if positive.returncode else 'snapshot-positive-passed',
                           positive_output=positive.stdout.strip())
            inverses = (
                ('old-history', 'nr_compatibility_runtime.inl', r'if \(proof\.capacity_unknown\) \{',
                 'if (proof.capacity_unknown || proof.frame_evidence_lost || snapshot.receipt_overflow) {',
                 ('RECEIPT_HISTORY_ORACLE', 'FRAME_HISTORY_ORACLE')),
                ('old-view-signature', 'nr_resource_recovery_semantics.inl',
                 r'return \{result\.plan\.srv, result\.plan\.uav,\s*static_cast<unsigned>\(result\.semantic\.contract\), 1\};',
                 'return {result.plan.srv, result.plan.uav, static_cast<unsigned>(result.semantic.source), '
                 'result.semantic.source == nr_resource_semantics::Source::ObservedNativeView ? '
                 'result.evidence.first_call : (static_cast<unsigned long long>(result.semantic.contract) + 1)};',
                 ('SIGNATURE_SOURCE_ORACLE', 'SIGNATURE_CHURN_ORACLE')),
            )
            controls = []
            for label, source_file, anchor, replacement, markers in inverses:
                original = source_contents[source_file].decode('utf-8-sig')
                if len(re.findall(anchor, original)) != 1:
                    save()
                    raise SystemExit(label + ' inverse anchor changed; update the narrowly scoped private mutation.')
                mutated = re.sub(anchor, lambda _: replacement, original, count=1).encode('utf-8')
                negative_snapshot = output / (label + '-core/src')
                for name, data in source_contents.items():
                    path = negative_snapshot / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(mutated if name == source_file else data)
                negative_wrapper = output / ('snapshot-' + label + '.cpp')
                negative_wrapper.write_text('#include "' + (negative_snapshot / source.name).as_posix() + '"\n'
                    '#include "runtime-fixture.inl"\nint main(int argc,char** argv){std::setvbuf(stdout,nullptr,_IONBF,0);'
                    'return resource_recovery_snapshot_test::Run(argc,argv);}\n', encoding='utf-8')
                negative_binary = output / ('snapshot-' + label + '.exe')
                negative_compile = run([str(compiler), *common, '-std=c++20', '-static', '-Wl,--gc-sections',
                    str(negative_wrapper), *objects, '-o', str(negative_binary), '-lkernel32', '-luser32', '-ld3d12',
                    '-ldxgi', '-ldxguid', '-lole32', '-luuid', '-ld3dcompiler', '-lshell32'], 'compile-' + label)
                if negative_compile.returncode:
                    summary.update(status='negative-control-compile-failed', negative_compile_exit=negative_compile.returncode)
                    save()
                    raise SystemExit(negative_compile.returncode)
                negative = run([str(negative_binary)], 'snapshot-' + label, 30)
                caught = negative.returncode == 41 and all(marker in negative.stdout for marker in markers)
                controls.append({'name': label, 'caught': caught, 'exit': negative.returncode,
                    'source_file': source_file, 'original_sha256': sha(source_contents[source_file]),
                    'mutated_sha256': sha(mutated), 'binary_sha256': sha(negative_binary.read_bytes()),
                    'output': negative.stdout.strip()})
            passed = positive.returncode == 0 and 'SNAPSHOT_CPU PASS' in positive.stdout and all(c['caught'] for c in controls)
            summary.update(status='pass' if passed else 'failed', negative_control=controls[0], negative_controls=controls,
                           gpu_executed=False, device_created=False)
            save()
            print(('PASS' if passed else 'FAIL') + ' actual snapshot mapper CPU wiring and history/signature inverse controls; no GPU/device/NGX')
            raise SystemExit(0 if passed else 1)
        elif args.run_warp:
            summary['gpu_executed'] = True
            result = run([str(binary), args.case], 'warp-' + args.case, 120, warp=True)
            passed_cases = re.findall(r'^RUNTIME_RECOVERY_CASE (\S+) PASS ', result.stdout, re.MULTILINE)
            expected_cases = list(CASES) if args.case == 'all' else list(POST_CASES) if args.case == 'post' else [args.case]
            complete = passed_cases == expected_cases and 'RUNTIME_RECOVERY WARP PASS' in result.stdout
            summary.update(status='pass' if not result.returncode and complete else 'failed', run_exit=result.returncode,
                           passed_cases=passed_cases, expected_cases=expected_cases,
                           all_case_markers_present=complete, child_environment={'NR_VALIDATION_WARP_ONLY': '1'})
            print(result.stdout)
            if not complete and not result.returncode:
                save()
                raise SystemExit('Runtime returned zero without the complete requested scenario evidence.')
    save()
    raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
