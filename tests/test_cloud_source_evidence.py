"""Actual Git/blob export and filesystem evidence retention; no GPU doubles."""
import hashlib
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT/'scripts/ci'/f'{name}.py')
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


exporter = module('export-reviewed-source')
collector = module('collect-f8-evidence')


class SourceEvidence(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_committed_blobs_ignore_dirty_untracked_and_eol_attributes(self):
        if not shutil.which('git'):
            self.skipTest('Git unavailable')
        repo = self.root/'repo'
        repo.mkdir()
        def git(*args):
            return subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True)
        git('init')
        git('config', 'user.name', 'fixture')
        git('config', 'user.email', 'fixture@example.invalid')
        (repo/'.gitattributes').write_text('*.txt text eol=crlf\n')
        (repo/'input.txt').write_bytes(b'committed\ninput\n')
        git('add', '.')
        git('commit', '-m', 'fixture')
        committed = git('show', 'HEAD:input.txt').stdout
        (repo/'input.txt').write_bytes(b'not-committed')
        (repo/'untracked-secret.txt').write_bytes(b'never-export')
        receipt = exporter.export(repo, self.root/'snapshot')
        with zipfile.ZipFile(self.root/'snapshot/committed-source.zip') as zipped:
            self.assertEqual(zipped.read('input.txt'), committed)
            self.assertNotIn('untracked-secret.txt', zipped.namelist())
            self.assertNotIn('.git/config', zipped.namelist())
        info = next(x for x in receipt['files'] if x['path']=='input.txt')
        self.assertEqual(info['sha256'], hashlib.sha256(committed).hexdigest())
        self.assertEqual(info['git_blob'], git('rev-parse', 'HEAD:input.txt').stdout.decode().strip())
        with self.assertRaises(FileExistsError):
            exporter.export(repo, self.root/'snapshot')

    def test_git_symlink_records_do_not_copy_targets(self):
        if not shutil.which('git'):
            self.skipTest('Git unavailable')
        repo = self.root/'links'
        repo.mkdir()
        def git(*args, **kwargs):
            return subprocess.run(['git', '-C', str(repo), *args], check=True,
                                  capture_output=True, **kwargs)
        git('init')
        git('config', 'user.name', 'fixture')
        git('config', 'user.email', 'fixture@example.invalid')
        (repo/'source.cpp').write_bytes(b'// committed source\n')
        git('add', 'source.cpp')
        # Index-only links also exercise this contract on Windows without
        # requiring developer-mode symlink privileges or following any target.
        for name, target in (('alias', b'source.cpp'),
                             ('missing', b'not-present'),
                             ('outside', b'/private/user-data')):
            oid = git('hash-object', '-w', '--stdin', input=target).stdout.decode().strip()
            git('update-index', '--add', '--cacheinfo', '120000', oid, name)
        git('commit', '-m', 'link fixture')
        receipt = exporter.export(repo, self.root/'snapshot')
        with zipfile.ZipFile(self.root/'snapshot/committed-source.zip') as zipped:
            self.assertEqual(zipped.namelist(), ['source.cpp'])
        self.assertEqual(len(receipt['unmaterialized_symlinks']), 3)
        for item in receipt['unmaterialized_symlinks']:
            self.assertEqual(item['status'], 'not-materialized')
            self.assertEqual(item['git_blob'], git('rev-parse', 'HEAD:'+item['path']).stdout.decode().strip())
            self.assertNotIn('target', item)

    def test_nested_f8_logs_and_generated_source_survive_without_payloads(self):
        original = b'error\r\n\xff\x00'
        for name in collector.ROOTS:
            base = self.root/'test-output'/name/'attempt-a'/'processes'/'execute'
            base.mkdir(parents=True)
            for file in ('stderr.log', 'process.json', 'controller.cpp', 'ci_warp_runtime.h', 'screen.png', 'model.dll', 'controller.exe', 'feedback.zip'):
                (base/file).write_bytes(original)
        result = collector.collect(self.root, self.root/'build/cloud-evidence/f8')
        self.assertEqual(len(result['files']), 8)
        for item in result['files']:
            target = self.root/'build/cloud-evidence/f8'/item['retained']
            self.assertEqual(target.read_bytes(), original)
            self.assertIn(target.suffix, ('.json', '.log', '.txt'))
            self.assertEqual(item['sha256'], hashlib.sha256(original).hexdigest())

    def test_missing_output_and_oversize_are_not_success(self):
        source = self.root/'test-output/f8-gpu-v4/attempt-a'
        source.mkdir(parents=True)
        (source/'large.log').write_bytes(b'x'*100)
        (source/'stderr-tail.log').write_bytes(b'failure')
        result = collector.collect(self.root, self.root/'out', file_limit=20)
        self.assertEqual(result['roots']['f8-controller-v4'], 'not-produced')
        self.assertEqual(result['files'][0]['status'], 'omitted-size-limit')
        self.assertEqual(result['files'][1]['status'], 'copied')
        self.assertNotIn('passed', result)

    def test_linked_evidence_is_rejected(self):
        source = self.root/'test-output/f8-gpu-v4'
        source.mkdir(parents=True)
        outside = self.root/'private.log'
        outside.write_bytes(b'private')
        try:
            (source/'link.log').symlink_to(outside)
        except OSError as error:
            self.skipTest('Symlink unavailable: '+str(error))
        with self.assertRaisesRegex(ValueError, 'Linked'):
            collector.collect(self.root, self.root/'out')

    def test_workflow_retains_f8_before_unchanged_evidence_upload(self):
        workflow = (ROOT/'.github/workflows/compat-post-dev2.yml').read_text()
        step = workflow.split('- name: Retain F8 attempt evidence even on failure', 1)[1]
        self.assertIn('if: always()', step.split('- name:', 1)[0])
        self.assertIn('collect-f8-evidence.py --output build/cloud-evidence/f8', step)
        self.assertLess(workflow.index('collect-f8-evidence.py'), workflow.index('- name: Upload software/WARP evidence'))
        self.assertIn('build/**/*.log', workflow)
        self.assertNotIn('test-output/**/*.zip', workflow)


if __name__ == '__main__':
    unittest.main(verbosity=2)
