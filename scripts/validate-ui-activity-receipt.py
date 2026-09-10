#!/usr/bin/env python3
"""Freeze and test actual activity Window/read/display/F8 CPU boundaries."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def definition(source, marker):
    start = source.index(marker)
    opening = source.index('{', start)
    depth = 0
    quote = None
    escape = False
    line_comment = block_comment = False
    at = opening
    while at < len(source):
        ch = source[at]
        nxt = source[at + 1] if at + 1 < len(source) else ''
        if line_comment:
            if ch == '\n': line_comment = False
        elif block_comment:
            if ch == '*' and nxt == '/': block_comment = False; at += 1
        elif quote:
            if escape: escape = False
            elif ch == '\\': escape = True
            elif ch == quote: quote = None
        elif ch == '/' and nxt == '/': line_comment = True
        elif ch == '/' and nxt == '*': block_comment = True
        elif ch in ('"', "'"): quote = ch
        elif ch == '{': depth += 1
        elif ch == '}':
            depth -= 1
            if not depth: return source[start:at + 1]
        at += 1
    raise RuntimeError('Unclosed source definition: ' + marker)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source_root = args.source_root.resolve(strict=True)
    output = args.output.resolve()
    if output.exists(): raise SystemExit('Preserve existing evidence; choose a fresh output.')
    names = ('src/nr_ui_activity.h', 'src/nr_ui_activity_runtime.inl', 'src/nr_ui_activity_feedback.inl',
             'src/nr_feedback_text.h', 'src/nr_feedback_limits.h', 'src/nr_feedback_capture.inl',
             'src/nr_settings_ui.inl', 'src/nr_before_sr.cpp', 'tests/ui_activity_receipt_runtime.cpp.in',
             'tests/ui_activity_test.cpp')
    paths = {name: (ROOT if name.startswith('tests/') else source_root) / name for name in names}
    inputs = {name: path.read_bytes() for name, path in paths.items()}
    if any(paths[name].read_bytes() != value for name, value in inputs.items()): raise SystemExit('Inputs changed while freezing.')
    sha = lambda value: hashlib.sha256(value).hexdigest()
    manifest_path = source_root.parent / 'source-manifest.json'
    manifest_bytes = manifest_path.read_bytes() if manifest_path.is_file() else None
    manifest = json.loads(manifest_bytes) if manifest_bytes else {}
    if manifest.get('files') and any(manifest['files'].get('core/' + name) != sha(value)
                                   for name, value in inputs.items() if name.startswith('src/')):
        raise SystemExit('Source inputs disagree with their canonical manifest.')
    compiler = args.compiler.resolve(strict=True)
    output.mkdir(parents=True)
    if manifest_bytes: (output / 'input-source-manifest.json').write_bytes(manifest_bytes)
    result = {'status': 'running', 'source_root': str(source_root), 'source_commit': manifest.get('source_commit'),
              'source_manifest_sha256': sha(manifest_bytes) if manifest_bytes else None,
              'inputs': {name: sha(value) for name, value in inputs.items()},
              'compiler_sha256': sha(compiler.read_bytes()), 'gpu_executed': False, 'device_created': False,
              'scope': 'actual Window, extracted ReadUiActivity/DrawNrActivityStatus/FeedbackAppend and real production value journal/F8 writer; Win32 value locks and CPU threads; scripted clock/atomic/UI APIs', 'runs': []}
    controls = (
        ('production', None, None, None, 'UI_RECEIPT_CPU PASS'),
        ('missing-gap-reason', 'src/nr_ui_activity.h', 'receipt->reset_mask|=WindowGap;', 'receipt->reset_mask|=0u;', 'UI_GAP_REASON'),
        ('unstable-thread-identity', 'src/nr_ui_activity_runtime.inl', 'receipt.thread_cookie=thread.cookie;', 'receipt.thread_cookie=NrUiActivityThreadCookie();', 'UI_TLS_ID'),
        ('f8-observes-window', 'src/nr_ui_activity_runtime.inl',
         'void SnapshotNrUiActivity(nr_ui_activity::TraceSnapshot& output) {',
         'void SnapshotNrUiActivity(nr_ui_activity::TraceSnapshot& output) {\n    NrUiActivityThread().window.Observe(GetTickCount64(),{},{});', 'UI_F8_NO_OBSERVE'),
        ('wrong-display-receipt', 'src/nr_settings_ui.inl', 'displayed=Display::Observed;', 'displayed=Display::Checking;', 'UI_TEXT_STATE'),
        ('redact-ui-decimals', 'src/nr_feedback_text.h', 'if (typed_metadata && UiActivityMetadataNumber(line, i, end))', 'if (false)', 'UI_TYPED_PRIVACY'),
    )
    for label, target, anchor, replacement, marker in controls:
        folder = output / label
        frozen = {}
        for name, original in inputs.items():
            value = original.decode('utf-8-sig').replace('\r\n', '\n')
            if name == target:
                if value.count(anchor) != 1: raise SystemExit('Inverse anchor changed: ' + label)
                value = value.replace(anchor, replacement, 1)
            frozen[name] = value
            path = folder / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_text(value, encoding='utf-8')
        ui = frozen['src/nr_settings_ui.inl']; capture = frozen['src/nr_feedback_capture.inl']
        if 'DrawNrActivityStatus(runtime,current.config,current.generation,nr_ui_activity::Site::Top);' not in ui or \
           'DrawNrActivityStatus(runtime,next,current.generation,nr_ui_activity::Site::Feedback);' not in frozen['src/nr_before_sr.cpp']:
            raise SystemExit('Actual top/feedback sites do not call the tested production display boundary.')
        harness = frozen['tests/ui_activity_receipt_runtime.cpp.in']
        for token, text in (
            ('// @READ@', definition(ui, 'nr_ui_activity::Sample ReadUiActivity(')),
            ('// @DRAW@', definition(ui, 'void DrawNrActivityStatus(')),
            ('// @FEEDBACK_SESSION@', definition(capture, 'struct FeedbackSession {') + ';'),
            ('// @FEEDBACK_APPEND@', definition(capture, 'void FeedbackAppend(')),
        ): harness = harness.replace(token, text)
        generated = folder / 'ui-receipt.cpp'; generated.write_text(harness, encoding='utf-8')
        executable = folder / 'ui-receipt.exe'
        command = [str(compiler), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static', '-pthread',
                   '-I' + str(folder / 'src'), str(generated), '-o', str(executable)]
        compiled = subprocess.run(command, capture_output=True, text=True, timeout=120)
        (folder / 'compile.log').write_text(compiled.stdout + compiled.stderr, encoding='utf-8')
        row = {'name': label, 'command': command, 'compile_exit': compiled.returncode}; result['runs'].append(row)
        if compiled.returncode: print(compiled.stdout + compiled.stderr); break
        ran = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
        row.update(exit=ran.returncode, output=ran.stdout + ran.stderr, binary_sha256=sha(executable.read_bytes()))
        (folder / 'run.log').write_text(row['output'], encoding='utf-8')
        row['expected_result'] = ran.returncode == (41 if target else 0) and marker in row['output']
        print(label + ': ' + ('expected inverse assertion caught: ' + marker if target and row['expected_result'] else row['output'].strip()))
        if not target:
            baseline = folder / 'ui-original-policy.exe'
            command = [str(compiler), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                       '-I' + str(folder / 'src'), str(folder / 'tests/ui_activity_test.cpp'), '-o', str(baseline)]
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=60)
            (folder / 'original-policy-compile.log').write_text(compiled.stdout + compiled.stderr, encoding='utf-8')
            if compiled.returncode: row['original_policy_pass'] = False
            else:
                original = subprocess.run([str(baseline)], capture_output=True, text=True, timeout=15)
                row['original_policy_pass'] = original.returncode == 0 and 'UI activity:' in original.stdout
                (folder / 'original-policy.log').write_text(original.stdout + original.stderr, encoding='utf-8')
            row['expected_result'] = row['expected_result'] and row['original_policy_pass']
    result['status'] = 'pass' if len(result['runs']) == len(controls) and all(row.get('expected_result') for row in result['runs']) else 'failed'
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    raise SystemExit(0 if result['status'] == 'pass' else 1)


if __name__ == '__main__': main()
