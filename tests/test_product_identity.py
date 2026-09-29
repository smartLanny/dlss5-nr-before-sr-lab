"""Final product identities, not a replacement for real PE/build validation."""
from pathlib import Path
import importlib.util
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('identity_overlay', ROOT/'scripts/patch-colour-edge.py')
edge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(edge)


spec_private = importlib.util.spec_from_file_location('private_identity', ROOT/'scripts/patch-private-sr.py')
private = importlib.util.module_from_spec(spec_private)
spec_private.loader.exec_module(private)

class ProductIdentity(unittest.TestCase):
    def test_final_private_overlay_all_resource_fields(self):
        original=(ROOT/'src/nr_before_sr.rc').read_text(encoding='utf-8-sig')
        beta2=edge.patch_product_file(original,'src/nr_before_sr.rc')
        final=private.patch_product_identity(beta2,'src/nr_before_sr.rc')
        self.assertEqual(private.VERSION,'0.5.2-beta.11')
        self.assertIn('FILEVERSION 0,5,2,11',final)
        self.assertIn('PRODUCTVERSION 0,5,2,11',final)
        self.assertEqual(final.count('0.5.2 Beta 11'),2)
        self.assertEqual(final.count('0.5.2-beta.11'),4)
        self.assertNotIn('Beta 2',final)
        with self.assertRaises(ValueError):private.patch_product_identity(final,'src/nr_before_sr.rc')
        with self.assertRaises(ValueError):private.patch_product_identity(beta2.replace('FILEVERSION 0,5,2,2','FILEVERSION 0,5,2,3'),'src/nr_before_sr.rc')

    def test_final_private_source_and_build(self):
        for path in ('src/nr_before_sr.cpp','scripts/build.ps1'):
            original='identifier=0.5.2-beta.2; display=0.5.2 Beta 2'
            final=private.patch_product_identity(original,path)
            self.assertEqual(final,'identifier=0.5.2-beta.11; display=0.5.2 Beta 11')
            with self.assertRaises(ValueError):private.patch_product_identity('unknown',path)

    def test_actual_resource_keeps_every_non_identity_byte(self):
        original = (ROOT/'src/nr_before_sr.rc').read_text(encoding='utf-8-sig')
        final = edge.patch_product_file(original, 'src/nr_before_sr.rc')
        numeric = edge.PE_VERSION.replace('.',',')
        self.assertIn('FILEVERSION '+numeric, final)
        self.assertIn('PRODUCTVERSION '+numeric, final)
        self.assertEqual(final.count(edge.DISPLAY_VERSION), 2)
        self.assertEqual(final.count(edge.VERSION), 4)
        inverse = final.replace(edge.VERSION, 'beta0.5-dline21-uniform3')
        inverse = inverse.replace(edge.DISPLAY_VERSION, '0.5.1 dline21-uniform3 beta')
        inverse = inverse.replace('FILEVERSION '+numeric,'FILEVERSION 0,5,1,26')
        inverse = inverse.replace('PRODUCTVERSION '+numeric,'PRODUCTVERSION 0,5,1,26')
        self.assertEqual(inverse, original)

    def test_build_leaf_identity_only(self):
        original = "Output='en-beta0.5-dline21-uniform3.addon64'\nOutput='zh-beta0.5-dline21-uniform3.addon64'\n# keep ABI and flags"
        final = edge.patch_product_file(original, 'scripts/build.ps1')
        self.assertEqual(final.replace(edge.VERSION,'beta0.5-dline21-uniform3'), original)
        self.assertNotIn('beta0.5-0.5.2', final)

    def test_unknown_repeated_and_changed_anchors_fail(self):
        original = (ROOT/'src/nr_before_sr.rc').read_text(encoding='utf-8-sig')
        with self.assertRaises(ValueError):edge.patch_product_file(original, 'unknown.rc')
        with self.assertRaises(ValueError):edge.patch_product_file(original.replace('FILEVERSION 0,5,1,26','FILEVERSION 0,5,1,27'),'src/nr_before_sr.rc')
        with self.assertRaises(ValueError):edge.patch_product_file(edge.patch_product_file(original,'src/nr_before_sr.rc'),'src/nr_before_sr.rc')

    def test_short_version_paths_remain_safe(self):
        self.assertRegex(edge.VERSION, r'^0\.5\.2-beta\.[1-9][0-9]*$')
        beta = edge.VERSION.rsplit('.',1)[1]
        self.assertEqual(edge.PACKAGE_VERSION, '0.5.2-Beta-'+beta)
        self.assertEqual(edge.DISPLAY_VERSION, '0.5.2 Beta '+beta)
        self.assertEqual(edge.PE_VERSION, '0.5.2.'+beta)
        self.assertEqual(Path(edge.PACKAGE_VERSION).name, edge.PACKAGE_VERSION)


if __name__ == '__main__':
    unittest.main()
