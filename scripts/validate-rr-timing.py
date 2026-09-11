#!/usr/bin/env python3
"""Run the actual RR dispatch and CPU timing modules through explicit API seams."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        raise SystemExit('Preserve earlier evidence; choose a fresh output.')
    names = ('src/nr_runtime_timing.h', 'src/nr_runtime_timing.inl',
             'src/interop/nr_rr_post_runtime.inl', 'src/interop/nr_post_compat.h',
             'tests/multi_api/rr_post_runtime.cpp')
    inputs = {name: (ROOT / name).read_bytes() for name in names}
    if any((ROOT / name).read_bytes() != data for name, data in inputs.items()):
        raise SystemExit('Inputs changed during snapshot.')
    compiler = args.compiler.resolve(strict=True)
    output.mkdir(parents=True)
    def sha(data): return hashlib.sha256(data).hexdigest()
    result = {'schema': 1, 'status': 'running', 'inputs': {name: sha(data) for name, data in inputs.items()},
              'compiler_sha256': sha(compiler.read_bytes()), 'gpu_executed': False,
              'device_created': False, 'nvidia_nr': False, 'native_win32_qpc': True,
              'actual_process_rr_post': True, 'feature1_recovery_scope': False,
              'scripted_api_model_boundaries': True, 'runs': []}
    controls = (
        ('production', None, None, 'RR_TIMING_CPU PASS'),
        ('no-timing-build', None, None, 'actual RR-post execution seam checks passed'),
        ('missing-rr-carrier',
         'timing_carrier.Begin(rr_frame,feature.generation,\n        g_config_generation.load(std::memory_order_acquire),1,timing_device_identity);',
         '(void)rr_frame; (void)timing_device_identity;', 'nr_timing::Stage::Carrier'),
        ('missing-native-return', 'NrTimingNativeResult(static_cast<std::int32_t>(result));',
         '// Private inverse: discard the actual native return receipt.', 'carrier.detail.native_calls==1'),
    )
    for label, anchor, replacement, marker in controls:
        folder = output / label
        for name, original in inputs.items():
            data = original
            if name == 'src/interop/nr_rr_post_runtime.inl' and anchor:
                text = data.decode('utf-8-sig').replace('\r\n', '\n')
                if text.count(anchor) != 1:
                    raise SystemExit('Private inverse control anchor changed: ' + label)
                data = text.replace(anchor, replacement, 1).encode('utf-8')
            path = folder / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        binary = folder / 'rr-timing-cpu.exe'
        command = [str(compiler), '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                   '-I' + str(folder), str(folder / 'tests/multi_api/rr_post_runtime.cpp'), '-o', str(binary)]
        if label != 'no-timing-build':
            command.append('-DNR_RR_TIMING_TEST=1')
        compiled = subprocess.run(command, capture_output=True, text=True, timeout=120)
        (folder / 'compile.log').write_text(compiled.stdout + compiled.stderr, encoding='utf-8')
        row = {'name': label, 'command': command, 'compile_exit': compiled.returncode}
        result['runs'].append(row)
        if compiled.returncode:
            print(compiled.stdout + compiled.stderr)
            break
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
        (folder / 'run.log').write_text(run.stdout + run.stderr, encoding='utf-8')
        row.update(exit=run.returncode, output=run.stdout + run.stderr, binary_sha256=sha(binary.read_bytes()))
        row['expected_result'] = run.returncode == (1 if anchor else 0) and marker in row['output']
        print(label + ': ' + row['output'].strip())
    result['status'] = 'pass' if len(result['runs']) == len(controls) and all(
        row.get('expected_result') for row in result['runs']) else 'failed'
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    raise SystemExit(0 if result['status'] == 'pass' else 1)


if __name__ == '__main__':
    main()
