"""Destructive paths are exercised only inside fresh temporary Git fixtures."""
from pathlib import Path
import importlib.util, tempfile, subprocess, unittest, json, hashlib, zipfile
from unittest import mock
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('archive_worktree', ROOT/'scripts/archive-retired-worktree.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class WorktreeArchival(unittest.TestCase):
    def setUp(self):
        # Ignored build/ is absent in a clean checkout; never require an earlier build.
        (ROOT/'build').mkdir(exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix='archival-test-', dir=ROOT/'build')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.repo = self.root/'repo'; self.repo.mkdir()
        self.call('init', '-b', 'main')
        self.call('config', 'user.name', 'Archive Test')
        self.call('config', 'user.email', 'archive-test@example.invalid')
        (self.repo/'tracked.txt').write_text('baseline\n', encoding='utf-8')
        (self.repo/'.gitignore').write_text('cache/\n', encoding='utf-8')
        self.call('add', '.'); self.call('commit', '-m', 'fixture')
        self.head = self.call('rev-parse', 'HEAD')
        self.ref = 'refs/tags/archive/fixture'
        self.call('tag', '-a', 'archive/fixture', '-m', 'fixture backup', self.head)
        self.src = self.root/'linked'
        self.call('worktree', 'add', '-b', 'fixture-work', str(self.src))
        (self.src/'tracked.txt').write_text('staged version\n', encoding='utf-8')
        subprocess.run(['git','-C',str(self.src),'add','tracked.txt'], check=True, capture_output=True)
        (self.src/'tracked.txt').write_text('unstaged version\n', encoding='utf-8')
        (self.src/'cache').mkdir(); (self.src/'empty').mkdir()
        (self.src/'cache/result.bin').write_bytes(bytes(range(256))*1024)
        (self.src/'未跟踪.txt').write_text('preserve Unicode\n', encoding='utf-8')
        self.out = self.root/'archive'
    def call(self,*args):
        return subprocess.check_output(['git','-C',str(self.repo),*args],stderr=subprocess.PIPE).decode().strip()
    def save(self):
        return m.archive(self.repo,self.src,self.out,self.head,self.ref)
    def remove(self):
        return m.remove(self.repo,self.out/'receipt.json',self.src)
    def test_roundtrip_index_untracked_ignored_and_empty_directories(self):
        r=self.save(); self.assertTrue(self.src.exists()); self.assertEqual(r['status'],'verified-not-removed')
        with zipfile.ZipFile(self.out/'worktree.zip') as z:
            self.assertEqual(z.read('files/未跟踪.txt'), (self.src/'未跟踪.txt').read_bytes())
            self.assertIn('staged version', z.read('git-state/staged.patch').decode())
            self.assertIn('unstaged version', z.read('git-state/unstaged.patch').decode())
            self.assertIn('files/empty/',z.namelist())
            self.assertEqual(len(z.read('files/cache/result.bin')),262144)
        done=self.remove();self.assertFalse(self.src.exists());self.assertTrue((self.out/'worktree.zip').is_file())
        self.assertFalse((self.out/'restore-check').exists());self.assertFalse(done['local_branch_deleted'])
        self.assertEqual(self.call('rev-parse',self.ref+'^{commit}'),self.head)
        # Recover an actual fresh checkout, including different staged/working
        # versions. Never restore the old .git pointer over its new binding.
        recovered=self.root/'recovered'
        self.call('worktree','add','-b','recovered-work',str(recovered),self.ref)
        with zipfile.ZipFile(self.out/'worktree.zip') as z:
            subprocess.run(['git','-C',str(recovered),'apply','--index','--binary','-'],
                           input=z.read('git-state/staged.patch'),check=True,capture_output=True)
            manifest=json.loads(z.read('manifest.json'))
            for name in manifest['directories']:(recovered/name).mkdir(parents=True,exist_ok=True)
            for name in manifest['files']:
                if name=='.git':continue
                (recovered/name).parent.mkdir(parents=True,exist_ok=True)
                (recovered/name).write_bytes(z.read('files/'+name))
            self.assertEqual(m.git(recovered,'diff','--cached','--binary','--full-index'),z.read('git-state/staged.patch'))
            self.assertEqual(m.git(recovered,'diff','--binary','--full-index'),z.read('git-state/unstaged.patch'))
            self.assertEqual(m.git(recovered,'status','--porcelain=v1','-z','--untracked-files=all'),z.read('git-state/status.z'))
    def test_modified_original_prevents_removal(self):
        self.save();(self.src/'cache/result.bin').write_bytes(b'changed')
        with self.assertRaisesRegex(RuntimeError,'Content changed'):self.remove()
        self.assertTrue(self.src.exists())
    def test_added_original_prevents_removal(self):
        self.save();(self.src/'cache/another.bin').write_bytes(b'new ignored data')
        with self.assertRaisesRegex(RuntimeError,'set changed'):self.remove()
    def test_staged_state_change_prevents_removal(self):
        self.save();subprocess.run(['git','-C',str(self.src),'add','tracked.txt'],check=True,capture_output=True)
        with self.assertRaisesRegex(RuntimeError,'Git state changed'):self.remove()
    def test_tampered_archive_prevents_removal(self):
        self.save();p=self.out/'worktree.zip';p.write_bytes(p.read_bytes()+b'corrupt trailer')
        with self.assertRaisesRegex(RuntimeError,'Archive changed'):self.remove()
    def test_tampered_restored_copy_prevents_removal(self):
        self.save();(self.out/'restore-check/files/tracked.txt').write_bytes(b'wrong')
        with self.assertRaisesRegex(RuntimeError,'Content changed'):self.remove()
    def test_wrong_ref_nested_output_and_primary_rejected(self):
        with self.assertRaisesRegex(RuntimeError,'archival tag'):m.archive(self.repo,self.src,self.out,self.head,'main')
        with self.assertRaisesRegex(RuntimeError,'must not nest'):m.archive(self.repo,self.src,self.src/'archive',self.head,self.ref)
        with self.assertRaisesRegex(RuntimeError,'controlling'):m.archive(self.repo,self.repo,self.out,self.head,self.ref)
    def test_locked_worktree_and_wrong_receipt_source_rejected(self):
        self.call('worktree','lock',str(self.src))
        with self.assertRaisesRegex(RuntimeError,'locked'):self.save()
        self.call('worktree','unlock',str(self.src));self.save()
        with self.assertRaisesRegex(RuntimeError,'mismatch'):m.remove(self.repo,self.out/'receipt.json',self.root/'other')
    def test_existing_output_and_changed_head_rejected(self):
        self.save()
        with self.assertRaisesRegex(RuntimeError,'already exists'):self.save()
        with self.assertRaisesRegex(RuntimeError,'HEAD changed'):m.archive(self.repo,self.src,self.root/'wrong','0'*40,self.ref)
    def test_manifest_tamper_rejected(self):
        self.save();p=self.out/'manifest.json';p.write_bytes(p.read_bytes()+b'\n')
        with self.assertRaisesRegex(RuntimeError,'Manifest changed'):self.remove()

class FreshCheckoutSetup(unittest.TestCase):
    def test_absent_build_directory_is_created_by_actual_setup(self):
        with tempfile.TemporaryDirectory(prefix='fresh-archival-') as directory:
            root = Path(directory)
            self.assertFalse((root/'build').exists())
            case = WorktreeArchival('test_roundtrip_index_untracked_ignored_and_empty_directories')
            with mock.patch.dict(globals(), {'ROOT': root}):
                try:
                    case.setUp()
                    self.assertTrue((root/'build').is_dir())
                    self.assertEqual(case.root.parent, root/'build')
                finally:
                    case.doCleanups()

    def test_existing_build_file_is_not_replaced(self):
        with tempfile.TemporaryDirectory(prefix='fresh-archival-') as directory:
            root = Path(directory)
            (root/'build').write_bytes(b'not a directory; preserve me')
            case = WorktreeArchival('test_roundtrip_index_untracked_ignored_and_empty_directories')
            with mock.patch.dict(globals(), {'ROOT': root}):
                with self.assertRaises(FileExistsError):
                    case.setUp()
            self.assertEqual((root/'build').read_bytes(), b'not a directory; preserve me')

if __name__=='__main__':unittest.main()
