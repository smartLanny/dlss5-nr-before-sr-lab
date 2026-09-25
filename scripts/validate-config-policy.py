"""Run production config transactions with real Win32 INI IO and fresh processes.

Read-only fixtures use the Windows attribute. Partial-write faults are injected
only at WritePrivateProfileStringW; all successful writes and all reads use the
real OS APIs. No game, NR model, shader, or GPU is constructed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def definition(source, marker, semicolon=False):
    start = source.index(marker)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end + int(semicolon)]


def assemble(source):
    reload = definition(source, 'void ReloadConfig(')
    publish_start = reload.index('    const bool changed = !SameConfig(next, g_config);')
    publish_end = reload.index('    if (changed || force) {\n        Log(', publish_start)
    helpers = [definition(source, signature) for signature in (
        'float ReadIniFloat(', 'float FiniteOr(', 'int MigrateConfigMode(',
        'bool WriteIniInt(const wchar_t *key, int value) {',
        'bool WriteIniFloat(const wchar_t *key, float value) {',
        'bool IsNewWorkConfigFile(', 'void RecordWorkRequestStatus(',
        'void PersistWorkControl(', 'nr_work_config::Request ReadWorkRequest(',
        'float ReadConfiguredTransferStrength(')]
    parts = {
        'JITTER_ENUM': definition(source, 'enum class NrJitterMode', True),
        'CHAIN_CONFIG': definition(source, 'struct ChainConfig', True),
        'PROCESSING_RUNTIME': (ROOT / 'src/nr_processing_runtime.inl').read_text(encoding='utf-8-sig'),
        'HELPERS': '\n\n'.join(helpers),
        'POLICY_RUNTIME': (ROOT / 'src/nr_config_policy_runtime.inl').read_text(encoding='utf-8-sig'),
        'NORMALIZE': definition(source, 'void NormalizeConfig('),
        'SAME_CONFIG': definition(source, 'bool SameConfig('),
        'SAME_HISTORY': definition(source, 'bool SameHistoryConfig('),
        'ACTIVE_TRANSFER': definition(source, 'float ActiveTransferStrength('),
        'RELOAD_PREFIX': reload[:reload.index('    const bool shortfuse_loaded =')],
        'RELOAD_PUBLISH': reload[publish_start:publish_end],
        'SAVE_CONFIG': definition(source, 'bool SaveConfig('),
        'APPLY_CONFIG': definition(source, 'bool ApplyConfig('),
        'SNAPSHOT_CONFIG': definition(source, 'ChainConfig SnapshotConfig('),
        'POLL_CONFIG': definition(source, 'void PollConfigFromPresent('),
        'TOGGLE_MASTER': definition(source, 'void ToggleMasterFromHotkey('),
    }
    code = (ROOT / 'tests/config_policy_win32.cpp.in').read_text(encoding='utf-8-sig')
    for name, value in parts.items():
        assert code.count('@' + name + '@') == 1, name
        code = code.replace('@' + name + '@', value)
    assert not re.search(r'@[A-Z_]+@', code)
    return code


def run(compiler, code, directory, negative=False):
    directory.mkdir()
    cpp, exe = directory / 'config-policy.cpp', directory / 'config-policy.exe'
    cpp.write_text(code, encoding='utf-8')
    subprocess.run([compiler, '-std=c++20', '-O0', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', '-static', '-municode', '-I', str(ROOT / 'src'),
                    str(cpp), '-o', str(exe)], check=True)
    result = subprocess.run([str(exe), str(directory)], text=True, capture_output=True)
    (directory / 'run.txt').write_text(result.stdout + result.stderr, encoding='utf-8')
    if negative:
        assert result.returncode == 23 and 'FAIL ' in result.stdout, result.stdout + result.stderr
    else:
        assert result.returncode == 0, result.stdout + result.stderr
        print(result.stdout, end='')
    return exe, result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--source', type=Path, default=ROOT / 'src/nr_before_sr.cpp')
    parser.add_argument('--negative-control', action='store_true')
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    directory = args.output_dir or Path(tempfile.mkdtemp(prefix='nr-config-policy-'))
    if args.output_dir:
        directory.mkdir(parents=True, exist_ok=False)
    source = args.source.read_text(encoding='utf-8-sig')
    code = assemble(source)
    exe, output = run(args.compiler, code, directory / 'positive')
    restarts = []
    for name, strength, enabled, jitter in (
        ('restart-off.ini', '1', '0', '0'),
        ('restart-user-on.ini', '2.3', '1', '1'),
        ('restart-partial.ini', '1', '1', '0'),
        ('restart-partial-off.ini', '1', '0', '0'),
        ('restart-diagnostic-zero.ini', '0', '1', '1'),
    ):
        result = subprocess.run([str(exe), str(directory), str(exe.parent / name),
                                 strength, enabled, jitter, '1.73'], text=True, capture_output=True)
        assert result.returncode == 0, name + ': ' + result.stdout + result.stderr
        restarts.append({'fixture': name, 'passed': True, 'output': result.stdout.strip()})
    for name, route, passes, percent in (
        ('restart-route-before.ini', '0', '3', '50'),
        ('restart-route-after.ini', '1', '4', '75'),
        ('restart-route-present.ini', '2', '5', '100'),
    ):
        result = subprocess.run([str(exe), str(directory), str(exe.parent / name),
                                 route, passes, percent, '1', 'processing'], text=True, capture_output=True)
        assert result.returncode == 0, name + ': ' + result.stdout + result.stderr
        restarts.append({'fixture': name, 'passed': True, 'output': result.stdout.strip()})
    for name, route, mode in (
        ('launch-empty.ini', '0', '2'),
        ('launch-legacy-before.ini', '0', '2'),
        ('launch-legacy-after.ini', '1', '1'),
        ('launch-invalid-after.ini', '1', '1'),
        ('launch-saved-before-r0.ini', '0', '2'),
        ('launch-saved-after-r0.ini', '1', '1'),
        ('launch-saved-present-r0.ini', '2', '2'),
        ('launch-saved-before-r1.ini', '0', '2'),
        ('launch-saved-after-r1.ini', '1', '1'),
        ('launch-saved-present-r1.ini', '2', '1'),
    ):
        result = subprocess.run([str(exe), str(directory), str(exe.parent / name),
                                 route, mode, 'launch'], text=True, capture_output=True)
        assert result.returncode == 0, name + ': ' + result.stdout + result.stderr
        restarts.append({'fixture': name, 'passed': True, 'output': result.stdout.strip()})
    negatives = []
    mutations = (
        ('repeat-jitter', 'if (g_config_policy_attempted) return;',
         'if (g_config_policy_attempted) { next.jitter_mode = NrJitterMode::Legacy; return; }',
         'later polling retains'),
        ('marker-before-value', 'if (ok) write(WriteIniFloat(L"PostTransferStrength", next.post_transfer_strength));',
         'if (ok) write(WriteIniInt(L"ExtraStrengthPolicyVersion", 1));\n'
         '        if (ok) write(WriteIniFloat(L"PostTransferStrength", next.post_transfer_strength));',
         'shared selection completes'),
        ('discard-zero-bypass', 'next.enabled = plan.enabled;', 'next.enabled = true;',
         'legacy active zero'),
        ('no-failure-latch', 'if (!force &&\n        g_config_persistence_failed.load(std::memory_order_acquire)) return;',
         'if (!force && false) return;', 'polling never overwrites'),
        ('f8-writes-ini', 'persist_to_ini && !preview_only ? SaveConfig(g_config) : true',
         '!preview_only ? SaveConfig(g_config) : true', 'performs no policy or INI writes'),
        ('reselect-on-config-poll', 'if(session.Select(g_config.processing_start,g_config.processing_selection)) {',
         'if(session.Select(g_config.processing_start,++g_config.processing_selection)) {',
         'ordinary edits preserve fallback'),
        ('unbounded-full-pass-count', 'config.nr_full_passes=std::clamp(config.nr_full_passes,3u,5u);',
         '(void)config.nr_full_passes;', 'more-layer count stays'),
        ('allow-legacy-native-sr-retry', 'config.retry_original_on_sr_failure=false;',
         '(void)config.retry_original_on_sr_failure;', 'normalization disables legacy same-source'),
    )
    if args.negative_control:
        for name, original, mutation, expected in mutations:
            assert code.count(original) == 1, name
            _, text = run(args.compiler, code.replace(original, mutation), directory / name, True)
            assert expected in text, name + ': intended assertion not reached\n' + text
            negatives.append({'name': name, 'compiled': True, 'behavioral_rejection': True,
                              'expected_assertion': expected})
            print('PASS compiled negative control:', name)
    receipt = {
        'passed': True, 'scope': 'production config prefix/normalization/apply/save; real Win32 INI; no GPU/NR/game',
        'source_sha256': hashlib.sha256(args.source.read_bytes()).hexdigest(),
        'launch_preferences_sha256': hashlib.sha256(
            (ROOT / 'src/nr_launch_preferences.h').read_bytes()).hexdigest(),
        'harness_sha256': hashlib.sha256(code.encode()).hexdigest(),
        'suite': output.strip(), 'fresh_process_restarts': restarts,
        'negative_controls': negatives,
    }
    (directory / 'validation.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(f'PASS {len(restarts)} fresh-process restarts; receipt:', directory / 'validation.json')


if __name__ == '__main__':
    main()
