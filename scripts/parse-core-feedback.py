#!/usr/bin/env python3
"""Verify core feedback and an optional paired image supplement without extraction.

CRC and SHA256 prove byte consistency, not trusted authorship. Core archives are
bounded to 8 MiB on disk and expanded; optional image archives retain their own
512 MiB budget and are validated by the existing sequence parser. A partial core
with unavailable artifact identity remains inspectable on its own, but cannot
establish a verified pair merely because both source IDs are absent or unknown.
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
import zlib

LIMIT = 8 * 1024 * 1024
META_LIMIT = 512 * 1024
ENTRY = re.compile(r"^logs/(nr-current|nr-previous|reshade-addon|reshade-game|reshade-module|carrier)-summary\.txt$")
SESSION = re.compile(r"^DLSS5_CORE_[0-9]+_[0-9]+_[0-9]+$")
CURRENT_LOG = 'logs/nr-current-summary.txt'


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


def reject_constant(value):
    raise FormatError(f'non-JSON manifest constant: {value}')


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


def flag(info, key, default=None):
    value = info.get(key, default)
    require(value in ('0', '1'), f'invalid or missing metadata flag: {key}')
    return value == '1'


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
            require(entry.orig_filename == entry.filename, 'archive member was silently normalized')
            safe_name(entry.filename)
            key = entry.filename.casefold()
            require(key not in seen, 'duplicate archive name')
            seen.add(key)
            mode = stat.S_IFMT(entry.external_attr >> 16)
            require(not entry.is_dir() and mode in (0, stat.S_IFREG), 'nonregular member')
            require(not entry.flag_bits & 1, 'encrypted member')
            require(entry.compress_type in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED), 'unsupported compression')
            require(entry.filename in ('manifest.json', 'capture-info.txt') or ENTRY.fullmatch(entry.filename),
                    'core contains unexpected image, raw log or payload')
            maximum = META_LIMIT if entry.filename == 'manifest.json' else 32768 if entry.filename == 'capture-info.txt' else 256 * 1024
            require(entry.file_size <= maximum, 'per-entry byte limit exceeded')
            # ZipFile.read verifies the member CRC, after the expanded-size gate.
            payloads[entry.filename] = archive.read(entry)
    require('manifest.json' in payloads and 'capture-info.txt' in payloads, 'core metadata missing')
    manifest = json.loads(payloads['manifest.json'].decode('utf-8'), object_pairs_hook=unique_json,
                          parse_constant=reject_constant)
    require(isinstance(manifest, dict) and manifest.get('format') == 'DLSS5-DiagnosticBundle-v1', 'wrong manifest format')
    for key in ('complete', 'required_complete'):
        require(type(manifest.get(key)) is bool, f'manifest {key} must be a JSON boolean')
    rejected = manifest.get('input_rejections', 0)
    require(type(rejected) is int and rejected >= 0, 'invalid input rejection count')
    info_text = payloads['capture-info.txt'].decode('utf-8')
    require(manifest.get('metadata_entry') == 'capture-info.txt' and manifest.get('metadata_text') == info_text,
            'manifest metadata differs')
    records = manifest.get('entries')
    require(isinstance(records, list) and len(records) <= 64, 'invalid manifest entries')
    included, declared = set(), set()
    required_missing = False
    missing = []
    incomplete_scans = []
    current_record = None
    for record in records:
        require(isinstance(record, dict) and isinstance(record.get('name'), str), 'invalid manifest record')
        name = record['name']
        safe_name(name)
        require(name.casefold() not in declared, 'duplicate manifest entry')
        declared.add(name.casefold())
        require(type(record.get('required')) is bool, 'record required must be a JSON boolean')
        if name == CURRENT_LOG:
            current_record = record
        status = record.get('status')
        require(status in ('included', 'missing'), 'core entries must be copied summaries or missing records')
        if status == 'missing':
            require(name not in payloads, 'missing entry has payload')
            reason = record.get('reason')
            require(isinstance(reason, str) and bool(reason.strip()), 'missing reason must be nonempty text')
            required_missing |= record['required']
            missing.append(record)
            continue
        require(record.get('kind') == 'copy' and ENTRY.fullmatch(name), 'unexpected copied core member')
        data = payloads.get(name)
        require(type(record.get('bytes')) is int and record['bytes'] >= 0, 'invalid payload byte count')
        require(data is not None and record['bytes'] == len(data), 'missing payload or size mismatch')
        require(record.get('sha256') == hashlib.sha256(data).hexdigest(), 'summary SHA256 mismatch')
        text = data.decode('ascii')
        require(text.startswith('format=DLSS5-LogSummary-v1\n'), 'summary schema or encoding invalid')
        # Only parse the summary header; free-text log excerpts can contain
        # repeated keys and must not be interpreted as schema fields.
        summary_info = metadata(text.partition('\n[head]')[0])
        if not flag(summary_info, 'scan.complete'):
            incomplete_scans.append(name)
            required_missing |= record['required']
        included.add(name)
    require(set(payloads) == included | {'manifest.json', 'capture-info.txt'}, 'unlisted archive member')
    # The collector always adds this record, including an explicit required
    # missing record on failure. Omitting it must never become a complete core.
    require(current_record is not None and current_record['required'], 'required current-log declaration missing')
    require(not required_missing or not manifest['required_complete'], 'false required completeness')
    require(not manifest['complete'] or
            (manifest['required_complete'] and not missing and not incomplete_scans and rejected == 0),
            'complete archive contradicts missing, incomplete or rejected inputs')
    info = metadata(info_text)
    require(info.get('format') == 'DLSS5-Core-v1' and info.get('capture.kind') == 'core', 'not a core package')
    require(bool(SESSION.fullmatch(info.get('session', ''))), 'invalid core session')
    for key in ('config.writesToIni', 'images.requested', 'gpu.readbackRequested'):
        require(info.get(key) == '0', f'core side effect declared: {key}')
    require(info.get('core.zipLimitBytes') == str(LIMIT), 'core budget declaration differs')
    snapshot_valid = flag(info, 'snapshot.valid')
    cancelled = flag(info, 'collection.cancelled', '0')
    truncated = flag(info, 'collection.metadataTruncated', '0')
    require(not manifest['required_complete'] or (snapshot_valid and not cancelled and not truncated),
            'required completeness contradicts snapshot, cancellation or metadata coverage')
    return {'session': info['session'], 'manifest_verified': True,
            'required_complete': manifest['required_complete'], 'complete': manifest['complete'],
            'source_commit': info.get('artifact.sourceCommit'), 'snapshot_valid': snapshot_valid,
            'missing': missing, 'incomplete_scans': incomplete_scans, 'metadata': info}


def known_hash(info, key, digits):
    value = info.get(key, '')
    require(bool(re.fullmatch(r'[0-9a-fA-F]{' + str(digits) + '}', value)) and set(value) != {'0'},
            f'known {key} required for verified pairing; inspect core alone for partial evidence')
    return value.lower()


def config_id(info, key):
    value = info.get(key, '')
    require(bool(re.fullmatch(r'[0-9]{1,10}', value)) and int(value) <= 0xffffffff,
            f'invalid or missing configuration identity: {key}')
    return int(value)


def inspect_pair(core: Path, image: Path | None = None):
    result = {'core': inspect_core(core)}
    if image is None:
        return result
    require(image.stat().st_size <= 512 * 1024 * 1024, 'image ZIP exceeds budget')
    spec = importlib.util.spec_from_file_location('nr_image_parser', Path(__file__).with_name('parse-f8-bundle.py'))
    require(spec is not None and spec.loader is not None, 'image parser unavailable')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    parsed = module.inspect_bundle(image)  # wire format, manifest, CRC, budgets, sequence identity
    require(parsed.get('manifest_verified'), 'image manifest required for pairing')
    with zipfile.ZipFile(image) as archive:
        entry = archive.getinfo('capture-info.txt')
        require(entry.file_size <= 32768, 'image metadata oversized')
        info = metadata(archive.read(entry).decode('utf-8'))
    require(info.get('capture.kind') == 'image-supplement', 'image was not collected with paired core support')
    core_info = result['core']['metadata']
    require(info.get('core.session') == result['core']['session'], 'core/image session mismatch')
    for key, digits in (('artifact.sourceCommit', 40), ('artifact.addonSha256', 64)):
        require(known_hash(info, key, digits) == known_hash(core_info, key, digits), f'core/image {key} differs')
    require(flag(info, 'artifact.sourceDirty') == flag(core_info, 'artifact.sourceDirty'), 'core/image source dirty state differs')
    require(config_id(info, 'core.config') == config_id(core_info, 'requested.config'), 'core snapshot configuration differs')
    # core.complete in the image is the collector's required-evidence result,
    # not manifest.complete (optional logs may be absent). It is also NOT an
    # assertion that the later image sequence or original SR was complete.
    require(flag(info, 'core.complete') == result['core']['required_complete'], 'image core completeness reference differs')
    result['pair_verified'] = True
    result['verification_scope'] = 'Byte-consistent manifests and matching recorded session/artifact identities; not trusted authorship.'
    result['image'] = parsed
    result['warning'] = 'Paired session is not a same-frame claim. Images may follow the core observation interval; partial evidence remains partial.'
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('core', type=Path)
    p.add_argument('--image', type=Path)
    args = p.parse_args()
    try:
        print(json.dumps(inspect_pair(args.core, args.image), ensure_ascii=False, indent=2))
    except (OSError, ValueError, KeyError, TypeError, RecursionError, UnicodeError, zipfile.BadZipFile, zlib.error) as error:
        print(f'INVALID FEEDBACK: {error}', file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
