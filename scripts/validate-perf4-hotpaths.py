#!/usr/bin/env python3
"""Perf3 negative controls vs actual Perf4 headers: multi-heap and full-table paths."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = 'c144d7cb8553f19972ab4b20a88b13dea1b69b94'
HEADERS = ('nr_descriptor_provenance.h', 'nr_source_frame.hpp')


def instrument(text: str, name: str) -> str:
    if name == HEADERS[0]:
        # Count exact outer-loop predicate evaluations, including the bounded
        # fallback. Production sources/benchmarks do not contain these counters.
        needle = 'if(r.type==heap.type&&r.first<=range.end&&range.base<=r.last)'
        if text.count(needle) != 1:
            raise ValueError('Unknown candidate fallback boundary changed')
        text = text.replace(needle, 'if((++perf4_heap_tests,r.type==heap.type&&r.first<=range.end&&range.base<=r.last))')
        text = text.replace('const auto& candidate=cpu_ranges[--upper];',
                            '++perf4_heap_tests;const auto& candidate=cpu_ranges[--upper];')
        return text.replace('namespace nr_descriptor {', 'namespace nr_descriptor {\ninline std::uint64_t perf4_heap_tests=0;', 1)
    needle = 'for(auto& value:values_)value.label=Unknown(value.label);'
    if text.count(needle) != 1:
        raise ValueError('Saturation invalidation boundary changed')
    text = text.replace(needle, 'for(auto& value:values_){++perf4_unknown_writes;value.label=Unknown(value.label);}')
    a = text.index('    Batch* BatchFor(')
    b = text.index('    Value* ValueFor(', a)
    body = text[a:b]
    for expr in ('if(b.recording==r)', 'if(!b.recording)'):
        if body.count(expr) != 1:
            raise ValueError('Batch scan boundary changed: ' + expr)
        body = body.replace(expr, 'if((++perf4_batch_tests,' + expr[3:-1] + '))')
    text = text[:a] + body + text[b:]
    return text.replace('namespace nr_source_frame {', 'namespace nr_source_frame {\ninline std::uint64_t perf4_unknown_writes=0,perf4_batch_tests=0;', 1)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', required=True)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--expect-unfixed', action='store_true')
    p.add_argument('--sanitize', action='store_true')
    args = p.parse_args()
    out = args.output.resolve()
    if out.exists():
        raise ValueError('Choose a fresh evidence directory')
    out.mkdir(parents=True)
    result = {'baseline': BASE, 'game_verified': False, 'cpu_only': True, 'runs': [], 'status': 'running'}

    def run(cmd, name, expected=0):
        r = subprocess.run(list(map(str, cmd)), cwd=ROOT, capture_output=True, text=True,
                           encoding='utf-8', errors='replace', timeout=180)
        (out / (name + '.log')).write_text('$ ' + ' '.join(map(str, cmd)) + '\n' + r.stdout + r.stderr, encoding='utf-8')
        result['runs'].append({'name': name, 'exit': r.returncode, 'stdout': r.stdout.strip()})
        print(name, r.returncode, r.stdout.strip(), flush=True)
        if r.returncode != expected:
            raise RuntimeError(name + ': ' + r.stderr[-6000:])

    try:
        run([args.compiler, '--version'], 'compiler')
        current = {n: (ROOT / 'src' / n).read_text(encoding='utf-8-sig') for n in HEADERS}
        old = {n: subprocess.check_output(['git', '-C', str(ROOT), 'show', BASE + ':src/' + n], text=True, encoding='utf-8') for n in HEADERS}
        result['source_sha256'] = {n: hashlib.sha256((ROOT / 'src' / n).read_bytes()).hexdigest() for n in HEADERS}
        flags = ['-std=c++20', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-pthread']
        if os.name == 'nt': flags += ['-static']
        if args.sanitize: flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        for name, headers, counting in (('old', old, True), ('candidate', current, True), ('timing', current, False)):
            folder = out / name
            folder.mkdir()
            for n, text in headers.items():
                (folder / n).write_text(instrument(text, n) if counting else text, encoding='utf-8')
            for source, target, namespace in ((HEADERS[0], 'perf3_reference_descriptor.h', 'descriptor'), (HEADERS[1], 'perf3_reference_source.h', 'source_frame')):
                replace = 'perf3_reference_descriptor' if namespace == 'descriptor' else 'perf3_reference_source'
                (folder / target).write_text(old[source].replace('namespace nr_' + namespace + ' {', 'namespace ' + replace + ' {'), encoding='utf-8')
            exe = folder / ('hotpaths.exe' if os.name == 'nt' else 'hotpaths')
            run([args.compiler, *flags, '-DPERF3_COUNT_SCANS=0', '-DPERF4_COUNT_WORK=' + str(int(counting)), '-I' + str(folder), ROOT / 'tests/perf4_hotpaths.cpp', '-o', exe], name + '-build')
            if counting:
                expected = 3 if name == 'old' or args.expect_unfixed else 0
                for test in ('outer', 'saturation'):
                    run([exe, '--' + test + '-budget'], name + '-' + test, expected)
                run([exe], name + '-semantics')
            elif not args.sanitize:
                for repeat in range(3): run([exe, '--bench'], 'timing-' + str(repeat))
        result['status'] = 'passed'
    except Exception as error:
        result.update(status='failed', error=str(error))
        raise
    finally:
        (out / 'validation.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__': main()
