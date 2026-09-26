"""Validate source-retirement provenance, not feature parity or game behavior."""
from pathlib import Path
import hashlib,json,subprocess,unittest
R=Path(__file__).resolve().parents[1]
class ArchiveIndex(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.d=json.loads((R/'docs/maintenance/ARCHIVE_INDEX_20260926.json').read_text(encoding='utf-8'))
 def test_all_original_remote_heads_have_unique_retention_entries(self):
  rows=self.d['source_refs'];self.assertEqual(len(rows),177);self.assertEqual(len({r['original_branch'] for r in rows}),177)
  self.assertEqual(sum(r['phase']==1 for r in rows),84);self.assertEqual(sum(r['phase']==2 for r in rows),93)
  self.assertNotIn('main',{r['original_branch'] for r in rows})
  self.assertNotIn('codex/051-guide-domain-audit-20260925',{r['original_branch'] for r in rows})
 def test_retained_commit_or_exact_tag_resolves(self):
  for row in self.d['source_refs']:
   with self.subTest(branch=row['original_branch']):
    commit=row['commit'];self.assertRegex(commit,r'^[0-9a-f]{40}$')
    if row['phase']==1:
     subprocess.run(['git','merge-base','--is-ancestor',commit,self.d['snapshot_main']],cwd=R,check=True,capture_output=True)
    else:
     found=subprocess.check_output(['git','rev-parse',row['preserved_by']+'^{commit}'],cwd=R,text=True).strip()
     self.assertEqual(found,commit)
     self.assertTrue(row['path_summary_is_not_equivalence_proof']);self.assertTrue(row['successor_issues'])
 def test_unique_local_work_and_dirty_research_are_not_omitted(self):
  rows=self.d['local_worktree_heads'];self.assertEqual(len(rows),36)
  self.assertEqual(len({r['private_worktree_archive_key'] for r in rows}),36)
  research=next(r for r in rows if 'post-residual-sr' in r['original_directory_leaf'])
  self.assertTrue(research['had_local_changes'])
  for row in rows:
   actual=subprocess.check_output(['git','rev-parse',row['preserved_by']+'^{commit}'],cwd=R,text=True).strip()
   self.assertEqual(actual,row['head'])
 def test_local_branch_aliases_have_exact_retention(self):
  rows=self.d['local_branch_aliases'];self.assertEqual(len(rows),222)
  self.assertEqual(len({r['original_local_ref'] for r in rows}),222)
  for row in rows:
   self.assertTrue(row['preserved_by'].startswith('refs/tags/archive/'))
   actual=subprocess.check_output(['git','rev-parse',row['preserved_by']+'^{commit}'],cwd=R,text=True).strip()
   self.assertEqual(actual,row['commit'])
  recovered=self.d['reflog_only_commits'];self.assertEqual(len(recovered),3)
  for row in recovered:
   actual=subprocess.check_output(['git','rev-parse',row['preserved_by']+'^{commit}'],cwd=R,text=True).strip()
   self.assertEqual(actual,row['commit'])
 def test_old_cleanup_proposal_is_byte_exact_not_rewritten(self):
  old=subprocess.check_output(['git','show','c66f4a064bd150dcc775a8f432c60b4916b22e42:docs/maintenance/REPO_CLEANUP_20260925.md'],cwd=R)
  actual=(R/'docs/history/pre-mainline-20260926/REPO_CLEANUP_PROPOSAL.md').read_bytes()
  # Git checkout may normalize line endings; content and original history remain.
  self.assertEqual(actual.replace(b'\r\n',b'\n'),old.replace(b'\r\n',b'\n'))
if __name__=='__main__':unittest.main()
