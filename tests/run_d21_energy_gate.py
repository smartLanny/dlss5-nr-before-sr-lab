"""Compile policy and real platform-adapter/route CPU tests; never deploy a Core."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    runtime = (ROOT / 'src/nr_source_frame_runtime.inl').read_text(encoding='utf-8')
    begin = '// BEGIN ENERGY GATE PLATFORM ADAPTER\n'
    end = '// END ENERGY GATE PLATFORM ADAPTER'
    assert runtime.count(begin) == runtime.count(end) == 1
    # Verbatim production adapter, with only OS/source/config services supplied
    # by the host. Do not maintain a copied version of scheduling behavior.
    adapter = runtime.split(begin, 1)[1].split(end, 1)[0]
    (args.output / 'energy-runtime-under-test.inl').write_text(adapter, encoding='utf-8')
    env = os.environ.copy()
    env['PATH'] = str(args.compiler.parent) + os.pathsep + env.get('PATH', '')
    results = []
    cases = ['d21_energy_gate_test.cpp', 'd21_energy_gate_runtime_test.cpp',
             'nr_processing_runtime_test.cpp', 'd16_processing_runtime_recovery_test.cpp']
    for case in cases:
        executable = args.output / (Path(case).stem + ('.exe' if os.name == 'nt' else ''))
        command = [str(args.compiler), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                   '-I' + str(ROOT / 'src'), '-I' + str(args.output),
                   str(ROOT / 'tests' / case), '-o', str(executable)]
        build = subprocess.run(command, text=True, capture_output=True, env=env)
        (args.output / (case + '.build.log')).write_text(build.stdout + build.stderr, encoding='utf-8')
        if build.returncode:
            raise RuntimeError(case + ' compilation failed:\n' + build.stdout + build.stderr)
        run = subprocess.run([str(executable)], text=True, capture_output=True, env=env)
        (args.output / (case + '.run.log')).write_text(run.stdout + run.stderr, encoding='utf-8')
        if run.returncode:
            raise RuntimeError(case + ' failed:\n' + run.stdout + run.stderr)
        results.append({'test': case, 'result': 'passed', 'output': run.stdout.strip()})
        print(run.stdout.strip())
    sources = ['src/nr_energy_gate.h', 'src/nr_processing_runtime.inl',
               'src/nr_source_frame_runtime.inl']
    report = {'scope': 'CPU policy and host adapter only; no GPU/game validation',
              'tests': results,
              'sha256': {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sources}}
    (args.output / 'validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
