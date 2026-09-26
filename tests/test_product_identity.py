"""Final product identities, not a replacement for real PE/build validation."""
from pathlib import Path
import importlib.util
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('identity_overlay', ROOT/'scripts/patch-colour-edge.py')
edge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(edge)


class ProductIdentity(unittest.TestCase):
    def test_actual_resource_keeps_every_non_identity_byte(self):
        original = (ROOT/'src/nr_before_sr.rc').read_text(encoding='utf-8-sig')
        final = edge.patch_product_file(original, 'src/nr_before_sr.rc')
        self.assertIn('FILEVERSION 0,5,2,1', final)
        self.assertIn('PRODUCTVERSION 0,5,2,1', final)
        self.assertEqual(final.count('0.5.2 Beta 1'), 2)
        self.assertEqual(final.count('0.5.2-beta.1'), 4)
        inverse = final.replace(edge.VERSION, 'beta0.5-dline21-uniform3')
        inverse = inverse.replace(edge.DISPLAY_VERSION, '0.5.1 dline21-uniform3 beta')
        inverse = inverse.replace('FILEVERSION 0,5,2,1','FILEVERSION 0,5,1,26')
        inverse = inverse.replace('PRODUCTVERSION 0,5,2,1','PRODUCTVERSION 0,5,1,26')
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
        self.assertEqual(edge.VERSION, '0.5.2-beta.1')
        self.assertEqual(edge.PACKAGE_VERSION, '0.5.2-Beta-1')
        self.assertEqual(edge.PE_VERSION, '0.5.2.1')
        self.assertEqual(Path(edge.PACKAGE_VERSION).name, edge.PACKAGE_VERSION)


if __name__ == '__main__':
    unittest.main()
