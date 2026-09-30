"""Check the maintained product identity before PE/build validation."""

import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ProductIdentity(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.layout = json.loads((ROOT / 'scripts/core-source-layout.json').read_text(encoding='utf-8'))
        cls.product = cls.layout['product']
        cls.core = (ROOT / 'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
        cls.resource = (ROOT / 'src/nr_before_sr.rc').read_text(encoding='utf-8-sig')
        cls.build = (ROOT / 'scripts/build.ps1').read_text(encoding='utf-8-sig')

    def test_version_fields_agree(self):
        version = self.product['version']
        # Canonical packaging selects this final section, not the historical
        # integration/colour versions retained in the same manifest.
        self.assertEqual(self.layout['manifest_metadata']['private_residual_sr']['version'], version)
        display = self.product['display_version']
        pe = ','.join(map(str, self.product['pe_version']))
        self.assertRegex(version, r'^0\.5\.2-beta\.[1-9][0-9]*$')
        self.assertEqual(self.product['pe_version'][:3], [0, 5, 2])
        self.assertEqual(display, f"0.5.2 Beta {self.product['pe_version'][3]}")
        self.assertIn(f'FILEVERSION {pe}', self.resource)
        self.assertIn(f'PRODUCTVERSION {pe}', self.resource)
        self.assertEqual(self.resource.count(display), 2)
        self.assertEqual(self.resource.count(version), 4)
        self.assertIn(f'kAddonVersion = "{display}"', self.core)
        self.assertIn(f'kBuildIdentity = "{version}"', self.core)
        self.assertIn(version, self.build)

    def test_package_names_use_same_identity(self):
        display = self.product['display_version']
        names = re.findall(r'const char \*NAME =\s*"([^"]+)"', self.core)
        self.assertEqual(len(names), 2)
        for name in names:
            self.assertIn(display, name)
        self.assertNotIn('dline21-uniform3', '\n'.join(names))
        package = f"0.5.2-Beta-{self.product['pe_version'][3]}"
        self.assertEqual(Path(package).name, package)
        self.assertNotIn('beta0.5-0.5.2', self.build)


if __name__ == '__main__':
    unittest.main()
