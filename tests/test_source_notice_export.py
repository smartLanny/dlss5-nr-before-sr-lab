"""Exercise actual source staging and OTA notice assembly with inert fixtures.

No carrier checkout, NVIDIA DLL or Windows toolchain is needed. PE validation
is stubbed only in the OTA format fixture; all source/notice hashes and ZIP
contents pass through the production exporter and packager.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]

# Full license bytes read from the exact public dependencies used by the cloud
# build. These are documentation integrity checks, not redistribution approval.
PINNED_NOTICE_TEXTS = (
    ('ReShade SDK headers', 'Dear ImGui',
     '237ded5b8344f820113efab1e65e91e1f159d9202c5b4856606a0590d3ffdab0'),
    ('Dear ImGui', 'AMD FidelityFX and Microsoft DirectX 12 helpers',
     'c80c5789748d955c4a650562baa0d750494e2c7128c2ca66aaebe2e3c4e198bf'),
    ('AMD FidelityFX and Microsoft DirectX 12 helpers', 'NVIDIA and games',
     '19eadab1b941e9b7d0e72c83f47e747ffb6f1097b9b91320156300e024181ef7'),
)


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / filename)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def put(root, name, data):
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


def git(root, *args):
    return subprocess.run(['git', '-C', str(root), *args], check=True,
                          text=True, capture_output=True).stdout.strip()


class SourceNotices(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sys.path.insert(0, str(ROOT / 'scripts'))
        cls.exporter = module('notice_exporter', 'prepare-core-source.py')
        cls.ota = module('notice_ota', 'package-core-ota.py')

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='source-notice-test-')
        self.addCleanup(temporary.cleanup)
        self.base = Path(temporary.name)
        self.layout = {'core_build_inputs': [], 'experiment_inputs': [],
                       'carrier_overlay': {'patch_file': 'src/interop/carrier-beta13.patch'}}
        self.notices = {'LICENSE': b'project license\r\nverbatim bytes\n',
                        'THIRD_PARTY_NOTICES.txt': b'adaptation attribution\n'}

    def repository(self):
        core = self.base / 'git-core'
        core.mkdir()
        git(core, 'init', '-q')
        git(core, 'config', 'user.name', 'Notice Fixture')
        git(core, 'config', 'user.email', 'fixture@example.invalid')
        git(core, 'config', 'core.autocrlf', 'false')
        put(core, 'src/demo.cpp', b'// fixture source\n')
        for name, data in self.notices.items():
            put(core, name, data)
        put(core, 'PRIVATE_NOTES.txt', b'excluded root file\n')
        git(core, 'add', '.')
        git(core, 'commit', '-qm', 'source notice fixture')
        return core, git(core, 'rev-parse', 'HEAD')

    def test_committed_notices_are_verbatim_and_worktree_edits_are_not_exported(self):
        core, revision = self.repository()
        put(core, 'LICENSE', b'uncommitted replacement')
        put(core, 'THIRD_PARTY_NOTICES.extra.txt', b'untracked neighbour')
        out = self.base / 'export'
        self.exporter.stage_inputs(core, revision, self.layout, out)
        files = self.exporter.file_hashes(out)
        self.assertEqual(set(files), {'src/demo.cpp', *self.notices})
        for name, expected in self.notices.items():
            self.assertEqual((out / name).read_bytes(), expected)
            self.assertEqual(files[name], self.ota.digest(expected))

    def test_snapshot_notices_are_exact_and_historical_omission_remains_supported(self):
        for include in (False, True):
            with self.subTest(include_notices=include):
                snapshot = {'src/demo.cpp': b'// source', 'PRIVATE_NOTES.txt': b'excluded'}
                if include:
                    snapshot.update(self.notices)
                out = self.base / ('with-notices' if include else 'historical')
                self.exporter.stage_inputs(self.base, 'unused', self.layout, out, snapshot=snapshot)
                self.assertEqual(set(self.exporter.file_hashes(out)),
                                 {'src/demo.cpp'} | (set(self.notices) if include else set()))
                for name in self.notices if include else ():
                    self.assertEqual((out / name).read_bytes(), self.notices[name])

    def test_committed_notice_symlink_is_rejected(self):
        core, _ = self.repository()
        blob = subprocess.run(['git', '-C', str(core), 'hash-object', '-w', '--stdin'],
                              input=b'PRIVATE_NOTES.txt', check=True, capture_output=True).stdout.decode().strip()
        git(core, 'update-index', '--add', '--cacheinfo', f'120000,{blob},LICENSE')
        git(core, 'commit', '-qm', 'notice symlink negative control')
        with self.assertRaisesRegex(ValueError, 'Non-regular Core source: LICENSE'):
            self.exporter.stage_inputs(core, git(core, 'rev-parse', 'HEAD'),
                                       self.layout, self.base / 'symlink')

    def package_fixture(self, *, notices=True):
        source, version = 'a' * 40, '1.2.3-beta.4'
        generated = b'// frozen source fixture'
        core_bytes = ('inert fixture ' + source + ' ' + version).encode()
        bridge_bytes = b'inert bridge fixture'
        tree = self.base / 'source'
        put(tree, 'core/src/nr_before_sr.cpp', generated)
        minhook = b'MinHook license fixture'
        ngx = b'NGX SDK license fixture'
        put(tree, 'core/third_party/minhook/LICENSE.txt', minhook)
        put(tree, 'core/build/NVIDIA-NGX-LICENSE.txt', ngx)
        if notices:
            for name, data in self.notices.items():
                put(tree, 'core/' + name, data)
        manifest = {'schema': 'dlss5-direct-source-v1', 'source_commit': source,
                    'source_dirty': False, 'source_transforms': False,
                    'integration': {'version': version},
                    'files': self.exporter.file_hashes(tree)}
        manifest_path = put(tree, 'source-manifest.json', json.dumps(manifest).encode())
        addon = put(self.base, 'test-core.addon64', core_bytes)
        bridge = put(self.base, 'nrchain_nvngx.dll', bridge_bytes)
        canonical = {'candidate': version, 'source_commit': source, 'source_verified': True,
                     'compile_verified': True, 'warp_verified': True,
                     'binaries': [{'file': addon.name, 'sha256': self.ota.digest(core_bytes)},
                                  {'file': bridge.name, 'sha256': self.ota.digest(bridge_bytes)}]}
        canonical_path = put(self.base, 'validation.json', json.dumps(canonical).encode())
        for name in ('unified8-checks', 'reconstruction-checks'):
            gate = {'passed': True, 'source_sha256': self.ota.digest(generated), 'source_commit': source}
            put(self.base, name + '/validation.json', json.dumps(gate).encode())
        candidate = {'candidate': version, 'source_commit': source,
                     'canonical_validation': str(canonical_path),
                     'binaries': [{'language': 'en', 'file': addon.name,
                                   'source_commit': source, 'sha256': self.ota.digest(core_bytes)}]}
        args = SimpleNamespace(candidate=put(self.base, 'candidate.json', json.dumps(candidate).encode()),
                               checkout=self.base, core=addon, bridge=bridge, language='en', notice=[],
                               changelog=put(self.base, 'changes.md', b'fixture changelog'), output=self.base / 'ota')
        return args, manifest_path, [minhook, ngx] + (list(self.notices.values()) if notices else [])

    def package(self, args):
        # The production release gates still run. Only the external git identity
        # query and PE/Windows inspection are isolated for these inert bytes.
        with mock.patch.object(self.ota.subprocess, 'check_output', side_effect=['a' * 40 + '\n', '']), \
             mock.patch.object(self.ota, 'pe_check', return_value=['kernel32.dll']), \
             mock.patch.object(self.ota, 'companion_check'), \
             mock.patch.object(self.ota, 'pe_version', return_value='1.2.3.4'):
            return self.ota.package(args)

    def test_verified_notices_reach_zip_and_its_hashed_manifest(self):
        args, _, notices = self.package_fixture()
        result = self.package(args)
        expected = b'\n\n'.join(notices)
        with zipfile.ZipFile(result['file']) as archive:
            self.assertEqual(archive.read('LICENSES.txt'), expected)
            manifest = json.loads(archive.read('ota-manifest.json'))
            row, = [item for item in manifest['files'] if item['name'] == 'LICENSES.txt']
            self.assertEqual(row['sha256'], self.ota.digest(expected))
            self.assertEqual(row['bytes'], len(expected))
            self.assertFalse(any(name.lower().startswith('nvngx_') for name in archive.namelist()))
        self.assertEqual(result['license_sha256'], self.ota.digest(expected))

    def test_actual_pinned_notices_reach_zip_and_stripping_is_rejected(self):
        self.actual_notices_zip_check(force_crlf=False)

    def test_crlf_pinned_notices_reach_zip_and_stripping_is_rejected(self):
        self.actual_notices_zip_check(force_crlf=True)

    def actual_notices_zip_check(self, *, force_crlf):
        # Stage the real project/notices bytes through a committed fixture, then
        # feed that export to the real OTA packager. Only executable bytes and
        # platform/source identity calls use the existing explicit doubles.
        self.notices = {name: (ROOT / name).read_bytes()
                        for name in ('LICENSE', 'THIRD_PARTY_NOTICES.txt')}
        if force_crlf:
            self.notices = {name: data.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
                            for name, data in self.notices.items()}
        text = self.notices['THIRD_PARTY_NOTICES.txt'].decode('utf-8')
        newline = '\r\n' if '\r\n' in text else '\n'
        license_texts = []
        for title, next_title, expected_sha in PINNED_NOTICE_TEXTS:
            section = text.split(newline * 2 + title + newline, 1)[1]
            section = section.split(newline * 2 + next_title + newline, 1)[0]
            body = section.split('License text from pinned source:' + newline * 2, 1)[1] + newline
            data = body.encode('utf-8')
            self.assertEqual(self.ota.digest(data.replace(b'\r\n', b'\n')), expected_sha, title)
            license_texts.append(data)
        core, revision = self.repository()
        exported = self.base / 'actual-notice-export'
        self.exporter.stage_inputs(core, revision, self.layout, exported)
        self.notices = {name: (exported / name).read_bytes() for name in self.notices}
        args, manifest_path, notices = self.package_fixture()
        # Exercise the same additional-notice path used for the external AMD
        # root license. No SDK object or runtime is part of this inert fixture.
        amd = b'AMD root license fixture\r\nverbatim additional notice\r\n'
        args.notice = [put(self.base, 'AMD-FidelityFX-LICENSE.txt', amd)]
        result = self.package(args)
        expected = b'\n\n'.join([*notices, amd])
        with zipfile.ZipFile(result['file']) as archive:
            actual = archive.read('LICENSES.txt')
            self.assertEqual(actual, expected)
            for data in license_texts:
                self.assertIn(data, actual)
            manifest = json.loads(archive.read('ota-manifest.json'))
            row, = [item for item in manifest['files'] if item['name'] == 'LICENSES.txt']
            self.assertEqual(row['sha256'], self.ota.digest(actual))
            self.assertEqual(row['bytes'], len(actual))
        self.assertEqual(result['license_sha256'], self.ota.digest(expected))
        source_manifest = json.loads(manifest_path.read_text())
        self.assertEqual(source_manifest['files']['core/THIRD_PARTY_NOTICES.txt'],
                         self.ota.digest(self.notices['THIRD_PARTY_NOTICES.txt']))
        staged = manifest_path.parent / 'core/THIRD_PARTY_NOTICES.txt'
        args.output = self.base / 'must-not-package-stripped-notices'
        for data in license_texts:
            with self.subTest(stripped_license_sha256=self.ota.digest(data)):
                staged.write_bytes(self.notices['THIRD_PARTY_NOTICES.txt'].replace(data, b'', 1))
                with self.assertRaisesRegex(ValueError, 'Exported source changed: core/THIRD_PARTY_NOTICES.txt'):
                    self.package(args)
                self.assertFalse(args.output.exists())
        staged.write_bytes(self.notices['THIRD_PARTY_NOTICES.txt'])
        args.notice[0].unlink()
        with self.assertRaises(FileNotFoundError):
            self.package(args)
        self.assertFalse(args.output.exists())

    def test_historical_package_without_root_notices_keeps_original_identity(self):
        args, _, notices = self.package_fixture(notices=False)
        result = self.package(args)
        with zipfile.ZipFile(result['file']) as archive:
            self.assertEqual(archive.read('LICENSES.txt'), b'\n\n'.join(notices))
        self.assertEqual(result['version'], '1.2.3-beta.4')

    def test_changed_and_untracked_notices_are_rejected_before_output(self):
        args, manifest_path, _ = self.package_fixture()
        notice = manifest_path.parent / 'core/LICENSE'
        notice.write_bytes(b'tampered project notice')
        with self.assertRaisesRegex(ValueError, 'Exported source changed: core/LICENSE'):
            self.package(args)
        self.assertFalse(args.output.exists())
        notice.write_bytes(self.notices['LICENSE'])
        manifest = json.loads(manifest_path.read_text())
        del manifest['files']['core/LICENSE']
        manifest_path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, 'Untracked source notice: LICENSE'):
            self.package(args)
        self.assertFalse(args.output.exists())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=Path)
    args, rest = parser.parse_known_args()
    if args.repo:
        ROOT = args.repo.resolve()
    unittest.main(argv=[sys.argv[0]] + rest)
