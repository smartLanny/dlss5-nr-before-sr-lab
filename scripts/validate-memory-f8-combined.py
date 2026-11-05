#!/usr/bin/env python3
"""Run production memory/retirement F8 writers, TXT collector and ZIP encoder on CPU.

Linux services replace Windows file, lock and D3D interfaces only. Owner samples
and six bounded log summaries are synthetic inputs. Every report append, privacy
pass, TXT collector, SHA256, JSON manifest and ZIP framing function is production
source. This is not the complete Core TU or a Windows/GPU/game acceptance gate.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--text-parser', type=Path, default=ROOT / 'scripts/parse-core-text.py')
    parser.add_argument('--positive-only', action='store_true')
    parser.add_argument('--sanitizers', action='store_true')
    args = parser.parse_args()
    if os.name == 'nt':
        parser.error('Use validate-feedback-timing-full.py for the real Windows Core TU.')
    root = args.source_root.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    extractor = load('memory_f8_extract', ROOT / 'scripts/validate-feedback-capture.py')
    platform = load('memory_f8_platform', ROOT / 'scripts/validate-memory-observation.py')
    definition = extractor.extract_function
    sources = {}

    def source(name):
        if name not in sources:
            sources[name] = (root / name).read_text(encoding='utf-8-sig')
        return sources[name]

    capture = source('src/nr_feedback_capture.inl')
    runtime = source('src/nr_allocation_ledger_runtime.inl')
    core = source('src/nr_feedback_core.inl')
    report = source('src/nr_feedback_report.inl')
    bundle = source('src/nr_diagnostic_bundle.inl')
    # One atomic observation hook makes the lock-removal counterexample
    # deterministic without reading the concurrently written report buffer.
    append = definition(capture, 'void FeedbackAppend(')
    assert append.count('va_list args;') == 1
    append = append.replace('va_list args;', 'NoteAppend();\n    va_list args;')
    functions = [definition(bundle, marker) + (';' if marker == 'struct Sha256 {' else '')
                 for marker in ('bool CopyText(', 'struct Sha256 {', 'std::uint32_t UpdateCrc(',
                     'unsigned long long ArchiveLimit(', 'bool Write(', 'bool W16(', 'bool W32(',
                     'bool LocalHeader(', 'bool EndEntry(', 'bool Text(', 'bool JsonString(',
                     'bool Number(', 'bool Manifest(', 'bool FinishZip(')]
    replacements = {
        'PRODUCTION_SESSION': definition(capture, 'struct FeedbackSession {') + ';',
        'PRODUCTION_APPEND': append,
        'PRODUCTION_RETIREMENT_SNAPSHOT': definition(source('src/nr_retirement_evidence_runtime.inl'), 'void SnapshotNrRetirementEvidence('),
        'PRODUCTION_ALLOCATION_APPEND': definition(runtime, 'void NrAppendAllocationLedger('),
        'PRODUCTION_BUNDLE_TYPES': bundle[bundle.index('constexpr unsigned int kMaxFiles'):bundle.index('static std::atomic<bool> g_running')],
        'PRODUCTION_BUNDLE_FUNCTIONS': '\n'.join(functions),
        'PRODUCTION_OUTPUT': definition(report, 'struct Output {') + ';',
        'PRODUCTION_SAFE_LINES': definition(report, 'std::string SafeLines('),
        'PRODUCTION_CORE_SESSION': definition(core, 'struct CoreSession {') + ';',
        'PRODUCTION_COLLECT_SINGLE_TEXT': definition(core, 'nr_feedback_report::Output CollectSingleText('),
        'RETIREMENT_WIDTH_FIXTURE': definition(source('tests/feedback_timing_full.inl'), 'void SeedRetirementEvidence('),
        'PRODUCTION_CRC_INITIALIZATION': '[](nr_bundle::Job& job){' + definition(bundle, 'for (unsigned int i=0;i<256;++i)') + '}(*job);',
    }
    template = (ROOT / 'tests/memory_f8_combined.cpp.in').read_text(encoding='utf-8')
    for marker, value in replacements.items():
        assert template.count(marker) == 1, marker
        template = template.replace(marker, value)
    for header in ('nr_feedback_limits.h', 'nr_feedback_single_text.h', 'nr_feedback_text.h',
                   'nr_retirement_evidence.h', 'nr_retirement_feedback.inl', 'nr_allocation_ledger.h',
                   'nr_memory_observation.h', 'nr_model_memory_events.h', 'nr_process_memory.h'):
        source('src/' + header)
    controls = [
        ('positive', None, None, None, None),
        ('without-append-lock', 'runtime', 'AcquireSRWLockExclusive(&g_feedback_lock);', '', 'COMBINED_APPEND_LOCK'),
        ('missing-retirement-budget', 'nr_feedback_limits.h', 'kMandatoryTailBytes = 361u * 1024u',
         'kMandatoryTailBytes = 337u * 1024u', 'COMBINED_METADATA_CAPACITY'),
        ('old-metadata-capacity', 'nr_feedback_limits.h', 'kMetadataBytes = 640u * 1024u',
         'kMetadataBytes = 288u * 1024u', 'COMBINED_METADATA_CAPACITY'),
        ('old-text-capacity', 'nr_feedback_single_text.h', 'kTextLimit = 832u * 1024u',
         'kTextLimit = 480u * 1024u', 'COMBINED_TEXT_CAPACITY'),
        ('old-manifest-capacity', 'nr_feedback_limits.h', 'kManifestBytes = 6u * kMetadataBytes + 256u * 1024u',
         'kManifestBytes = 384u * 1024u', 'COMBINED_MANIFEST_CAPACITY'),
        ('omit-retirement', 'nr_retirement_feedback.inl', 'void FeedbackAppendRetirement(FeedbackSession& s,const char* prefix) {',
         'void FeedbackAppendRetirement(FeedbackSession& s,const char* prefix) { return;', 'COMBINED_RETIREMENT_REQUIRED'),
        ('old-limit-declaration', 'nr_feedback_single_text.h', '"text.limitBytes=" + std::to_string(kTextLimit) + "\\n"',
         '"text.limitBytes=491520\\n"', 'COMBINED_TEXT_LIMIT_DECLARATION'),
    ]
    receipt = {'schema': 'memory-retirement-f8-host-v1', 'passed': False,
        'scope': __doc__, 'gpu': False, 'ngx': False, 'windows': False, 'game': False,
        'address_and_undefined_sanitizers': args.sanitizers,
        'leak_detection_disabled': 'detect_leaks=0' in os.environ.get('ASAN_OPTIONS', ''),
        'source_head': subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=root, capture_output=True, text=True).stdout.strip(),
        'source_sha256': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in sources},
        'pieces_sha256': {name: hashlib.sha256(value.encode()).hexdigest() for name, value in replacements.items()},
        'host_services_sha256': hashlib.sha256((ROOT / 'tests/f8_memory_host_services.h').read_bytes()).hexdigest(),
        'fixture_sha256': hashlib.sha256((ROOT / 'tests/memory_f8_combined.cpp.in').read_bytes()).hexdigest(),
        'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), 'runs': []}

    def save():
        (output / 'validation.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')

    save()
    for name, target, before, after, failure in controls[:1] if args.positive_only else controls:
        case = output / name
        case.mkdir()
        platform.stage_host_headers(root, case)
        for header in ('nr_feedback_single_text.h', 'nr_retirement_evidence.h', 'nr_retirement_feedback.inl'):
            (case / header).write_bytes((root / 'src' / header).read_bytes())
        (case / 'f8_memory_host_services.h').write_bytes((ROOT / 'tests/f8_memory_host_services.h').read_bytes())
        body = template
        if target == 'runtime':
            actual = replacements['PRODUCTION_ALLOCATION_APPEND']
            assert actual.count(before) == 1
            changed = actual.replace(before, after).replace('ReleaseSRWLockExclusive(&g_feedback_lock);', '')
            body = body.replace(actual, changed)
        elif target:
            path = case / target
            text = path.read_text(encoding='utf-8')
            assert text.count(before) == 1, name
            text = text.replace(before, after)
            if name == 'old-metadata-capacity':
                text = text.replace('kMandatoryTailBytes = 361u * 1024u', 'kMandatoryTailBytes = 185u * 1024u')
            path.write_text(text, encoding='utf-8')
        generated = case / 'combined.cpp'
        generated.write_text(body, encoding='utf-8')
        exe = case / 'combined'
        flags = ['-std=c++20', '-O1', '-Wall', '-Wextra', '-Werror', '-fstack-usage', '-pthread']
        if args.sanitizers:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        command = [args.compiler, *flags, '-I' + str(case), str(generated), '-o', str(exe)]
        compiled = subprocess.run(command, capture_output=True, text=True, timeout=180)
        (case / 'compile.log').write_text(compiled.stdout + compiled.stderr, encoding='utf-8')
        item = {'name': name, 'compile_command': command, 'compile_exit': compiled.returncode,
                'generated_sha256': hashlib.sha256(generated.read_bytes()).hexdigest()}
        receipt['runs'].append(item)
        save()
        if compiled.returncode:
            raise RuntimeError(name + ' compile failed: ' + compiled.stderr[-12000:])
        result = subprocess.run([str(exe), str(case / 'artifacts')], capture_output=True, text=True, timeout=90)
        (case / 'run.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        item.update(exit=result.returncode, expected_exit=41 if failure else 0, expected_assertion=failure)
        save()
        if result.returncode != item['expected_exit'] or (failure and 'FAIL ' + failure not in result.stderr):
            raise RuntimeError(name + ' missed exact oracle: ' + result.stdout + result.stderr)
        print(name + ': ' + (result.stderr.strip() if failure else result.stdout.strip()), flush=True)
        if not failure:
            artifacts = case / 'artifacts'
            zip_parser = load('memory_f8_zip_parser', root / 'scripts/parse-core-feedback.py')
            parsed = zip_parser.inspect_core(artifacts / 'combined.zip')
            assert parsed['manifest_verified'] and parsed['required_complete']
            worst = json.loads((artifacts / 'worst-escaped-manifest.json').read_text(encoding='utf-8'))
            assert len(worst['metadata_text']) == 640 * 1024 - 1 and len(worst['entries']) == 64
            item['zip_parser_passed'] = True
            if args.text_parser.is_file():
                text_parser = load('memory_f8_text_parser', args.text_parser)
                parsed_text = text_parser.inspect_text(artifacts / 'combined.txt')
                assert parsed_text['text_verified'] and parsed_text['required_complete']
                assert len(parsed_text['summaries']) == 6
                item['text_parser_result'] = {key: parsed_text[key] for key in
                    ('input_kind', 'text_verified', 'required_complete', 'complete', 'sha256')}
                item['text_parser_result']['summaries'] = sorted(parsed_text['summaries'])
                item['text_parser_sha256'] = hashlib.sha256(args.text_parser.read_bytes()).hexdigest()
            else:
                item['text_parser_result'] = 'not installed in source checkout; generated TXT structural assertions ran in C++'
            metrics = re.search(r'COMBINED_F8 (.+)', result.stdout)
            assert metrics
            item['metrics'] = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', metrics[1])}
            save()
    receipt['passed'] = True
    save()
    print('PASS production memory+retirement F8 writers, TXT collector, SHA256/JSON/ZIP, host small-stack/lock and exact controls; CPU only')


if __name__ == '__main__':
    main()
