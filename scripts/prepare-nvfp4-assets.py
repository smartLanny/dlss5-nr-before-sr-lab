#!/usr/bin/env python3
"""Acquire the immutable upstream reference assets outside the source checkout."""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request
import zipfile

COMMIT = 'ec96f42f8d32a4fc82f7c4558c0ed12a9f90d853'
URL = ('https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/'
       'releases/download/v0.7.1-hybrid/OptiScaler-DLSSNR-v0.7.1-hybrid.zip')
SHA256 = 'ee0824f7fa58649f8333d23ded5d7c48a0252a34d22eac30fd236e65ece034df'
PREFIX = 'OptiScaler/nvfp4/hybrid/'

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--archive', type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    repo = Path(__file__).resolve().parents[1]
    if output == repo or output.is_relative_to(repo):
        parser.error('Reference vendor assets belong outside the source checkout.')
    output.mkdir(parents=True, exist_ok=True)
    archive = args.archive or output / 'upstream-hybrid.zip'
    if not archive.exists():
        with urllib.request.urlopen(URL, timeout=60) as source, archive.open('xb') as target:
            while block := source.read(1024 * 1024):
                target.write(block)
    if digest(archive) != SHA256:
        raise ValueError('The release archive does not match the pinned SHA-256.')
    manifest = {'upstream_commit': COMMIT, 'archive_url': URL, 'archive_sha256': SHA256,
                'purpose': 'local experimental reference, not an NVIDIA runtime', 'files': []}
    with zipfile.ZipFile(archive) as source:
        for entry in source.infolist():
            if entry.is_dir() or not entry.filename.startswith(PREFIX):
                continue
            relative = entry.filename[len(PREFIX):]
            target = (output / 'hybrid' / relative).resolve()
            if not target.is_relative_to(output / 'hybrid'):
                raise ValueError('Invalid asset path')
            data = source.read(entry)
            expected = hashlib.sha256(data).hexdigest()
            if target.exists() and digest(target) != expected:
                raise ValueError(f'Preserve existing, different asset: {target}')
            target.parent.mkdir(parents=True, exist_ok=True)
            if not target.exists():
                target.write_bytes(data)
            manifest['files'].append({'path': relative, 'bytes': len(data), 'sha256': expected})
    if not manifest['files']:
        raise ValueError('Pinned archive has no hybrid assets')
    (output / 'reference-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(f"Verified {len(manifest['files'])} assets at {output / 'hybrid'}")

if __name__ == '__main__':
    main()
