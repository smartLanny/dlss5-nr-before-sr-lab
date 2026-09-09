#!/usr/bin/env python3
"""Run the actual bounded recovery ledger with independent false-success controls.

Freezes the production header and fixture in a fresh evidence directory, then
compiles the real policy plus narrowly mutated private copies. No checkout
mutation, GPU, NGX, worker, submission or in-game success is implied by this gate.
"""
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
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        raise ValueError('Preserve prior evidence; choose a fresh --output.')
    output.mkdir(parents=True)
    source_root = args.source_root.resolve(strict=True)
    header = next((p for p in (source_root / 'src/nr_resource_recovery.h',
                              source_root / 'nr_resource_recovery.h') if p.is_file()), None)
    if header is None:
        raise FileNotFoundError('nr_resource_recovery.h not found at --source-root')
    contents = header.read_bytes()
    fixture = (ROOT / 'tests/resource_recovery.cpp').read_bytes()
    if contents != header.read_bytes():
        raise RuntimeError('Production header changed while freezing; retry with a fresh output.')
    source = output / 'source'
    source.mkdir()
    (source / 'nr_resource_recovery.h').write_bytes(contents)
    frozen_fixture = output / 'resource_recovery.cpp'
    frozen_fixture.write_bytes(fixture)
    compiler = args.compiler.resolve(strict=True)
    commands: list[list[str]] = []

    def compile_and_run(include: Path, label: str, extra: tuple[str, ...] = ()) -> subprocess.CompletedProcess:
        binary = output / (label + '.exe')
        command = [str(compiler), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                   '-I' + str(include), str(frozen_fixture), '-o', str(binary)]
        commands.append(command)
        built = subprocess.run(command, capture_output=True, text=True, encoding='utf-8',
                               errors='replace', timeout=90)
        (output / (label + '-compile.log')).write_text(built.stdout + built.stderr, encoding='utf-8')
        if built.returncode:
            print((built.stdout + built.stderr)[-12000:])
            raise RuntimeError(label + ' compilation failed')
        command = [str(binary), *extra]
        commands.append(command)
        result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8',
                                errors='replace', timeout=15)
        (output / (label + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        print(result.stdout.strip())
        return result

    positive = compile_and_run(source, 'policy')
    original = contents.decode('utf-8-sig')
    mutations = [
        ('single-frame', '--single-frame-oracle', 'SINGLE_FRAME_ORACLE',
         'if (r.correlated_window && r.descriptor_verified && r.writeback_verified)',
         'if (r.descriptor_verified && r.writeback_verified)'),
        ('recording-as-submit', '--submission-oracle', 'SUBMISSION_ORACLE',
         'Succeeded(f, Stage::NativeSrEvaluate) && Succeeded(f, Stage::Submitted);',
         'Succeeded(f, Stage::NativeSrEvaluate) && Succeeded(f, Stage::TransferRecorded);'),
        ('ignore-frozen-target', '--target-oracle', 'TARGET_ORACLE',
         'if (proof.resource != f->target.resource || proof.resource_generation != f->target.resource_generation ||\n'
         '            proof.subresource != f->target.subresource)',
         'if (false)'),
        ('erase-failed-latch', '--retirement-oracle', 'RETIREMENT_ORACLE',
         'for (auto& e : entries_) if (e.used && e.key == key && !e.failure.present) {',
         'for (auto& e : entries_) if (e.used && e.key == key) {'),
    ]
    negative_results = {}
    sha = lambda data: hashlib.sha256(data).hexdigest()
    for label, argument, oracle, anchor, replacement in mutations:
        if original.count(anchor) != 1:
            raise RuntimeError(label + ' mutation anchor changed; update its narrowly scoped inverse change.')
        mutated = original.replace(anchor, replacement, 1)
        folder = output / (label + '-source')
        folder.mkdir()
        (folder / 'nr_resource_recovery.h').write_text(mutated, encoding='utf-8')
        result = compile_and_run(folder, label, (argument,))
        caught = result.returncode == 41 and oracle in result.stdout
        negative_results[label] = {'caught': caught, 'exit': result.returncode,
                                   'source_sha256': sha(mutated.encode('utf-8')), 'output': result.stdout.strip()}
    passed = positive.returncode == 0 and all(item['caught'] for item in negative_results.values())
    summary = {
        'status': 'PASS' if passed else 'FAIL',
        'actual_policy_direct_calls': True, 'source_root': str(source_root),
        'source_sha256': sha(contents), 'fixture_sha256': sha(fixture),
        'compiler_sha256': sha(compiler.read_bytes()),
        'positive_exit': positive.returncode, 'positive': positive.stdout.strip(),
        'negative_controls': negative_results, 'commands': commands,
        'gpu': False, 'ngx': False, 'runtime_integration': False,
    }
    (output / 'result.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print(('PASS' if passed else 'FAIL') + ' bounded recovery policy and all false-success/unsafe-retirement controls; no runtime/GPU claim')
    raise SystemExit(0 if passed else 1)


if __name__ == '__main__':
    main()
