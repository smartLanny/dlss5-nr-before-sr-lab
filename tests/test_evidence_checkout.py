"""Exercise the real attributes with synthetic mixed-EOL evidence, not player data."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class EvidenceCheckoutTests(unittest.TestCase):
    def test_archive_bytes_and_clean_identity(self):
        attributes = (ROOT / '.gitattributes').read_text(encoding='utf-8')
        for autocrlf in ('true', 'false'):
            with self.subTest(autocrlf=autocrlf), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                env = os.environ.copy()
                env['GIT_CONFIG_NOSYSTEM'] = '1'
                env['GIT_CONFIG_GLOBAL'] = os.devnull

                def git(*args):
                    return subprocess.run(['git', '-C', str(root), *args],
                        check=True, capture_output=True, env=env).stdout

                git('init', '-q')
                git('config', 'user.name', 'Synthetic checkout regression')
                git('config', 'user.email', 'checkout-test@example.invalid')
                git('config', 'core.autocrlf', autocrlf)
                git('config', 'core.safecrlf', 'false')
                (root / '.gitattributes').write_text(attributes, encoding='utf-8', newline='\n')
                name = 'bug-inbox/synthetic/nr-before-sr.log.txt'
                evidence = root / name
                evidence.parent.mkdir(parents=True)
                original = b'first synthetic line\r\nsecond synthetic line\nlast\r\n'
                evidence.write_bytes(original)
                source = root / 'test.cpp'
                source.write_bytes(b'int synthetic_value = 1;\n')
                git('add', '.gitattributes', name, 'test.cpp')
                git('commit', '-qm', 'Synthetic fixture only')
                self.assertEqual(git('show', 'HEAD:' + name), original)
                evidence.unlink()
                git('checkout', '--', name)
                self.assertEqual(evidence.read_bytes(), original)
                self.assertEqual(git('status', '--porcelain', '--untracked-files=no'), b'')
                attrs = git('check-attr', 'text', 'eol', '--', name).decode()
                self.assertIn('text: unset', attrs)
                self.assertIn('eol: unspecified', attrs)

                # A genuine runtime edit must still taint the same identity gate.
                source.write_bytes(b'int synthetic_value = 2;\n')
                self.assertIn(b'test.cpp', git('status', '--porcelain', '--untracked-files=no'))
                git('checkout', '--', 'test.cpp')

                # Keep the archived blob untouched, then restore old attributes:
                # a fresh checkout must reproduce the former dirty-tree defect.
                old_attributes = attributes.replace('bug-inbox/** -text !eol', '')
                self.assertNotEqual(old_attributes, attributes)
                (root / '.gitattributes').write_text(old_attributes, encoding='utf-8', newline='\n')
                git('add', '.gitattributes')
                git('commit', '-qm', 'Synthetic old-policy negative control')
                evidence.unlink()
                git('checkout', '--', name)
                self.assertIn(name.encode(), git('status', '--porcelain', '--untracked-files=no'))


if __name__ == '__main__':
    unittest.main(verbosity=2)
