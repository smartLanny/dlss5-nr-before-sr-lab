"""Compile and run the actual pure Compatibility Verdict header and fault controls."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
HIDDEN = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    files = {'header': ROOT / 'src/nr_compatibility_verdict.h',
             'test': ROOT / 'tests/compatibility_verdict_test.cpp',
             'runner': Path(__file__)}
    captured = {name: path.read_bytes() for name, path in files.items()}
    hashes = {name: hashlib.sha256(data).hexdigest() for name, data in captured.items()}
    def git(*values: str) -> str:
        return subprocess.run(['git', '-C', str(ROOT), *values], capture_output=True,
                              text=True, check=True, creationflags=HIDDEN).stdout.strip()
    identity = {'source_commit': git('rev-parse', 'HEAD'),
                'source_dirty': bool(git('status', '--porcelain', '--untracked-files=normal')),
                'tested_source_sha256': hashes,
                'scope': 'direct include of actual pure header; host CPU only; no rendering/provider/runtime inference'}
    (args.output_dir / 'source-identity.json').write_text(json.dumps(identity, indent=2), encoding='utf-8')
    controls = {
        'production': ('', ''),
        'one-success': ('kRequiredSuccessStreak = 3', 'kRequiredSuccessStreak = 1'),
        'ignore-config-scope': ('a.config == b.config', 'true'),
        'skip-writeback': ('if (!recovery.writeback_verified)', 'if (false)'),
    }
    oracles = {'one-success': 'ONE_SUCCESS_NOT_RECOVERY',
               'ignore-config-scope': 'STALE_SCOPE_CANNOT_SUCCEED',
               'skip-writeback': 'WRITEBACK_REQUIRED'}
    results = []
    for name, (before, after) in controls.items():
        output = args.output_dir / name
        output.mkdir()
        header = captured['header'].decode('utf-8-sig')
        if before:
            assert header.count(before) == 1, before
            header = header.replace(before, after)
        if name == 'one-success':
            assert header.count('kRequiredSubmissions = 3') == 1
            header = header.replace('kRequiredSubmissions = 3', 'kRequiredSubmissions = 1')
        (output / 'nr_compatibility_verdict.h').write_text(header, encoding='utf-8')
        cpp = output / 'compatibility_verdict_test.cpp'
        cpp.write_bytes(captured['test'])
        exe = output / ('verdict.exe' if os.name == 'nt' else 'verdict')
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
        else:
            if result.returncode != 1 or oracles[name] not in result.stderr:
                raise SystemExit('Negative control missed its exact oracle: ' + name)
            print('PASS negative control:', name)
        results.append({'variant': name, 'command': command, 'exit_code': result.returncode,
                        'expected_negative': name != 'production',
                        'compiled_header_sha256': hashlib.sha256(header.encode()).hexdigest(),
                        'binary_sha256': hashlib.sha256(exe.read_bytes()).hexdigest()})
    if any(path.read_bytes() != captured[name] for name, path in files.items()):
        raise SystemExit('Verdict sources changed during validation')
    (args.output_dir / 'validation.json').write_text(
        json.dumps({'status': 'PASS', 'identity': identity, 'results': results}, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
