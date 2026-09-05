import importlib.util
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

fixtures = load('report_fixtures', ROOT / 'tests/test_core_feedback.py')
report = load('feedback_report', ROOT / 'scripts/feedback-report.py')

class FeedbackReportTests(unittest.TestCase):
    def setUp(self):
        self.f = fixtures.CoreFeedbackTests()
        self.f.setUp()
        self.addCleanup(self.f.doCleanups)

    def test_complete_core_and_missing_values(self):
        text = report.prepare(self.f.bundle())
        self.assertIn('核心必需证据：完整', text)
        self.assertIn('a' * 40, text)
        self.assertIn('b' * 64, text)
        self.assertIn('观测区间 NR 成功调用增量：未取得', text)
        self.assertNotIn(str(self.f.root), text)

    def test_partial_remains_reportable(self):
        text = report.prepare(self.f.bundle(missing=True))
        self.assertIn('部分完成（仍可分析）', text)
        self.assertIn('不要直接丢弃', text)

    def test_visual_report_reminds_new_pair(self):
        text = report.prepare(self.f.bundle(), symptom='visual')
        self.assertIn('不能用旧 F8 包与新 F7 图像混配', text)

    def test_pair_contains_both_archive_hashes(self):
        core, image = self.f.bundle(), self.f.bundle('image.zip', image=True)
        text = report.prepare(core, image)
        self.assertIn('核心/图像记录配对：通过', text)
        self.assertIn(report.digest(image, 512 * 1024 * 1024), text)
        self.assertNotIn(str(image), text)

    def test_wrong_binary_pair_rejected_before_report(self):
        core, image = self.f.bundle(), self.f.bundle('image.zip', image=True)
        self.f.rewrite(image, lambda m,i,p: i.update({'artifact.addonSha256': 'c' * 64}))
        with self.assertRaises(ValueError): report.prepare(core, image)

    def test_raw_paths_and_unknown_metadata_never_copied(self):
        path = self.f.bundle()
        self.f.rewrite(path, lambda m,i,p: i.update({'exe.leaf': r'C:\Users\PRIVATE\Game.exe',
            'gpu': '[click](https://example.invalid)', 'raw.private': 'SENSITIVE-MARKER'}))
        text = report.prepare(path)
        self.assertNotIn('PRIVATE', text)
        self.assertNotIn('SENSITIVE-MARKER', text)
        self.assertNotIn('https://', text)

    def test_explicit_duplicate_reshade_evidence_is_historical(self):
        path = self.f.bundle(extra={'logs/nr-current-summary.txt':
            b'format=DLSS5-LogSummary-v1\nscan.complete=1\n[head]\nAnother ReShade instance exists. Aborting initialization.\n'})
        text = report.prepare(path)
        self.assertIn('历史摘要出现 ReShade 重复实例', text)
        self.assertIn('不要只凭', text)

    def test_two_module_names_are_not_duplicate_proof(self):
        path = self.f.bundle(extra={'logs/nr-current-summary.txt':
            b'format=DLSS5-LogSummary-v1\nscan.complete=1\n[head]\ndxgi.dll d3d12.dll\n'})
        self.assertNotIn('历史摘要出现 ReShade 重复实例', report.prepare(path))

    def test_null_pso_advice_does_not_claim_fix(self):
        path = self.f.bundle(extra={'logs/nr-current-summary.txt':
            b'format=DLSS5-LogSummary-v1\nscan.complete=1\n[head]\nreason=null-pso-policy-disabled\n'})
        self.assertIn('保留安全保护', report.prepare(path))

    def test_hotpatch_evidence_is_not_an_unlock_recommendation(self):
        path = self.f.bundle()
        self.f.rewrite(path, lambda m,i,p: i.update({'end.ngxHookFailure.available':'1',
            'end.ngxHookFailure.entry':'existing-short-hotpatch', 'end.ngxHookFailure.status':'8'}))
        text = report.prepare(path)
        self.assertIn('existing-short-hotpatch', text)
        self.assertIn('不要覆盖', text)

    def test_pristine_route_is_last_sample_not_universal_compatibility(self):
        path = self.f.bundle()
        self.f.rewrite(path, lambda m,i,p: i.update({'end.commandState.available':'1',
            'end.commandState.route':'pristine-null', 'end.commandState.ageMs':'999'}))
        text = report.prepare(path)
        self.assertIn('pristine-null', text)
        self.assertIn('最近一次', text)
        self.assertIn('999', text)

    def test_new_field_path_injection_stays_redacted(self):
        path = self.f.bundle()
        self.f.rewrite(path, lambda m,i,p: i.update({'end.ngxHookFailure.operation':r'C:\Users\SECRET\mod.dll'}))
        self.assertNotIn('SECRET', report.prepare(path))

    def test_counter_reset_and_invalid_values(self):
        self.assertEqual(report.delta({'start.nrSuccess':'100','end.nrSuccess':'99'},'nrSuccess'), '计数重置，不能计算')
        self.assertEqual(report.delta({'start.nrSuccess':'0','end.nrSuccess':'0'},'nrSuccess'), '0')
        self.assertEqual(report.delta({'start.nrSuccess':'-1','end.nrSuccess':'8'},'nrSuccess'), '未取得')

    def test_cli_never_overwrites_and_errors_do_not_leak_paths(self):
        core = self.f.bundle()
        target = self.f.root / 'private-user-report.md'
        command = [sys.executable, str(ROOT/'scripts/feedback-report.py'), str(core), '--output', str(target)]
        first = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(first.returncode, 0, first.stderr)
        original = target.read_bytes()
        second = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(second.returncode, 2)
        self.assertEqual(target.read_bytes(), original)
        self.assertNotIn(str(self.f.root), second.stderr)

    def test_bad_archive_no_report_and_crash_rescue_hint(self):
        bad = self.f.bundle(bad_hash=True)
        with self.assertRaises(ValueError): report.prepare(bad)
        self.assertIn('F8 不是 dump/异常栈的替代品', report.prepare(self.f.bundle('ok.zip'), symptom='crash'))

if __name__ == '__main__': unittest.main(verbosity=2)
