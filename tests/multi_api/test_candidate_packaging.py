"""Run the real PS packager against synthetic files; this is not binary validation."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SHELL = shutil.which('powershell.exe')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


@unittest.skipUnless(SHELL, 'Actual Windows PowerShell packaging contract')
class Packaging(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root/'build'
        self.stage = self.build/'source'
        self.core = self.stage/'core/build'
        self.post = self.build/'tests/post-scaled-work'
        self.core.mkdir(parents=True)
        self.post.mkdir(parents=True)
        self.commit = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
        for name, value in {
            'core/src/nr_before_sr.cpp': 'synthetic core fixture',
            'core/config/nr_before_sr.ini': '[NRBeforeSR]\nEnabled=1\n',
            'carrier/LICENSE': 'synthetic carrier license fixture',
        }.items():
            path = self.stage/name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(value)
        files = {p.relative_to(self.stage).as_posix(): digest(p) for p in self.stage.rglob('*') if p.is_file()}
        shutil.copyfile(self.stage/'core/config/nr_before_sr.ini', self.core/'nr_before_sr.ini')
        carrier = 'dlss5-native-carrier-fixture.addon64'
        binaries = [self.core/'core-zh.addon64', self.core/'core-en.addon64',
                    self.core/'nrchain_nvngx.dll', self.build/carrier]
        for path in binaries:
            path.write_text('synthetic packaging fixture, not executable')
        self.manifest = {'source_commit': self.commit, 'files': files,
                         'integration': {'source_commit': self.commit, 'version': 'synthetic-fixture', 'carrier_filename': carrier}}
        self.validation = {'source_commit': self.commit, 'source_verified': True, 'compile_verified': True,
                           'warp_verified': True, 'nvidia_feature18_verified': False, 'game_runtime_verified': False,
                           'checks': ['post scaled-work WARP: synthetic fixture'],
                           'binaries': [{'file': p.name, 'sha256': digest(p)} for p in binaries]}
        (self.post/'stdout.txt').write_text('PASS post-scaled-work WARP: 48 scenarios\n')
        (self.post/'stderr.txt').write_text('')
        (self.post/'post_scaled_work_gpu.cpp').write_text('synthetic generated harness')
        result = {'exit_code': 0, 'warp_verified': True, 'nvidia_feature18_verified': False, 'game_runtime_verified': False,
                  'staged_core_sha256': digest(self.stage/'core/src/nr_before_sr.cpp'),
                  'generated_harness_sha256': digest(self.post/'post_scaled_work_gpu.cpp'),
                  'stdout_sha256': digest(self.post/'stdout.txt'),
                  'template_sha256': digest(ROOT/'tests/post_scaled_work_gpu.cpp.in')}
        (self.post/'post-scaled-work-result.json').write_text(json.dumps(result))

    def run_package(self):
        (self.stage/'source-manifest.json').write_text(json.dumps(self.manifest))
        (self.build/'validation.json').write_text(json.dumps(self.validation))
        environment = os.environ.copy()
        # A Python child of pwsh otherwise passes PowerShell 7's module roots to PS5.
        environment = {key: value for key, value in environment.items() if key.upper() != 'PSMODULEPATH'}
        return subprocess.run([SHELL, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                               str(ROOT/'scripts/package-multi-api-candidate.ps1'), '-BuildRoot', str(self.build),
                               '-OutputRoot', str(self.root/'output')], capture_output=True, text=True, env=environment)

    def test_separate_languages_and_ini_preserved(self):
        run = self.run_package()
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        index = json.loads((self.root/'output/packages.json').read_text())
        self.assertEqual(index['version'], 'synthetic-fixture')
        self.assertEqual(len(index['packages']), 2)
        for entry in index['packages']:
            package = self.root/'output'/entry['file']
            self.assertEqual(digest(package), entry['sha256'])
            with zipfile.ZipFile(package) as archive:
                names = archive.namelist()
                self.assertFalse(any(n.lower().endswith('.ini') for n in names))
                self.assertNotIn('nr_before_sr.ini', names)
                self.assertIn('validation/post-scaled-work-result.json', names)
                self.assertEqual(len([n for n in names if n.endswith('.addon64')]), 2)

    def test_tampered_evidence_never_publishes_package(self):
        for relative in ('source/core/src/nr_before_sr.cpp', 'source/core/build/core-en.addon64',
                         'source/core/build/nr_before_sr.ini', 'tests/post-scaled-work/stdout.txt',
                         'tests/post-scaled-work/post_scaled_work_gpu.cpp'):
            with self.subTest(relative=relative):
                path = self.build/relative
                previous = path.read_bytes()
                path.write_bytes(previous + b' tampered')
                run = self.run_package()
                self.assertNotEqual(run.returncode, 0)
                self.assertIn('SHA256 mismatch', run.stdout + run.stderr)
                self.assertFalse((self.root/'output').exists())
                path.write_bytes(previous)

    def test_incomplete_checks_and_identity_rejected(self):
        self.validation['checks'] = ['baseline WARP only']
        run = self.run_package()
        self.assertNotEqual(run.returncode, 0)
        self.assertIn('baseline WARP alone is not sufficient', run.stdout + run.stderr)
        self.validation['checks'] = ['post scaled-work WARP: fixture']
        self.validation['source_commit'] = '0'*40
        run = self.run_package()
        self.assertNotEqual(run.returncode, 0)
        self.assertIn('identity disagree', run.stdout + run.stderr)
        self.assertFalse((self.root/'output').exists())


if __name__ == '__main__':
    unittest.main()
