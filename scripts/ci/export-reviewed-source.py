"""Archive only regular committed Git blobs, never the runner working directory.

This is an offline review snapshot, not a build, dependency bundle or candidate.
Git attributes must not rewrite compiler inputs while exporting them.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import zipfile


def git(root, *args, **kwargs):
    return subprocess.run(['git', '-C', str(root), *args], check=True,
                          capture_output=True, **kwargs).stdout


def export(root: Path, output: Path):
    commit = git(root, 'rev-parse', 'HEAD').decode().strip()
    tree = git(root, 'rev-parse', 'HEAD^{tree}').decode().strip()
    entries, gitlinks = [], []
    for line in git(root, 'ls-tree', '-r', '-z', '-l', commit).split(b'\0'):
        if not line:
            continue
        metadata, raw_name = line.split(b'\t', 1)
        mode, kind, oid, size = metadata.split()
        name = raw_name.decode('utf-8')
        path = PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or '\\' in name:
            raise ValueError('Unsafe committed path: '+name)
        if mode == b'160000' and kind == b'commit':
            gitlinks.append({'path': name, 'commit': oid.decode()})
            continue
        if kind != b'blob' or mode not in (b'100644', b'100755'):
            raise ValueError('Non-regular source entry: '+name)
        entries.append((name, mode, oid, int(size)))
    if not entries or sum(item[3] for item in entries) > 256*1024*1024:
        raise ValueError('Empty or oversized source snapshot')
    data = git(root, 'cat-file', '--batch', input=b'\n'.join(e[2] for e in entries)+b'\n')
    output.mkdir(parents=True, exist_ok=False)
    archive = output/'committed-source.zip'
    files, offset = [], 0
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED) as zipped:
        for name, mode, oid, size in entries:
            end = data.index(b'\n', offset)
            if data[offset:end] != oid+b' blob '+str(size).encode():
                raise ValueError('Git object identity changed')
            blob = data[end+1:end+1+size]
            offset = end+1+size
            if len(blob) != size or data[offset:offset+1] != b'\n':
                raise ValueError('Truncated Git blob')
            offset += 1
            info = zipfile.ZipInfo(name, (2020, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = int(mode, 8) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            zipped.writestr(info, blob)
            files.append({'path': name, 'mode': mode.decode(), 'git_blob': oid.decode(),
                          'bytes': size, 'sha256': hashlib.sha256(blob).hexdigest()})
    if offset != len(data):
        raise ValueError('Trailing Git blob data')
    receipt = {'schema': 'dlss5-committed-source-review-v1', 'source_commit': commit,
               'source_tree': tree, 'archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
               'files': files, 'external_gitlinks': gitlinks,
               'scope': 'Committed project source only; no build, dependency, GPU or game validation.'}
    (output/'source-review.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    export(args.root, args.output)
