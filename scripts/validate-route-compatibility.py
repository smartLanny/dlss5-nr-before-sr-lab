#!/usr/bin/env python3
"""Execute current production suffix plus retained route-proof/recovery/F8 CPU controls."""
import argparse
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


def definition(source, marker):
    start = source.index(marker)
    at = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[at] == '{') - (source[at] == '}')
        at += 1
    return source[start:at]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source-root', type=Path, default=ROOT,
                        help='Repository or final staged core root containing src; fixture stays in this repository.')
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        raise SystemExit('Preserve prior evidence; choose a fresh output.')
    names = ('src/nr_route_compatibility.h', 'src/nr_route_compatibility_runtime.inl',
             'src/nr_resource_recovery.h', 'src/nr_resource_semantics.h', 'src/nr_native_format.h',
             'src/interop/nr_format_access.h', 'src/nr_compatibility_verdict.h',
             'src/nr_before_sr.cpp', 'src/nr_resource_recovery_runtime.inl',
             'src/interop/nr_post_feedback.inl', 'src/nr_processing_route.h', 'src/nr_processing_runtime.inl',
             'src/nr_native_format_runtime.inl', 'src/nr_native_clear_request.h', 'src/nr_pre_fallback.h',
             'tests/route_compatibility_runtime.cpp.in', 'tests/test_compat_route.py')
    source_root = args.source_root.resolve(strict=True)
    paths = {name: (ROOT if name.startswith('tests/') else source_root) / name for name in names}
    inputs = {name: path.read_bytes() for name, path in paths.items()}
    if any(paths[name].read_bytes() != data for name, data in inputs.items()):
        raise SystemExit('Inputs changed while freezing.')
    def sha(value): return hashlib.sha256(value).hexdigest()
    receipt_path = source_root.parent / 'source-manifest.json'
    if not receipt_path.is_file():
        receipt_path = source_root.parent / 'receipt.json'
    receipt = receipt_path.read_bytes() if receipt_path.is_file() else None
    receipt_metadata = json.loads(receipt) if receipt else None
    if receipt_metadata and receipt_metadata.get('files'):
        expected = receipt_metadata['files']
        if any(expected.get('core/' + name) != sha(data)
               for name, data in inputs.items() if name.startswith('src/')):
            raise SystemExit('Staged production inputs disagree with their transformation receipt.')
    compiler = args.compiler.resolve(strict=True)
    output.mkdir(parents=True)
    if receipt:
        (output / 'input-stage-receipt.json').write_bytes(receipt)
    # D15 uses the processing Session for actual dispatch. Execute the exact
    # frozen suffix, not the retired nr_route::Prepare orchestration wrapper.
    # The latter remains below as a historical policy comparison; its proof,
    # recovery and F8 observers are still connected to the production path.
    spec=importlib.util.spec_from_file_location('current_suffix_contract',ROOT/'tests/test_compat_route.py')
    suffix_test=importlib.util.module_from_spec(spec);spec.loader.exec_module(suffix_test)
    suffix_test.ROOT=source_root;suffix_test.CORE=source_root/'src/nr_before_sr.cpp'
    os.environ['COMPAT_ROUTE_TEST_CXX']=str(compiler)
    stream=io.StringIO()
    tested=unittest.TextTestRunner(stream=stream,verbosity=2).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(suffix_test.CompatibilityRoute))
    (output/'current-suffix.log').write_text(stream.getvalue(),encoding='utf-8')
    print(stream.getvalue())
    if not tested.wasSuccessful() or tested.skipped:
        raise SystemExit('Current frozen production suffix contract did not pass.')
    result = {'schema': 1, 'status': 'running', 'inputs': {name: sha(data) for name, data in inputs.items()},
              'source_root': str(source_root), 'fixture_root': str(ROOT),
              'source_stage_manifest_path': str(receipt_path) if receipt else None,
              'source_stage_receipt_sha256': sha(receipt) if receipt else None,
              'source_stage_scope': receipt_metadata.get('scope') if receipt_metadata else None,
              'compiler_sha256': sha(compiler.read_bytes()), 'gpu_executed': False, 'nvidia_nr': False,
              'current_production_suffix_verified':True,
              'scope': 'Current frozen ProcessEvaluate suffix and early guards; retained route proof/runtime/recovery/F8 observers, with historical Prepare comparison; resource/native/NR boundaries scripted; real Win32 value locks.', 'runs': []}
    controls = (
        ('production', None, None, None, 'ROUTE_CPU PASS'),
        ('unknown-pre-is-not-rejection', 'src/nr_route_compatibility.h',
         'value_.pre_definitive && value_.post_verified && !blocked_;',
         'value_.post_verified && !blocked_;', 'PRE_UNKNOWN_ORACLE'),
        ('old-scope-proof', 'src/nr_route_compatibility.h',
         'proof.window_first_frame_id >= value_.first_frame && proof.proof_frame_id >= value_.first_frame &&',
         'true &&', 'SCOPE_ORACLE'),
        ('skip-locked-post-admission', 'src/nr_route_compatibility.h',
         'after = prepare_after(fallback);', 'if (!skip_pre) after = prepare_after(fallback);', 'POST_GATE_ORACLE'),
        ('counts-are-not-writeback', 'src/nr_route_compatibility.h',
         'proof.window_last_frame_id <= value_.frame && proof.proof_frame_id <= value_.frame;',
         'proof.window_last_frame_id <= value_.frame && proof.proof_frame_id <= value_.frame;\n        if (proof.correlated_window) value_.post_verified = true;', 'STORAGE_ORACLE'),
        ('coarse-format-is-not-definitive', 'src/nr_compatibility_verdict.h',
         'if (current_rejection && !resource.definitive)', 'if (false)', 'ZZ_UNKNOWN_ORACLE'),
        ('ignore-current-lifetime-retirement', 'src/nr_route_compatibility_runtime.inl',
         'g_nr_route.Invalidate("resource-retired");', '(void)identity;', 'LIFETIME_ORACLE'),
        ('ignore-lifetime-receipts', 'src/nr_route_compatibility.h',
         'value_.current && value_.scope.lifetime_known && value_.scope.requested',
         'value_.current && value_.scope.requested', 'LIFETIME_COVERAGE_ORACLE'),
    )
    for label, target, anchor, replacement, marker in controls:
        folder = output / label
        text_inputs = {}
        for name, original in inputs.items():
            value = original.decode('utf-8-sig').replace('\r\n', '\n')
            if name == target:
                if value.count(anchor) != 1:
                    raise SystemExit('Inverse control anchor changed: ' + label)
                value = value.replace(anchor, replacement, 1)
            text_inputs[name] = value
            path = folder / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(value, encoding='utf-8')
        core = text_inputs['src/nr_before_sr.cpp']
        for anchor in ('nr_processing_runtime::CarrierScope processing_carrier;',
                       'NrRouteCarrierScope route_carrier(params, feature);',
                       'NrRouteAttemptPost();','NrRoutePostPrepared(after_sr.active,'):
            if anchor not in core:raise SystemExit('Current route observer/dispatch disconnected: '+anchor)
        feedback = text_inputs['src/interop/nr_post_feedback.inl']
        start = feedback.index('    const auto route=SnapshotNrRoute();')
        feedback = feedback[start:feedback.index('#endif', start)]
        harness = text_inputs['tests/route_compatibility_runtime.cpp.in']
        harness = harness.replace('// @RECORD@', definition(text_inputs['src/nr_resource_recovery_runtime.inl'], 'void NrRecoveryRecord('))
        harness = harness.replace('// @BYPASS@', definition(text_inputs['src/nr_resource_recovery_runtime.inl'], 'void NrObserveCompatibilityBypass('))
        harness = harness.replace('// @STORED@', definition(core, 'void NrRecoveryStoredEvidence('))
        harness = harness.replace('// @FEEDBACK@', feedback)
        source = folder / 'route-runtime.cpp'
        source.write_text(harness, encoding='utf-8')
        binary = folder / 'route-cpu.exe'
        command = [str(compiler), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                   '-I' + str(folder / 'src'), str(source), '-o', str(binary)]
        compiled = subprocess.run(command, capture_output=True, text=True, timeout=120)
        (folder / 'compile.log').write_text(compiled.stdout + compiled.stderr, encoding='utf-8')
        row = {'name': label, 'command': command, 'compile_exit': compiled.returncode}
        result['runs'].append(row)
        if compiled.returncode:
            print(compiled.stdout + compiled.stderr)
            break
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
        row.update(exit=run.returncode, output=run.stdout + run.stderr, binary_sha256=sha(binary.read_bytes()))
        (folder / 'run.log').write_text(row['output'], encoding='utf-8')
        row['expected_result'] = run.returncode == (41 if target else 0) and marker in row['output']
        print(label + ': ' + ('expected inverse assertion caught: ' + marker
              if target and row['expected_result'] else row['output'].strip()))
    result['status'] = 'pass' if len(result['runs']) == len(controls) and all(
        item.get('expected_result') for item in result['runs']) else 'failed'
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    raise SystemExit(0 if result['status'] == 'pass' else 1)


if __name__ == '__main__':
    main()
