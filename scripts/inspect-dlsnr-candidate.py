#!/usr/bin/env python3
"""Inspect one explicitly supplied DLL as data; never load or execute it.

Standard-library PE metadata reader, not a Windows loader/Authenticode check.
Format references: https://learn.microsoft.com/windows/win32/debug/pe-format
and https://learn.microsoft.com/windows/win32/menurc/vs-versioninfo .
Only the fixed VS_VERSIONINFO value is interpreted; other resource payloads,
model weights, imports, executable code and free-form version strings are not.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import struct

MAX_FILE_BYTES = 1024 * 1024 * 1024
HASH_CHUNK_BYTES = 1024 * 1024
MAX_METADATA_BYTES = 8 * 1024 * 1024
MAX_READ_BYTES = 65536
MAX_SECTIONS = 96
MAX_RESOURCE_ENTRIES = 8192
MAX_RESOURCE_DIRECTORIES = 2048
MAX_RESOURCE_DEPTH = 8
MAX_RESOURCE_NAME_UNITS = 256
MAX_RESOURCE_TYPES = 256
MAX_VERSION_RESOURCES = 32
UINT32_END = 1 << 32

# Keep these file receipts synchronized with src/nr_network_policy.h. Tests
# check the exact digest set; a familiar version alone never promotes a file.
KNOWN_FILES = {
    '2bc19bc09a7b89dd44f73e2ac0108fcc586e2154bdd0830f242f4f040e28789f': 'original-and-optimized',
    '8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206': 'original-only',
    'e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e': 'original-only',
}
VERSION_HINTS = {(310, 8, 0, 0): 'original-only', (310, 9, 16, 0): 'original-and-optimized'}
MACHINES = {0x14c: 'x86', 0x8664: 'x64', 0xaa64: 'arm64', 0xa641: 'arm64ec', 0xa64e: 'arm64x'}


class InspectionError(ValueError):
    pass


class Reader:
    def __init__(self, stream, size):
        self.stream, self.size, self.metadata_bytes = stream, size, 0

    def check(self, offset, size):
        if offset < 0 or size < 0 or offset > self.size or size > self.size - offset:
            raise InspectionError('file range is out of bounds')

    def read(self, offset, size):
        self.check(offset, size)
        if size > MAX_READ_BYTES or self.metadata_bytes + size > MAX_METADATA_BYTES:
            raise InspectionError('metadata read budget exceeded')
        self.metadata_bytes += size
        self.stream.seek(offset)
        data = self.stream.read(size)
        if len(data) != size:
            raise InspectionError('file changed or a metadata read was truncated')
        return data


def fixed_version(data):
    if len(data) < 6:
        raise InspectionError('version resource header is truncated')
    length, value_length, value_type = struct.unpack_from('<HHH', data)
    key = 'VS_VERSION_INFO\0'.encode('utf-16le')
    start = (6 + len(key) + 3) & ~3
    if not start <= length <= len(data) or data[6:6 + len(key)] != key or value_type != 0:
        raise InspectionError('version root length, key or type is invalid')
    if value_length == 0:
        return None
    if value_length < 52 or value_length > length - start:
        raise InspectionError('fixed version value is out of bounds')
    values = struct.unpack_from('<13I', data, start)
    if values[0] != 0xfeef04bd or values[1] != 0x00010000:
        raise InspectionError('fixed version signature or structure version is invalid')

    def version(ms, ls):
        return [ms >> 16, ms & 0xffff, ls >> 16, ls & 0xffff]

    return {'file_version': version(values[2], values[3]),
            'product_version': version(values[4], values[5])}


class PE:
    def __init__(self, reader):
        self.reader, self.sections, self.headers_size = reader, [], 0

    def map_rva(self, rva, size):
        if rva < 0 or size < 0 or rva >= UINT32_END or size > UINT32_END - rva:
            raise InspectionError('RVA range overflows 32 bits')
        candidates = []
        if rva < self.headers_size and size <= self.headers_size - rva:
            candidates.append(rva)
        for section in self.sections:
            delta = rva - section['rva']
            if 0 <= delta < section['raw_size'] and size <= section['raw_size'] - delta:
                candidates.append(section['raw_offset'] + delta)
        if len(candidates) != 1:
            raise InspectionError('RVA is unbacked or has ambiguous section mapping')
        self.reader.check(candidates[0], size)
        return candidates[0]

    def parse(self):
        dos = self.reader.read(0, 64)
        if dos[:2] != b'MZ':
            raise InspectionError('DOS MZ signature is missing')
        pe_offset = struct.unpack_from('<I', dos, 0x3c)[0]
        if pe_offset < 64:
            raise InspectionError('PE header overlaps the DOS header')
        header = self.reader.read(pe_offset, 24)
        if header[:4] != b'PE\0\0':
            raise InspectionError('PE signature is missing')
        machine, count = struct.unpack_from('<HH', header, 4)
        optional_size, characteristics = struct.unpack_from('<HH', header, 20)
        if not 1 <= count <= MAX_SECTIONS or not 2 <= optional_size <= 4096:
            raise InspectionError('section count or optional-header size exceeds bounds')
        optional_offset = pe_offset + 24
        optional = self.reader.read(optional_offset, optional_size)
        magic = struct.unpack_from('<H', optional)[0]
        directory_start = {0x10b: 96, 0x20b: 112}.get(magic)
        if directory_start is None or optional_size < directory_start:
            raise InspectionError('optional header is not a bounded PE32/PE32+ header')
        directory_count = struct.unpack_from('<I', optional, directory_start - 4)[0]
        if directory_count > 32 or directory_count > (optional_size - directory_start) // 8:
            raise InspectionError('data-directory count exceeds the optional header')
        image_size, self.headers_size = struct.unpack_from('<II', optional, 56)
        section_table = optional_offset + optional_size
        if not section_table + count * 40 <= self.headers_size <= self.reader.size:
            raise InspectionError('section table or SizeOfHeaders is out of bounds')
        if image_size < self.headers_size:
            raise InspectionError('SizeOfImage is smaller than its headers')
        virtual_ranges, raw_ranges = [], []
        for index in range(count):
            entry = self.reader.read(section_table + index * 40, 40)
            virtual_size, rva, raw_size, raw_offset = struct.unpack_from('<IIII', entry, 8)
            span = max(virtual_size, raw_size)
            if span and (rva < self.headers_size or span > UINT32_END - rva or rva + span > image_size):
                raise InspectionError('section virtual range is invalid')
            if raw_size:
                self.reader.check(raw_offset, raw_size)
                if raw_offset < self.headers_size:
                    raise InspectionError('section raw data overlaps headers')
                raw_ranges.append((raw_offset, raw_offset + raw_size))
            if span:
                virtual_ranges.append((rva, rva + span))
            self.sections.append({'name': entry[:8].split(b'\0', 1)[0].decode('ascii', 'backslashreplace'),
                                  'rva': rva, 'virtual_size': virtual_size,
                                  'raw_offset': raw_offset, 'raw_size': raw_size})
        for ranges in (virtual_ranges, raw_ranges):
            ordered = sorted(ranges)
            if any(a[1] > b[0] for a, b in zip(ordered, ordered[1:])):
                raise InspectionError('section ranges overlap')
        resource_rva, resource_size = (struct.unpack_from('<II', optional, directory_start + 16)
                                       if directory_count > 2 else (0, 0))
        resources = self.resources(resource_rva, resource_size)
        return {'status': 'bounded-metadata-parsed', 'architecture': MACHINES.get(machine, 'unknown'),
                'machine': machine, 'format': 'PE32+' if magic == 0x20b else 'PE32',
                'is_dll': bool(characteristics & 0x2000),
                'x64_dll_headers': machine == 0x8664 and magic == 0x20b and bool(characteristics & 0x2000),
                'sections': self.sections, 'resources': resources,
                'metadata_bytes_read': self.reader.metadata_bytes}

    def resources(self, rva, size):
        result = {'present': bool(rva or size), 'rva': rva, 'declared_size': size,
                  'directories': 0, 'entries': 0, 'leaves': 0, 'max_depth': 0,
                  'types': [], 'versions': [], 'non_version_payloads_interpreted': False}
        if not rva and not size:
            return result
        if not rva or size < 16:
            raise InspectionError('resource directory RVA/size is invalid')
        base = self.map_rva(rva, size)
        visited, types = set(), {}

        def take(relative, amount):
            if relative < 0 or relative > size or amount > size - relative:
                raise InspectionError('resource-relative range is out of bounds')
            return self.reader.read(base + relative, amount)

        def label(encoded):
            if not encoded & 0x80000000:
                return encoded
            offset = encoded & 0x7fffffff
            units = struct.unpack('<H', take(offset, 2))[0]
            if units > MAX_RESOURCE_NAME_UNITS:
                raise InspectionError('resource name length exceeds bound')
            try:
                return take(offset + 2, units * 2).decode('utf-16le')
            except UnicodeDecodeError:
                raise InspectionError('resource name is not valid UTF-16') from None

        def walk(offset, path):
            depth = len(path)
            if depth > MAX_RESOURCE_DEPTH:
                raise InspectionError('resource directory depth exceeds bound')
            if offset in visited:
                raise InspectionError('resource directory cycle or shared directory refused')
            if len(visited) >= MAX_RESOURCE_DIRECTORIES:
                raise InspectionError('resource directory count exceeds bound')
            visited.add(offset)
            result['directories'] += 1
            result['max_depth'] = max(result['max_depth'], depth)
            named, numbered = struct.unpack_from('<HH', take(offset, 16), 12)
            count = named + numbered
            if result['entries'] + count > MAX_RESOURCE_ENTRIES:
                raise InspectionError('resource entry count exceeds bound')
            result['entries'] += count
            table = take(offset + 16, count * 8)
            labels = set()
            for index in range(count):
                name, target = struct.unpack_from('<II', table, index * 8)
                if bool(name & 0x80000000) != (index < named):
                    raise InspectionError('resource name/ID partition is invalid')
                value = label(name)
                if value in labels:
                    raise InspectionError('duplicate resource directory label')
                labels.add(value)
                route = (*path, value)
                if not path:
                    if len(types) >= MAX_RESOURCE_TYPES:
                        raise InspectionError('resource type count exceeds bound')
                    types[value] = {'type': value, 'leaves': 0, 'declared_payload_bytes': 0}
                if target & 0x80000000:
                    walk(target & 0x7fffffff, route)
                    continue
                data_rva, data_size, codepage, _ = struct.unpack('<IIII', take(target, 16))
                data_offset = self.map_rva(data_rva, data_size) if data_size else None
                result['leaves'] += 1
                types[route[0]]['leaves'] += 1
                types[route[0]]['declared_payload_bytes'] += data_size
                if route[0] == 16:  # Numeric RT_VERSION only; never a string search.
                    if len(route) != 3 or not data_size or data_size > MAX_READ_BYTES:
                        raise InspectionError('version resource path or length exceeds supported bounds')
                    if len(result['versions']) >= MAX_VERSION_RESOURCES:
                        raise InspectionError('version resource count exceeds bound')
                    version = fixed_version(self.reader.read(data_offset, data_size))
                    result['versions'].append({'resource_path': list(route), 'codepage': codepage,
                                               'fixed_info': version})

        walk(0, ())
        result['types'] = list(types.values())
        return result


def evidence(digest, pe):
    family = KNOWN_FILES.get(digest, 'unknown')
    versions = {tuple(row['fixed_info']['file_version']) for row in pe.get('resources', {}).get('versions', [])
                if row['fixed_info'] is not None}
    hints = {VERSION_HINTS.get(version, 'unknown') for version in versions}
    conflicts, notes = [], []
    if len(versions) > 1:
        conflicts.append('fixed-version-resources-disagree')
    if family != 'unknown':
        if pe.get('status') != 'bounded-metadata-parsed':
            # Exceeding this deliberately narrow parser's limits is not proof
            # that the reviewed file receipt is false. Preserve both facts.
            notes.append('PE-metadata-unavailable; file-hash-receipt-only')
        elif not pe.get('x64_dll_headers'):
            conflicts.append('known-file-receipt-conflicts-with-PE-metadata')
        if versions and hints != {family}:
            conflicts.append('known-file-receipt-conflicts-with-fixed-version')
    status = 'conflict' if conflicts else 'known-file-receipt' if family != 'unknown' else 'unknown'
    if status == 'unknown':
        notes.append('SHA256-does-not-match-a-reviewed-file-receipt')
    return {'status': status, 'file_receipt_family': family,
            'version_hint_family': next(iter(hints)) if len(hints) == 1 else 'unknown',
            'network_family_inferred_from_receipt': family if status == 'known-file-receipt' else 'unknown',
            'reasons': conflicts + notes, 'receipt_source': 'src/nr_network_policy.h',
            'network_execution_verified': False}


def file_identity(value):
    return value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns, value.st_ctime_ns


def inspect_candidate(path):
    path = Path(path)
    report = {'schema': 'dlsnr-candidate-inspection-v1', 'input_name': path.name,
              'status': 'rejected', 'file_size': None, 'sha256': None,
              'pe': {'status': 'not-inspected'},
              'checks': {'authenticode': 'not-checked', 'dll_loaded_or_executed': False,
                         'loaded_module': 'not-checked', 'ngx': 'not-checked', 'gpu': 'not-checked',
                         'weights_extracted': False, 'free_form_string_scan': False,
                         'network_execution_verified': False},
              'limits': {'file_bytes': MAX_FILE_BYTES, 'metadata_bytes': MAX_METADATA_BYTES,
                         'single_metadata_read_bytes': MAX_READ_BYTES, 'sections': MAX_SECTIONS,
                         'resource_entries': MAX_RESOURCE_ENTRIES, 'resource_directories': MAX_RESOURCE_DIRECTORIES,
                         'resource_depth': MAX_RESOURCE_DEPTH, 'resource_name_utf16_units': MAX_RESOURCE_NAME_UNITS,
                         'resource_types': MAX_RESOURCE_TYPES, 'version_resources': MAX_VERSION_RESOURCES}}
    try:
        before = path.stat()
        if not stat.S_ISREG(before.st_mode):
            raise InspectionError('input must be an explicitly supplied regular file')
        report['file_size'] = before.st_size
        if not 0 < before.st_size <= MAX_FILE_BYTES:
            raise InspectionError('file size exceeds the supported nonempty 1 GiB bound')
        flags = os.O_RDONLY | getattr(os, 'O_BINARY', 0) | getattr(os, 'O_NONBLOCK', 0)
        with os.fdopen(os.open(path, flags), 'rb') as stream:
            opened = os.fstat(stream.fileno())
            if not stat.S_ISREG(opened.st_mode) or file_identity(opened) != file_identity(before):
                raise InspectionError('file identity changed before reading')
            digest = hashlib.sha256()
            remaining = opened.st_size
            while remaining:
                chunk = stream.read(min(HASH_CHUNK_BYTES, remaining))
                if not chunk:
                    raise InspectionError('file was truncated while hashing')
                digest.update(chunk)
                remaining -= len(chunk)
            if stream.read(1):
                raise InspectionError('file grew while hashing')
            try:
                report['pe'] = PE(Reader(stream, opened.st_size)).parse()
            except InspectionError as error:
                report['pe'] = {'status': 'rejected', 'error': str(error)}
            if file_identity(os.fstat(stream.fileno())) != file_identity(opened):
                raise InspectionError('file changed during inspection; hash and metadata are not a stable receipt')
            report['sha256'] = digest.hexdigest()
            report['file_stat_unchanged_during_inspection'] = True
            report['status'] = 'inspected' if report['pe']['status'] == 'bounded-metadata-parsed' else 'rejected'
    except (InspectionError, OSError) as error:
        report['error'] = str(error) if isinstance(error, InspectionError) else f'file IO error {error.errno}: {error.strerror}'
        report['pe'] = {'status': 'not-trusted'}
    report['evidence'] = evidence(report['sha256'], report['pe'])
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dll', type=Path, help='explicit path to one regular DLL candidate')
    parser.add_argument('--output', type=Path, help='optional new JSON report; existing files are never overwritten')
    args = parser.parse_args()
    report = inspect_candidate(args.dll)
    text = json.dumps(report, indent=2, ensure_ascii=True) + '\n'
    print(text, end='')
    if args.output:
        try:
            # Exclusive creation also prevents an output path or hard link from
            # overwriting the inspected DLL. No implicit report location exists.
            with args.output.open('x', encoding='utf-8') as output:
                output.write(text)
        except OSError as error:
            parser.exit(2, f'Cannot create report: {error.strerror}\n')
    return 0 if report['status'] == 'inspected' and report['evidence']['status'] != 'conflict' else 1


if __name__ == '__main__':
    raise SystemExit(main())
