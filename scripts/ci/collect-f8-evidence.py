"""Retain F8 attempt logs under build/, where the existing CI uploader looks.

Keep the evidence artifact's existing root layout. Never collect screenshots,
ZIPs, executables, DLLs or arbitrary user folders. No receipt denotes a pass.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOTS = ('f8-controller-v4', 'f8-gpu-v4')
SUFFIXES = ('.json', '.log', '.cpp', '.h')


def linked(path):
    return path.is_symlink() or getattr(path, 'is_junction', lambda: False)()


def collect(root: Path, output: Path, *, file_limit=8*1024*1024, total_limit=64*1024*1024):
    output.mkdir(parents=True, exist_ok=False)
    receipt = {'schema': 'dlss5-f8-evidence-collection-v1', 'roots': {}, 'files': [],
               'scope': 'Test process evidence only; original gates decide acceptance.'}
    copied = 0
    for name in ROOTS:
        source = root/'test-output'/name
        if linked(source) or linked(root/'test-output'):
            raise ValueError('Linked F8 evidence root')
        if not source.exists():
            receipt['roots'][name] = 'not-produced'
            continue
        receipt['roots'][name] = 'present'
        for path in sorted(source.rglob('*')):
            if linked(path):
                raise ValueError('Linked F8 evidence entry')
            if not path.is_file() or path.suffix not in SUFFIXES:
                continue
            relative = path.relative_to(source)
            size = path.stat().st_size
            entry = {'path': (Path(name)/relative).as_posix(), 'bytes': size}
            if size > file_limit or copied+size > total_limit:
                entry['status'] = 'omitted-size-limit'
            else:
                # The established uploader retains .txt, not .cpp/.h. Preserve
                # generated source bytes exactly while giving them a text suffix.
                destination = output/name/relative
                if path.suffix in ('.cpp', '.h'):
                    destination = destination.with_name(destination.name+'.txt')
                destination.parent.mkdir(parents=True, exist_ok=True)
                with path.open('rb') as src, destination.open('xb') as dst:
                    shutil.copyfileobj(src, dst)
                digest = hashlib.sha256(destination.read_bytes()).hexdigest()
                if destination.stat().st_size != size:
                    raise ValueError('F8 evidence changed during collection')
                entry.update(status='copied', sha256=digest,
                             retained=destination.relative_to(output).as_posix())
                copied += size
            receipt['files'].append(entry)
    (output/'collection.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    collect(args.root, args.output)
