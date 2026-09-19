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
        # A clean isolated repository exercises real source binding while allowing
        # this test to run before the implementation is committed.
        self.repo = self.root/'repo'
        for relative in ('scripts/package-multi-api-candidate.ps1', 'third_party/minhook/LICENSE.txt',
                         'THIRD_PARTY_NOTICES.txt', 'tests/post_scaled_work_gpu.cpp.in'):
            target = self.repo/relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT/relative, target)
        self.git('init', '-q')
        self.git('config', 'core.autocrlf', 'false')
        self.git('add', '.')
        self.git('-c', 'user.name=Packaging fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-qm', 'fixture')
        self.commit = self.git('rev-parse', 'HEAD').strip()
        self.face_args = []
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
        binaries = [self.core/'core-zh.addon64', self.core/'core.en.addon64',
                    self.core/'nrchain_nvngx.dll', self.build/carrier]
        for path in binaries:
            path.write_text('synthetic packaging fixture, not executable')
        self.manifest = {'source_commit': self.commit, 'files': files,
                         'integration': {'source_commit': self.commit, 'version': 'synthetic-fixture', 'carrier_filename': carrier, 'bridge_commit': 'b'*40}}
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

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.repo), *args], text=True)

    def run_package(self):
        (self.stage/'source-manifest.json').write_text(json.dumps(self.manifest))
        (self.build/'validation.json').write_text(json.dumps(self.validation))
        environment = os.environ.copy()
        # A Python child of pwsh otherwise passes PowerShell 7's module roots to PS5.
        environment = {key: value for key, value in environment.items() if key.upper() != 'PSMODULEPATH'}
        return subprocess.run([SHELL, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                               str(self.repo/'scripts/package-multi-api-candidate.ps1'), '-BuildRoot', str(self.build),
                               '-OutputRoot', str(self.root/'output'), *self.face_args], capture_output=True, text=True, encoding="utf-8", errors="replace", env=environment)

    def test_separate_languages_and_ini_preserved(self):
        run = self.run_package()
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        index = json.loads((self.root/'output/packages.json').read_text(encoding='utf-8-sig'))
        self.assertEqual(index['version'], 'synthetic-fixture')
        self.assertEqual(len(index['packages']), 2)
        self.assertEqual({e['language'] for e in index['packages']}, {'zh-CN', 'en'})
        for entry in index['packages']:
            package = self.root/'output'/entry['file']
            self.assertEqual(digest(package), entry['sha256'])
            with zipfile.ZipFile(package) as archive:
                names = archive.namelist()
                self.assertFalse(any(n.lower().endswith('.ini') for n in names))
                self.assertNotIn('nr_before_sr.ini', names)
                self.assertEqual(len(names), 7)
                self.assertIn('LICENSES.txt', names)
                self.assertIn('READ-FIRST.txt' if entry['language'] == 'en' else '安装说明.txt', names)
                self.assertIn('build-info.json', names)
                self.assertNotIn('source-manifest.json', names)
                self.assertFalse(any(n.startswith('validation/') for n in names))
                self.assertEqual(any(n.endswith('.en.addon64') for n in names), entry['language'] == 'en')
                self.assertEqual(len([n for n in names if n.endswith('.addon64')]), 2)

    def test_public_chinese_label_is_carried_by_zip_and_manifest(self):
        self.manifest['integration']['version'] = 'core'
        self.manifest['integration']['display_version'] = '0.4.5-DX11-兼容增强'
        run = self.run_package()
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        index = json.loads((self.root/'output/packages.json').read_text(encoding='utf-8-sig'))
        self.assertEqual(index['display_version'], '0.4.5-DX11-兼容增强')
        for entry in index['packages']:
            self.assertIn('0.4.5-DX11-兼容增强', entry['file'])
            with zipfile.ZipFile(self.root/'output'/entry['file']) as archive:
                self.assertIsNone(archive.testzip())
                self.assertIn('0.4.5-DX11-兼容增强', archive.read('READ-FIRST.txt' if entry['language'] == 'en' else '安装说明.txt').decode('utf-8-sig'))

    def test_tampered_evidence_never_publishes_package(self):
        for relative in ('source/core/src/nr_before_sr.cpp', 'source/core/build/core.en.addon64',
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

    def unified(self):
        self.manifest['colour_lab'] = {'source_commit': self.commit, 'version': 'beta0.5-dline21-unified1'}
        self.validation.update(quality_successor=True, full_face_backend=True)
        license_path = self.core/'NVIDIA-NGX-LICENSE.txt'
        license_path.write_text('synthetic SDK license fixture; not a vendor payload')
        (self.core/'nrchain-build.json').write_text(json.dumps({
            'input_sha256': {'LICENSE.txt': digest(license_path)},
            'binary_sha256': digest(self.core/'nrchain_nvngx.dll')}))
        receipt = {'schema': 'nr-face-build-v1', 'enabled': True, 'runtime_version': '1.23.2',
                   'runtime_sha256': 'dec964ab1ee36cc9b0ae247d13b376627992fc57dec0454354017ab8fd84f1ea',
                   'model_sha256': 'b1b3061b1ae7a43bcc1497284ade754f701db774ef3a19481edcaf27fbc425b1'}
        (self.core/'face-backend.json').write_text(json.dumps(receipt))

    def test_unified_rejects_missing_quality_face_or_wrong_overlay_identity(self):
        self.unified()
        for key, expected in (('quality_successor', 'quality-successor'), ('full_face_backend', 'complete face backend')):
            with self.subTest(key=key):
                self.validation[key] = False
                run = self.run_package()
                self.assertNotEqual(run.returncode, 0)
                self.assertIn(expected, run.stdout + run.stderr)
                self.validation[key] = True
        run = self.run_package()
        self.assertNotEqual(run.returncode, 0)
        self.assertIn('requires all pinned assets', run.stdout + run.stderr)
        self.manifest['colour_lab']['source_commit'] = '0'*40
        run = self.run_package()
        self.assertNotEqual(run.returncode, 0)
        self.assertIn('Final product source identity', run.stdout + run.stderr)
        self.assertFalse((self.root/'output').exists())

    def test_dirty_and_changed_build_sources_rejected(self):
        template = self.repo/'tests/post_scaled_work_gpu.cpp.in'
        template.write_bytes(template.read_bytes() + b'\n// dirty fixture\n')
        run = self.run_package()
        self.assertNotEqual(run.returncode, 0)
        self.assertIn('committed and clean', run.stdout + run.stderr)
        self.git('add', '.')
        self.git('-c', 'user.name=Packaging fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-qm', 'changed build source')
        run = self.run_package()
        self.assertNotEqual(run.returncode, 0)
        self.assertIn('rebuild required', run.stdout + run.stderr)
        self.assertFalse((self.root/'output').exists())

    @unittest.skipUnless(os.environ.get('DLSS5_FACE_TEST_ASSETS'), 'Set DLSS5_FACE_TEST_ASSETS to real pinned face resources')
    def test_full_face_real_assets_bilingual_and_tamper_controls(self):
        assets = Path(os.environ['DLSS5_FACE_TEST_ASSETS'])
        self.unified()
        self.face_args = ['-FaceRuntimeRoot', os.environ.get('DLSS5_FACE_TEST_RUNTIME', str(assets/'onnxruntime-win-x64-1.23.2')),
                          '-FaceModel', str(assets/'yunet-dynamic.onnx'),
                          '-FaceModelLicense', str(assets/'YUNET-LICENSE')]
        # Never alter the actual shared dependencies: negative controls use private copies.
        bad_model = self.root/'bad.onnx'
        bad_model.write_bytes((assets/'yunet-dynamic.onnx').read_bytes() + b'tampered')
        original = self.face_args[3]
        self.face_args[3] = str(bad_model)
        run = self.run_package()
        self.assertNotEqual(run.returncode, 0)
        self.assertIn('SHA256 mismatch', run.stdout + run.stderr)
        self.assertFalse((self.root/'output').exists())
        self.face_args[3] = original
        bad_runtime = self.root/'bad-runtime/lib'
        bad_runtime.mkdir(parents=True)
        (bad_runtime/'onnxruntime.dll').write_bytes(b'unapproved runtime')
        bad_license = self.root/'bad-license'
        bad_license.write_text('unapproved license')
        for position, path in ((1, bad_runtime.parent), (5, bad_license)):
            with self.subTest(tampered=path.name):
                previous = self.face_args[position]
                self.face_args[position] = str(path)
                run = self.run_package()
                self.assertNotEqual(run.returncode, 0)
                self.assertIn('SHA256 mismatch', run.stdout + run.stderr)
                self.assertFalse((self.root/'output').exists())
                self.face_args[position] = previous
        private_model = self.root/'private-model.onnx'
        shutil.copyfile(assets/'yunet-dynamic.onnx', private_model)
        model_receipt = json.loads((assets/'yunet-dynamic.json').read_text())
        model_receipt['nodes_weights_unchanged'] = False
        private_model.with_suffix('.json').write_text(json.dumps(model_receipt))
        self.face_args[3] = str(private_model)
        run = self.run_package()
        self.assertNotEqual(run.returncode, 0)
        self.assertIn('Prepared face model provenance mismatch', run.stdout + run.stderr)
        self.assertFalse((self.root/'output').exists())
        self.face_args[3] = original
        receipt_path = self.core/'face-backend.json'
        receipt = json.loads(receipt_path.read_text())
        receipt['enabled'] = False
        receipt_path.write_text(json.dumps(receipt))
        run = self.run_package()
        self.assertNotEqual(run.returncode, 0)
        self.assertIn('Face build receipt differs', run.stdout + run.stderr)
        self.assertFalse((self.root/'output').exists())
        receipt['enabled'] = True
        receipt_path.write_text(json.dumps(receipt))
        run = self.run_package()
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        index = json.loads((self.root/'output/packages.json').read_text(encoding='utf-8-sig'))
        self.assertEqual(index['version'], 'beta0.5-dline21-unified1')
        self.assertTrue(index['full_face_backend'])
        self.assertEqual({e['language'] for e in index['packages']}, {'zh-CN', 'en'})
        expected_face = {'onnxruntime.dll', 'onnxruntime_providers_shared.dll', 'LICENSE',
                         'ThirdPartyNotices.txt', 'yunet-dynamic.onnx', 'YUNET-LICENSE', 'yunet-dynamic.json'}
        for entry in index['packages']:
            self.assertIn('beta0.5-dline21-unified1', entry['file'])
            package = self.root/'output'/entry['file']
            self.assertEqual(digest(package), entry['sha256'])
            with zipfile.ZipFile(package) as archive:
                names = {n.replace('\\', '/') for n in archive.namelist() if not n.endswith('/')}
                self.assertEqual({n.removeprefix('nr_face/') for n in names if n.startswith('nr_face/')}, expected_face)
                self.assertEqual(len(names), 15)
                self.assertEqual(archive.read('NVIDIA-NGX-LICENSE.txt'),
                                 (self.core/'NVIDIA-NGX-LICENSE.txt').read_bytes())
                self.assertFalse(any(n.endswith('.ini') or 'nvngx_dlss' in n.lower() for n in names))
                info = json.loads(archive.read('build-info.json'))
                self.assertEqual(info['language'], entry['language'])
                self.assertTrue(info['full_face_backend'])
                self.assertEqual(info['face_backend_receipt_sha256'], digest(receipt_path))
                hashes = json.loads(archive.read('SHA256.json'))
                self.assertEqual(len(hashes), 14)
                for item in hashes:
                    self.assertEqual(hashlib.sha256(archive.read(item['file'])).hexdigest(), item['sha256'])


if __name__ == '__main__':
    unittest.main()
