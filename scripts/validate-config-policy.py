"""Exercise actual jitter config functions with Win32 INI files and fresh processes.

Adapted from D8 cb33e482's policy fixture; only jitter policy is included. The
module-observation suffix is excluded from ReloadConfig; actual configuration
publication, normalization, apply, save, polling and reset code is executed.
Write faults are injected only at the OS write entrance. No GPU/NR/game runs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HIDDEN = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0


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
    publication = reload.index('    const bool changed = !SameConfig(next, g_config);')
    publication_end = reload.index('    if (changed || force) {\n        Log(', publication)
    reload = reload[:reload.index('    const bool shortfuse_loaded =')] + \
        reload[publication:publication_end] + '}\n'
    helpers = [definition(source, marker) for marker in (
        'float ReadIniFloat(', 'float FiniteOr(', 'int MigrateConfigMode(',
        'bool WriteIniInt(const wchar_t *key, int value) {',
        'bool WriteIniFloat(const wchar_t *key, float value) {',
        'bool IsNewWorkConfigFile(', 'void RecordWorkRequestStatus(',
        'void PersistWorkControl(', 'nr_work_config::Request ReadWorkRequest(',
        'float ReadConfiguredTransferStrength(')]
    parts = {
        'JITTER_ENUM': definition(source, 'enum class NrJitterMode', True),
        'CHAIN_CONFIG': definition(source, 'struct ChainConfig', True),
        'HELPERS': '\n\n'.join(helpers),
        'POLICY_RUNTIME': (ROOT / 'src/nr_config_policy_runtime.inl').read_text(encoding='utf-8-sig'),
        'NORMALIZE': definition(source, 'void NormalizeConfig('),
        'SAME_CONFIG': definition(source, 'bool SameConfig('),
        'SAME_HISTORY': definition(source, 'bool SameHistoryConfig('),
        'ACTIVE_TRANSFER': definition(source, 'float ActiveTransferStrength('),
        'RELOAD_CONFIG': reload,
        'SAVE_CONFIG': definition(source, 'bool SaveConfig('),
        'APPLY_CONFIG': definition(source, 'bool ApplyConfig('),
        'SNAPSHOT_CONFIG': definition(source, 'ChainConfig SnapshotConfig('),
        'POLL_CONFIG': definition(source, 'void PollConfigFromPresent('),
        'RESTORE_DEFAULTS': (ROOT / 'src/nr_recommended_picture.inl').read_text(encoding='utf-8-sig'),
        'TUNING_SIGNATURE': definition(source, 'struct NrTuningSignature', True),
        'CURRENT_TUNING': definition(source, 'NrTuningSignature CurrentNrTuningSignature('),
        'SAME_TUNING': definition(source, 'bool SameNrTuningSignature('),
    }
    code = (ROOT / 'tests/config_policy_win32.cpp.in').read_text(encoding='utf-8-sig')
    for name, value in parts.items():
        assert code.count('@' + name + '@') == 1, name
        code = code.replace('@' + name + '@', value)
    assert not re.search(r'@[A-Z_]+@', code)
    return code


def execute(args, **kwargs):
    return subprocess.run(args, creationflags=HIDDEN, **kwargs)


def run(compiler, code, directory, expected=None):
    directory.mkdir()
    cpp, exe = directory / 'config-policy.cpp', directory / 'config-policy.exe'
    cpp.write_text(code, encoding='utf-8')
    execute([compiler, '-std=c++20', '-O0', '-Wall', '-Wextra', '-Werror',
             '-Wno-unused-function', '-static', '-municode', '-I', str(ROOT / 'src'),
             str(cpp), '-o', str(exe)], check=True)
    result = execute([str(exe), str(directory), str(ROOT / 'config/nr_before_sr.ini')], text=True, capture_output=True)
    (directory / 'run.txt').write_text(result.stdout + result.stderr, encoding='utf-8')
    if expected:
        assert result.returncode == 23 and ('FAIL ' + expected) in result.stdout, result.stdout + result.stderr
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
    directory = args.output_dir or Path(tempfile.mkdtemp(prefix='nr-jitter-policy-'))
    if args.output_dir:
        directory.mkdir(parents=True, exist_ok=False)
    source = args.source.read_text(encoding='utf-8-sig')
    code = assemble(source)
    exe, output = run(args.compiler, code, directory / 'positive')
    restarts = []
    for name, jitter, intensity in (
        ('restart-off.ini', '0', '1.73'),
        ('restart-user-on.ini', '1', '1.73'),
        ('restart-retried-on.ini', '1', '1.21'),
        ('restart-value-failed.ini', '0', '1.73'),
        ('restart-marker-failed.ini', '0', '1.73'),
        ('restart-reset-off.ini', '0', '1.2'),
    ):
        result = execute([str(exe), str(exe.parent), str(exe.parent / name), jitter, intensity],
                         text=True, capture_output=True)
        assert result.returncode == 0, name + ': ' + result.stdout + result.stderr
        restarts.append({'fixture': name, 'passed': True, 'output': result.stdout.strip()})
    mutations = (
        ('retain-old-on', 'if (pending) {\n        status.jitter_changed',
         'if (false) {\n        status.jitter_changed', 'old request is forced off once'),
        ('repeat-jitter', 'if (g_config_policy_attempted) return;',
         'if (g_config_policy_attempted) { next.jitter_mode = NrJitterMode::Legacy; return; }',
         'later polling retains explicit re-enable without rewriting'),
        ('marker-before-value', 'write(WriteIniInt(L"JitterMode", 0));',
         'write(WriteIniInt(L"JitterPolicyVersion", 1));\n        write(WriteIniInt(L"JitterMode", 0));',
         'jitter value is persisted before its marker'),
        ('no-failure-latch', 'if (!force &&\n        g_config_persistence_failed.load(std::memory_order_acquire)) return;',
         'if (!force && false) return;', 'failed save polling never overwrites live manual settings'),
        ('f8-writes-ini', 'persist_to_ini ? SaveConfig(g_config) : true',
         'SaveConfig(g_config)', 'F8 restoration performs no policy or INI writes'),
        ('reset-retains-on', 'next.ui_correction = true;\n    next.jitter_mode = NrJitterMode::Legacy;',
         'next.ui_correction = true;', 'default reset disables jitter while preserving other provider contracts'),
        ('no-explicit-retry', 'if (persist_to_ini &&\n            g_config_persistence_failed.load(std::memory_order_acquire))',
         'if (false)', 'explicit unchanged choice retries save without history change'),
    )
    negatives = []
    if args.negative_control:
        for name, original, mutation, expected in mutations:
            assert code.count(original) == 1, name
            run(args.compiler, code.replace(original, mutation), directory / name, expected)
            negatives.append({'name': name, 'compiled': True, 'behavioral_rejection': True, 'expected_assertion': expected})
            print('PASS compiled negative control:', name)
    paths = [args.source, ROOT / 'config/nr_before_sr.ini', ROOT / 'src/nr_config_policy.h', ROOT / 'src/nr_config_policy_runtime.inl',
             ROOT / 'src/nr_recommended_picture.inl', ROOT / 'src/nr_work_config.h',
             ROOT / 'tests/config_policy_win32.cpp.in', Path(__file__)]
    receipt = {
        'passed': True,
        'scope': 'actual config functions and reset; real Win32 INI; six fresh processes; no GPU/NR/game',
        'source_commit': execute(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True, capture_output=True, check=True).stdout.strip(),
        'source_dirty': bool(execute(['git', '-C', str(ROOT), 'status', '--porcelain'], text=True, capture_output=True, check=True).stdout),
        'sources': [{'path': str(path.resolve()), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()} for path in paths],
        'harness_sha256': hashlib.sha256(code.encode()).hexdigest(),
        'suite': output.strip(), 'fresh_process_restarts': restarts, 'negative_controls': negatives,
    }
    (directory / 'validation.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print('PASS six fresh-process restarts; receipt:', directory / 'validation.json')


if __name__ == '__main__':
    main()
