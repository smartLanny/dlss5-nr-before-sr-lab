"""Source routing checks, not execution of PowerShell, Windows or Actions.

Regression: next/0.5 selected the old dev4 raw build instead of the complete
pinned D21 generator, bypassing the newly added hotpath gates and package label.
"""
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = 'd8de528f4df23999c4f6711320a94ac522e0f44e'


def read(name):
    return (ROOT / name).read_text(encoding='utf-8-sig')


class Lockperf2Workflow(unittest.TestCase):
    def test_actual_research_route_reaches_canonical_builder(self):
        router = read('.github/workflows/pull-request-validation.yml').split('  research-050:', 1)[1]
        self.assertIn("github.base_ref == 'next/0.5'", router)
        self.assertIn('uses: ./.github/workflows/beta050-validate.yml', router)
        research = read('.github/workflows/beta050-validate.yml').split('  full-contract-build-and-package:', 1)[1]
        self.assertIn('needs: [host-bug-regressions, shader-structure-and-math]', research)
        self.assertIn('uses: ./.github/workflows/multi-api-foundation.yml', research)
        self.assertNotIn('scripts/build.ps1', research)
        foundation = read('.github/workflows/multi-api-foundation.yml')
        self.assertIn('uses: ./.github/workflows/compat-post-dev2.yml', foundation)
        windows = read('.github/workflows/compat-post-dev2.yml')
        self.assertIn('-File scripts/build-lockperf2-candidate.ps1', windows)

    def test_frozen_git_source_is_available_and_head_is_consistent(self):
        for name in ('beta050-validate.yml', 'multi-api-foundation.yml', 'compat-post-dev2.yml'):
            text = read('.github/workflows/' + name)
            count = text.count('uses: actions/checkout@v4')
            self.assertGreater(count, 0)
            self.assertEqual(text.count('fetch-depth: 0'), count)
            self.assertEqual(text.count('ref: ${{ github.event.pull_request.head.sha || github.sha }}'), count)

    def test_old_windows_validator_names_are_still_reachable(self):
        old = subprocess.check_output(['git', '-C', str(ROOT), 'show', BASE + ':.github/workflows/beta050-validate.yml'], text=True)
        # The old raw build is intentionally replaced by the generated build.
        names = set(re.findall(r'(validate-[\w-]+\.(?:py|ps1)|test-beta04-math.py)', old))
        current = '\n'.join(read(p) for p in (
            '.github/workflows/beta050-validate.yml',
            'scripts/build-perf4-candidate.ps1',
            'scripts/build-multi-api-candidate.ps1',
        ))
        self.assertFalse(sorted(n for n in names if n not in current))
        for flag in ('-NegativeControl', '-DynamicStateNegativeControl', '-BlendStateNegativeControl', '-LayerNegativeControl'):
            self.assertIn(flag, current)
        self.assertIn('tests/post_sr_snapshot_gpu.cpp', current)
        self.assertIn('native/snapshot FP16 bytes', current)

    def test_gates_precede_packaging_and_never_claim_game_verification(self):
        text = read('scripts/build-perf4-candidate.ps1')
        build = text.index("& (Join-Path $PSScriptRoot 'build-multi-api-candidate.ps1')")
        self.assertLess(text.index("'validate-command-state-lockperf.py'"), build)
        self.assertLess(text.index("'validate-command-state-hotpath.py'"), build)
        self.assertLess(build, text.index('Compress-Archive'))
        self.assertLess(text.index('-not $Validation.warp_verified'), text.index('Compress-Archive'))
        self.assertIn('game_tested=$false', text)
        self.assertIn('nvidia_feature18_tested=$false', text)
        self.assertIn('deployed=$false', text)
        self.assertNotIn('Copy-Item build/nr_before_sr.ini', text)
        self.assertIn('exact linker maps', text)

    def test_legacy_command_cannot_label_new_code_as_lockperf1(self):
        text = read('scripts/build-lockperf1-candidate.ps1')
        self.assertIn('build-lockperf2-candidate.ps1', text)
        self.assertNotIn('Compress-Archive', text)
        self.assertNotIn("candidate='beta0.5-dline21-lockperf1", text)


if __name__ == '__main__':
    unittest.main()
