"""Wire-level adversarial fixtures independent of the capture implementation."""
import importlib.util
import hashlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
import warnings
import zipfile

SPEC = importlib.util.spec_from_file_location('parser', Path(__file__).parents[1] / 'scripts/parse-f8-bundle.py')
P = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(P)


def record(index, valid=1, frame=None, instance=20, width=2, stride=1, flags=65):
    metadata = json.dumps({'requested': {'strength': 1}, 'actual': {'strength': 1}}).encode()
    pixels = struct.pack('<' + 'f' * (width * 2 * 5 * 4), *([0.5] * (width * 2 * 5 * 4))) if valid else b''
    return struct.pack('<2I4Q12IQ', index, valid, 10, 100 + index if frame is None else frame,
                       instance, 30, 40, 3840, 2160, 1920, 1080, 0, 0, width, 2,
                       stride, flags, len(metadata), len(pixels)) + metadata + pixels


def chunk(number, records):
    return struct.pack('<8s6I', b'F8CHK004', 4, 0, number, len(records), 16, 5) + b''.join(records)


class ParserTest(unittest.TestCase):
    def bundle(self, entries):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'feedback.zip'
            with warnings.catch_warnings():
                warnings.simplefilter('ignore', UserWarning)
                with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as out:
                    for name, data in entries:
                        info = zipfile.ZipInfo()
                        info.filename = name  # Preserve malicious wire names even on Windows.
                        out.writestr(info, data)
            return P.inspect_bundle(path)

    def test_complete_sequence_separate_overview(self):
        entries = [(f'groups/0/chunk-{n:03}.f8c', chunk(n, [record(i) for i in range(n * 4, n * 4 + 4)])) for n in range(4)]
        entries.append(('groups/0/overview.f8c', chunk(4, [record(16, frame=500, instance=999, stride=4)])))
        result = self.bundle(entries)['groups'][0]
        self.assertTrue(result['complete_sequence'])
        self.assertTrue(result['overview_present'])
        self.assertEqual(set(result['records'][0]['planes']), set(P.PLANES))

    def test_partial_does_not_claim_sixteen(self):
        result = self.bundle([('groups/0/chunk-000.f8c', chunk(0, [record(0), record(1), record(2, 0), record(3, 0)]))])
        self.assertFalse(result['groups'][0]['complete_sequence'])
        self.assertEqual(result['groups'][0]['valid_roi_frames'], 2)

    def test_frame_gap_rejected(self):
        with self.assertRaises(P.FormatError):
            self.bundle([('groups/0/chunk-000.f8c', chunk(0, [record(0), record(1, frame=102)]))])

    def test_missing_middle_rejected(self):
        with self.assertRaises(P.FormatError):
            self.bundle([('groups/0/chunk-000.f8c', chunk(0, [record(0), record(1, 0), record(2)]))])

    def test_instance_change_rejected(self):
        with self.assertRaises(P.FormatError):
            self.bundle([('groups/0/chunk-000.f8c', chunk(0, [record(0), record(1, instance=21)]))])

    def test_periodic_diagnostics_do_not_break_color_contract(self):
        result = self.bundle([('groups/0/chunk-000.f8c', chunk(0, [record(0), record(1, flags=65 | 8)]))])
        self.assertEqual(result['groups'][0]['valid_roi_frames'], 2)

    def test_color_contract_change_rejected(self):
        with self.assertRaises(P.FormatError):
            self.bundle([('groups/0/chunk-000.f8c', chunk(0, [record(0), record(1, flags=65 | 2)]))])

    def test_wrong_header_and_name(self):
        with self.assertRaises(P.FormatError):
            self.bundle([('groups/1/chunk-000.f8c', chunk(0, [record(0)]))])

    def test_truncated_plane(self):
        with self.assertRaises(P.FormatError):
            self.bundle([('groups/0/chunk-000.f8c', chunk(0, [record(0)])[:-1])])

    def test_trailing_garbage(self):
        with self.assertRaises(P.FormatError):
            self.bundle([('groups/0/chunk-000.f8c', chunk(0, [record(0)]) + b'x')])

    def test_duplicate_archive_member(self):
        with self.assertRaises(P.FormatError):
            self.bundle([('log.txt', b'a'), ('LOG.txt', b'b')])

    def test_path_escape(self):
        for name in ('../a', 'x/./a', 'x\\a', '/a', 'C:/a'):
            with self.subTest(name=name), self.assertRaises(P.FormatError):
                self.bundle([(name, b'x')])

    def test_oversize_member(self):
        original = P.FILE_LIMIT
        try:
            P.FILE_LIMIT = 2
            with self.assertRaises(P.FormatError):
                self.bundle([('log.txt', b'abc')])
        finally:
            P.FILE_LIMIT = original

    def test_roi_sampling_stride(self):
        with self.assertRaises(P.FormatError):
            self.bundle([('groups/0/chunk-000.f8c', chunk(0, [record(0, stride=2)]))])

    def test_manifest_verifies_copied_bytes(self):
        manifest = {'format': 'DLSS5-DiagnosticBundle-v1', 'complete': False,
                    'metadata_entry': 'capture-info.txt', 'metadata_text': 'partial\n',
                    'entries': [{'name': 'log.txt', 'status': 'included', 'bytes': 3,
                                 'sha256': hashlib.sha256(b'abc').hexdigest()}]}
        entries = [('log.txt', b'abc'), ('capture-info.txt', b'partial\n'),
                   ('manifest.json', json.dumps(manifest).encode())]
        self.assertTrue(self.bundle(entries)['manifest_verified'])
        entries[0] = ('log.txt', b'abd')  # ZIP CRC remains valid; manifest SHA is stale.
        with self.assertRaises(P.FormatError):
            self.bundle(entries)

    def test_manifest_cannot_hide_present_file(self):
        manifest = {'format': 'DLSS5-DiagnosticBundle-v1',
                    'metadata_entry': 'capture-info.txt', 'metadata_text': '', 'entries': []}
        with self.assertRaises(P.FormatError):
            self.bundle([('unlisted.txt', b'x'), ('capture-info.txt', b''),
                         ('manifest.json', json.dumps(manifest).encode())])


if __name__ == '__main__':
    unittest.main()
