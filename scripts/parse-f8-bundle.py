#!/usr/bin/env python3
"""Independent, bounded reader for F8 sequence-v4 feedback ZIPs.

Never extracts archive paths or treats screenshots as same-frame GPU output.
Only the documented wire format is shared with the production collector.
"""
from __future__ import annotations

import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys
import zipfile

MIB = 1024 * 1024
FILE_LIMIT = 64 * MIB
TOTAL_LIMIT = 512 * MIB
HEADER = struct.Struct('<8s6I')
RECORD = struct.Struct('<2I4Q12IQ')
PLANES = ('original', 'proxy', 'neural', 'composited', 'protection')
CHUNK_NAME = re.compile(r'^groups/(\d+)/(chunk-(\d{3})|overview)\.f8c$')
SETTINGS_NAME = re.compile(r'^groups/(\d+)/settings\.json$')
DISPLAY_NAME = re.compile(r'^groups/(\d+)/display\.png$')
FACE_LABELS = ('current-face-off', 'current-face-on0', 'baseline-face-off', 'baseline-face-on0',
               'baseline-tone2-face-off', 'baseline-tone2-face-on0', 'baseline-structure2-face-off',
               'baseline-structure2-face-on0', 'current-face-on0-repeat')
KEYS = ('index', 'valid', 'session', 'frame', 'instance', 'feature_generation',
        'config_generation', 'source_width', 'source_height', 'model_width',
        'model_height', 'roi_x', 'roi_y', 'width', 'height', 'stride',
        'codec_flags', 'metadata_bytes', 'pixel_bytes')
IDENTITY = ('session', 'instance', 'feature_generation', 'config_generation',
            'source_width', 'source_height', 'model_width', 'model_height',
            'roi_x', 'roi_y', 'width', 'height', 'stride')
FRAME_METADATA = {
    'schema', 'mode', 'codecFlags', 'sourceFormat', 'resultFormat',
    'sourceBaseX', 'sourceBaseY', 'destinationBaseX', 'destinationBaseY',
    'regionWidth', 'regionHeight', 'sinceReset', 'evaluation', 'warmedUp',
    'resetKnown', 'reset', 'gameJitterValid', 'nrJitterForwarded', 'gameMvValid',
    'gameJitterX', 'gameJitterY', 'nrJitterX', 'nrJitterY',
    'gameMvX', 'gameMvY', 'nrMvX', 'nrMvY', 'depthWidth', 'depthHeight',
    'mvWidth', 'mvHeight', 'hasPreExposure', 'hasExposureScale',
    'preExposure', 'exposureScale', 'gpuRawExposure', 'gpuResolvedExposure',
    'gpuSourceToCanonical', 'gpuPreExposure', 'transferEffective', 'colourEffective',
    'frameConfidence', 'detailStability', 'lightingLock', 'edgeGuard',
    'requested', 'actual', 'appliedTuning',
}
CONFIG_METADATA = {'enabled', 'mode', 'style', 'autoMask', 'uiCorrection', 'workMode',
                   'jitterMode', 'transfer', 'postTransfer', 'colour', 'modelIntensity',
                   'skin', 'localTone', 'localStructure', 'proxyScale', 'customWorkScale',
                   'maxLumaRatio', 'lightingLock', 'edgeGuard', 'detailStability', 'proxyCurve'}


class FormatError(ValueError):
    pass


class DigestReader:
    def __init__(self, stream):
        self.stream = stream
        self.digest = hashlib.sha256()

    def read(self, size=-1):
        block = self.stream.read(size)
        self.digest.update(block)
        return block


def require(condition, reason):
    if not condition:
        raise FormatError(reason)


def read_exact(stream, size):
    data = stream.read(size)
    require(len(data) == size, 'truncated record')
    return data


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'duplicate metadata key')
        result[key] = value
    return result


def reject_constant(value):
    raise FormatError(f'non-JSON metadata constant: {value}')


def safe_name(name):
    parts = name.split('/')
    require(name and not name.startswith('/') and '\\' not in name
            and ':' not in name and all(p not in ('', '.', '..') for p in parts),
            f'unsafe archive member: {name!r}')


def plane_digest(stream, size):
    digest = hashlib.sha256()
    nonfinite = 0
    while size:
        block = read_exact(stream, min(size, 64 * 1024))
        digest.update(block)
        values = array.array('f')
        values.frombytes(block)
        if sys.byteorder != 'little':
            values.byteswap()
        nonfinite += sum(not math.isfinite(x) for x in values)
        size -= len(block)
    return {'sha256': digest.hexdigest(), 'nonfinite_values': nonfinite}


def parse_chunk(stream, filename, file_bytes):
    match = CHUNK_NAME.fullmatch(filename)
    require(match is not None, f'unrecognized chunk name: {filename}')
    expected_group = int(match[1])
    expected_chunk = 4 if match[2] == 'overview' else int(match[3])
    magic, version, group, chunk, count, target, planes = HEADER.unpack(read_exact(stream, HEADER.size))
    require((magic, version, group, chunk, planes) ==
            (b'F8CHK004', 4, expected_group, expected_chunk, 5) and 1 <= target <= 16,
            f'header/name/schema mismatch: {filename}')
    require(0 <= chunk <= 4 and 0 < count <= (1 if chunk == 4 else 4), 'invalid chunk count')
    consumed = HEADER.size
    records = []
    seen = set()
    for _ in range(count):
        record = dict(zip(KEYS, RECORD.unpack(read_exact(stream, RECORD.size))))
        consumed += RECORD.size
        index = record['index']
        require(index not in seen and
                (index == 16 if chunk == 4 else chunk * 4 <= index < chunk * 4 + 4),
                'duplicate or out-of-chunk record index')
        seen.add(index)
        require(record['valid'] in (0, 1), 'invalid validity flag')
        metadata_bytes, pixel_bytes = record['metadata_bytes'], record['pixel_bytes']
        require(0 < metadata_bytes <= MIB, 'invalid metadata length')
        require(consumed + metadata_bytes + pixel_bytes <= file_bytes, 'record exceeds chunk')
        metadata = json.loads(read_exact(stream, metadata_bytes).decode('utf-8'),
                              object_pairs_hook=unique_object, parse_constant=reject_constant)
        require(isinstance(metadata, dict), 'metadata must be an object')
        if 'codecFlags' in metadata:
            require(metadata['codecFlags'] == record['codec_flags'], 'codec flags contradict record header')
        if 'schema' in metadata:
            require(metadata['schema'] == 4, 'metadata schema contradicts chunk')
        record['metadata'] = metadata
        record['roi_target'] = target
        missing_metadata = sorted(FRAME_METADATA - metadata.keys())
        for kind in ('requested', 'actual'):
            config = metadata.get(kind)
            if isinstance(config, dict):
                missing_metadata.extend(f'{kind}.{key}' for key in sorted(CONFIG_METADATA - config.keys()))
            else:
                missing_metadata.append(f'{kind} object')
        record['missing_metadata'] = missing_metadata
        consumed += metadata_bytes
        if record['valid']:
            w, h, stride = (record[k] for k in ('width', 'height', 'stride'))
            require(w > 0 and h > 0 and stride > 0, 'invalid plane extent')
            require(all(record[k] > 0 for k in ('source_width', 'source_height', 'model_width', 'model_height')),
                    'missing source/model extent')
            require(pixel_bytes == w * h * 5 * 16, 'pixel byte/plane extent mismatch')
            require(record['roi_x'] + (w - 1) * stride < record['source_width'] and
                    record['roi_y'] + (h - 1) * stride < record['source_height'],
                    'sample grid outside source')
            require(index == 16 or stride == 1, 'ROI frames must not be downsampled')
            record['planes'] = {name: plane_digest(stream, w * h * 16) for name in PLANES}
        else:
            require(pixel_bytes == 0, 'missing record contains pixels')
        consumed += pixel_bytes
        records.append(record)
    require(consumed == file_bytes and not stream.read(1), 'trailing bytes in chunk')
    require([r['index'] for r in records] == sorted(seen), 'record order is not increasing')
    return records


def inspect_bundle(path):
    result = {'format': 'f8-independent-analysis-v1', 'archive': str(Path(path).resolve()),
              'sequence_schema': 4, 'groups': [], 'warnings': [], 'files': []}
    groups = {}
    file_digests = {}
    manifest_data = None
    metadata_data = None
    settings_by_group = {}
    display_sizes = {}
    with zipfile.ZipFile(path) as archive:
        entries = archive.infolist()
        require(len(entries) <= 8192, 'too many archive entries')
        names = set()
        total = 0
        for entry in entries:
            require(entry.orig_filename == entry.filename,
                    'archive member was silently normalized')
            if entry.is_dir():
                safe_name(entry.filename.rstrip('/'))
                continue
            safe_name(entry.filename)
            require(entry.filename.casefold() not in names, 'duplicate archive member')
            names.add(entry.filename.casefold())
            require(not entry.flag_bits & 1, 'encrypted member')
            require(entry.file_size <= FILE_LIMIT, 'member exceeds 64 MiB')
            total += entry.file_size
            require(total <= TOTAL_LIMIT, 'archive exceeds 512 MiB uncompressed')
            result['files'].append({'name': entry.filename, 'bytes': entry.file_size})
            if entry.filename.endswith('.f8c'):
                with archive.open(entry) as stream:
                    reader = DigestReader(stream)
                    records = parse_chunk(reader, entry.filename, entry.file_size)
                    file_digests[entry.filename] = (entry.file_size, reader.digest.hexdigest())
                group = int(CHUNK_NAME.fullmatch(entry.filename)[1])
                groups.setdefault(group, []).extend(records)
            else:
                # Read to EOF to validate ZIP CRC without loading logs/screenshots into memory.
                with archive.open(entry) as stream:
                    reader = DigestReader(stream)
                    saved = bytearray()
                    settings_name = SETTINGS_NAME.fullmatch(entry.filename)
                    display_name = DISPLAY_NAME.fullmatch(entry.filename)
                    prefix = bytearray()
                    while block := reader.read(64 * 1024):
                        if len(prefix) < 24:
                            prefix.extend(block[:24-len(prefix)])
                        if entry.filename in ('manifest.json', 'capture-info.txt') or settings_name:
                            saved.extend(block)
                            require(len(saved) <= 2 * MIB, 'manifest/metadata exceeds 2 MiB')
                    file_digests[entry.filename] = (entry.file_size, reader.digest.hexdigest())
                    if entry.filename == 'manifest.json':
                        manifest_data = bytes(saved)
                    if entry.filename == 'capture-info.txt':
                        metadata_data = bytes(saved)
                    if settings_name:
                        settings_by_group[int(settings_name[1])] = json.loads(saved.decode('utf-8'),
                            object_pairs_hook=unique_object, parse_constant=reject_constant)
                    if display_name and len(prefix) == 24 and prefix[:16] == b'\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR':
                        display_sizes[int(display_name[1])] = struct.unpack('>II', prefix[16:24])
        result['uncompressed_bytes'] = total
    result['manifest_verified'] = False
    if manifest_data is not None:
        manifest = json.loads(manifest_data.decode('utf-8-sig'), object_pairs_hook=unique_object,
                              parse_constant=reject_constant)
        require(manifest.get('format') == 'DLSS5-DiagnosticBundle-v1', 'unknown bundle manifest format')
        included = set()
        listed = set()
        for entry in manifest.get('entries', []):
            name = entry.get('name')
            require(isinstance(name, str) and name not in listed, 'duplicate/invalid manifest name')
            listed.add(name)
            status = entry.get('status')
            require(status in ('included', 'included-truncated', 'hashed', 'missing'),
                    f'unknown manifest entry status: {status!r}')
            if status in ('included', 'included-truncated'):
                expected = (entry.get('bytes'), entry.get('sha256', '').lower())
                require(file_digests.get(name) == expected, f'manifest SHA256/size mismatch: {name}')
                included.add(name)
            else:
                require(name not in file_digests,
                        'manifest declares a hash-only or missing file present')
        require(manifest.get('metadata_entry') == 'capture-info.txt' and metadata_data is not None,
                'missing metadata entry')
        require(metadata_data.decode('utf-8') == manifest.get('metadata_text'), 'metadata text mismatch')
        require(set(file_digests) == included | {'manifest.json', 'capture-info.txt'},
                'archive contains unlisted members')
        result['manifest_verified'] = True
        result['bundle_declares_complete'] = manifest.get('complete')
        result['input_rejections'] = manifest.get('input_rejections', 0)
        result['missing_files'] = [e for e in manifest['entries'] if e.get('status') == 'missing']
    else:
        result['warnings'].append('SHA256 manifest missing; only ZIP CRC and wire structure were checked.')
    for group, records in sorted(groups.items()):
        records.sort(key=lambda r: r['index'])
        require(len({r['index'] for r in records}) == len(records), 'duplicate group record')
        roi = [r for r in records if r['index'] < 16]
        valid = [r for r in roi if r['valid']]
        targets = {r['roi_target'] for r in records}
        require(len(targets) == 1, 'group chunks disagree on ROI target')
        target = targets.pop()
        previous = None
        for record in valid:
            if previous:
                require(all(record[k] == previous[k] for k in IDENTITY), 'sequence identity changed')
                # Bit 3 enables periodic diagnostic atomics, not a color contract.
                require((record['codec_flags'] & ~8) == (previous['codec_flags'] & ~8),
                        'sequence color contract changed')
                require(record['index'] == previous['index'] + 1 and
                        record['frame'] == previous['frame'] + 1,
                        'non-contiguous sequence presented as one segment')
            previous = record
        complete = len(valid) == target and [r['index'] for r in valid] == list(range(target))
        metadata_complete = bool(valid) and all(not r['missing_metadata'] for r in valid)
        overview = next((r for r in records if r['index'] == 16 and r['valid']), None)
        if not complete:
            result['warnings'].append(f'Group {group}: partial ROI sequence ({len(valid)}/{target}).')
        if not overview and target in (3, 16):
            result['warnings'].append(f'Group {group}: overview missing.')
        if valid and not metadata_complete:
            result['warnings'].append(f'Group {group}: metadata incomplete; do not infer missing settings.')
        result['groups'].append({'group': group, 'complete_sequence': complete,
                                 'metadata_complete': metadata_complete,
                                 'roi_target': target, 'valid_roi_frames': len(valid), 'overview_present': bool(overview),
                                 'records': records})
    if not groups:
        result['warnings'].append('No NR sequence chunks; inspect collection state and missing reasons.')
    info = dict(line.split('=', 1) for line in metadata_data.decode('utf-8').splitlines() if '=' in line) if metadata_data else {}
    if info.get('faceDiagnostic.version') == '1':
        face_complete = result.get('bundle_declares_complete') is True and result.get('input_rejections', 0) == 0
        face_complete &= info.get('groups') == '9' and info.get('config.restored') == '1' and info.get('postConfig.restored') == '1'
        require(info.get('faceDiagnostic.roiTarget') == '1', 'face diagnostic must request one ROI per group')
        face_complete &= 'core/feedback.txt' in file_digests and any(e.get('name') == 'core/paired-archive' and e.get('status') == 'hashed' for e in manifest.get('entries', []))
        anchor = None
        for group, label in enumerate(FACE_LABELS):
            settings = settings_by_group.get(group)
            if not settings:
                face_complete = False
                continue
            require(settings.get('groupName') == label and settings.get('group') == group, 'face group label/order mismatch')
            diagnostic = settings.get('faceDiagnostic', {})
            require(diagnostic.get('roiTarget') == 1, 'face settings must request one ROI')
            image_size = display_sizes.get(group)
            face_complete &= settings.get('complete') == 1 and image_size == (diagnostic.get('displayWidth'), diagnostic.get('displayHeight'))
            records = groups.get(group, [])
            valid = [r for r in records if r['valid']]
            face_complete &= len(valid) == 1 and len(records) == 1
            for record in valid:
                m = record['metadata']; f = m.get('faceDiagnostic', {})
                require(record['index'] == 0 and record['roi_target'] == 1 and record['stride'] == 1 and
                        record['width'] <= 512 and record['height'] <= 512, 'face ROI format mismatch')
                require(m.get('groupName') == label, 'face record group label mismatch')
                for key, header in (('roiX', 'roi_x'), ('roiY', 'roi_y'), ('roiWidth', 'width'), ('roiHeight', 'height'),
                                    ('expectedSourceWidth', 'source_width'), ('expectedSourceHeight', 'source_height'),
                                    ('expectedModelWidth', 'model_width'), ('expectedModelHeight', 'model_height')):
                    require(f.get(key) == record[header] and diagnostic.get(key) == record[header], 'face anchor differs from actual ROI geometry')
                for key, actual in (('expectedSourceBaseX', 'sourceBaseX'), ('expectedSourceBaseY', 'sourceBaseY'),
                                    ('expectedDestinationBaseX', 'destinationBaseX'), ('expectedDestinationBaseY', 'destinationBaseY'),
                                    ('expectedSourceFormat', 'sourceFormat'), ('expectedResultFormat', 'resultFormat'), ('expectedMode', 'mode')):
                    require(f.get(key) == m.get(actual), 'face expected route or colour contract differs from recorded data')
                require(m.get('warmedUp') == 1 and m.get('sinceReset', 0) >= 64 and m.get('reset') == 0,
                        'face data was not stabilized after reset')
                requested, tuning = m['requested'], m['appliedTuning']
                for actual, requested_key in (('intensity', 'modelIntensity'), ('localTone', 'localTone'), ('localStructure', 'localStructure'),
                                               ('style', 'style'), ('autoMask', 'autoMask'), ('uiCorrection', 'uiCorrection')):
                    require(tuning.get(actual) == requested.get(requested_key), 'face actual NR tuning differs from requested group')
                require(tuning.get('skin') == (requested.get('skin') if requested.get('autoMask') else -1), 'face actual skin disable contract differs')
                geometry = tuple(record[k] for k in ('feature_generation', 'source_width', 'source_height', 'model_width', 'model_height',
                                 'roi_x', 'roi_y', 'width', 'height')) + tuple(m[k] for k in ('mode', 'sourceBaseX', 'sourceBaseY',
                                 'destinationBaseX', 'destinationBaseY', 'sourceFormat', 'resultFormat', 'hasPreExposure', 'hasExposureScale'))
                require(anchor is None or anchor == geometry, 'face geometry changed across groups')
                anchor = geometry
        result['face_diagnostic'] = {'version': 1, 'groups_expected': 9, 'complete': bool(face_complete),
                                     'same_frame_display_claim': False, 'native_face_mask_obtained': False}
        if not face_complete:
            result['warnings'].append('Face diagnostic is partial: inspect group settings, geometry, screenshots and paired-core missing reasons.')
    result['warnings'].append('Display screenshots have separate identities; they do not prove same-frame final SR output.')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('zip', type=Path)
    parser.add_argument('--output', type=Path, help='Write JSON report (default: stdout)')
    args = parser.parse_args()
    try:
        result = inspect_bundle(args.zip)
    except (FormatError, OSError, zipfile.BadZipFile, json.JSONDecodeError, UnicodeError, RecursionError) as error:
        print(f'INVALID F8 BUNDLE: {error}', file=sys.stderr)
        return 2
    output = json.dumps(result, ensure_ascii=False, indent=2)
    if args.output:
        args.output.write_text(output + '\n', encoding='utf-8')
    else:
        print(output)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
