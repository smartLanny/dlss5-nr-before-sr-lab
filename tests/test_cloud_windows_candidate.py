"""Production workflow admission/routing checks; not Windows execution evidence."""
from pathlib import Path
import re
import unittest

from test_public_ci_policy import Expression, condition, event, jobs

ROOT = Path(__file__).resolve().parents[1]


def read(name):
    return (ROOT / name).read_text(encoding='utf-8-sig')


class CloudWindowsCandidate(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.router = read('.github/workflows/pull-request-validation.yml')
        cls.job = jobs(cls.router)['public-windows-candidate']
        cls.rule = condition(cls.job)
        cls.workflow = read('.github/workflows/compat-post-dev2.yml')
        cls.bootstrap = read('scripts/ci/prepare-hosted-windows.ps1')

    def admitted(self, *, cloud=False, **kwargs):
        context = event(**kwargs)
        context['inputs']['cloud_windows_build'] = cloud
        return Expression(self.rule, context).evaluate()

    def test_explicit_public_owner_requests_only(self):
        for private in (False, True):
            with self.subTest(private=private):
                self.assertFalse(self.admitted(private=private))
                self.assertFalse(self.admitted(private=private, labels=('full-build',)))
                self.assertEqual(self.admitted(private=private, labels=('cloud-windows-build',)), not private)
                self.assertEqual(self.admitted(private=private, action='labeled', label='cloud-windows-build', labels=('cloud-windows-build',)), not private)
                self.assertFalse(self.admitted(private=private, action='labeled', label='documentation', labels=('cloud-windows-build',)))
                self.assertFalse(self.admitted(private=private, name='push', cloud=True))
                self.assertFalse(self.admitted(private=private, name='workflow_dispatch', full_build=True))
                self.assertEqual(self.admitted(private=private, name='workflow_dispatch', cloud=True), not private)
                self.assertFalse(self.admitted(private=private, name='workflow_dispatch', cloud=True, actor='collaborator'))

    def test_forks_and_non_owner_sources_never_enter_the_cloud_build(self):
        for action, label in (('opened', ''), ('synchronize', ''), ('reopened', ''), ('labeled', 'cloud-windows-build')):
            for change in ({'fork': True}, {'head': 'outsider/core'}, {'author': 'COLLABORATOR'},
                           {'author': 'CONTRIBUTOR'}, {'author': 'MEMBER'}, {'actor': 'collaborator'}, {'base': 'release'}):
                with self.subTest(action=action, change=change):
                    self.assertFalse(self.admitted(action=action, label=label, labels=('cloud-windows-build',), **change))

    def test_private_reusable_execution_is_blocked_without_enabling_paid_families(self):
        self.assertNotIn('ENABLE_HOSTED_CI', self.job)
        self.assertNotIn('secrets:', self.job)
        reusable = jobs(self.workflow)
        for job in reusable.values():
            rule = condition(job)
            self.assertTrue(Expression(rule, event(private=False)).evaluate())
            self.assertFalse(Expression(rule, event(private=True)).evaluate())
        self.assertIn('runs-on: windows-2022', reusable['windows'])
        self.assertIn('timeout-minutes: 180', reusable['windows'])
        self.assertNotIn('self-hosted', self.workflow)
        self.assertNotIn('  workflow_dispatch:', self.workflow)
        self.assertEqual(self.workflow.count('persist-credentials: false'), 2)

    def test_canonical_builder_is_unconditional_and_keeps_full_inputs(self):
        command = next(line for line in self.workflow.splitlines() if '& ./scripts/build-unified-candidate.ps1' in line)
        for argument in ('-UpstreamRoot', '-ToolchainRoot', '-ReShadeSourceRoot', '-DirectXMathRoot',
                         '-FaceRuntimeRoot', '-FfxSdkRoot', '-FfxCacheRoot'):
            self.assertIn(argument, command)
        for bypass in ('AllowMissingDebugLayer', '--compile-only', '--build-only', '--allow-no-debug', 'continue-on-error:'):
            self.assertNotIn(bypass, self.workflow)
        self.assertNotIn("Test-Path 'experiments/final_residual/recorded_temporal.cpp'", self.bootstrap + self.workflow)
        self.assertIn('build_amd_probe.py', self.bootstrap)
        self.assertIn('--sdk-only', self.bootstrap)  # Compile the real SDK cache; canonical WARP is separate.
        self.assertIn('DLSS5_NGX_SDK_ROOT=$Ngx', self.bootstrap)
        self.assertIn('DLSS5_VCVARS_PATH=$VcVars', self.bootstrap)

    def test_dependency_commits_match_production_contracts(self):
        contracts = '\n'.join(read(name) for name in (
            'scripts/prepare-core-source.py', 'scripts/build-multi-api-candidate.ps1',
            'scripts/build-nrchain.ps1', 'experiments/final_residual/build_amd_probe.py'))
        commits = re.findall(r"Checkout-Pinned '[^']+' '[^']+' '([0-9a-f]{40})'", self.bootstrap)
        self.assertEqual(len(commits), 5)
        for commit in commits:
            self.assertIn(commit, contracts)
        self.assertIn('git -C $Path config core.autocrlf $AutoCrlf', self.bootstrap)
        ngx_line = next(line for line in self.bootstrap.splitlines() if "$Ngx = Checkout-Pinned" in line)
        self.assertIn("-AutoCrlf 'true'", ngx_line)
        self.assertIn("Join-Path $env:RUNNER_TEMP 'dlss5-build-inputs'", self.workflow)
        self.assertIn('Dependency digest mismatch', self.bootstrap)
        self.assertIn('Prepared face model hash mismatch', self.bootstrap)

    def test_packages_and_failure_evidence_have_distinct_scope(self):
        packaging = read('scripts/ci/package-hosted-candidate.ps1')
        self.assertIn('scripts/package-core-ota.py', packaging)
        self.assertIn('cpu_detector_worker_verified', packaging)
        self.assertIn('AMD-FidelityFX-LICENSE.txt', packaging)
        self.assertLess(self.workflow.index('build-unified-candidate.ps1'), self.workflow.index('validate-face-runtime.ps1'))
        self.assertLess(self.workflow.index('validate-face-runtime.ps1'), self.workflow.index('package-hosted-candidate.ps1'))
        self.assertNotIn('package-multi-api-candidate.ps1', self.workflow)
        self.assertNotIn('UNIFIED2-CANDIDATE.json', self.workflow)
        artifact_steps = self.workflow.split('- name: Upload ')
        self.assertEqual(len(artifact_steps), 3)
        candidate, evidence = artifact_steps[1:]
        self.assertIn('build/cloud-packages/**/*.zip', candidate)
        self.assertNotIn('if: always()', candidate.split('- name: Retain ')[0])
        self.assertIn('if: always()', evidence)
        for pattern in ('build/**/*.json', 'build/**/*.txt', 'build/**/*.log'):
            self.assertIn(pattern, evidence)
        for suffix in ('.dll', '.exe', '.addon64', '.onnx', '.bin', '.zip'):
            self.assertNotIn('build/**/*' + suffix, evidence)
        self.assertEqual(self.workflow.count('retention-days: 7'), 2)


if __name__ == '__main__':
    unittest.main()
