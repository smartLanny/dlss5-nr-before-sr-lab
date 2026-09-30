"""Isolated Git and pinned-carrier checks for the direct source exporter."""
from __future__ import annotations

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('direct_source_export', ROOT / 'scripts/prepare-core-source.py')
assert SPEC and SPEC.loader
EXPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXPORTER)
CHECK_SPEC = importlib.util.spec_from_file_location('fold_comparison', ROOT / 'scripts/validate-folded-source.py')
assert CHECK_SPEC and CHECK_SPEC.loader
CHECKER = importlib.util.module_from_spec(CHECK_SPEC)
CHECK_SPEC.loader.exec_module(CHECKER)


def git(root: Path, *args: str) -> str:
    return subprocess.run(['git', '-C', str(root), *args], check=True,
                          capture_output=True, text=True).stdout.strip()


def put(root: Path, name: str, data: bytes) -> None:
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def commit(root: Path) -> str:
    git(root, 'add', '.')
    git(root, 'commit', '-q', '-m', 'test inputs')
    return git(root, 'rev-parse', 'HEAD')


class DirectSourceExport(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix='direct-source-test-')
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.core = self.base / 'core'
        self.upstream = self.base / 'upstream'
        self.core.mkdir()
        git(self.core, 'init', '-q')
        git(self.core, 'config', 'user.email', 'source-test@example.invalid')
        git(self.core, 'config', 'user.name', 'Source Test')
        headers = {
            'src/nr_bridge_baseline.h': 'src/interop/nr_bridge_baseline.h',
            'src/nr_mirror_rect.h': 'src/interop/nr_mirror_rect.h',
            'src/nr_native_bridge_abi.h': 'src/interop/nr_native_bridge_abi.h',
            'src/nr_native_bridge_client.h': 'src/interop/nr_native_bridge_client.h',
            'src/nr_native_bridge_history.h': 'src/interop/nr_native_bridge_history.h',
            'src/nr_ngx_parameter_abi.h': 'src/interop/nr_ngx_parameter_abi.h',
        }
        self.layout = {
            'core_build_inputs': ['scripts/build.ps1'],
            'experiment_inputs': ['experiments/nr-edit/shaders/demo.hlsl'],
            'carrier_overlay': {'patch_file': 'src/interop/carrier-beta13.patch',
                                'headers': headers},
            'manifest_metadata': {section: {} for section in
                                  ('integration', 'colour_lab', 'reconstruction',
                                   'private_residual_sr')},
        }
        put(self.core, 'scripts/core-source-layout.json',
            json.dumps(self.layout).encode())
        put(self.core, 'src/interop/carrier-beta13.patch',
            (ROOT / 'src/interop/carrier-beta13.patch').read_bytes())
        put(self.core, 'src/demo.cpp', b'// committed owned source\n')
        put(self.core, 'config/demo.ini', b'enabled=1\n')
        put(self.core, 'scripts/build.ps1', b'# build helper\n')
        put(self.core, 'experiments/nr-edit/shaders/demo.hlsl', b'// shader\n')
        for source in headers.values():
            put(self.core, source, ('// ' + source + '\n').encode())
        self.core_commit = commit(self.core)

        source = Path(os.environ.get('DLSS5_UPSTREAM_ROOT', 'D:/agent-tools/dlss5-carrier'))
        if not source.is_dir() or not (source / 'src/bridge.inc').is_file():
            self.skipTest('Pinned carrier checkout required for real integration diff test')
        self.upstream.mkdir()
        git(self.upstream, 'init', '-q')
        git(self.upstream, 'config', 'user.email', 'carrier-test@example.invalid')
        git(self.upstream, 'config', 'user.name', 'Carrier Test')
        for name in git(source, 'ls-files').splitlines():
            put(self.upstream, name, (source / name).read_bytes())
        self.upstream_commit = commit(self.upstream)
        self.pin = patch.object(EXPORTER, 'UPSTREAM', self.upstream_commit)
        self.pin.start()
        self.addCleanup(self.pin.stop)

    def output(self, name='out') -> Path:
        return self.base / name

    def test_committed_source_and_real_carrier_diff_are_verbatim(self):
        out = self.output()
        EXPORTER.prepare(self.core, self.upstream, out)
        manifest = json.loads((out / 'source-manifest.json').read_text())
        self.assertEqual(manifest['source_commit'], self.core_commit)
        self.assertFalse(manifest['source_dirty'])
        self.assertFalse(manifest['source_transforms'])
        self.assertEqual((out / 'core/src/demo.cpp').read_bytes(), b'// committed owned source\n')
        self.assertEqual((out / 'core/scripts/build.ps1').read_bytes(), b'# build helper\n')
        self.assertEqual((out / 'core/experiments/nr-edit/shaders/demo.hlsl').read_bytes(), b'// shader\n')
        self.assertFalse((out / 'core/src/interop/carrier-beta13.patch').exists())
        for name, sha in manifest['files'].items():
            self.assertEqual(hashlib.sha256((out / name).read_bytes()).hexdigest(), sha, name)
        self.assertEqual(set(manifest['files']), set(EXPORTER.file_hashes(out)))
        self.assertEqual((out / 'carrier/src/nr_bridge_baseline.h').read_bytes(),
                         (out / 'core/src/interop/nr_bridge_baseline.h').read_bytes())
        self.assertNotEqual((out / 'carrier/src/bridge.inc').read_bytes(),
                            (self.upstream / 'src/bridge.inc').read_bytes())

    def test_rejects_dirty_source_wrong_or_dirty_dependency_and_existing_output(self):
        put(self.core, 'src/demo.cpp', b'changed\n')
        with self.assertRaisesRegex(ValueError, 'Commit reviewed source changes'):
            EXPORTER.prepare(self.core, self.upstream, self.output('dirty-core'))
        self.assertFalse(self.output('dirty-core').exists())
        put(self.core, 'src/demo.cpp', b'// committed owned source\n')
        with patch.object(EXPORTER, 'UPSTREAM', '0' * 40):
            with self.assertRaisesRegex(ValueError, 'pinned carrier commit'):
                EXPORTER.prepare(self.core, self.upstream, self.output('wrong-pin'))
        original_bridge = (self.upstream / 'src/bridge.inc').read_bytes()
        put(self.upstream, 'src/bridge.inc', b'changed\n')
        with self.assertRaisesRegex(ValueError, 'Dirty carrier'):
            EXPORTER.prepare(self.core, self.upstream, self.output('dirty-carrier'))
        put(self.upstream, 'src/bridge.inc', original_bridge)
        existing = self.output('existing')
        existing.mkdir()
        with self.assertRaisesRegex(ValueError, 'Output already exists'):
            EXPORTER.prepare(self.core, self.upstream, existing)

    def test_rejects_invalid_layout_path(self):
        bad = dict(self.layout)
        bad['core_build_inputs'] = ['scripts/../escape.txt']
        put(self.core, 'scripts/core-source-layout.json', json.dumps(bad).encode())
        commit(self.core)
        with self.assertRaisesRegex(ValueError, 'Unexpected Core input'):
            EXPORTER.prepare(self.core, self.upstream, self.output('bad-path'))
        self.assertFalse(self.output('bad-path').exists())

    def test_snapshot_cannot_claim_arbitrary_commit_identity(self):
        snapshot = {name: (self.core / name).read_bytes() for name in
                    git(self.core, 'ls-files').splitlines()}
        with self.assertRaises(ValueError):
            EXPORTER.prepare(self.core, self.upstream, self.output('fake-parent'),
                             snapshot=snapshot, parent='0' * 40)

    def test_snapshot_reports_dirty_identity_and_exact_source_bytes(self):
        snapshot = {name: (self.core / name).read_bytes() for name in
                    git(self.core, 'ls-files').splitlines()}
        snapshot['src/demo.cpp'] = b'// changed development snapshot\n'
        out = self.output('snapshot')
        EXPORTER.prepare(self.core, self.upstream, out,
                         snapshot=snapshot, parent=self.core_commit)
        manifest = json.loads((out / 'source-manifest.json').read_text())
        self.assertTrue(manifest['source_dirty'])
        self.assertEqual(manifest['source_commit'], self.core_commit)
        self.assertEqual((out / 'core/src/demo.cpp').read_bytes(), snapshot['src/demo.cpp'])
        self.assertEqual(manifest['files']['core/src/demo.cpp'],
                         hashlib.sha256(snapshot['src/demo.cpp']).hexdigest())

    def test_carrier_diff_requires_exact_upstream_preimage(self):
        bridge = self.upstream / 'src/bridge.inc'
        bridge.write_bytes(bridge.read_bytes().replace(b'SafeEvaluateGuarded',
                                                      b'SafeEvaluateModified', 1))
        changed_commit = commit(self.upstream)
        with patch.object(EXPORTER, 'UPSTREAM', changed_commit):
            with self.assertRaisesRegex(ValueError, 'Pinned carrier overlay failed'):
                EXPORTER.prepare(self.core, self.upstream, self.output('bad-preimage'))
        self.assertFalse(self.output('bad-preimage').exists())

    def test_committed_symlink_is_not_exported_as_regular_source(self):
        blob = subprocess.run(['git', '-C', str(self.core), 'hash-object', '-w', '--stdin'],
                              input=b'demo.cpp', check=True, capture_output=True).stdout.decode().strip()
        git(self.core, 'update-index', '--add', '--cacheinfo', f'120000,{blob},src/link.cpp')
        git(self.core, 'commit', '-q', '-m', 'add symbolic link')
        with self.assertRaises(ValueError):
            EXPORTER.stage_inputs(self.core, git(self.core, 'rev-parse', 'HEAD'),
                                  self.layout, self.output('symlink'))


class FrozenMigrationComparison(unittest.TestCase):
    def test_exact_sets_and_negative_controls(self):
        with tempfile.TemporaryDirectory(prefix='fold-comparison-test-') as temporary:
            root = Path(temporary)
            core, carrier = root / 'core', root / 'carrier'
            put(core, 'src/example.cpp', b'owned\n')
            put(carrier, 'LICENSE', b'MIT\n')
            baseline = root / 'baseline.json'
            baseline.write_text(json.dumps({
                'schema': 'dlss5-beta13-fold-baseline-v1',
                'original_manifest_sha256': 'a' * 64,
                'owned_files_sha256': {'src/example.cpp': hashlib.sha256(b'owned\n').hexdigest()},
                'carrier_files_sha256': {'LICENSE': hashlib.sha256(b'MIT\n').hexdigest()},
            }), encoding='utf-8')
            self.assertTrue(CHECKER.validate(core, carrier, baseline)['passed'])
            put(core, 'src/example.cpp', b'changed\n')
            self.assertEqual(CHECKER.validate(core, carrier, baseline)['owned']['changed'],
                             ['src/example.cpp'])
            put(core, 'src/example.cpp', b'owned\n')
            (core / 'src/example.cpp').unlink()
            self.assertEqual(CHECKER.validate(core, carrier, baseline)['owned']['missing'],
                             ['src/example.cpp'])
            put(core, 'src/example.cpp', b'owned\n')
            put(carrier, 'src/extra.cpp', b'extra\n')
            self.assertEqual(CHECKER.validate(core, carrier, baseline)['carrier']['unexpected'],
                             ['src/extra.cpp'])
            self.assertIsNone(CHECKER.validate(core, None, baseline)['carrier'])


if __name__ == '__main__':
    unittest.main()
