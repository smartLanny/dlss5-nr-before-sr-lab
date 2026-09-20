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
        self.assertIn('-File scripts/build-unified-candidate.ps1', windows)
        self.assertNotIn('-File scripts/build-lockperf2-candidate.ps1', windows)
        self.assertIn('-FaceRuntimeRoot $env:FACE_RUNTIME_ROOT -OutputRoot build/unified2', windows)
        self.assertIn('-QualitySuccessor', read('scripts/build-unified-candidate.ps1'))

    def test_full_face_assets_and_gate_are_bound_before_packaging(self):
        text = read('.github/workflows/compat-post-dev2.yml')
        self.assertIn('https://github.com/microsoft/onnxruntime/releases/download/v1.23.2/onnxruntime-win-x64-1.23.2.zip', text)
        self.assertIn('0b38df9af21834e41e73d602d90db5cb06dbd1ca618948b8f1d66d607ac9f3cd', text)
        self.assertIn('47534e27c9851bb1128ccc0102f1145e27f23f98', text)
        self.assertIn('onnx==1.19.1', text)
        self.assertIn('scripts/prepare-face-model.py', text)
        self.assertIn('b1b3061b1ae7a43bcc1497284ade754f701db774ef3a19481edcaf27fbc425b1', text)
        self.assertIn('-FaceModelPath face-model/yunet-dynamic.onnx', text)
        self.assertLess(text.index('-File scripts/validate-face-runtime.ps1'), text.index('-File scripts/package-multi-api-candidate.ps1'))
        self.assertIn('-FaceModel face-model/yunet-dynamic.onnx -FaceModelLicense face-model/YUNET-LICENSE', text)
        for artifact in ('build/candidate-unified2/*.zip', 'UNIFIED2-CANDIDATE.json', 'unified2.face-checks/validation.json', 'source/core/build/face-backend.json'):
            self.assertIn(artifact, text)

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
            'scripts/build-perf12-candidate.ps1',
            'scripts/build-multi-api-candidate.ps1',
        ))
        self.assertFalse(sorted(n for n in names if n not in current))
        for flag in ('-NegativeControl', '-DynamicStateNegativeControl', '-BlendStateNegativeControl', '-LayerNegativeControl'):
            self.assertIn(flag, current)
        self.assertIn('tests/post_sr_snapshot_gpu.cpp', current)
        self.assertIn('native/snapshot FP16 bytes', current)

    def test_gates_precede_packaging_and_never_claim_game_verification(self):
        text = read('scripts/build-perf12-candidate.ps1')
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
