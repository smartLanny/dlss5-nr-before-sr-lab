#!/usr/bin/env python3
"""Verify core feedback and an optional paired image supplement without extraction.

CRC and SHA256 prove byte consistency, not trusted authorship. Core archives are
bounded to 8 MiB on disk and expanded; optional image archives retain their own
512 MiB budget and are validated by the existing sequence parser.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import stat
import sys
import zipfile

LIMIT = 8 * 1024 * 1024
META_LIMIT = 512 * 1024
ENTRY = re.compile(r"^logs/(nr-current|nr-previous|reshade-addon|reshade-game|reshade-module)-summary\.txt$")
SESSION = re.compile(r"^DLSS5_CORE_\d+_\d+_\d+$")

class FormatError(ValueError):
    pass

def require(ok, message):
    if not ok:
        raise FormatError(message)

def unique_json(pairs):
    value = {}
    for key, item in pairs:
        require(key not in value, 'duplicate JSON key')
        value[key] = item
    return value

def metadata(text):
    result = {}
    # Resource records and the observation CSV are not key-value properties.
    for line in text.splitlines():
        if '=' not in line or line.startswith(('frame,', 'resource.', 'feature.flags=')):
            continue
        key, value = line.split('=', 1)
        require(key not in result, f'duplicate metadata field: {key}')
        result[key] = value
    return result

def safe_name(name):
    parts = name.split('/')
    require(name and not name.startswith('/') and '\\' not in name and ':' not in name and
            all(p not in ('', '.', '..') and not p.endswith((' ', '.')) for p in parts), 'unsafe member name')
    require(not any(ord(c) < 32 or ord(c) == 127 for c in name), 'control character in member')

def inspect_core(path: Path):
    require(path.stat().st_size <= LIMIT, 'core ZIP exceeds 8 MiB')
    payloads = {}
    with zipfile.ZipFile(path) as archive:
        entries = archive.infolist()
        require(2 <= len(entries) <= 66, 'invalid core entry count')
        require(sum(e.file_size for e in entries) <= LIMIT, 'expanded core exceeds budget')
        seen = set()
        for entry in entries:
            safe_name(entry.filename)
            key = entry.filename.casefold()
            require(key not in seen, 'duplicate archive name')
            seen.add(key)
            require(not entry.is_dir() and not stat.S_ISLNK(entry.external_attr >> 16), 'nonregular member')
            require(not entry.flag_bits & 1, 'encrypted member')
            require(entry.compress_type in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED), 'unsupported compression')
            require(entry.filename in ('manifest.json', 'capture-info.txt') or ENTRY.fullmatch(entry.filename),
                    'core contains unexpected image, raw log or payload')
            maximum = META_LIMIT if entry.filename == 'manifest.json' else 32768 if entry.filename == 'capture-info.txt' else 256 * 1024
            require(entry.file_size <= maximum, 'per-entry byte limit exceeded')
            # ZipFile.read verifies the member CRC, after the expanded-size gate.
            payloads[entry.filename] = archive.read(entry)
    require('manifest.json' in payloads and 'capture-info.txt' in payloads, 'core metadata missing')
    manifest = json.loads(payloads['manifest.json'].decode('utf-8'), object_pairs_hook=unique_json)
    require(isinstance(manifest, dict) and manifest.get('format') == 'DLSS5-DiagnosticBundle-v1', 'wrong manifest format')
    info_text = payloads['capture-info.txt'].decode('utf-8')
    require(manifest.get('metadata_entry') == 'capture-info.txt' and manifest.get('metadata_text') == info_text,
            'manifest metadata differs')
    records = manifest.get('entries')
    require(isinstance(records, list) and len(records) <= 64, 'invalid manifest entries')
    included, declared = set(), set()
    required_missing = False
    for record in records:
        require(isinstance(record, dict) and isinstance(record.get('name'), str), 'invalid manifest record')
        name = record['name']; safe_name(name)
        require(name.casefold() not in declared, 'duplicate manifest entry')
        declared.add(name.casefold())
        status = record.get('status')
        require(status in ('included', 'missing'), 'core entries must be copied summaries or missing records')
        if status == 'missing':
            require(name not in payloads, 'missing entry has payload')
            require(bool(record.get('reason')), 'missing reason absent')
            required_missing |= record.get('required') is True
            continue
        require(record.get('kind') == 'copy' and ENTRY.fullmatch(name), 'unexpected copied core member')
        data = payloads.get(name)
        require(data is not None and record.get('bytes') == len(data), 'missing payload or size mismatch')
        require(record.get('sha256') == hashlib.sha256(data).hexdigest(), 'summary SHA256 mismatch')
        require(data.decode('ascii').startswith('format=DLSS5-LogSummary-v1\n'), 'summary schema or encoding invalid')
        included.add(name)
    require(set(payloads) == included | {'manifest.json', 'capture-info.txt'}, 'unlisted archive member')
    require(not required_missing or manifest.get('required_complete') is False, 'false required completeness')
    info = metadata(info_text)
    require(info.get('format') == 'DLSS5-Core-v1' and info.get('capture.kind') == 'core', 'not a core package')
    require(bool(SESSION.fullmatch(info.get('session', ''))), 'invalid core session')
    for key in ('config.writesToIni', 'images.requested', 'gpu.readbackRequested'):
        require(info.get(key) == '0', f'core side effect declared: {key}')
    require(info.get('core.zipLimitBytes') == str(LIMIT), 'core budget declaration differs')
    return {'session': info['session'], 'manifest_verified': True,
            'required_complete': manifest.get('required_complete') is True,
            'complete': manifest.get('complete') is True,
            'source_commit': info.get('artifact.sourceCommit'),
            'snapshot_valid': info.get('snapshot.valid') == '1',
            'missing': [r for r in records if r.get('status') == 'missing'],
            'metadata': info}

def inspect_pair(core: Path, image: Path | None = None):
    result = {'core': inspect_core(core)}
    if image is None:
        return result
    require(image.stat().st_size <= 512 * 1024 * 1024, 'image ZIP exceeds budget')
    spec = importlib.util.spec_from_file_location('nr_image_parser', Path(__file__).with_name('parse-f8-bundle.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    parsed = module.inspect_bundle(image)  # wire format, manifest, CRC, budgets, sequence identity
    require(parsed.get('manifest_verified'), 'image manifest required for pairing')
    with zipfile.ZipFile(image) as archive:
        entry = archive.getinfo('capture-info.txt')
        require(entry.file_size <= 32768, 'image metadata oversized')
        info = metadata(archive.read(entry).decode('utf-8'))
    require(info.get('capture.kind') == 'image-supplement', 'image was not collected with paired core support')
    require(info.get('core.session') == result['core']['session'], 'core/image session mismatch')
    require(info.get('artifact.sourceCommit') == result['core']['metadata'].get('artifact.sourceCommit'), 'core/image build differs')
    result['pair_verified'] = True
    result['image'] = parsed
    result['warning'] = 'Paired session is not a same-frame claim. Images may follow the core observation interval.'
    return result

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('core', type=Path)
    p.add_argument('--image', type=Path)
    args = p.parse_args()
    try:
        print(json.dumps(inspect_pair(args.core, args.image), ensure_ascii=False, indent=2))
    except (OSError, ValueError, KeyError, TypeError, RecursionError, UnicodeError, zipfile.BadZipFile) as error:
        print(f'INVALID FEEDBACK: {error}', file=sys.stderr)
        return 2
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
