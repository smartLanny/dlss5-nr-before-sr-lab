"""Retain test-process evidence; never interpret an exit as GPU acceptance."""
from __future__ import annotations

import hashlib
import json
import locale
from pathlib import Path
import re
import subprocess
import sys

TAIL_BYTES = 64 * 1024


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def run_logged(command, directory: Path, label: str, *, check=False,
               capture_output=False, text=False, inputs=None):
    """Execute unchanged argv/environment/cwd; preserve logs before raising.

    stdout/stderr stream to files, not pipes or unbounded in-memory buffers.
    A receipt records process completion, NOT test or archive acceptance. Labels
    are single-use per attempt; evidence collisions fail before launching a child.
    The original caller still owns expected-exit/FAIL and archive assertions.
    """
    if not re.fullmatch(r'[A-Za-z0-9_-]+', label):
        raise ValueError('Invalid F8 evidence label')
    directory = Path(directory)
    command = [str(arg) for arg in command]
    paths = {kind: directory / f'{label}.{kind}'
             for kind in ('stdout.log', 'stderr.log', 'stdout-tail.txt',
                          'stderr-tail.txt', 'process.json')}
    receipt = {
        'schema': 1, 'scope': 'process evidence only; not validation acceptance',
        'command': command, 'cwd': str(Path.cwd()), 'state': 'prepared',
        'returncode': None,
        'inputs_sha256': {name: sha256(Path(path)) for name, path in (inputs or {}).items()},
    }
    executable = Path(command[0])
    receipt['executable_sha256'] = sha256(executable) if executable.is_file() else None
    # Reserve every evidence file exclusively before launching. Existing evidence
    # is never truncated, even if a stale caller accidentally repeats a label.
    for path in paths.values():
        with path.open('x', encoding='utf-8'):
            pass
    def write_receipt():
        paths['process.json'].write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    write_receipt()
    try:
        with paths['stdout.log'].open('wb') as stdout, paths['stderr.log'].open('wb') as stderr:
            result = subprocess.run(command, stdout=stdout, stderr=stderr, check=False)
        receipt.update(state='completed', returncode=result.returncode)
    except OSError as error:
        receipt.update(state='launch-error', error_type=type(error).__name__, errno=error.errno)
        raise
    except BaseException as error:
        receipt.update(state='interrupted', error_type=type(error).__name__)
        raise
    finally:
        for name in ('stdout', 'stderr'):
            path = paths[f'{name}.log']
            receipt[f'{name}_bytes'] = path.stat().st_size
            receipt[f'{name}_sha256'] = sha256(path)
            with path.open('rb') as stream:
                stream.seek(max(0, path.stat().st_size - TAIL_BYTES))
                tail = stream.read(TAIL_BYTES)
            paths[f'{name}-tail.txt'].write_bytes(tail)
        write_receipt()
        # Console decoding cannot invalidate the retained raw logs or receipt.
        for name in ('stdout', 'stderr'):
            tail = paths[f'{name}-tail.txt'].read_bytes()
            if tail:
                try:
                    print(f'F8 {label} {name} tail:\n' + tail.decode('utf-8', errors='replace'),
                          file=sys.stderr if name == 'stderr' else sys.stdout, flush=True)
                except (OSError, UnicodeError):
                    pass
    if capture_output:
        result.stdout = paths['stdout.log'].read_bytes()
        result.stderr = paths['stderr.log'].read_bytes()
        if text:
            encoding = locale.getpreferredencoding(False)
            result.stdout = result.stdout.decode(encoding).replace('\r\n', '\n').replace('\r', '\n')
            result.stderr = result.stderr.decode(encoding).replace('\r\n', '\n').replace('\r', '\n')
    if check:
        result.check_returncode()
    return result
