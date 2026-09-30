#!/usr/bin/env python3
"""Freeze explicit working-tree inputs and use the direct-source exporter.

This is a local development compiler input, never a reviewed release candidate.
It creates no Git commit and changes no Git index. Final acceptance and packaging
continue to require the normal clean, committed-source exporter.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOTS = ('src', 'scripts', 'config', 'third_party')


def git(root: Path, *args: str) -> bytes:
    return subprocess.run(['git', '-C', str(root), *args], check=True,
                          capture_output=True).stdout


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def git_blob(data: bytes) -> str:
    return hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()


def checked_name(name: str) -> str:
    p = PurePosixPath(name)
    if p.is_absolute() or '..' in p.parts or not p.parts or p.parts[0] not in SOURCE_ROOTS:
        raise ValueError(f'Unexpected source path: {name}')
    return p.as_posix()


def freeze(core: Path, extra: list[str]) -> tuple[str, dict[str, bytes]]:
    parent = git(core, 'rev-parse', 'HEAD').decode().strip()
    names = set(git(core, 'ls-files', '-z', '--', *SOURCE_ROOTS).decode().rstrip('\0').split('\0'))
    for name in extra:
        name = checked_name(name.replace('\\', '/'))
        if not name.startswith('src/') and name != 'scripts/build-nrchain.ps1':
            raise ValueError('Additional development inputs must be explicit src/ files or the official nrchain build helper')
        names.add(name)
    inputs: dict[str, bytes] = {}
    for name in sorted(names):
        name = checked_name(name)
        path = core / name
        if path.is_symlink() or not path.resolve(strict=True).is_relative_to(core):
            raise ValueError(f'Unsafe source: {name}')
        if not path.is_file():
            raise ValueError(f'Expected regular source file: {name}')
        inputs[name] = path.read_bytes()
    # Do not silently omit a newly introduced sibling include. A developer
    # must name each untracked source explicitly rather than export a folder.
    for name, data in inputs.items():
        if not name.startswith('src/'):
            continue
        for include in re.findall(rb'^\s*#\s*include\s*"([^"]+)"', data, re.M):
            candidate = ((core / name).parent / include.decode('utf-8')).resolve()
            if candidate.is_file() and candidate.is_relative_to(core / 'src'):
                dependency = candidate.relative_to(core).as_posix()
                if dependency not in inputs:
                    raise ValueError(f'Unlisted dependency {dependency}; add --extra-source explicitly')
    if git(core, 'rev-parse', 'HEAD').decode().strip() != parent or any(
            (core / name).read_bytes() != data for name, data in inputs.items()):
        raise ValueError('Source changed while freezing; retry with a fresh output')
    return parent, inputs


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if not spec or not spec.loader:
        raise ValueError(f'Cannot import {path.name}')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def prepare(core: Path, upstream: Path, output: Path, extra: list[str]) -> None:
    if output.exists():
        raise ValueError('Output exists; preserve old snapshots and evidence')
    parent, inputs = freeze(core, extra)
    output.mkdir(parents=True)
    raw = output / 'inputs'
    for name, data in inputs.items():
        target = raw / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    receipt = {
        'schema': 'nr-working-source-snapshot-v1', 'source_parent': parent,
        'source_dirty': True, 'development_snapshot': True,
        'explicit_additional_sources': sorted(extra),
        'files': {name: digest(data) for name, data in inputs.items()},
        'game_verified': False, 'nvidia_feature18_verified': False,
    }
    (output / 'input-manifest.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    canonical = load('development_canonical', core / 'scripts/prepare-core-source.py')
    stage = output / 'source'
    canonical.prepare(core, upstream, stage, snapshot=inputs, parent=parent)
    manifest_path = stage / 'source-manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    manifest.update({
        'development_snapshot': True, 'source_dirty': True,
        'source_parent': parent,
        'input_manifest_sha256': digest((output / 'input-manifest.json').read_bytes()),
        'development_input_files_sha256': receipt['files'],
        'explicit_additional_sources': sorted(extra),
    })
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'Development snapshot staged: {stage}; parent={parent}; files={len(inputs)}')
    print('Not eligible for final acceptance/package; source bytes are in input-manifest.json.')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, default=ROOT)
    parser.add_argument('--upstream', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--extra-source', action='append', default=[])
    args = parser.parse_args()
    prepare(args.core.resolve(), args.upstream.resolve(strict=True), args.output.resolve(), args.extra_source)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(str(exc)) from exc
