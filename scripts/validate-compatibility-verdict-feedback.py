"""Exercise the actual verdict writer, FeedbackAppend and reserved report tail on the CPU."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
HIDDEN = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0


def between(text: str, start: str, end: str) -> str:
    if text.count(start) != 1 or text.count(end) != 1:
        raise SystemExit('Actual feedback source extraction anchor changed: ' + start)
    return text[text.index(start):text.index(end, text.index(start))]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    files = {'header': ROOT / 'src/nr_compatibility_verdict.h',
             'limits': ROOT / 'src/nr_feedback_limits.h',
             'writer': ROOT / 'src/nr_compatibility_verdict_feedback.inl',
             'feedback': ROOT / 'src/nr_feedback_capture.inl',
             'test': ROOT / 'tests/compatibility_verdict_feedback_test.cpp',
             'runner': Path(__file__)}
    captured = {name: path.read_bytes() for name, path in files.items()}
    hashes = {name: hashlib.sha256(data).hexdigest() for name, data in captured.items()}
    source = captured['feedback'].decode('utf-8-sig').replace('\r\n', '\n')
    fixture = '#include "nr_feedback_limits.h"\n'
    fixture += between(source, 'struct FeedbackSession {', 'FeedbackSession *g_feedback_session')
    fixture += between(source, 'void FeedbackAppend(FeedbackSession &s, const char *format, ...) {',
                       'const char *FeedbackStageBlockReason(')
    fixture += '\nvoid FinishFeedbackReport(FeedbackSession *s) {\n'
    fixture += between(source, '    const int summary_bytes = std::snprintf(s->report + s->report_used,',
                       '    wchar_t directory[1024] = {};\n    nr_bundle::Result result = {};')
    fixture += '}\n'

    def git(*values: str) -> str:
        return subprocess.run(['git', '-C', str(ROOT), *values], capture_output=True, text=True,
                              check=True, creationflags=HIDDEN).stdout.strip()

    identity = {'source_commit': git('rev-parse', 'HEAD'),
                'source_dirty': bool(git('status', '--porcelain', '--untracked-files=normal')),
                'tested_source_sha256': hashes,
                'extracted_feedback_fixture_sha256': hashlib.sha256(fixture.encode()).hexdigest(),
                'scope': 'actual Resolve and writer; production FeedbackSession/FeedbackAppend/final tail '
                         'with line endings normalized only; '
                         'host CPU only; no runtime sampling, GPU, provider or full F8 integration claim'}
    (args.output_dir / 'source-identity.json').write_text(json.dumps(identity, indent=2), encoding='utf-8')
    controls = {
        'production': ('', ''),
        'unknown-as-zero': ('known ? std::to_string(value) : "unknown"', 'known ? std::to_string(value) : "0"'),
        'hide-execution-without-storage': (
            'record.Number(rec, "nrSuccessStreak", recovery.nr_success_streak, window_known);',
            'record.Number(rec, "nrSuccessStreak", recovery.nr_success_streak, storage_proof_current);'),
        'hide-text-truncation': ('if (!complete) session.report_truncated = true;',
                                 'if (prefix_rejected) session.report_truncated = true;'),
        'partial-decision': ('FeedbackAppend(session, "%s", record.text.c_str());',
                             'for (std::size_t offset = 0; offset < record.text.size();) {\n'
                             '        const auto end = record.text.find(\'\\n\', offset) + 1;\n'
                             '        const auto line = record.text.substr(offset, end - offset);\n'
                             '        FeedbackAppend(session, "%s", line.c_str());\n'
                             '        offset = end;\n    }'),
    }
    oracles = {'unknown-as-zero': 'UNKNOWN_IS_NOT_ZERO',
               'hide-execution-without-storage': 'PARTIAL_STORAGE_COUNTERS_VISIBLE',
               'hide-text-truncation': 'TEXT_TRUNCATION_MARKS_REPORT',
               'partial-decision': 'CAPACITY_REJECTS_WHOLE_DECISION'}
    results = []
    for name, (before, after) in controls.items():
        output = args.output_dir / name
        output.mkdir()
        writer = captured['writer'].decode('utf-8-sig')
        if before:
            if writer.count(before) != 1:
                raise SystemExit('Negative control writer anchor changed: ' + name)
            writer = writer.replace(before, after)
        (output / 'nr_compatibility_verdict_feedback.inl').write_text(writer, encoding='utf-8')
        (output / 'nr_compatibility_verdict.h').write_bytes(captured['header'])
        (output / 'nr_feedback_limits.h').write_bytes(captured['limits'])
        (output / 'feedback_append_fixture.inl').write_text(fixture, encoding='utf-8')
        cpp = output / 'compatibility_verdict_feedback_test.cpp'
        cpp.write_bytes(captured['test'])
        exe = output / ('verdict-feedback.exe' if os.name == 'nt' else 'verdict-feedback')
        command = [args.compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror']
        if os.name == 'nt': command += ['-static']
        command += [str(cpp), '-o', str(exe)]
        built = subprocess.run(command, capture_output=True, text=True, creationflags=HIDDEN)
        (output / 'build.log').write_text(built.stdout + built.stderr, encoding='utf-8')
        if built.returncode:
            print(built.stdout + built.stderr)
            raise SystemExit(built.returncode)
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30, creationflags=HIDDEN)
        (output / 'result.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        print(result.stdout + result.stderr, end='')
        if name == 'production':
            if result.returncode: raise SystemExit(result.returncode)
        elif result.returncode != 1 or oracles[name] not in result.stderr:
            raise SystemExit('Negative control missed its exact oracle: ' + name)
        else:
            print('PASS negative control:', name)
        results.append({'variant': name, 'command': command, 'exit_code': result.returncode,
                        'expected_negative': name != 'production',
                        'compiled_writer_sha256': hashlib.sha256(writer.encode()).hexdigest(),
                        'binary_sha256': hashlib.sha256(exe.read_bytes()).hexdigest()})
    if any(path.read_bytes() != captured[name] for name, path in files.items()):
        raise SystemExit('Verdict feedback sources changed during validation')
    (args.output_dir / 'validation.json').write_text(
        json.dumps({'status': 'PASS', 'identity': identity, 'results': results}, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
