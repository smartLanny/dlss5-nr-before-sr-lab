"""Exercise the production PS5 source-identity writer, not a duplicate encoder."""
from __future__ import annotations
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'scripts/build.ps1'
SHA = 'af3c932126d845c9a29b903a5df680b419395301'


def ps_quote(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


class BuildSourceIdentity(unittest.TestCase):
    def test_bom_and_separate_unicode_output(self):
        self.assertTrue(BUILD.read_bytes().startswith(b'\xef\xbb\xbf'))
        text = BUILD.read_text(encoding='utf-8-sig')
        self.assertEqual(text.count('LinkLeaf = '), 2)
        self.assertIn("'-o', $LinkOutput", text)
        self.assertIn('Move-Item -LiteralPath $LinkOutput -Destination $Output', text)
        self.assertIn('@Common $SourceDefines', text)

    @unittest.skipUnless(os.name == 'nt', 'Windows PowerShell native marshalling test')
    def test_actual_production_response_with_windows_compiler(self):
        root = os.environ.get('DLSS5_LLVM_MINGW_ROOT', os.environ.get('TOOLCHAIN_ROOT', ''))
        compiler = str(Path(root) / 'bin/x86_64-w64-mingw32-clang++.exe') if root else shutil.which('clang++')
        self.assertTrue(compiler and Path(compiler).is_file(), 'Set DLSS5_LLVM_MINGW_ROOT for the Windows build test')
        ps = shutil.which('powershell.exe')
        self.assertIsNotNone(ps, 'Windows PowerShell 5 must be exercised')
        text = BUILD.read_text(encoding='utf-8-sig')
        matches = re.findall(r'(?ms)^function New-NrSourceDefinesResponse \{.*?^\}', text)
        self.assertEqual(len(matches), 1)
        with tempfile.TemporaryDirectory(prefix='nr identity with spaces ') as directory:
            work = Path(directory)
            response = work / 'source-defines.rsp'
            script = work / 'production-writer.ps1'
            source = work / 'identity.cpp'
            exe = work / 'identity.exe'
            for dirty in ('0', '1'):
                script.write_text("$ErrorActionPreference='Stop'\n" + matches[0] + '\n' +
                    'New-NrSourceDefinesResponse -Path ' + ps_quote(str(response)) +
                    ' -Commit ' + ps_quote(SHA) + ' -Dirty ' + ps_quote(dirty) + '\n', encoding='utf-8-sig')
                subprocess.run([ps, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(script)], check=True, capture_output=True)
                source.write_text('#include <cstring>\nconstexpr char identity[]=NR_SOURCE_COMMIT;\n'
                    'static_assert(sizeof(identity)==41);\nstatic_assert(NR_SOURCE_DIRTY==' + dirty + ');\n'
                    'int main(){return std::strcmp(identity,"' + SHA + '");}\n', encoding='utf-8')
                subprocess.run([compiler, '@' + str(response), '-std=c++17', '-static', str(source), '-o', str(exe)], check=True, capture_output=True)
                subprocess.run([str(exe)], check=True)
            script.write_text("$ErrorActionPreference='Stop'\n" + matches[0] + '\n' +
                'New-NrSourceDefinesResponse -Path ' + ps_quote(str(response)) +
                " -Commit 'not-a-sha' -Dirty '0'\n", encoding='utf-8-sig')
            result = subprocess.run([ps, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(script)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            # A rejected identity must not overwrite the previous valid response.
            self.assertIn(SHA, response.read_text(encoding='utf-8'))


if __name__ == '__main__':
    unittest.main()
