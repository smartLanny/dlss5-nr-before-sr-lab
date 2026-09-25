#!/usr/bin/env python3
"""Compile real submission functions against CPU COM seams, including inverse controls."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def extract(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists():
        raise SystemExit('Preserve existing evidence; choose a fresh output.')
    names = ('src/nr_before_sr.cpp', 'src/nr_runtime_timing.h',
             'src/nr_runtime_timing.inl', 'src/nr_timing_feedback.inl',
             'tests/submit_batch_cpu.cpp', 'scripts/validate-submit-batch.py')
    inputs = {name: (ROOT / name).read_bytes() for name in names}
    sha = lambda data: hashlib.sha256(data).hexdigest()
    out.mkdir(parents=True)
    compiler = args.compiler.resolve(strict=True)
    result = {'status': 'running', 'inputs': {name: sha(data) for name, data in inputs.items()},
              'compiler_sha256': sha(compiler.read_bytes()), 'gpu': False, 'game': False,
              'actual_functions': ['ProcessSubmittedCommandLists', 'CaptureSubmissionBatchLocked',
                                   'CompleteSubmissionBatchLocked', 'QuarantineUnprovenSubmissionLocked',
                                   'RememberLastSubmissionQueue', 'TrackGraphicsQueueLocked', 'FeedbackAppendTiming',
                                   'NrExternalSetTraining', 'NrExternalTrainSubmission'],
              'seams': ['COM list and queue callbacks', 'queue fence creation',
                        'context/slot completion query', 'source/boundary/capture observers'], 'runs': []}
    functions = ('LifecycleCommandList *FindLifecycleCommandListLocked(',
                 'bool ReadLifecycleToken(', 'bool ReadLastSubmissionQueueMarker(',
                 'template <typename CommandList>\nbool RememberLastSubmissionQueue(',
                 'GraphicsQueueRecord *FindGraphicsQueueRecordLocked(',
                 'GraphicsQueueRecord *TrackGraphicsQueueLocked(',
                 'template <typename T, std::size_t N>\nbool AppendUnique(',
                 'bool CaptureSubmissionBatchLocked(', 'void QuarantineUnprovenSubmissionLocked(',
                 'void CompleteSubmissionBatchLocked(', 'void ProcessSubmittedCommandLists(')
    controls = (
        ('production', None, None, 'SUBMIT_BATCH_CPU PASS'),
        ('old-qi-cost', 'if (direct_result == DXGI_ERROR_NOT_FOUND &&',
         'if (false && direct_result == DXGI_ERROR_NOT_FOUND &&', 'UNRELATED_QI_BUDGET_ORACLE'),
        ('skip-affinity', 'RememberLastSubmissionQueue(command_lists[i], queue_identity);',
         '// inverse: drop affinity', 'UNRELATED_QI_BUDGET_ORACLE'),
        ('skip-source-observer', 'nr_source_runtime::OnExecute(queue,count,command_lists,true);',
         '// inverse: drop independent observer', 'INDEPENDENT_OBSERVERS_ORACLE'),
        ('error-is-absence', 'if (direct_result == DXGI_ERROR_NOT_FOUND &&',
         'if (FAILED(direct_result) &&', 'UNKNOWN_FALLBACK_ORACLE'),
        ('skip-lifecycle-capture', '!may_have_lifecycle_work || !batch_has_lifecycle_token)',
         '!may_have_lifecycle_work || true)', 'RESET_REUSE_ORACLE'),
        ('reuse-stale-token', 'const bool has_token = ReadLifecycleToken(graphics, token);\n        CommandListQueueMarker queue_marker',
         'token = g_nr_submit_timing->batch.first_token; const bool has_token = token != 0;\n        CommandListQueueMarker queue_marker',
         'CAPTURE_REVALIDATES_TOKEN_ORACLE'),
    )
    try:
        original = inputs['src/nr_before_sr.cpp'].decode('utf-8-sig').replace('\r\n', '\n')
        for label, anchor, replacement, marker in controls:
            folder = out / label
            folder.mkdir()
            text = original
            if anchor:
                expected = 6 if label == 'skip-source-observer' else 1
                if text.count(anchor) != expected:
                    raise RuntimeError(f'Inverse anchor changed: {label}, found {text.count(anchor)}')
                text = text.replace(anchor, replacement)
            types = text[text.index('constexpr GUID kLifecycleCommandListGuid'):text.index('#include "nr_f8_gpu.inl"')]
            (folder / 'production_types.inl').write_text(types, encoding='utf-8')
            # Keep the actual inactive-provider fast path in the batch consumer.
            # Active exact-list training has its own external-provider fixture.
            training = text[text.index('SRWLOCK g_external_training_lock'):text.index('void ProcessSubmittedCommandLists(')]
            bodies = [extract(text, f) for f in functions[:-1]]
            bodies.extend((training, extract(text, functions[-1])))
            (folder / 'production_submit.inl').write_text('\n\n'.join(bodies), encoding='utf-8')
            for name in ('src/nr_runtime_timing.h', 'src/nr_runtime_timing.inl',
                         'src/nr_timing_feedback.inl', 'tests/submit_batch_cpu.cpp'):
                (folder / Path(name).name).write_bytes(inputs[name])
            binary = folder / 'submit-batch.exe'
            command = [str(compiler), '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                       '-I'+str(folder), str(folder/'submit_batch_cpu.cpp'), '-o', str(binary), '-lkernel32', '-ldxguid']
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=120)
            (folder/'compile.log').write_text(compiled.stdout+compiled.stderr, encoding='utf-8')
            row = {'name': label, 'command': command, 'compile_exit': compiled.returncode}
            result['runs'].append(row)
            if compiled.returncode:
                raise RuntimeError(compiled.stdout+compiled.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            (folder/'run.log').write_text(run.stdout+run.stderr, encoding='utf-8')
            row.update(exit=run.returncode, output=run.stdout+run.stderr, binary_sha256=sha(binary.read_bytes()))
            row['expected_result'] = run.returncode == (41 if anchor else 0) and marker in row['output']
            print(label+': '+row['output'].strip())
            if not row['expected_result']:
                raise RuntimeError('Unexpected CPU result: '+label)
        if any((ROOT/name).read_bytes() != data for name, data in inputs.items()):
            raise RuntimeError('Source changed during validation.')
        result['status'] = 'passed'
    finally:
        if result['status'] == 'running':
            result['status'] = 'failed'
        (out/'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
