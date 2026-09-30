"""The old dev2 entry is an alias, not a second set of source transformations.
Canonical composition, transaction and final manifest cases live in
 test_picture_integration.py and test_integration.py.
"""
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('dev2_alias',ROOT/'scripts/prepare-compat-dev2.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class Generator(unittest.TestCase):
    def parent(self,ancestor=True,version=m.VERSION):
        return SimpleNamespace(is_ancestor=lambda root,ref:ancestor,VERSION=version,
            configure=object(),finalize=object(),prepare=Mock())

    def test_missing_real_parent_refuses_without_mutation(self):
        parent=self.parent(False); original=vars(parent).copy()
        with self.assertRaisesRegex(ValueError,'ancestry'):m.configure(parent,ROOT)
        self.assertEqual(vars(parent),original)

    def test_alias_never_wraps_or_duplicates_transformations(self):
        parent=self.parent(); original=vars(parent).copy()
        self.assertIs(m.configure(parent,ROOT),parent)
        self.assertIs(m.configure(parent,ROOT),parent)
        self.assertEqual(vars(parent),original)

    def test_wrong_canonical_version_is_not_silently_relabelled(self):
        parent=self.parent(version='dev1')
        with self.assertRaisesRegex(ValueError,'identity'):m.configure(parent,ROOT)
        self.assertEqual(parent.VERSION,'dev1')

    def test_cli_calls_canonical_transaction_once(self):
        parent=self.parent()
        with patch.object(m,'load',return_value=parent), patch('sys.argv',[
                'prepare-compat-dev2.py','--core',str(ROOT),'--upstream',str(ROOT/'upstream'),
                '--output',str(ROOT/'candidate')]):
            m.main()
        parent.prepare.assert_called_once_with(ROOT.resolve(),(ROOT/'upstream').resolve(),(ROOT/'candidate').resolve())

if __name__=='__main__':unittest.main()
