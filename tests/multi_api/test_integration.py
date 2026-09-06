"""Transaction tests use a staging fixture; they are not a compiled core test."""
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('multi_prepare', ROOT/'scripts/prepare-multi-api-beta.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
NAMES = 'return Equal(name, L"dlss5-native-carrier-045-exp3.addon64");'
DEPLOYMENT = ('return nr_dx11_deployment::duplicate_carriers.load(std::memory_order_acquire) ||\n'
              '        !nr_dx11_deployment::Read().complete;')

def fixture(core, upstream, root):
    directory = root/'core/src/interop'
    directory.mkdir(parents=True)
    (directory/'nr_deployment_names.h').write_text(NAMES)
    (directory/'nr_dx11_deployment.inl').write_text(DEPLOYMENT)
    (root/'source-manifest.json').write_text('{}')

def fake_git(root, *args):
    if args[:1] == ('status',): return b''
    if args == ('rev-parse', 'HEAD'): return (m.UPSTREAM if root.name == 'upstream' else 'a'*40).encode()
    raise AssertionError(args)

class Staging(unittest.TestCase):
    def test_finalize_removes_only_diagnostic_gate(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)/'stage'; fixture(None,None,root)
            m.finalize(root, root, 'a'*40)
            d=(root/'core/src/interop/nr_dx11_deployment.inl').read_text()
            self.assertIn('duplicate_carriers.load',d)
            self.assertNotIn('!nr_dx11_deployment::Read().complete',d)
            self.assertIn('multiapi-dev1', (root/'core/src/interop/nr_deployment_names.h').read_text())
            manifest=json.loads((root/'source-manifest.json').read_text())
            self.assertEqual(manifest['files'],m.file_hashes(root))
            self.assertEqual(manifest['integration']['parents_in_history'],[m.PR131,m.PR132])
            self.assertEqual(manifest['integration']['feedback'],'F8 single TXT; F7 optional paired archive')

    def test_nested_manifest_not_excluded(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); (root/'nested').mkdir()
            (root/'source-manifest.json').write_text('{}')
            (root/'nested/source-manifest.json').write_text('child')
            self.assertEqual(list(m.file_hashes(root)),['nested/source-manifest.json'])

    def test_failure_does_not_publish_partial_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); out=root/'output'
            with patch.object(m,'git',side_effect=fake_git), \
                 patch.object(m,'configure',return_value=SimpleNamespace(prepare=fixture)), \
                 patch.object(m,'finalize',side_effect=ValueError('late failure')):
                with self.assertRaisesRegex(ValueError,'late failure'):
                    m.prepare(root/'core',root/'upstream',out)
            self.assertFalse(out.exists())
            self.assertEqual(list(root.iterdir()),[])

    def test_success_publishes_finalized_candidate(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); out=root/'output'
            with patch.object(m,'git',side_effect=fake_git), \
                 patch.object(m,'configure',return_value=SimpleNamespace(prepare=fixture)):
                m.prepare(root/'core',root/'upstream',out)
            self.assertEqual(list(root.iterdir()),[out])
            manifest=json.loads((out/'source-manifest.json').read_text())
            self.assertEqual(manifest['files'],m.file_hashes(out))

    def test_existing_output_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); (root/'sentinel').write_text('keep')
            with self.assertRaisesRegex(ValueError,'already exists'):
                m.prepare(root,root,root)
            self.assertEqual((root/'sentinel').read_text(),'keep')

    def test_missing_identity_anchor_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)/'stage'; fixture(None,None,root)
            (root/'core/src/interop/nr_deployment_names.h').write_text('changed')
            with self.assertRaisesRegex(ValueError,'identity anchor'):
                m.finalize(root,root,'a'*40)

if __name__ == '__main__': unittest.main()
