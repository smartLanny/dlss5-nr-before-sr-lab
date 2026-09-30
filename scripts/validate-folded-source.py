#!/usr/bin/env python3
"""Compare a one-time source migration with the frozen Beta 13 file evidence.

This is deliberately an explicit comparison, never a build or future source pin.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compare(root: Path, expected: dict[str, str], *, carrier: bool) -> dict:
    missing, changed, unexpected, unsafe = [], [], [], []
    for name, sha in expected.items():
        path = root / name
        if path.is_symlink():
            unsafe.append(name)
        elif not path.is_file():
            missing.append(name)
        elif digest(path) != sha:
            changed.append(name)
    # The carrier is a complete pinned upstream export. Core has additional
    # project files; its migration scope is the complete maintained src tree.
    scan = root if carrier else root / 'src'
    if scan.exists():
        for path in scan.rglob('*'):
            name = path.relative_to(root).as_posix()
            if path.is_symlink():
                unsafe.append(name)
            elif path.is_file() and name not in expected and (
                    carrier or name != 'src/interop/carrier-beta13.patch'):
                unexpected.append(name)
    return {key: sorted(set(value)) for key, value in (
        ('missing', missing), ('changed', changed), ('unexpected', unexpected), ('unsafe', unsafe))}


def validate(core: Path, carrier_root: Path | None, baseline: Path) -> dict:
    evidence = json.loads(baseline.read_text(encoding='utf-8'))
    if evidence.get('schema') != 'dlss5-beta13-fold-baseline-v1':
        raise ValueError('Unexpected migration baseline schema')
    owned = compare(core, evidence['owned_files_sha256'], carrier=False)
    carrier = (compare(carrier_root, evidence['carrier_files_sha256'], carrier=True)
               if carrier_root is not None else None)
    passed = all(not values for values in owned.values()) and (
        carrier is None or all(not values for values in carrier.values()))
    return {'schema': 'dlss5-beta13-fold-comparison-v1',
            'baseline_manifest_sha256': evidence['original_manifest_sha256'],
            'owned_file_count': len(evidence['owned_files_sha256']),
            'carrier_file_count': len(evidence['carrier_files_sha256']) if carrier is not None else None,
            'owned': owned, 'carrier': carrier, 'passed': passed,
            'scope': 'One-time migration comparison; never a future source pin.'}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, required=True)
    parser.add_argument('--carrier', type=Path)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = validate(args.core.resolve(), args.carrier.resolve() if args.carrier else None,
                      args.baseline.resolve())
    if args.output.exists():
        raise SystemExit('Comparison output already exists')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('Beta 13 migration comparison: ' + ('PASS' if result['passed'] else 'FAIL'))
    if not result['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
