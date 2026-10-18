"""Synthetic PE metadata only: no vendor DLL, signatures, weights or execution."""
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts/inspect-dlsnr-candidate.py'
SPEC = importlib.util.spec_from_file_location('candidate_inspector', SCRIPT)
INSPECT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INSPECT)


def version_blob(version=(310, 9, 16, 0)):
    result = bytearray(92)
    struct.pack_into('<HHH', result, 0, 92, 52, 0)
    result[6:38] = 'VS_VERSION_INFO\0'.encode('utf-16le')
    a, b, c, d = version
    struct.pack_into('<13I', result, 40, 0xfeef04bd, 0x10000,
                     a << 16 | b, c << 16 | d, a << 16 | b, c << 16 | d,
                     0x3f, 0, 0x40004, 2, 0, 0, 0)
    return bytes(result)


def candidate(version=(310, 9, 16, 0), pe32=False, resources=True):
    image = bytearray(1024)
    image[:2] = b'MZ'
    struct.pack_into('<I', image, 0x3c, 0x80)
    image[0x80:0x84] = b'PE\0\0'
    optional_size = 224 if pe32 else 240
    struct.pack_into('<HHIIIHH', image, 0x84, 0x14c if pe32 else 0x8664,
                     1, 0, 0, 0, optional_size, 0x2002)
    optional = 0x98
    struct.pack_into('<H', image, optional, 0x10b if pe32 else 0x20b)
    struct.pack_into('<II', image, optional + 56, 0x2000, 0x200)
    directories = optional + (96 if pe32 else 112)
    struct.pack_into('<I', image, directories - 4, 16)
    if resources:
        struct.pack_into('<II', image, directories + 16, 0x1000, 0x200)
    section = optional + optional_size
    image[section:section + 8] = b'.rsrc\0\0\0'
    struct.pack_into('<IIII', image, section + 8, 0x200, 0x1000, 0x200, 0x200)
    base = 0x200

    def directory(offset, named, ids, rows):
        struct.pack_into('<IIHHHH', image, base + offset, 0, 0, 0, 0, named, ids)
        for index, row in enumerate(rows):
            struct.pack_into('<II', image, base + offset + 16 + index * 8, *row)

    # One RCDATA leaf deliberately contains familiar network/version strings.
    # Only a separate numeric RT_VERSION leaf contains fixed version metadata.
    directory(0, 0, 2, [(10, 0x80000020), (16, 0x80000060)])
    directory(32, 1, 0, [(0x800000b0, 0x80000038)])
    directory(56, 0, 1, [(0x409, 80)])
    directory(96, 0, 1, [(1, 0x80000078)])
    directory(120, 0, 1, [(0x409, 144)])
    name = 'CC_SMALL_FP8'.encode('utf-16le')
    struct.pack_into('<H', image, base + 176, len(name) // 2)
    image[base + 178:base + 178 + len(name)] = name
    fixed = bytearray(version_blob(version))
    struct.pack_into('<H', fixed, 0, len(fixed))
    struct.pack_into('<IIII', image, base + 144, 0x1100, len(fixed), 1200, 0)
    image[base + 256:base + 256 + len(fixed)] = fixed
    payload = b'SYNTHETIC: CC_SMALL_FP8 310.9.16'
    struct.pack_into('<IIII', image, base + 80, 0x1190, len(payload), 0, 0)
    image[base + 400:base + 400 + len(payload)] = payload
    return image


class CandidateInspector(unittest.TestCase):
    def inspect_bytes(self, data):
        with tempfile.TemporaryDirectory(prefix='dlsnr-metadata-') as directory:
            path = Path(directory) / 'synthetic.dll'
            path.write_bytes(data)
            result = INSPECT.inspect_candidate(path)
            self.assertEqual(path.read_bytes(), data, 'inspection changed the candidate')
            return result

    def test_hash_architecture_fixed_version_and_nonexecution(self):
        for pe32, architecture in ((False, 'x64'), (True, 'x86')):
            with self.subTest(pe32=pe32):
                data = candidate(pe32=pe32)
                result = self.inspect_bytes(data)
                self.assertEqual(result['status'], 'inspected', result)
                self.assertEqual(result['sha256'], hashlib.sha256(data).hexdigest())
                self.assertEqual(result['pe']['architecture'], architecture)
                resources = result['pe']['resources']
                self.assertEqual((resources['directories'], resources['entries'], resources['leaves']), (5, 6, 2))
                self.assertEqual(resources['versions'][0]['fixed_info']['file_version'], [310, 9, 16, 0])
                self.assertFalse(resources['non_version_payloads_interpreted'])
                self.assertEqual(result['evidence']['status'], 'unknown')
                self.assertEqual(result['evidence']['network_family_inferred_from_receipt'], 'unknown')
                self.assertEqual(result['evidence']['version_hint_family'], 'original-and-optimized')
                self.assertFalse(result['checks']['network_execution_verified'])
                self.assertFalse(result['checks']['dll_loaded_or_executed'])
                for check in ('authenticode', 'loaded_module', 'ngx', 'gpu'):
                    self.assertEqual(result['checks'][check], 'not-checked')
        no_resource = self.inspect_bytes(candidate(resources=False))
        self.assertEqual(no_resource['pe']['resources']['versions'], [])
        self.assertEqual(no_resource['evidence']['version_hint_family'], 'unknown')

    def test_reviewed_receipts_match_policy_and_conflicts_do_not_promote(self):
        policy = (ROOT / 'src/nr_network_policy.h').read_text()
        function = policy.split('inline Family KnownFileFamily(', 1)[1].split('constexpr bool FileVersionConsistent', 1)[0]
        self.assertEqual(set(INSPECT.KNOWN_FILES), set(re.findall(r'"([a-f0-9]{64})"', function)))
        self.assertEqual(INSPECT.MAX_FILE_BYTES, 1 << 30)
        worker = (ROOT / 'src/nr_network_identity_worker.inl').read_text()
        self.assertIn('size.QuadPart <= (1ll << 30)', worker)
        for digest, family in INSPECT.KNOWN_FILES.items():
            # A pure policy seam supplies a reviewed digest; no synthetic file
            # is claimed to have that actual SHA256.
            version = (310, 8, 0, 0) if family == 'original-only' else (310, 9, 16, 0)
            pe = self.inspect_bytes(candidate(version))['pe']
            self.assertEqual(INSPECT.evidence(digest, pe)['status'], 'known-file-receipt')
            bad = copy.deepcopy(pe)
            bad['resources']['versions'][0]['fixed_info']['file_version'] = [999, 0, 0, 0]
            conflict = INSPECT.evidence(digest, bad)
            self.assertEqual(conflict['status'], 'conflict')
            self.assertEqual(conflict['network_family_inferred_from_receipt'], 'unknown')
            bad = copy.deepcopy(pe)
            bad['x64_dll_headers'] = False
            self.assertEqual(INSPECT.evidence(digest, bad)['status'], 'conflict')
            bounded_out = INSPECT.evidence(digest, {'status': 'rejected'})
            self.assertEqual(bounded_out['status'], 'known-file-receipt')
            self.assertIn('PE-metadata-unavailable', bounded_out['reasons'][0])
        pe = self.inspect_bytes(candidate())['pe']
        other = copy.deepcopy(pe['resources']['versions'][0])
        other['fixed_info']['file_version'] = [310, 8, 0, 0]
        pe['resources']['versions'].append(other)
        self.assertEqual(INSPECT.evidence('0' * 64, pe)['status'], 'conflict')

    def test_pe_and_resource_malformed_ranges_are_rejected_with_hash_retained(self):
        cases = [('truncated DOS header', b'MZ'),
                 ('truncated PE header', candidate()[:0x90]),
                 ('truncated optional/section headers', candidate()[:0x190])]
        def changed(name, offset, form, *values):
            data = candidate()
            struct.pack_into(form, data, offset, *values)
            cases.append((name, data))
        changed('PE pointer beyond file', 0x3c, '<I', 0xfffffff0)
        changed('section count', 0x86, '<H', 65535)
        changed('optional directory count', 0x98 + 108, '<I', 0xffffffff)
        changed('section raw past file', 0x188 + 20, '<I', 0x10000)
        changed('RVA arithmetic overflow', 0x98 + 128, '<II', 0xfffffff0, 64)
        changed('resource virtual-only tail', 0x98 + 128, '<II', 0x1240, 16)
        changed('resource cycle', 0x200 + 20, '<I', 0x80000000)
        changed('resource relative offset', 0x200 + 20, '<I', 0x80001000)
        changed('resource entry bound', 0x200 + 12, '<HH', 65535, 65535)
        changed('resource name length', 0x200 + 176, '<H', 65535)
        changed('resource payload out of file', 0x200 + 80, '<II', 0x7fff0000, 16)
        changed('fixed version length', 0x200 + 256, '<H', 65535)
        changed('fixed version signature', 0x200 + 256 + 40, '<I', 0)
        changed('oversized version payload', 0x200 + 144 + 4, '<I', 65537)
        for name, data in cases:
            with self.subTest(name=name):
                result = self.inspect_bytes(data)
                self.assertEqual(result['status'], 'rejected', result)
                self.assertEqual(result['sha256'], hashlib.sha256(data).hexdigest())
                self.assertEqual(result['evidence']['network_family_inferred_from_receipt'], 'unknown')

    def test_depth_directory_and_metadata_budgets(self):
        data = candidate()
        data[0x200:] = bytes(512)
        for depth in range(INSPECT.MAX_RESOURCE_DEPTH + 2):
            where = 0x200 + depth * 24
            struct.pack_into('<IIHHHHII', data, where, 0, 0, 0, 0, 0, 1,
                             10 if depth == 0 else 1, 0x80000000 | ((depth + 1) * 24))
        result = self.inspect_bytes(data)
        self.assertIn('depth', result['pe']['error'])
        with mock.patch.object(INSPECT, 'MAX_RESOURCE_DIRECTORIES', 2):
            self.assertIn('directory count', self.inspect_bytes(candidate())['pe']['error'])
        with mock.patch.object(INSPECT, 'MAX_METADATA_BYTES', 8):
            reader = INSPECT.Reader(io.BytesIO(b'123456789'), 9)
            with self.assertRaisesRegex(INSPECT.InspectionError, 'budget'):
                reader.read(0, 9)
        reader = INSPECT.Reader(io.BytesIO(b'abc'), 3)
        for offset, length in ((-1, 1), (2, 2), (1 << 64, 1)):
            with self.assertRaises(INSPECT.InspectionError):
                reader.read(offset, length)

    def test_overlapping_sections_cannot_choose_an_arbitrary_mapping(self):
        data = candidate()
        struct.pack_into('<H', data, 0x86, 2)
        data[0x188 + 40:0x188 + 80] = data[0x188:0x188 + 40]
        result = self.inspect_bytes(data)
        self.assertIn('overlap', result['pe']['error'])

    def test_regular_size_and_changed_file_guards(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            self.assertEqual(INSPECT.inspect_candidate(folder)['status'], 'rejected')
            self.assertIsNone(INSPECT.inspect_candidate(folder / 'missing.dll')['sha256'])
            path = folder / 'synthetic.dll'
            path.write_bytes(candidate())
            with mock.patch.object(INSPECT, 'MAX_FILE_BYTES', 100):
                rejected = INSPECT.inspect_candidate(path)
                self.assertIsNone(rejected['sha256'])
                self.assertIn('file size', rejected['error'])
            actual = path.stat()
            changed = types.SimpleNamespace(**{key: getattr(actual, key) for key in
                ('st_dev', 'st_ino', 'st_size', 'st_mtime_ns', 'st_ctime_ns')})
            changed.st_mtime_ns += 1
            with mock.patch.object(INSPECT.os, 'fstat', side_effect=[actual, changed]):
                rejected = INSPECT.inspect_candidate(path)
                self.assertIsNone(rejected['sha256'])
                self.assertEqual(rejected['pe']['status'], 'not-trusted')
                self.assertIn('changed during inspection', rejected['error'])

    def test_cli_emits_json_and_cannot_overwrite_the_candidate(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'synthetic.dll'
            output = Path(directory) / 'inspection.json'
            payload = candidate()
            path.write_bytes(payload)
            result = subprocess.run([sys.executable, str(SCRIPT), str(path), '--output', str(output)],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout), json.loads(output.read_text()))
            self.assertEqual(json.loads(result.stdout)['evidence']['status'], 'unknown')
            refused = subprocess.run([sys.executable, str(SCRIPT), str(path), '--output', str(path)],
                                     capture_output=True, text=True, timeout=10)
            self.assertEqual(refused.returncode, 2)
            self.assertEqual(path.read_bytes(), payload)


if __name__ == '__main__':
    unittest.main()
