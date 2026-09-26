#!/usr/bin/env python3
"""Archive an explicitly selected, registered linked worktree without data loss.

archive performs no deletion. remove is a separate explicit command that checks
its receipt, the actual restored bytes, current original bytes, git index/diffs,
and the named preservation ref again before invoking git worktree remove.
Private receipts include paths and untracked files; never publish them blindly.
This is a regular-file/Git-state backup, not an ACL/NTFS ADS or disk image backup.
"""
from __future__ import annotations
import argparse, datetime, hashlib, json, os, shutil, stat, subprocess, zipfile
from pathlib import Path, PurePosixPath

CHUNK = 4 * 1024 * 1024

def need(ok: bool, message: str) -> None:
    if not ok:
        raise RuntimeError(message)

def sha(path: Path) -> str:
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def git(repo: Path, *args: str) -> bytes:
    return subprocess.check_output(['git', '-C', str(repo), *args], stderr=subprocess.PIPE)

def text(repo: Path, *args: str) -> str:
    return git(repo, *args).decode('utf-8').strip()

def stamp() -> str:
    return datetime.datetime.now(datetime.timezone.utc).isoformat()

def atomic_json(path: Path, data: dict) -> None:
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)

def regular_tree(root: Path) -> tuple[dict[str, Path], list[str]]:
    """Do not follow symlinks, NTFS junctions, devices, or other reparse points."""
    files: dict[str, Path] = {}
    directories: list[str] = []
    pending = [root]
    while pending:
        folder = pending.pop()
        info = folder.lstat()
        need(not stat.S_ISLNK(info.st_mode) and not getattr(info, 'st_file_attributes', 0) & 0x400,
             'Link/reparse directory is not supported: ' + str(folder))
        for child in sorted(folder.iterdir()):
            info = child.lstat()
            need(not stat.S_ISLNK(info.st_mode) and not getattr(info, 'st_file_attributes', 0) & 0x400,
                 'Link/reparse entry is not supported: ' + str(child))
            name = child.relative_to(root).as_posix()
            need(':' not in name and '\\' not in name, 'Unsupported filename: ' + name)
            if stat.S_ISDIR(info.st_mode):
                directories.append(name)
                pending.append(child)
            else:
                need(stat.S_ISREG(info.st_mode), 'Non-regular file: ' + name)
                files[name] = child
    return files, sorted(directories)

def state(repo: Path, source: Path, expected: str, preserve_ref: str) -> dict[str, bytes]:
    need(source != repo and source != Path.cwd().resolve(), 'Cannot remove the controlling worktree')
    need(source.is_dir() and (source / '.git').is_file(), 'Only registered linked worktrees are supported')
    rows = []
    for block in text(repo, 'worktree', 'list', '--porcelain').split('\n\n'):
        row = dict(line.split(' ', 1) if ' ' in line else (line, '') for line in block.splitlines())
        if 'worktree' in row and Path(row['worktree']).resolve() == source:
            rows.append(row)
    need(len(rows) == 1 and 'locked' not in rows[0] and 'prunable' not in rows[0], 'Worktree missing, locked or prunable')
    need(rows[0].get('HEAD') == expected == text(source, 'rev-parse', 'HEAD'), 'Source HEAD changed')
    need(rows[0].get('branch') != 'refs/heads/main', 'Never remove main')
    need(preserve_ref.startswith('refs/tags/archive/'), 'A named archival tag is mandatory')
    need(text(repo, 'rev-parse', preserve_ref + '^{commit}') == expected, 'Preservation ref does not match HEAD')
    need(not git(source, 'ls-files', '-u'), 'Unresolved merges require manual preservation')
    gitdir = Path(text(source, 'rev-parse', '--absolute-git-dir'))
    for marker in ['index.lock', 'MERGE_HEAD', 'CHERRY_PICK_HEAD', 'REVERT_HEAD', 'rebase-merge', 'rebase-apply']:
        need(not (gitdir / marker).exists(), 'Git operation in progress: ' + marker)
    return {
        'status.z': git(source, 'status', '--porcelain=v1', '-z', '--untracked-files=all'),
        'staged.patch': git(source, 'diff', '--cached', '--binary', '--full-index'),
        'unstaged.patch': git(source, 'diff', '--binary', '--full-index'),
        'index-entries.z': git(source, 'ls-files', '--stage', '-z'),
        'index': (gitdir / 'index').read_bytes() if (gitdir / 'index').is_file() else b'',
    }

def verify_files(root: Path, manifest: dict) -> None:
    files, dirs = regular_tree(root)
    need(set(files) == set(manifest['files']) and dirs == manifest['directories'], 'File/directory set changed: ' + str(root))
    for name, row in manifest['files'].items():
        p = files[name]
        need(p.stat().st_size == row['bytes'] and sha(p) == row['sha256'], 'Content changed: ' + str(p))

def same_git_state(current: dict[str, bytes], expected: dict) -> None:
    # git status may rewrite the index stat-cache; logical stage entries + patches
    # are authoritative. Raw index bytes are retained for recovery, not equality.
    for name in ['status.z', 'staged.patch', 'unstaged.patch', 'index-entries.z']:
        need(hashlib.sha256(current[name]).hexdigest() == expected[name], 'Git state changed: ' + name)

def archive(repo: Path, source: Path, output: Path, expected: str, preserve_ref: str) -> dict:
    repo, source, output = repo.resolve(), source.resolve(), output.resolve()
    need(not output.is_relative_to(source) and not source.is_relative_to(output), 'Archive and source must not nest')
    need(not output.exists(), 'Output already exists; preserve previous receipts')
    before = state(repo, source, expected, preserve_ref)
    files, dirs = regular_tree(source)
    total = sum(p.stat().st_size for p in files.values())
    output.parent.mkdir(parents=True, exist_ok=True)
    need(shutil.disk_usage(output.parent).free > total * 2 + 256 * 1024**2, 'Insufficient space for archive plus independent restoration')
    output.mkdir()
    manifest = {'schema': 'dlss5-worktree-backup-v1', 'source': str(source), 'repository': str(repo),
                'head': expected, 'preservation_ref': preserve_ref, 'started_utc': stamp(),
                'directories': dirs, 'files': {}, 'git_state': {k: hashlib.sha256(v).hexdigest() for k, v in before.items()},
                'source_bytes': total, 'scope': 'regular files, empty directories and Git index/patches; excludes ACL and NTFS ADS'}
    pending = output / 'worktree.partial.zip'
    with zipfile.ZipFile(pending, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=1, allowZip64=True) as z:
        for name in dirs:
            z.writestr('files/' + name + '/', b'')
        for number, (name, p) in enumerate(sorted(files.items()), 1):
            old = p.stat()
            digest = hashlib.sha256()
            with p.open('rb') as src, z.open('files/' + name, 'w', force_zip64=True) as dst:
                while block := src.read(CHUNK):
                    digest.update(block)
                    dst.write(block)
            new = p.stat()
            need((old.st_size, old.st_mtime_ns) == (new.st_size, new.st_mtime_ns), 'Source modified while reading: ' + name)
            manifest['files'][name] = {'bytes': new.st_size, 'sha256': digest.hexdigest()}
            if number % 1500 == 0:
                print('archived', number, '/', len(files), source.name, flush=True)
        for name, data in before.items():
            z.writestr('git-state/' + name, data)
        z.writestr('manifest.json', json.dumps(manifest, ensure_ascii=False, indent=2).encode('utf-8'))
    same_git_state(state(repo, source, expected, preserve_ref), manifest['git_state'])
    restored = output / 'restore-check'
    restored.mkdir()
    with zipfile.ZipFile(pending) as z:
        need(z.testzip() is None, 'ZIP CRC failure')
        names = z.namelist()
        need(len(names) == len(set(names)), 'Duplicate ZIP entries')
        for item in z.infolist():
            parts = PurePosixPath(item.filename)
            need(not parts.is_absolute() and '..' not in parts.parts and ':' not in item.filename and '\\' not in item.filename, 'Unsafe ZIP path')
            need(not stat.S_ISLNK(item.external_attr >> 16), 'ZIP link is not supported')
            z.extract(item, restored)
    verify_files(restored / 'files', manifest)
    verify_files(source, manifest)
    for name, digest in manifest['git_state'].items():
        need(sha(restored / 'git-state' / name) == digest, 'Restored Git state differs')
    final = output / 'worktree.zip'
    pending.replace(final)
    atomic_json(output / 'manifest.json', manifest)
    receipt = {'schema': 'dlss5-worktree-retirement-v1', 'status': 'verified-not-removed', 'verified_utc': stamp(),
               'source': str(source), 'repository': str(repo), 'head': expected, 'preservation_ref': preserve_ref,
               'archive': str(final), 'archive_sha256': sha(final), 'manifest_sha256': sha(output / 'manifest.json'),
               'restored': str(restored), 'files': len(files), 'source_bytes': total, 'archive_bytes': final.stat().st_size}
    atomic_json(output / 'receipt.json', receipt)
    print('VERIFIED, ORIGINAL RETAINED', source.name, len(files), total, flush=True)
    return receipt

def remove(repo: Path, receipt_path: Path, expected_source: Path) -> dict:
    repo, receipt_path, source = repo.resolve(), receipt_path.resolve(), expected_source.resolve()
    out = receipt_path.parent
    receipt = json.loads(receipt_path.read_text(encoding='utf-8'))
    need(receipt['status'] == 'verified-not-removed', 'Receipt is not an unused verified archive')
    need(Path(receipt['source']).resolve() == source and Path(receipt['repository']).resolve() == repo, 'Explicit source/repository mismatch')
    zpath = out / 'worktree.zip'
    need(Path(receipt['archive']).resolve() == zpath and sha(zpath) == receipt['archive_sha256'], 'Archive changed')
    need(sha(out / 'manifest.json') == receipt['manifest_sha256'], 'Manifest changed')
    manifest = json.loads((out / 'manifest.json').read_text(encoding='utf-8'))
    restore = out / 'restore-check'
    need(Path(receipt['restored']).resolve() == restore, 'Restore directory mismatch')
    same_git_state(state(repo, source, receipt['head'], receipt['preservation_ref']), manifest['git_state'])
    verify_files(restore / 'files', manifest)
    verify_files(source, manifest)
    # Caller must coordinate other agents/processes. No command can prevent a
    # concurrent write between this final check and Git removing the worktree.
    free_before = shutil.disk_usage(source).free
    r = subprocess.run(['git', '-C', str(repo), 'worktree', 'remove', '--force', str(source)], capture_output=True)
    (out / 'removal.log').write_bytes(r.stdout + r.stderr)
    need(r.returncode == 0 and not source.exists(), 'Git worktree removal failed; archive retained')
    receipt.update(status='removed-after-verified-restoration', removed_utc=stamp(),
                   source_drive_free_before=free_before, source_drive_free_after=shutil.disk_usage(source.parent).free,
                   local_branch_deleted=False)
    atomic_json(receipt_path, receipt)
    # The validated ZIP and manifest remain. Only this newly created restore
    # duplicate is removed, never the sole archive or arbitrary parent directory.
    shutil.rmtree(restore)
    receipt['restore_duplicate_removed'] = True
    atomic_json(receipt_path, receipt)
    print('REMOVED AFTER VERIFIED RESTORATION', source.name, receipt['source_bytes'], flush=True)
    return receipt

def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=['archive', 'remove'])
    p.add_argument('--repo', required=True, type=Path)
    p.add_argument('--source', required=True, type=Path)
    p.add_argument('--output', type=Path)
    p.add_argument('--head')
    p.add_argument('--preserve-ref')
    p.add_argument('--receipt', type=Path)
    a = p.parse_args()
    if a.action == 'archive':
        need(a.output is not None and a.head is not None and a.preserve_ref is not None, 'archive requires output/head/preserve-ref')
        archive(a.repo, a.source, a.output, a.head, a.preserve_ref)
    else:
        need(a.receipt is not None, 'remove requires an exact receipt and source')
        remove(a.repo, a.receipt, a.source)

if __name__ == '__main__':
    main()
