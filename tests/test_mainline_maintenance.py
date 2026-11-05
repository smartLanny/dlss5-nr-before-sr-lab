"""Maintenance invariants; does not substitute for Windows or game validation."""
from pathlib import Path
import re,subprocess,unittest
R=Path(__file__).resolve().parents[1]
BASE='5364a37bacb48eff6b667e0238da78d4b278ecec'
def read(p):return (R/p).read_text(encoding='utf-8-sig')
class MainlineMaintenance(unittest.TestCase):
 def test_preserved_instruction_and_status_bodies(self):
  for old,new in [('AGENTS.md','AGENTS.pre-mainline.md'),('README.md','README.pre-mainline.md'),('docs/interop/CURRENT_STATUS.md','CURRENT_STATUS.pre-mainline.md')]:
   original=subprocess.check_output(['git','show',BASE+':'+old],cwd=R).decode('utf-8-sig').replace('\r\n','\n')
   self.assertEqual(read('docs/history/pre-mainline-20260926/'+new).replace('\r\n','\n'),original)
 def test_workflow_references_resolve(self):
  for p in (R/'.github/workflows').glob('*.yml'):
   s=p.read_text(encoding='utf-8-sig')
   for ref in re.findall(r'uses:\s+(\./\.github/workflows/[^\s]+)',s):self.assertTrue((R/ref).is_file(),(p,ref))
   self.assertNotRegex(s,r'^  push:',str(p))
   if '  pull_request:' in s:self.assertEqual(p.name,'pull-request-validation.yml')
 def test_all_router_jobs_are_explicitly_opt_in(self):
  s=read('.github/workflows/pull-request-validation.yml')
  self.assertIn('branches: [main]',s)
  jobs=dict(re.findall(r'^  ([\w-]+):\n(.*?)(?=^  [\w-]+:|\Z)',s[s.index('jobs:'):],re.M|re.S))
  hosted={n:j for n,j in jobs.items() if not n.startswith(('self-hosted-', 'public-'))}
  self.assertEqual(len(hosted),14)
  for job in hosted.values():self.assertIn("vars.ENABLE_HOSTED_CI == 'true'",job)
  public=jobs['public-portable']
  self.assertIn('runs-on: ubuntu-24.04',public)
  self.assertIn('github.event.repository.private == false',public)
  self.assertIn('persist-credentials: false',public)
  cloud=jobs['public-windows-candidate']
  self.assertIn('github.event.repository.private == false',cloud)
  self.assertIn('inputs.cloud_windows_build',cloud)
  self.assertIn("'cloud-windows-build'",cloud)
  self.assertIn('uses: ./.github/workflows/compat-post-dev2.yml',cloud)
  # The maintainer's runner is a separate opt-in and never runs hosted work.
  self_hosted={n:j for n,j in jobs.items() if n.startswith('self-hosted-')}
  self.assertEqual(set(self_hosted),{'self-hosted-tests','self-hosted-canonical-build'})
  for job in self_hosted.values():
   self.assertIn("vars.ENABLE_SELF_HOSTED_CI == 'true'",job)
   self.assertIn('runs-on: [self-hosted, Windows, X64, dlss5-build]',job)
   # The runner has Windows PowerShell only, and its service PATH may lack git.
   self.assertNotIn('shell: pwsh',job)
   self.assertLess(job.index('Locate git and python'),job.index('actions/checkout'))
  build=self_hosted['self-hosted-canonical-build']
  self.assertIn("inputs.full_build",build);self.assertIn("'full-build'",build)
  self.assertIn('scripts/ci/run-self-hosted-build.ps1',build)
  self.assertIn('uses: ./.github/workflows/beta050-validate.yml',s)
 def test_archived_workflows_preserve_original_content(self):
  files=list((R/'docs/history/pre-mainline-20260926/workflows').glob('*.yml'))
  self.assertEqual(len(files),9)
  for p in files:
   old=subprocess.check_output(['git','show',BASE+':.github/workflows/'+p.name],cwd=R).decode('utf-8-sig').replace('\r\n','\n')
   self.assertEqual(p.read_text(encoding='utf-8-sig').replace('\r\n','\n'),old)
   self.assertFalse((R/'.github/workflows'/p.name).exists())
 def test_acceptance_and_release_boundaries(self):
  s=read('docs/interop/CURRENT_STATUS.md')
  self.assertIn('尚未完整验证',s);self.assertIn('两项新功能默认关闭',s)
  self.assertIn('0.5.2-beta.1',read('AGENTS.md'))
  self.assertLess(len(read('AGENTS.md').splitlines()),90)
  self.assertLess(len(s.splitlines()),110)
if __name__=='__main__':unittest.main()
