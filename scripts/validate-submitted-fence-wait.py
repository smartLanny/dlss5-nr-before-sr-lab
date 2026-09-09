#!/usr/bin/env python3
"""Freeze the production event-wait/timing modules and run their Win32 CPU fixture."""
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
    if output.exists(): raise SystemExit('Preserve earlier evidence; choose a fresh output.')
    names = ('src/nr_runtime_timing.h', 'src/nr_runtime_timing.inl', 'src/nr_submitted_fence_wait.inl', 'tests/submitted_fence_wait.cpp')
    inputs = {name: (ROOT / name).read_bytes() for name in names}
    if any((ROOT / name).read_bytes() != data for name, data in inputs.items()): raise SystemExit('Inputs changed during snapshot.')
    compiler = args.compiler.resolve(strict=True)
    output.mkdir(parents=True)
    def sha(data): return hashlib.sha256(data).hexdigest()
    result = {'schema': 1, 'status': 'running', 'inputs': {name: sha(data) for name, data in inputs.items()},
              'compiler_sha256': sha(compiler.read_bytes()), 'gpu_executed': False, 'device_created': False,
              'nvidia_nr': False, 'native_win32_events': True, 'cpu_fence_callback_fixture': True, 'runs': []}
    controls = (
        ('production', None, None, None, None),
        ('ignore-epoch', 'src/nr_runtime_timing.inl',
         'if (record.identity.epoch != g_nr_timing.epoch) {', 'if (false) {', 'TIMING_EPOCH_ORACLE'),
        ('poll-instead-of-event', 'src/nr_submitted_fence_wait.inl',
         'use_event = ArmNrSubmittedFenceEvent(fence, target, event, result);',
         'use_event = ArmNrSubmittedFenceEvent(fence, target, event, result); use_event = false;', 'EVENT_WAIT_ORACLE'),
        ('event-is-not-completion', 'src/nr_submitted_fence_wait.inl',
         'result.completed = !result.removed && value >= target;',
         'result.completed = !result.removed && (value >= target || event.event != nullptr);', 'EXACT_FENCE_ORACLE'),
        ('pending-is-not-releasable', 'src/nr_submitted_fence_wait.inl',
         'if (event.registered_fence) return false;', 'if (event.registered_fence) return true;', 'PENDING_EVENT_ORACLE'),
    )
    for label, source, anchor, replacement, marker in controls:
        folder = output / label
        for name, original in inputs.items():
            data = original
            if name == source:
                text = data.decode('utf-8-sig')
                if text.count(anchor) != 1: raise SystemExit('Private inverse control anchor changed: ' + label)
                data = text.replace(anchor, replacement, 1).encode('utf-8')
            path = folder / name
            path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
        binary = folder / 'submitted-fence-cpu.exe'
        command = [str(compiler), '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                   '-I' + str(folder / 'src'), str(folder / 'tests/submitted_fence_wait.cpp'), '-o', str(binary), '-lkernel32']
        compiled = subprocess.run(command, capture_output=True, text=True, timeout=120)
        (folder / 'compile.log').write_text(compiled.stdout + compiled.stderr, encoding='utf-8')
        row = {'name': label, 'command': command, 'compile_exit': compiled.returncode}
        result['runs'].append(row)
        if compiled.returncode:
            print(compiled.stdout + compiled.stderr); result['status'] = 'compile-failed'; break
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
        (folder / 'run.log').write_text(run.stdout + run.stderr, encoding='utf-8')
        row.update(exit=run.returncode, output=run.stdout, binary_sha256=sha(binary.read_bytes()))
        row['expected_result'] = run.returncode == 0 and 'SUBMITTED_FENCE_CPU PASS' in run.stdout if source is None else run.returncode == 41 and marker in run.stdout
        print(label + ': ' + run.stdout.strip())
    passed = len(result['runs']) == len(controls) and all(row.get('expected_result') for row in result['runs'])
    result['status'] = 'pass' if passed else 'failed'
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    raise SystemExit(0 if passed else 1)

if __name__ == '__main__': main()
