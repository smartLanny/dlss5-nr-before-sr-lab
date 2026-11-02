#!/usr/bin/env python3
"""Export committed Core source verbatim and stage the pinned external carrier.

The carrier integration diff is the only vendor overlay. Core C++, HLSL, INI
and build scripts are never rewritten. Per-build hashes identify the actual
inputs; the frozen Beta 13 comparison is a separate, optional migration check.
"""
from __future__ import annotations
import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import uuid

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = '28aed4099b0fe1c207b20b5fee5364c0773c25c2'
LAYOUT = 'scripts/core-source-layout.json'


def git(root: Path, *args: str) -> bytes:
    return subprocess.run(['git', '-C', str(root), *args], check=True,
                          capture_output=True).stdout


def file_hashes(root: Path) -> dict[str, str]:
    result = {}
    for path in sorted(root.rglob('*')):
        if path.is_symlink():
            raise ValueError('Source export must not contain symlinks')
        if path.is_file() and path != root / 'source-manifest.json':
            result[path.relative_to(root).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def export(root: Path, revision: str, paths: list[str], destination: Path, *, carrier: bool = False) -> None:
    # Preserve the original Windows carrier archive convention independently
    # of the runner account's global Git configuration. Core has its own
    # checked-in newline attributes, including the inherited mixed files.
    config = ('-c', 'core.autocrlf=true') if carrier else ()
    data = git(root, *config, 'archive', revision, *paths)
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        for member in archive.getmembers():
            name = PurePosixPath(member.name)
            if name.is_absolute() or '..' in name.parts or not (member.isfile() or member.isdir()):
                raise ValueError('Unsafe source archive member: ' + member.name)
            target = destination.joinpath(*name.parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                stream = archive.extractfile(member)
                if stream is None:
                    raise ValueError('Missing source archive member')
                target.write_bytes(stream.read())


def stage_inputs(core: Path, revision: str, layout: dict, destination: Path,
                 snapshot: dict[str, bytes] | None = None) -> None:
    if snapshot is None:
        entries = git(core, 'ls-tree', '-r', '-z', revision, '--',
                      'src', 'config', 'third_party', 'scripts', 'experiments/nr-edit',
                      'LICENSE', 'THIRD_PARTY_NOTICES.txt')
        names = []
        kinds = {}
        blobs = {}
        for entry in entries.rstrip(b'\0').split(b'\0'):
            kind, name = entry.split(b'\t', 1)
            mode, object_type, blob = kind.split()
            decoded = name.decode()
            kinds[decoded] = (mode, object_type)
            blobs[decoded] = blob
            if decoded.split('/')[0] in ('src', 'config', 'third_party') or decoded in ('LICENSE', 'THIRD_PARTY_NOTICES.txt'):
                names.append(decoded)
    else:
        names = [n for n in snapshot if n.split('/')[0] in ('src', 'config', 'third_party')
                 or n in ('LICENSE', 'THIRD_PARTY_NOTICES.txt')]
    # The vendor diff is an exporter input, not a Core compiler input.
    names = [n for n in names if n != layout['carrier_overlay']['patch_file']]
    names += layout['core_build_inputs'] + layout['experiment_inputs']
    for name in names:
        p = PurePosixPath(name)
        if p.is_absolute() or '..' in p.parts or (p.parts[0] not in ('src', 'config', 'third_party', 'scripts', 'experiments')
                                                   and name not in ('LICENSE', 'THIRD_PARTY_NOTICES.txt')):
            raise ValueError('Unexpected Core input: ' + name)
        if snapshot is None:
            mode, object_type = kinds.get(name, (b'', b''))
            if mode not in (b'100644', b'100755') or object_type != b'blob':
                raise ValueError('Non-regular Core source: ' + name)
        else:
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(snapshot[name])
    if snapshot is None:
        # cat-file reads committed blobs verbatim. git archive can apply eol
        # attributes, which would silently change mixed newline source bytes.
        names = sorted(set(names))
        if not names:
            raise ValueError('No committed Core inputs')
        data = subprocess.run(
            ['git', '-C', str(core), 'cat-file', '--batch'],
            input=b'\n'.join(blobs[name] for name in names) + b'\n',
            check=True, capture_output=True).stdout
        offset = 0
        for name in names:
            end = data.index(b'\n', offset)
            object_id, object_type, size = data[offset:end].split()
            if object_id != blobs[name] or object_type != b'blob':
                raise ValueError('Unexpected Git compiler input: ' + name)
            size = int(size)
            start = end + 1
            if start + size >= len(data) or data[start + size:start + size + 1] != b'\n':
                raise ValueError('Truncated Git compiler input: ' + name)
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data[start:start + size])
            offset = start + size + 1
        if offset != len(data):
            raise ValueError('Unexpected trailing Git source data')


def stage_carrier(core: Path, upstream: Path, stage: Path, layout: dict,
                  snapshot: dict[str, bytes] | None = None, revision: str = 'HEAD') -> None:
    export(upstream, UPSTREAM, ['src', 'LICENSE'], stage / 'carrier', carrier=True)
    patch_name = layout['carrier_overlay']['patch_file']
    patch = git(core, 'show', revision + ':' + patch_name) if snapshot is None else snapshot[patch_name]
    # Limit the external overlay to the two reviewed integration files.
    headers = [line for line in patch.splitlines() if line.startswith((b'--- ', b'+++ '))]
    expected = [b'--- a/src/bridge.inc', b'+++ b/src/bridge.inc',
                b'--- a/src/dlss5-bridge.cpp', b'+++ b/src/dlss5-bridge.cpp']
    if headers != expected:
        raise ValueError('Unexpected carrier integration diff paths')
    # The two transformed files were LF in the original generated carrier.
    # Take their exact Git preimages, without a platform newline conversion.
    for name in ('src/bridge.inc', 'src/dlss5-bridge.cpp'):
        (stage / 'carrier' / name).write_bytes(git(upstream, 'show', UPSTREAM + ':' + name))
    for args in (['--check'], []):
        result = subprocess.run(['git', '-c', 'core.autocrlf=false', '-c', 'core.eol=lf',
                                 'apply', '--whitespace=nowarn', *args, '-'],
                                cwd=stage / 'carrier', input=patch, capture_output=True,
                                env={**os.environ, 'GIT_CEILING_DIRECTORIES': str(stage)})
        if result.returncode:
            raise ValueError('Pinned carrier overlay failed: ' + result.stderr.decode(errors='replace'))
    for target, source in layout['carrier_overlay']['headers'].items():
        p = PurePosixPath(target)
        if p.parent != PurePosixPath('src') or not p.name.startswith('nr_'):
            raise ValueError('Unexpected carrier header: ' + target)
        (stage / 'carrier' / target).write_bytes((stage / 'core' / source).read_bytes())


def prepare(core: Path, upstream: Path, output: Path, *,
            snapshot: dict[str, bytes] | None = None, parent: str | None = None) -> None:
    if output.exists():
        raise ValueError('Output already exists; preserve previous evidence')
    for dependency in (upstream,):
        if git(dependency, 'rev-parse', 'HEAD').decode().strip() != UPSTREAM:
            raise ValueError('Upstream is not the pinned carrier commit')
        if git(dependency, 'status', '--porcelain', '--untracked-files=no').strip():
            raise ValueError('Dirty carrier dependency')
    revision = git(core, 'rev-parse', 'HEAD').decode().strip()
    if parent is not None and parent != revision:
        raise ValueError('Snapshot parent differs from the actual source HEAD')
    if snapshot is None and git(core, 'status', '--porcelain', '--untracked-files=no').strip():
        raise ValueError('Commit reviewed source changes first')
    layout = json.loads(git(core, 'show', revision + ':' + LAYOUT) if snapshot is None else snapshot[LAYOUT])
    output.parent.mkdir(parents=True, exist_ok=True)
    # A Windows TemporaryDirectory creates an owner-only ACL. Renaming its
    # child into the published export retains that ACL and hides CI artifacts
    # from the maintainer. Create a unique sibling with the output parent's
    # normal inherited permissions, then publish with the same atomic rename.
    stage = output.parent / ('nr-direct-source-' + uuid.uuid4().hex)
    stage.mkdir()
    try:
        stage_inputs(core, revision, layout, stage / 'core', snapshot)
        stage_carrier(core, upstream, stage, layout, snapshot, revision)
        metadata = dict(layout['manifest_metadata'])
        metadata.update({'schema': 'dlss5-direct-source-v1', 'source_commit': revision,
                         'source_dirty': snapshot is not None, 'source_transforms': False,
                         'carrier_commit': UPSTREAM,
                         'source_layout_sha256': hashlib.sha256(
                             git(core, 'show', revision + ':' + LAYOUT) if snapshot is None else snapshot[LAYOUT]).hexdigest()})
        for section in ('integration', 'colour_lab', 'reconstruction', 'private_residual_sr'):
            metadata[section]['source_commit'] = revision
        metadata['integration']['bridge_commit'] = UPSTREAM
        metadata['files'] = file_hashes(stage)
        (stage / 'source-manifest.json').write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + '\n',
                                                   encoding='utf-8', newline='\n')
        if snapshot is None and (git(core, 'rev-parse', 'HEAD').decode().strip() != revision or
                                git(core, 'status', '--porcelain', '--untracked-files=no').strip()):
            raise ValueError('Source changed during export')
        if output.exists():
            raise ValueError('Output appeared during export')
        stage.rename(output)
    finally:
        if stage.exists():
            if stage.is_symlink() or stage.resolve().parent != output.parent.resolve():
                raise ValueError('Refusing cleanup outside the source export parent')
            shutil.rmtree(stage)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, default=ROOT)
    parser.add_argument('--upstream', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.core.resolve(), args.upstream.resolve(), args.output.resolve())
    print('Exported committed Core source without transformations; game verification is separate.')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(str(exc)) from exc
