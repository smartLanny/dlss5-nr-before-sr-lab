"""Execute the actual PowerShell identity gates without running a candidate build."""
import json
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHELL = shutil.which('pwsh') or shutil.which('powershell')


def block(file, start, end):
    text = (ROOT / 'scripts' / file).read_text(encoding='utf-8-sig')
    return text[text.index(start):text.index(end, text.index(start))]


@unittest.skipUnless(SHELL, 'PowerShell is required')
class CandidateIdentity(unittest.TestCase):
    def run_gate(self, manifest, quality, candidate, accepted=True):
        snippet = block('build-perf12-candidate.ps1', '$GeneratedVersion =', '$ValidationPath =')
        data = json.dumps(manifest).replace("'", "''")
        command = ("$ErrorActionPreference='Stop'; Set-StrictMode -Version Latest; "
                   f"$Manifest=ConvertFrom-Json '{data}'; $Head='abc'; "
                   f"$QualitySuccessor=${str(quality).lower()}; $CandidateVersion='{candidate}';\n" + snippet)
        result = subprocess.run([SHELL, '-NoProfile', '-Command', command], capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, accepted, result.stdout + result.stderr)

    def test_private_overrides_inherited_colour_identity(self):
        self.run_gate({'source_commit': 'abc', 'colour_lab': {'version': 'old-ui1'},
                       'private_residual_sr': {'version': '0.5.2-beta.3', 'source_commit': 'abc'}},
                      True, '0.5.2-beta.3')

    def test_wrong_private_source_and_version_rejected(self):
        for source, version in [('wrong', '0.5.2-beta.3'), ('abc', '0.5.2-beta.2')]:
            self.run_gate({'source_commit': 'abc', 'colour_lab': {'version': '0.5.2-beta.3'},
                           'private_residual_sr': {'version': version, 'source_commit': source}},
                          True, '0.5.2-beta.3', False)

    def test_historical_quality_and_perf_paths(self):
        self.run_gate({'source_commit': 'abc', 'colour_lab': {'version': 'old-ui1'}}, True, 'old-ui1')
        self.run_gate({'source_commit': 'abc', 'integration': {'version': 'old-perf12'}}, False, 'old-perf12')

    def test_short_and_historical_package_names(self):
        snippet = block('build-perf12-candidate.ps1', '$ShortIdentity =', '$PrivateSymbols =')
        for version, leaf, prefix in [('0.5.2-beta.3', '0.5.2-beta.3', 'DLSS5-Core-'),
                                     ('beta0.5-dline21-perf12', 'perf12', 'DLSS5-D21-')]:
            command = f"$CandidateVersion='{version}'; $LeafVersion='{leaf}';\n" + snippet
            command += f"\nif ($PackagePrefix -ne '{prefix}') {{ throw 'wrong package identity' }}"
            result = subprocess.run([SHELL, '-NoProfile', '-Command', command], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_bilingual_and_pe_identity_gate(self):
        snippet = block('build-multi-api-candidate.ps1',
                        '    if ($Addons.Count -ne 2)', '    $Binaries =')
        for second_name, revision, accepted in [
            ('core-0.5.2-beta.3.addon64', 3, True),
            ('other-0.5.2-beta.3.en.addon64', 3, False),
            ('core-0.5.2-beta.2.addon64', 3, False),
            ('core-0.5.2-beta.3.addon64', 2, False),
        ]:
            command = ("$ErrorActionPreference='Stop'; $ProductVersion='0.5.2-beta.3'; "
                       "$v=[pscustomobject]@{FileMajorPart=0;FileMinorPart=5;FileBuildPart=2;"
                       f"FilePrivatePart={revision}}}; "
                       "$Addons=@([pscustomobject]@{Name='core-0.5.2-beta.3.en.addon64';VersionInfo=$v},"
                       f"[pscustomobject]@{{Name='{second_name}';VersionInfo=$v}});\n" + snippet)
            result = subprocess.run([SHELL, '-NoProfile', '-Command', command], capture_output=True, text=True)
            self.assertEqual(result.returncode == 0, accepted, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
