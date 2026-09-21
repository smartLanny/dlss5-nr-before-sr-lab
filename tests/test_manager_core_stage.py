"""Reject ambiguous identities and unsafe component inputs; no runtime claims."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('stage', Path(__file__).resolve().parents[1]/'scripts/stage-manager-core-component.py')
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)


class ComponentInputTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_unsafe_paths_rejected(self):
        for name in ('../outside', 'D:/outside', '/outside', 'nr_face\\runtime', ''):
            with self.subTest(name=name), self.assertRaises(ValueError):
                stage.child(self.root, name)

    def test_changed_file_rejected(self):
        file = self.root/'core'; file.write_bytes(b'original')
        digest = stage.sha(file)
        self.assertEqual(stage.checked(self.root, 'core', digest), file)
        file.write_bytes(b'tampered')
        with self.assertRaises(ValueError): stage.checked(self.root, 'core', digest)

    def test_duplicate_hash_inventory_rejected(self):
        with self.assertRaises(ValueError):
            stage.file_hashes([{'file':'Core','sha256':'a'}, {'file':'core','sha256':'b'}])

    def test_duplicate_json_identity_rejected(self):
        file = self.root/'identity.json'; file.write_text('{"source":1,"source":2}')
        with self.assertRaises(ValueError): stage.read(file)

    def test_pristine_bom_template(self):
        file = self.root/'config.ini'; file.write_text('[NRBeforeSR]\nEnabled=1\n', encoding='utf-8-sig')
        self.assertEqual(stage.template_values(file), {'Enabled':'1'})

    def test_duplicate_template_not_user_ini_parser(self):
        file = self.root/'config.ini'
        for content in ('[NRBeforeSR]\nEnabled=1\n[nrbeforesr]\nEnabled=0',
                        '[NRBeforeSR]\nEnabled=1\nenabled=0'):
            file.write_text(content)
            with self.assertRaises(ValueError): stage.template_values(file)

    def test_non_pe_cannot_claim_provider_exports(self):
        file = self.root/'core'; file.write_bytes(b'not a PE')
        with self.assertRaises(ValueError): stage.pe_exports(file)


if __name__ == '__main__': unittest.main()
