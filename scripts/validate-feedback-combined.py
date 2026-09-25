"""Run every production F8 evidence writer through the real TXT/ZIP collectors.

Snapshot records are explicit CPU fixture inputs using the production layouts
and bounds. No runtime/provider/GPU success is inferred from these fixtures.
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
import zipfile

ROOT = Path(__file__).resolve().parents[1]
HIDDEN = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--old-capacity-negative', action='store_true')
    parser.add_argument('--omit-end-verdict-negative', action='store_true')
    parser.add_argument('--ascii-metadata-negative', action='store_true')
    parser.add_argument('--disable-privacy-negative', action='store_true')
    parser.add_argument('--unknown-store-negative', action='store_true')
    args = parser.parse_args()
    if sum((args.old_capacity_negative, args.omit_end_verdict_negative,
            args.ascii_metadata_negative, args.disable_privacy_negative, args.unknown_store_negative)) > 1:
        parser.error('Choose one independent negative control per run.')
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    core = load_module('combined_core_fixture', ROOT / 'scripts/validate-core-feedback.py')
    lexical = load_module('combined_feedback_extractor', ROOT / 'scripts/validate-feedback-capture.py')
    required = [
        'src/nr_before_sr.cpp', 'src/nr_feedback_capture.inl', 'src/nr_feedback_core.inl',
        'src/nr_feedback_limits.h', 'src/nr_compatibility_verdict.h',
        'src/nr_compatibility_verdict_feedback.inl', 'src/nr_command_state.inl',
        'src/nr_command_creation_observer.inl', 'src/nr_native_format_runtime.inl',
        'src/nr_exposure_semantic_evidence.inl', 'src/interop/nr_exposure_access_d3d12.inl',
        'src/nr_runtime_load_evidence.inl', 'src/nr_post_boundary_runtime.inl',
        'src/nr_ngx_lifecycle.inl', 'scripts/validate-core-feedback.py',
        'scripts/validate-feedback-capture.py', 'scripts/validate-feedback-combined.py',
        'tests/feedback_combined_test.inl',
    ]
    # Snapshot all transitive local headers/inline modules, not only the direct
    # extractor inputs. Compilation never reads a concurrently edited src file.
    required += [p.relative_to(ROOT).as_posix() for p in (ROOT / 'src').rglob('*')
                 if p.is_file() and p.suffix in ('.h', '.hpp', '.inl')]
    captured = {name: (ROOT / name).read_bytes() for name in sorted(set(required))}
    snapshot = output / 'source'
    for name, data in captured.items():
        path = snapshot / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    text = {name: data.decode('utf-8-sig') for name, data in captured.items()}
    cpp = text['src/nr_before_sr.cpp']
    feedback = text['src/nr_feedback_capture.inl']
    command = text['src/nr_command_state.inl']
    native_format = text['src/nr_native_format_runtime.inl']
    exposure = text['src/nr_exposure_semantic_evidence.inl']
    access = text['src/interop/nr_exposure_access_d3d12.inl']
    loader = text['src/nr_runtime_load_evidence.inl']
    lifecycle = text['src/nr_ngx_lifecycle.inl']
    boundary = text['src/nr_post_boundary_runtime.inl']
    definition = core.definition
    function = lexical.extract_function
    core.ROOT = snapshot
    values = core.production_values()
    records = '''
#include <dxgiformat.h>
#include "nr_native_format.h"
#include "nr_post_boundary.h"
#include "nr_resource_recovery.h"
#include "nr_probe_policy.h"
#include "nr_store_comparison.h"
#define NR_HAS_COMMAND_CREATION_EVIDENCE 1
#define NR_HAS_NATIVE_FORMAT_OBSERVER 1
#define NR_HAS_EXPOSURE_SEMANTIC_EVIDENCE 1
#define NR_HAS_RUNTIME_LOAD_EVIDENCE 1
#define NR_HAS_POST_BOUNDARY_TRACE 1
#define NR_HAS_NGX_LIFECYCLE_EVIDENCE 1
#define NR_HAS_RESOURCE_RECOVERY 1
namespace nr_recovery = nr_resource_recovery;
using NgxResult = unsigned int;
'''
    records += definition(loader, 'struct RuntimeLoadEvidence', True) + '\n'
    records += function(loader, 'const char *RuntimeLoadClassName(') + '\n'
    records += 'SRWLOCK g_runtime_load_evidence_lock=SRWLOCK_INIT;\nRuntimeLoadEvidence g_runtime_load_evidence;\n'
    records += function(loader, 'RuntimeLoadEvidence SnapshotRuntimeLoadEvidence(') + '\n'
    records += definition(native_format, 'struct NrFormatDiagnostic {', True) + '\n'
    records += definition(native_format, 'struct NrFormatDiagnostics {', True) + '\n'
    records += 'SRWLOCK g_nr_format_lock=SRWLOCK_INIT;\nNrFormatDiagnostics g_nr_format_diagnostics;\n'
    records += function(native_format, 'NrFormatDiagnostics SnapshotNrFormatDiagnostics(') + '\n'
    enum_start = access.index('enum class NrExposureAccessReason ')
    records += access[enum_start:access.index(';', enum_start) + 1] + '\n'
    records += definition(access, 'struct NrExposureAccess {', True) + '\n'
    records += definition(exposure, 'struct NrExposureSemanticEvidence {', True) + '\n'
    records += 'SRWLOCK g_exposure_semantic_lock=SRWLOCK_INIT;\nNrExposureSemanticEvidence g_exposure_semantic_evidence;\n'
    records += function(exposure, 'NrExposureSemanticEvidence SnapshotNrExposureSemantic(') + '\n'
    records += definition(cpp, 'struct NrPostStoreEvidence {', True) + '\n'
    records += 'SRWLOCK g_diagnostic_snapshot_lock=SRWLOCK_INIT;\nNrPostStoreEvidence g_post_store_evidence;\n'
    records += function(cpp, 'NrPostStoreEvidence SnapshotPostStoreEvidence(') + '\n'
    records += 'NrPostStoreEvidence g_pre_store_evidence;\n'
    records += function(cpp, 'NrPostStoreEvidence SnapshotPreStoreEvidence(') + '\n'
    recovery = text['src/nr_resource_recovery_runtime.inl']
    records += definition(recovery, 'struct NrRecoverySnapshot {', True) + '\n'
    records += definition(recovery, 'struct NrRecoveryDetailSnapshot {', True) + '\n'
    records += '''
// Explicit detail-snapshot seam: serialization at maximum field width is not
// an assertion that the recovery ledger observed those events on the GPU.
NrRecoveryDetailSnapshot fixture_recovery_details;
unsigned fixture_recovery_detail_snapshots = 0;
NrRecoveryDetailSnapshot SnapshotNrRecoveryDetails() {
    ++fixture_recovery_detail_snapshots;
    return fixture_recovery_details;
}
'''
    records += definition(lifecycle, 'struct NgxLifecycleEvent {', True) + '\n'
    records += definition(lifecycle, 'struct NgxLifecycleEvidence {', True) + '\n'
    records += 'SRWLOCK g_feature_lock=SRWLOCK_INIT;\nNgxLifecycleEvidence g_ngx_lifecycle_evidence;\n'
    records += function(lifecycle, 'NgxLifecycleEvidence SnapshotNgxLifecycleEvidence(') + '\n'
    history_start = command.index('constexpr unsigned kCommandHistoryLimit = ')
    history_end = command.index('void RememberCommandEvidence(', history_start)
    records += command[history_start:history_end] + '\n'
    records += boundary[boundary.index('SRWLOCK g_post_boundary_lock='):boundary.index('// Implemented beside')] + '\n'
    records += 'std::atomic<bool> g_enabled_fast{true};\n'
    for signature in ('nr_probe_policy::Snapshot PostBoundaryProbePolicy(', 'void ClosePostBoundaryLocked(',
                      'bool PostBoundaryWindowCurrentLocked(', 'nr_post_boundary::Window SnapshotPostBoundary(',
                      'nr_post_boundary::Selection SelectPostBoundary('):
        records += function(boundary, signature) + '\n'
    atoms = ('g_pipeline_stats_epoch', 'g_pipeline_stats_generation', 'g_pipeline_stats_mode',
             'g_pipeline_feature18_success', 'g_pipeline_transfer_submitted',
             'g_queue_affinity_admitted_frames', 'g_queue_affinity_unknown_bypass_frames',
             'g_queue_affinity_prediction_bypass_frames', 'g_queue_affinity_actual_mismatches',
             'g_queue_affinity_marker_failures', 'g_queue_retired_admitted_frames',
             'g_queue_retirement_waits', 'g_queue_retirement_wait_completed',
             'g_queue_retirement_wait_timeouts', 'g_queue_retirement_wait_ms',
             'g_queue_retirement_wait_max_ms')
    for name in atoms:
        matches = re.findall(r'^std::atomic<[^\n]+\b' + name + r'\{[^\n]+', cpp, re.MULTILINE)
        if len(matches) != 1: raise SystemExit('Atomic declaration changed: ' + name)
        records += matches[0] + '\n'
    for source, prefix in ((lifecycle, 'g_external_nr_'),
                           (text['src/nr_command_creation_observer.inl'], 'g_command_')):
        records += '\n'.join(line for line in source.splitlines()
                             if line.startswith('std::atomic<') and prefix in line) + '\n'
    values['COMBINED_EVIDENCE'] = records
    values['ACTIVITY'] += '\n' + function(feedback, 'void FeedbackAppendCommandHistory(')
    if args.omit_end_verdict_negative:
        before = '    FeedbackAppendCompatibilityVerdict(s, prefix, decision);'
        if values['ACTIVITY'].count(before) != 1: raise SystemExit('End-verdict control anchor changed')
        values['ACTIVITY'] = values['ACTIVITY'].replace(before,
            '    if (std::strcmp(prefix, "end") != 0) FeedbackAppendCompatibilityVerdict(s, prefix, decision);')
    if args.unknown_store_negative:
        before = 'value(sample.matches_expected,available && sample.comparison_known)'
        if values['ACTIVITY'].count(before) != 1: raise SystemExit('Unknown store control anchor changed')
        values['ACTIVITY'] = values['ACTIVITY'].replace(before, 'value(sample.matches_expected,available)')
    harness = core.HARNESS[:core.HARNESS.index('int main(int argc,char **argv) {')]
    for key, value in values.items():
        harness = harness.replace('\n' + key + '\n', '\n' + value + '\n')
    # validate-core-feedback.py exposes only the small command-state seam as a
    # named placeholder.  The combined F8 activity also reads the production
    # creation counters, command history, and pipeline-stat atomics assembled
    # above.  Inject that captured production declaration block at the same
    # global-declaration boundary instead of manufacturing test-only stubs.
    combined_anchor = '\nstd::atomic<unsigned long long> g_nr_successful_evaluations'
    if harness.count(combined_anchor) != 1:
        raise SystemExit('Combined evidence declaration anchor changed')
    harness = harness.replace(combined_anchor, '\n' + records + combined_anchor)
    before = '"TEST GPU (no NGX)"'
    if harness.count(before) != 1: raise SystemExit('GPU description privacy fixture anchor changed')
    harness = harness.replace(before, r'"TEST GPU C:\\Users\\Private Tester\\Game\\gpu.dll | adapter=fixture"')
    harness += text['tests/feedback_combined_test.inl']
    if args.old_capacity_negative:
        path = snapshot / 'src/nr_feedback_limits.h'
        original = path.read_text(encoding='utf-8')
        before = 'kMetadataBytes = 192u * 1024u'
        if original.count(before) != 1: raise SystemExit('Capacity control anchor changed')
        # Lower both actual physical buffers while preserving the production
        # reserve relationships so this is a runtime capacity oracle.
        original = original.replace(before, 'kMetadataBytes = 32u * 1024u')
        if original.count('kMandatoryTailBytes = 112u * 1024u') != 1:
            raise SystemExit('Tail reserve control anchor changed')
        original = original.replace('kMandatoryTailBytes = 112u * 1024u',
                                    'kMandatoryTailBytes = 16u * 1024u')
        path.write_text(original, encoding='utf-8')
    if args.ascii_metadata_negative or args.disable_privacy_negative:
        path = snapshot / 'src/nr_feedback_text.h'
        original = path.read_text(encoding='utf-8')
        before = ('inline std::string SanitizeMetadata(std::string_view input) {' if args.ascii_metadata_negative else
                  'inline std::string RedactSensitiveLine(std::string line, bool typed_metadata = false) {')
        if original.count(before) != 1: raise SystemExit('Metadata privacy control anchor changed')
        original = original.replace(before, before + ('\n    return Sanitize(input);' if args.ascii_metadata_negative else
                                                      '\n    return line;'))
        path.write_text(original, encoding='utf-8')
    generated = output / 'combined.cpp'
    generated.write_text(harness, encoding='utf-8')
    exe = output / 'feedback-combined.exe'
    flags = ['-DNR_COMBINED_OLD_CAPACITY'] if args.old_capacity_negative else []
    if args.omit_end_verdict_negative: flags += ['-DNR_COMBINED_OMIT_END_VERDICT']
    command_line = [args.compiler, '-std=c++20', '-O1', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', '-static', *flags,
                    '-I', str(snapshot / 'src'), str(generated), '-o', str(exe), '-ldxguid']
    build = subprocess.run(command_line, capture_output=True, text=True, creationflags=HIDDEN)
    (output / 'build.log').write_text(build.stdout + build.stderr, encoding='utf-8')
    if build.returncode:
        print(build.stdout + build.stderr)
        raise SystemExit(build.returncode)
    fixtures = output / 'fixtures'
    result = subprocess.run([str(exe), str(fixtures)], capture_output=True, text=True,
                            encoding='utf-8', errors='replace', timeout=30, creationflags=HIDDEN)
    (output / 'native.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    print(result.stdout + result.stderr, end='')
    negative = (args.old_capacity_negative or args.omit_end_verdict_negative or
                args.ascii_metadata_negative or args.disable_privacy_negative or args.unknown_store_negative)
    oracle = ('COMBINED_CAPACITY_TOO_SMALL' if args.old_capacity_negative else
              'COMBINED_END_VERDICT_REQUIRED' if args.omit_end_verdict_negative else
              'METADATA_UTF8_REQUIRED' if args.ascii_metadata_negative else
              'METADATA_PRIVACY_REQUIRED' if args.disable_privacy_negative else 'LATEST_UNKNOWN_MUST_REMAIN_UNKNOWN')
    if negative:
        if result.returncode != 1 or oracle not in result.stderr:
            raise SystemExit('Combined negative control missed its exact oracle: ' + oracle)
    elif result.returncode:
        raise SystemExit(result.returncode)
    verification = {}
    if not negative:
        required_keys = (
            'start.decision.stage=unknown\n', 'end.decision.stage=unknown\n',
            'decision.evidenceComplete=0\n', 'decision.recoveryResult=unavailable\n',
            'decision.nextAction=needs-code\n', 'decision.firstFailureHistorical=1\n',
            'decision.scope.config=8\n', 'decision.firstFailureScope.config=7\n',
            'decision.serializationComplete=1\n', 'collection.metadataTruncated=0\n',
            'decision.evidenceCompleteMeaning=enough evidence for the decision, not final visual acceptance\n',
            'start.semantic.Exposure=', 'end.semantic.Exposure=',
            'end.semantic.ColorInput.candidate.5=', 'end.semantic.Output.candidate.5=',
            'commandHistory.11.blend=', 'end.post.boundary.native.5=',
            'commandHistory.11.shadingRate.observation=observed-native-setter\n',
            'commandHistory.11.shadingRate.combiners=4294967295,4294967295\n',
            'commandHistory.11.shadingRate.deviceSupport=not-queried\n',
            'end.post.boundary.following.11=', 'ngxLifecycle.firstFault.15=',
            'ngxLifecycle.recent.15=', 'artifact.identityStatus=', 'color.contract=',
            'end.recovery.receipt=scopeAvailable:1,scopeMatchesDecision:0,config:9,',
            'failureStage:actual-storage-read,failureResult:0x8007000E,failureKind:4,',
            'capacityUnknown:1,frameLost:1,receiptOverflow:1\n',
            'start.store.pre=available:1,source:frozen-recording-and-readback-sample,mode:2,',
            'end.store.post=available:1,source:frozen-recording-and-readback-sample,mode:1,',
            'actualNonfinite:1,referenceNonfinite:1,known:0,match:unknown,',
            'expected uses the frozen actual typed UAV storage conversion',
        )
        raw = (fixtures / 'combined-single.txt').read_bytes()
        single = raw.decode('utf-8-sig')
        paired_raw = (fixtures / 'combined-paired.txt').read_bytes()
        paired = paired_raw.decode('utf-8-sig')
        latest = (fixtures / 'latest-store.txt').read_bytes().decode('utf-8-sig')
        for key in ('end.store.pre=available:1,', 'mode:2,config:7,feature:41,frame:102,',
                    'end.store.post=available:1,', 'mode:1,config:6,feature:40,frame:103,',
                    'known:0,match:unknown,', 'proofFrame:501,proofAge:1,proofOrdinal:3,',
                    'decision.evidenceComplete=1\n', 'decision.nextAction=needs-visual-F7\n',
                    'decision.evidenceCompleteMeaning=enough evidence for the decision, not final visual acceptance\n'):
            if key not in latest: raise SystemExit('Latest/historical store fixture missing: ' + key)
        assert latest.endswith('END DLSS5 FEEDBACK TEXT\n')
        for label, content in (('single', single), ('paired', paired)):
            for key in required_keys:
                if key not in content: raise SystemExit(label + ' missing key: ' + key)
            assert '未知资源语义：等待当前载体证明' in content
            assert 'Private Tester' not in content and '[local-path]' in content
            assert '\ndecision.evidenceComplete=1\n' not in content
            assert content.count('\ndecision.evidenceComplete=0\n') == 1
            assert 'reported\\x0Adecision.evidenceComplete=1\\xC0\\xAF\\x00' in content
            assert content.endswith('END DLSS5 FEEDBACK TEXT\n')
            assert 'text.complete=1\n' in content
        with zipfile.ZipFile(fixtures / 'combined.zip') as archive:
            assert archive.testzip() is None
            metadata = archive.read('capture-info.txt').decode('utf-8-sig')
            manifest = json.loads(archive.read('manifest.json'))
            assert manifest['required_complete']
            assert manifest['metadata_text'] == metadata
            for key in required_keys:
                if key not in metadata: raise SystemExit('ZIP metadata missing key: ' + key)
            assert '未知资源语义：等待当前载体证明' in metadata
            assert 'Private Tester' not in metadata and '[local-path]' in metadata
            assert metadata.count('\ndecision.evidenceComplete=0\n') == 1
            assert '\ndecision.evidenceComplete=1\n' not in metadata
            verification = {'single_txt_bytes': len(raw), 'paired_txt_bytes': len(paired_raw),
                            'zip_metadata_bytes': len(metadata.encode()),
                            'single_txt_sha256': hashlib.sha256(raw).hexdigest(),
                            'paired_txt_sha256': hashlib.sha256(paired_raw).hexdigest(),
                            'zip_sha256': hashlib.sha256((fixtures / 'combined.zip').read_bytes()).hexdigest(),
                            'zip_manifest_matches_metadata': True, 'utf8_decoded': True,
                            'path_redacted': True, 'embedded_bytes_cannot_inject_keys': True,
                            'latest_unknown_and_historical_proof_kept_separate': True,
                            'required_keys': list(required_keys)}
    if any((ROOT / name).read_bytes() != data for name, data in captured.items()):
        raise SystemExit('Source changed during combined validation')
    def git(*args):
        return subprocess.run(['git', '-C', str(ROOT), *args], capture_output=True, text=True,
                              check=True, creationflags=HIDDEN).stdout.strip()
    manifest = {'status': 'PASS', 'source_commit': git('rev-parse', 'HEAD'),
                'source_dirty': bool(git('status', '--porcelain', '--untracked-files=normal')),
                'inputs_sha256': {name: hashlib.sha256(data).hexdigest() for name, data in captured.items()},
                'generated_sha256': hashlib.sha256(generated.read_bytes()).hexdigest(),
                'binary_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
                'compile_command': command_line, 'exit_code': result.returncode,
                'negative_control': oracle if negative else None, 'verification': verification,
                'scope': 'all actual F8 evidence writers plus actual core Present/Worker, single-TXT and ZIP paths; '
                         'production snapshot layouts and bounded arrays; CPU fixture input, not real Runtime/GPU evidence'}
    (output / 'validation.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print('PASS actual combined F8 writers, mandatory tail and UTF-8 TXT/ZIP' if not negative else
          'PASS precise combined negative control: ' + oracle)


if __name__ == '__main__':
    main()
