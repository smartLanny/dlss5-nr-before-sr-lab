#!/usr/bin/env python3
"""Freeze and execute actual Feature1/RR verdict boundaries through CPU inputs.

The complete notification/getter bodies, bounded ledger and F8 writer are real;
no RR rendering, GPU, NGX model or additional recovery scope is exercised.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
INPUTS = ('nr_before_sr.cpp', 'nr_resource_recovery.h', 'nr_resource_recovery_runtime.inl',
          'nr_compatibility_verdict.h', 'nr_compatibility_runtime.inl', 'nr_compatibility_verdict_feedback.inl')


def block(source: str, marker: str) -> str:
    start = source.index(marker)
    end = source.index('{', start) + 1
    depth = 1
    while depth and end < len(source):
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    if depth:
        raise ValueError('Unterminated production definition: ' + marker)
    return source[start:end]


def generate(contents: dict[str, bytes], template: str) -> str:
    core, runtime, compatibility = (contents[name].decode('utf-8-sig') for name in
        ('nr_before_sr.cpp', 'nr_resource_recovery_runtime.inl', 'nr_compatibility_runtime.inl'))
    types = block(core, 'enum class BypassReason :') + ';\n' + block(runtime, 'struct NrRecoverySnapshot {') + ';\n'
    functions = '\n'.join((block(core, 'const char *BypassReasonName('),
        block(runtime, 'void NrObserveCompatibilityBypass('), block(core, 'void SetBypassReason('),
        block(compatibility, 'const char* NrRecoveryFailureReason('),
        block(compatibility, 'nr_compatibility_verdict::Input SnapshotNrCompatibilityVerdict(')))
    return template.replace('// @PRODUCTION_TYPES@', types).replace('// @PRODUCTION_FUNCTIONS@', functions)


def replace_once(source: str, anchor: str, replacement: str) -> str:
    if source.count(anchor) != 1:
        raise ValueError('Negative-control anchor changed: ' + anchor[:100])
    return source.replace(anchor, replacement, 1)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--source-root', type=Path, default=ROOT,
                        help='Repository/core root or its complete src directory.')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        raise ValueError('Preserve prior evidence; choose a fresh --output.')
    source = args.source_root.resolve(strict=True)
    if not (source / 'nr_before_sr.cpp').is_file():
        source /= 'src'
    contents = {name: (source / name).read_bytes() for name in INPUTS}
    fixture_path = ROOT / 'tests/compatibility_rr_scope.cpp.in'
    fixture = fixture_path.read_bytes()
    if any((source / name).read_bytes() != data for name, data in contents.items()):
        raise RuntimeError('Production inputs changed during snapshot.')
    compiler = args.compiler.resolve(strict=True)
    output.mkdir(parents=True)
    sha = lambda data: hashlib.sha256(data).hexdigest()
    results = {}

    def execute(label: str, actual: dict[str, bytes], case: str = 'all', oracle: str = '') -> bool:
        folder = output / label
        folder.mkdir()
        for name, data in actual.items():
            (folder / name).write_bytes(data)
        generated = generate(actual, fixture.decode('utf-8-sig'))
        cpp, exe = folder / 'fixture.cpp', folder / 'fixture.exe'
        cpp.write_text(generated, encoding='utf-8')
        command = [str(compiler), '-std=c++20', '-O1', '-Wall', '-Wextra', '-Werror', '-static',
                   '-I' + str(folder), str(cpp), '-o', str(exe)]
        built = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=90)
        (folder / 'compile.log').write_text(built.stdout + built.stderr, encoding='utf-8')
        record = {'compile_command': command, 'compile_exit': built.returncode,
                  'source_sha256': {name: sha(data) for name, data in actual.items()},
                  'fixture_sha256': sha(cpp.read_bytes()), 'case': case, 'expected_oracle': oracle}
        results[label] = record
        if built.returncode:
            print(built.stdout + built.stderr)
            record['passed'] = False
            return False
        run = subprocess.run([str(exe), case], capture_output=True, text=True,
                             encoding='utf-8', errors='replace', timeout=15)
        (folder / 'run.log').write_text(run.stdout + run.stderr, encoding='utf-8')
        passed = run.returncode == 0 if not oracle else run.returncode == 41 and run.stderr.strip() == 'FAIL ' + oracle
        record.update({'run_exit': run.returncode, 'run_stdout': run.stdout, 'run_stderr': run.stderr,
                       'binary_sha256': sha(exe.read_bytes()), 'passed': passed})
        print(run.stdout.strip() if not oracle else ('PASS' if passed else 'FAIL') + ' negative control: ' + label)
        return passed

    passed = execute('production', contents)
    runtime = contents['nr_resource_recovery_runtime.inl'].decode('utf-8-sig')
    none_block = block(runtime, '    if (reason == BypassReason::None) {\n        AcquireSRWLockExclusive')
    mutations = [
        ('old-nontls-none', 'clear', 'RR_CLEAR_ORACLE', 'nr_resource_recovery_runtime.inl',
         ((none_block, '    if (reason == BypassReason::None) return;'),)),
        ('clear-classified-failure', 'classified', 'CLASSIFIED_NONE_ORACLE', 'nr_resource_recovery_runtime.inl',
         (('if (!value.input.current.feature_generation && !value.key.resource)', 'if (true)'),)),
        ('old-rr-inherits-feature1', 'carrier', 'RR_PREVIOUS_SUCCESS_ORACLE', 'nr_compatibility_runtime.inl',
         (('    const bool different_carrier = g_last_carrier_feature.load(std::memory_order_acquire) == 13 &&\n'
           '        input.current.feature_generation != 0 && snapshot.key.resource != 0;\n', ''),
          (' || different_carrier)', ')'))),
        ('old-coverage-omitted', 'coverage', 'F8_COVERAGE_ORACLE', 'nr_compatibility_verdict_feedback.inl',
         (('    record.Field(root, "coverage", "classified Feature1 recovery; RR recovery uses its separate diagnostics and is not certified here");\n', ''),)),
    ]
    for label, case, oracle, target, replacements in mutations:
        altered = contents[target].decode('utf-8-sig')
        for anchor, replacement in replacements:
            altered = replace_once(altered, anchor, replacement)
        variant = dict(contents)
        variant[target] = altered.encode('utf-8')
        passed = execute(label, variant, case, oracle) and passed
    stable = all((source / name).read_bytes() == data for name, data in contents.items()) and fixture_path.read_bytes() == fixture
    passed = passed and stable
    summary = {'status': 'PASS' if passed else 'FAIL', 'source_root': str(source),
               'input_sha256': {name: sha(data) for name, data in contents.items()},
               'template_sha256': sha(fixture), 'compiler_sha256': sha(compiler.read_bytes()),
               'source_stable': stable, 'results': results, 'gpu': False, 'ngx': False,
               'scope': 'actual notification/getter/ledger/F8 through CPU values; real Win32 locks; synthetic receipts, no rendering'}
    (output / 'result.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print(('PASS' if passed else 'FAIL') + ' RR/Feature1 compatibility scope and four exact negative controls; no GPU/NGX')
    raise SystemExit(0 if passed else 1)


if __name__ == '__main__':
    main()
