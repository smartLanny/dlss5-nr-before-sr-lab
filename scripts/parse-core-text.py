#!/usr/bin/env python3
"""Inspect an original F8 TXT without treating it as a signed or paired archive.

Read the actual CoreText-v2 framing and summary byte lengths. A log excerpt may
contain metadata-looking lines: only the capture section supplies current fields.
Only explicitly requested scalar fields are returned; repeated window/resource
records and private/unknown metadata never become report fields.
"""
from __future__ import annotations

import hashlib
from pathlib import Path
import re

LIMIT = 2 * 1024 * 1024
SUMMARY_LIMIT = 24 * 1024
MARKER = '\n===== CURRENT CAPTURE METADATA / 本次采集 =====\n'.encode()
NAMES = ('nr-current', 'nr-previous', 'reshade-addon', 'reshade-game', 'reshade-module', 'carrier')
REQUIRED_FIELDS = frozenset(('metadata.format', 'capture.kind', 'session', 'snapshot.valid',
    'config.writesToIni', 'images.requested', 'gpu.readbackRequested',
    'collection.cancelled', 'collection.metadataTruncated'))
MISSING = re.compile(r'evidence\.(' + '|'.join(NAMES) + r')(\.coverage)?=missing; required=([01]); .+')


class FormatError(ValueError):
    pass


def require(condition, message):
    if not condition:
        # Messages deliberately contain no submitted field values or paths.
        raise FormatError(message)


def properties(lines, selected=None):
    result = {}
    for line in lines:
        if '=' not in line:
            continue
        key, value = line.split('=', 1)
        if selected is not None and key not in selected:
            continue
        require(key not in result, 'duplicate scalar field')
        result[key] = value
    return result


def boolean(values, key, default=None):
    value = values.get(key, default)
    require(value in ('0', '1'), 'invalid or missing boolean')
    return value == '1'


def unsigned(value, maximum):
    require(isinstance(value, str) and bool(re.fullmatch(r'0|[1-9][0-9]{0,9}', value)),
            'invalid unsigned count')
    number = int(value)
    require(number <= maximum, 'count exceeds bound')
    return number


def inspect_text(path: Path, fields=()):
    # Bounded read also detects a file that grew after its initial size check.
    require(path.stat().st_size <= LIMIT, 'TXT exceeds inspection budget')
    with path.open('rb') as stream:
        data = stream.read(LIMIT + 1)
    require(len(data) <= LIMIT, 'TXT grew beyond inspection budget')
    data.decode('utf-8')
    require(b'\0' not in data and b'\r' not in data, 'original UTF-8 LF text required')
    require(data.startswith('DLSS5 核心反馈 / Core feedback\n'.encode()), 'not an F8 CoreText-v2 file')
    marker = data.find(MARKER)
    require(0 < marker < 2048, 'capture section missing')
    header = properties(data[:marker].decode().splitlines())
    require(header.get('format') == 'DLSS5-CoreText-v2' and header.get('encoding') == 'UTF-8',
            'unsupported TXT schema')
    require(header.get('archive.requested') == '0', 'TXT is not a text-only F8 capture')
    declared_limit = unsigned(header.get('text.limitBytes'), LIMIT)
    require(0 < len(data) <= declared_limit, 'TXT exceeds its declared budget')
    offset = marker + len(MARKER)

    def line():
        nonlocal offset
        end = data.find(b'\n', offset)
        require(end >= 0, 'unfinished TXT line')
        value = data[offset:end].decode('utf-8')
        offset = end + 1
        return value

    meta_lines = []
    while offset < len(data):
        saved = offset
        value = line()
        if value.startswith(('===== ', 'evidence.', 'text.optionalMissing=')):
            offset = saved
            break
        meta_lines.append(value)
    info = properties(meta_lines, REQUIRED_FIELDS | frozenset(fields))
    require(info.get('metadata.format') == 'DLSS5-Core-v1' and info.get('capture.kind') == 'core',
            'invalid capture metadata schema')
    require(bool(re.fullmatch(r'DLSS5_CORE_[0-9]+_[0-9]+_[0-9]+', info.get('session', ''))),
            'invalid session identity')
    for key in ('config.writesToIni', 'images.requested', 'gpu.readbackRequested'):
        require(info.get(key) == '0', 'capture declares unexpected side effects')
    snapshot_valid = boolean(info, 'snapshot.valid')
    cancelled = boolean(info, 'collection.cancelled', '0')
    truncated = boolean(info, 'collection.metadataTruncated', '0')
    summaries, missing, incomplete_scans, omitted_summaries = {}, [], [], []
    missing_keys = set()
    optional_missing = 0
    active = None
    footer = None

    def missing_record(value):
        nonlocal optional_missing
        match = MISSING.fullmatch(value)
        require(match is not None, 'invalid missing-evidence record')
        name, coverage, required = match.groups()
        key = name + (coverage or '')
        require(key not in missing_keys and (required == '1') == (name == 'nr-current'),
                'duplicate evidence or incorrect required flag')
        require((not coverage and name not in summaries) or (coverage and active == name),
                'contradictory missing-evidence record')
        missing_keys.add(key)
        optional_missing += required == '0'
        missing.append({'name': key, 'required': required == '1',
                        'reason': 'incomplete source scan' if coverage else 'source or read unavailable'})

    while offset < len(data):
        value = line()
        if not value:
            continue
        if value.startswith('evidence.'):
            missing_record(value)
            continue
        if value == 'collection.cancelled=1':
            cancelled = True
            continue
        if value.startswith('text.optionalMissing='):
            footer = {'text.optionalMissing': value.split('=', 1)[1]}
            while offset < len(data):
                value = line()
                if value == 'END DLSS5 FEEDBACK TEXT':
                    require(offset == len(data), 'data follows TXT end marker')
                    break
                require(value.startswith(('text.complete=', 'text.reason=')), 'invalid TXT footer')
                key, item = value.split('=', 1)
                require(key not in footer, 'duplicate TXT footer field')
                footer[key] = item
            else:
                raise FormatError('TXT end marker missing')
            break
        matches = [name for name in NAMES if value == '===== ' + name +
                   (' / PREVIOUS PROCESS, NOT CURRENT =====' if name == 'nr-previous' else
                    ' / CURRENT PROCESS (first distinct/latest/tail) =====')]
        require(len(matches) == 1, 'unknown evidence section')
        active = matches[0]
        require(active not in summaries and active not in missing_keys, 'duplicate evidence section')
        value = line()
        if value.startswith('evidence.'):
            missing_record(value)
            value = line()
        require(value.startswith('summary.bytes='), 'summary byte count missing')
        size = unsigned(value.split('=', 1)[1], SUMMARY_LIMIT)
        digest_line = line()
        require(bool(re.fullmatch(r'summary.sha256=[0-9a-f]{64}', digest_line)), 'invalid summary digest')
        require(line() == 'summary.sha256Scope=bounded sanitized selected evidence', 'unknown summary scope')
        # Document::Append can preserve the summary's framing but decline the
        # whole payload at its byte budget. Finish then explicitly says partial.
        # Preserve its metadata without claiming that omitted bytes were hashed.
        if data[offset:].startswith((b'\ntext.optionalMissing=', b'collection.cancelled=1\n\ntext.optionalMissing=')):
            omitted_summaries.append(active)
            active = None
            continue
        require(size > 0 and size <= len(data) - offset, 'unfinished summary')
        summary = data[offset:offset + size]
        require(hashlib.sha256(summary).hexdigest() == digest_line.split('=', 1)[1], 'summary digest mismatch')
        offset += size
        text = summary.decode('ascii')
        require(text.startswith('format=DLSS5-LogSummary-v1\n') and '\n[head]\n' in text,
                'invalid summary schema')
        summary_info = properties(text.partition('\n[head]\n')[0].splitlines())
        complete_scan = boolean(summary_info, 'scan.complete')
        require(complete_scan == (active + '.coverage' not in missing_keys), 'scan coverage contradicts record')
        if not complete_scan:
            incomplete_scans.append(active)
        summaries[active] = text
        active = None

    require(footer is not None, 'TXT completion footer missing')
    require(unsigned(footer.get('text.optionalMissing'), 12) == optional_missing,
            'optional missing count differs')
    complete = boolean(footer, 'text.complete')
    require(complete or bool(footer.get('text.reason')), 'partial TXT reason missing')
    require('nr-current' in summaries or 'nr-current' in missing_keys or not complete,
            'required current evidence absent')
    required_ok = (snapshot_valid and not cancelled and not truncated and not omitted_summaries and
                   'nr-current' in summaries and 'nr-current' not in incomplete_scans and
                   not any(item['required'] for item in missing))
    require(not complete or required_ok, 'TXT completeness contradicts required evidence')
    missing += [{'name': name, 'required': name == 'nr-current', 'reason': 'summary omitted by TXT byte budget'}
                for name in omitted_summaries]
    return {'input_kind': 'text', 'session': info['session'], 'manifest_verified': False,
            'text_verified': True, 'required_complete': complete, 'complete': complete and not missing,
            'source_commit': info.get('artifact.sourceCommit'), 'snapshot_valid': snapshot_valid,
            'missing': missing, 'incomplete_scans': incomplete_scans, 'metadata': info,
            'summaries': summaries, 'sha256': hashlib.sha256(data).hexdigest(),
            'verification_scope': 'Original TXT structure and included summary hashes; not authorship, game quality, or image pairing.'}
