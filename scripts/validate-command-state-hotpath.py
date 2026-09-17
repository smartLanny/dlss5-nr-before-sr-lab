#!/usr/bin/env python3
"""Exact-source CPU regressions for D21 root stores and registry lookup cost.

The frozen lockperf1 source is a negative control, not a handwritten algorithm.
These tests exercise CPU/COM doubles, never a game or real D3D12 recording.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BASE = 'd8de528f4df23999c4f6711320a94ac522e0f44e'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--compiler', default=os.environ.get('CXX', 'c++'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--expect-unfixed', action='store_true', help='test-first: expect both current budgets to fail')
    args = parser.parse_args()
    root, out = args.root.resolve(), args.output.resolve()
    if out.exists():
        raise ValueError('Output already exists; retain prior evidence and choose a new directory')
    compiler = shutil.which(args.compiler)
    if not compiler:
        raise ValueError('Compiler unavailable: ' + args.compiler)
    spec = importlib.util.spec_from_file_location('lockperf', root / 'scripts/validate-command-state-lockperf.py')
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    extract = helper.extract
    source = (root / 'src/nr_command_state.inl').read_text(encoding='utf-8-sig')
    old = subprocess.run(['git', '-C', str(root), 'show', BASE + ':src/nr_command_state.inl'],
                         capture_output=True, text=True, check=True, timeout=30).stdout
    readers = (root / 'src/nr_descriptor_provenance_commands.inl').read_text(encoding='utf-8-sig')
    readers = extract(readers, 'std::uint64_t NrDescriptorBoundHeap(') + extract(readers, 'bool NrDescriptorBoundGpuHeapMatches(')

    def mutation(text):
        start, end = '// BEGIN COMMAND_STATE_MUTATION_SYNCHRONIZATION', '// END COMMAND_STATE_MUTATION_SYNCHRONIZATION'
        if text.count(start) != 1 or text.count(end) != 1:
            raise ValueError('Synchronization extraction boundary changed')
        sync = text[text.index(start):text.index(end) + len(end)] + '\n'
        body = extract(text, 'template <typename Mutation> void MutateNativeCommandState(')
        needle = 'for (auto &record : g_command_states)'
        if body.count(needle) != 1:
            raise ValueError('Registry walk instrumentation boundary changed')
        # Test-only counter: no shared atomic, logging, or timers in the addon.
        return sync + body.replace(needle, '++registry_walks;\n        ' + needle)

    def arguments(text):
        types = re.search(r'enum class CommandRootKind[^;]+;', text).group(0) + '\n'
        types += extract(text, 'struct CommandRootArgument {').rstrip() + ';\n'
        body = extract(text, 'template <typename T> void CommandStateAssign(')
        optional = 'void InvalidateCommandRootArgument('
        if optional in text:
            body += extract(text, optional)
        for name in ('CommandStateSetRoot', 'CommandStateSetHeaps', 'CommandStateSetValue', 'CommandStateSetConstants'):
            body += extract(text, 'void ' + name + '(')
        return types, body

    out.mkdir(parents=True)
    result = {'candidate': 'D21-lockperf2', 'baseline': BASE, 'native_srw': os.name == 'nt',
              'objects': 'CPU/COM test doubles', 'game_verified': False, 'warp_verified': False,
              'expect_unfixed': args.expect_unfixed,
              'source_sha256': hashlib.sha256(source.encode()).hexdigest(), 'runs': []}

    def run(cmd, name, expected=0):
        p = subprocess.run([str(x) for x in cmd], capture_output=True, text=True, timeout=120)
        (out / (name + '.txt')).write_text('$ ' + ' '.join(map(str, cmd)) + '\n' + p.stdout + p.stderr, encoding='utf-8')
        result['runs'].append({'name': name, 'exit': p.returncode, 'stdout': p.stdout.strip()})
        if p.returncode != expected:
            raise RuntimeError(name + ': expected ' + str(expected) + ', got ' + str(p.returncode) + '\n' + p.stdout + p.stderr)

    try:
        flags = ['-std=c++20', '-O2', '-g', '-pthread']
        if os.name == 'nt':
            flags += ['-static']
        if args.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        run([compiler, '--version'], 'compiler')
        with tempfile.TemporaryDirectory(prefix='nr-state-hotpath-') as tmp:
            tmp = Path(tmp)
            for name, text, expected in (('baseline', old, 3), ('candidate', source, 3 if args.expect_unfixed else 0)):
                td = tmp / name
                td.mkdir()
                (td / 'production_mutation.inl').write_text(mutation(text), encoding='utf-8')
                (td / 'production_readers.inl').write_text(readers, encoding='utf-8')
                types, body = arguments(text)
                (td / 'production_argument_types.inl').write_text(types, encoding='utf-8')
                (td / 'production_arguments.inl').write_text(body, encoding='utf-8')
                (td / 'baseline_arguments.inl').write_text(arguments(old)[1], encoding='utf-8')
                for test in ('cache', 'arguments'):
                    exe = td / (test + ('.exe' if os.name == 'nt' else ''))
                    run([compiler, *flags, '-I' + str(td), root / ('tests/command_state_hotpath_' + test + '.cpp'), '-o', exe], name + '-' + test + '-build')
                    if test == 'arguments':
                        run([exe], name + '-arguments-semantics')
                        run([exe, '--store-budget'], name + '-arguments-budget', expected)
                    else:
                        run([exe], name + '-cache-budget', expected)
        result['status'] = 'passed'
    except Exception as error:
        result['status'] = 'failed'
        result['error'] = str(error)
        raise
    finally:
        (out / 'validation.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        raise SystemExit(str(error))
