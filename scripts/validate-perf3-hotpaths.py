#!/usr/bin/env python3
"""Fixed-source Lock2/Perf3 CPU regression and work-budget tests, not game FPS."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = '63efc2bd5f55a4e9035b8b454dec5a8bb598f5d1'
HEADERS = ('nr_descriptor_provenance.h', 'nr_source_frame.hpp')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', required=True)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--expect-unfixed', action='store_true')
    p.add_argument('--sanitize', action='store_true')
    args = p.parse_args()
    out = args.output.resolve()
    if out.exists():
        raise ValueError('Preserve previous evidence; choose a fresh output')
    out.mkdir(parents=True)
    result = {'baseline': BASE, 'cpu_only': True, 'game_verified': False,
              'source_sha256': {}, 'runs': [], 'status': 'running'}

    def run(cmd, name, expected=0, timeout=120):
        r = subprocess.run(list(map(str, cmd)), cwd=ROOT, capture_output=True,
                           text=True, encoding='utf-8', errors='replace', timeout=timeout)
        (out / (name + '.log')).write_text('$ ' + ' '.join(map(str, cmd)) + '\n' + r.stdout + r.stderr, encoding='utf-8')
        result['runs'].append({'name': name, 'exit': r.returncode, 'stdout': r.stdout.strip()})
        print(name, 'exit', r.returncode, r.stdout.strip(), flush=True)
        if r.returncode != expected:
            raise RuntimeError(name + ': ' + r.stderr[-4000:])

    def count_scans(text):
        # Count only full-table comparisons inside BatchFor; never instrument
        # timing binaries or add a counter/clock to the shipped addon.
        a = text.index('    Batch* BatchFor(')
        b = text.index('    Value* ValueFor(', a)
        body = text[a:b]
        needle = 'if(b.recording==r)'
        if body.count(needle) != 1:
            raise ValueError('BatchFor full-scan boundary changed')
        body = body.replace(needle, 'if((++perf3_batch_scan_visits,b.recording==r))')
        text = text[:a] + body + text[b:]
        return text.replace('namespace nr_source_frame {',
                            'namespace nr_source_frame {\ninline std::uint64_t perf3_batch_scan_visits=0;', 1)

    try:
        run([args.compiler, '--version'], 'compiler')
        current = {n: (ROOT / 'src' / n).read_text(encoding='utf-8-sig') for n in HEADERS}
        old = {n: subprocess.check_output(['git', '-C', str(ROOT), 'show', BASE + ':src/' + n], text=True, encoding='utf-8') for n in HEADERS}
        result['source_sha256'] = {n: hashlib.sha256((ROOT / 'src' / n).read_bytes()).hexdigest() for n in HEADERS}
        flags = ['-std=c++20', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-pthread']
        if os.name == 'nt':
            flags += ['-static']
        if args.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        for name, source, instrument in (('old-budget', old, True), ('candidate-budget', current, True), ('timing', current, False)):
            folder = out / name
            folder.mkdir()
            # Explicit production dependency for the current normalizer.
            (folder / "nr_descriptor_scratch.h").write_bytes((ROOT / "src/nr_descriptor_scratch.h").read_bytes())
            for n, text in source.items():
                (folder / n).write_text(count_scans(text) if instrument and n == HEADERS[1] else text, encoding='utf-8')
            (folder / 'perf3_reference_descriptor.h').write_text(old[HEADERS[0]].replace('namespace nr_descriptor {', 'namespace perf3_reference_descriptor {'), encoding='utf-8')
            (folder / 'perf3_reference_source.h').write_text(old[HEADERS[1]].replace('namespace nr_source_frame {', 'namespace perf3_reference_source {'), encoding='utf-8')
            binary = folder / ('hotpaths.exe' if os.name == 'nt' else 'hotpaths')
            run([args.compiler, *flags, '-DPERF3_COUNT_SCANS=' + str(int(instrument)), '-I' + str(folder), ROOT / 'tests/perf3_hotpaths.cpp', '-o', binary], name + '-build')
            if instrument:
                expected = 3 if name == 'old-budget' or args.expect_unfixed else 0
                for test in ('descriptor', 'lineage'):
                    run([binary, '--' + test + '-budget'], name + '-' + test, expected)
                run([binary], name + '-semantics')
            elif not args.sanitize:
                for repeat in range(3):
                    run([binary, '--bench'], 'timing-' + str(repeat))
        # Existing differential matrix uses its actual assertions and threads;
        # redirect only the reference namespace to this precise Lock2 revision.
        folder = out / 'legacy-differential'
        folder.mkdir()
        (folder / 'r2_descriptor_generated.h').write_text(old[HEADERS[0]].replace('namespace nr_descriptor {', 'namespace r2_descriptor {'), encoding='utf-8')
        binary = folder / ('test.exe' if os.name == 'nt' else 'test')
        run([args.compiler, *flags, '-I' + str(ROOT / 'src'), '-I' + str(folder), ROOT / 'tests/descriptor_r3_registry.cpp', '-o', binary], 'legacy-differential-build')
        run([binary], 'legacy-differential')
        result['status'] = 'passed'
    except Exception as error:
        result['status'] = 'failed'
        result['error'] = str(error)
        raise
    finally:
        (out / 'validation.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
