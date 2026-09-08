"""Fail-closed tests for the focused Core-only acceptance packager."""
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('package050', ROOT / 'scripts/package-050-core-acceptance.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class PackageGuards(unittest.TestCase):
    def test_verified_file_and_corruption(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'good').write_bytes(b'original')
            digest = m.sha(b'original')
            self.assertEqual(m.checked_file(root, 'good', digest), b'original')
            (root / 'good').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'Hash mismatch'):
                m.checked_file(root, 'good', digest)

    def test_path_escape_and_absolute_outside_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            parent = Path(tmp)
            root = parent / 'stage'
            root.mkdir()
            external = parent / 'outside'
            external.write_bytes(b'outside')
            for name in ('../outside', str(external)):
                with self.subTest(name=name):
                    with self.assertRaisesRegex(ValueError, 'Unsafe manifest'):
                        m.checked_file(root, name, m.sha(b'outside'))

    def test_preserve_existing_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            evidence = root / 'existing'
            evidence.mkdir()
            marker = evidence / 'original'
            marker.write_bytes(b'keep')
            with self.assertRaisesRegex(ValueError, 'already exists'):
                m.package(root / 'missing-build', evidence)
            self.assertEqual(marker.read_bytes(), b'keep')

    def fixture(self, build):
        stage = build / 'source'
        stage.mkdir()
        commit = 'a' * 40
        manifest = {'source_commit': commit, 'integration': {'source_commit': commit, 'version': m.VERSION}}
        raw = json.dumps(manifest).encode()
        (stage / 'source-manifest.json').write_bytes(raw)
        result = {'schema': 'nr050-focused-acceptance-v1', 'success': True,
                  'source_commit': commit, 'source_manifest_sha256': m.sha(raw),
                  'warp_verified': True, 'carrier_compiled': False,
                  'nvidia_feature18_verified': False, 'game_runtime_verified': False,
                  'five_percent_performance_gate': 'unmeasured', 'checks': []}
        return result

    def test_capability_promotion_and_missing_checks_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp)
            original = self.fixture(build)
            changes = ({}, {'success': False}, {'warp_verified': False},
                       {'carrier_compiled': True}, {'game_runtime_verified': True},
                       {'nvidia_feature18_verified': True}, {'five_percent_performance_gate': 'pass'},
                       {'source_commit': 'b' * 40})
            for change in changes:
                with self.subTest(change=change):
                    (build / 'validation.json').write_text(json.dumps(dict(original, **change)), encoding='utf-8')
                    with patch.object(m, 'git', side_effect=AssertionError('No Git operation before evidence admission')):
                        with self.assertRaisesRegex(ValueError, 'evidence required|gate missing'):
                            m.package(build, build / 'new-output')
                    self.assertFalse((build / 'new-output').exists())

if __name__ == '__main__':
    unittest.main(verbosity=2)
