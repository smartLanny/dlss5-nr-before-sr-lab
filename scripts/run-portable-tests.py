#!/usr/bin/env python3
"""Run every tests/**/test_*.py and report PASS / FAIL / SKIP.

This is a convenience entry for Linux, cloud agents and a future hosted job.
It does not replace scripts/build-unified-candidate.ps1: the canonical Windows
build still runs its own gates with the production Core. A SKIP always names the
missing platform, module or prerequisite; it is never counted as a pass.
"""
from __future__ import annotations

import argparse
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]

# Tests that need arguments when run on their own (the canonical build passes
# the same values).
ARGUMENTS = {
    'tests/test_colour_stage.py': ['--repo', '.'],
}
# Tests that exercise real Win32 APIs or the Windows toolchain by design.
WINDOWS_ONLY = {
    'tests/test_face_selection_ui.py',
    'tests/test_light_style.py',
}
# Tests that consume output produced by another script first.
PREREQUISITES = {
    'tests/test_replay_reader.py': (
        'build/replay-tests/captured.nrf', 'scripts/validate-replay.ps1'),
}
# Tests that need an optional Python module.
MODULES = {
    'tests/test_nr_dual_reference_math.py': 'numpy',
}


def skip_reason(test: str) -> str | None:
    if test in WINDOWS_ONLY and os.name != 'nt':
        return 'requires Windows'
    if test in PREREQUISITES:
        path, producer = PREREQUISITES[test]
        if not (ROOT / path).exists():
            return f'needs {path} (run {producer} first)'
    if test in MODULES and importlib.util.find_spec(MODULES[test]) is None:
        return f'needs Python module {MODULES[test]}'
    return None


def environment() -> dict[str, str]:
    env = dict(os.environ)
    if not env.get('CXX'):
        found = shutil.which('clang++') or shutil.which('g++')
        if found:
            env['CXX'] = found
    return env


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('-k', dest='pattern', help='only run tests whose path contains this text')
    parser.add_argument('--timeout', type=int, default=600, help='seconds per test file')
    parser.add_argument('--verbose', action='store_true', help='print output of failing tests')
    args = parser.parse_args()

    (ROOT / 'build').mkdir(exist_ok=True)
    tests = sorted(
        p.relative_to(ROOT).as_posix()
        for p in (ROOT / 'tests').rglob('test_*.py')
        if 'archive' not in p.relative_to(ROOT / 'tests').parts
    )
    if args.pattern:
        tests = [t for t in tests if args.pattern in t]
    env = environment()
    counts = {'PASS': 0, 'FAIL': 0, 'SKIP': 0}
    failures: list[tuple[str, str]] = []
    for test in tests:
        reason = skip_reason(test)
        if reason:
            counts['SKIP'] += 1
            print(f'SKIP  {test}  ({reason})')
            continue
        started = time.monotonic()
        try:
            result = subprocess.run(
                [sys.executable, test, *ARGUMENTS.get(test, [])], cwd=ROOT, env=env,
                capture_output=True, text=True, errors='replace', timeout=args.timeout)
            ok, output = result.returncode == 0, result.stdout + result.stderr
        except subprocess.TimeoutExpired as error:
            ok, output = False, f'timed out after {args.timeout}s\n{error.stdout or ""}'
        elapsed = time.monotonic() - started
        status = 'PASS' if ok else 'FAIL'
        counts[status] += 1
        print(f'{status}  {test}  ({elapsed:.1f}s)')
        if not ok:
            failures.append((test, output))
    print(f"\n{counts['PASS']} passed, {counts['FAIL']} failed, {counts['SKIP']} skipped "
          f'(skipped tests are not passes)')
    if args.verbose:
        for test, output in failures:
            print(f'\n===== {test}\n{output[-4000:]}')
    return 1 if counts['FAIL'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
