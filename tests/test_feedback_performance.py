"""Execute typed performance reports through actual TXT and ZIP readers."""
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

perf = load('performance_policy', ROOT/'scripts/feedback_performance.py')
report = load('performance_report', ROOT/'scripts/feedback-report.py')
zip_fixtures = load('performance_zip_fixtures', ROOT/'tests/test_core_feedback.py')
text_fixtures = load('performance_text_fixtures', ROOT/'tests/test_core_text_feedback.py')


def metadata():
    return {'snapshot.valid':'1', 'requested.config':'42',
        'end.dual.currentConfig':'42','end.dual.timingConfig':'42',
        'end.dual.timingSamples':'120', 'end.dual.gpuMs.Chain':'last:2,median:3,p95:4',
        'end.privateSR.gpuTimingAvailable':'1','end.privateSR.timingConfig':'42',
        'end.privateSR.timingInstance':'17','end.privateSR.gpuSamples':'120',
        'end.privateSR.gpuMedianMs':'1.25','end.privateSR.gpuP95Ms':'2.5'}


class PerformancePolicy(unittest.TestCase):
    def test_current_samples_are_partial_and_never_combined(self):
        result = perf.summarize(metadata())
        self.assertEqual(result['dual_chain'], {'status':'observed','samples':120,'median_ms':3.,'p95_ms':4.})
        self.assertEqual(result['private_sr']['median_ms'], 1.25)
        self.assertEqual(result['whole_frame'], 'unavailable')
        self.assertNotIn('total', result)
        self.assertNotIn('fps', result)

    def test_every_missing_field_stays_unavailable(self):
        for key in metadata():
            with self.subTest(key=key):
                info = metadata(); del info[key]
                output = perf.summarize(info)
                if key.startswith('end.privateSR.'):
                    self.assertEqual(output['private_sr']['status'], 'unavailable')
                    self.assertEqual(output['dual_chain']['status'], 'observed')
                elif key.startswith('end.dual.') and key!='end.dual.currentConfig':
                    self.assertEqual(output['dual_chain']['status'], 'unavailable')
                    self.assertEqual(output['private_sr']['status'], 'observed')
                else:
                    self.assertEqual(output['dual_chain']['status'], 'unavailable')
                    self.assertEqual(output['private_sr']['status'], 'unavailable')

    def test_invalid_timing_and_counts_cannot_become_fast_results(self):
        for value in ('NaN','inf','-1','10001','1e999','0x1234','1\nfoo=2',r'C:\Users\SECRET', '9'*200):
            for key in ('end.privateSR.gpuMedianMs','end.privateSR.gpuP95Ms'):
                with self.subTest(key=key,value=value):
                    info=metadata();info[key]=value
                    self.assertEqual(perf.summarize(info)['private_sr']['status'],'unavailable')
                    self.assertNotIn('SECRET','\n'.join(perf.render(info)))
        for value in ('-1','0','01',str(2**64),'[click](https://SECRET)','NaN'):
            for key,name in (('end.dual.timingSamples','dual_chain'),('end.privateSR.gpuSamples','private_sr'),('end.privateSR.timingInstance','private_sr')):
                info=metadata();info[key]=value
                self.assertEqual(perf.summarize(info)[name]['status'],'unavailable')
        for value in ('last:1,median:2,p95:1', 'last:NaN,median:2,p95:3',
                      'median:2,p95:3,last:1', 'last:1,median:2,p95:3,extra:4'):
            info=metadata();info['end.dual.gpuMs.Chain']=value
            self.assertEqual(perf.summarize(info)['dual_chain']['status'],'unavailable')

    def test_stale_changed_and_zero_identity_are_not_current(self):
        for key in ('requested.config','end.dual.currentConfig'):
            for value in ('0','41','43','unknown'):
                info=metadata();info[key]=value
                self.assertTrue(all(perf.summarize(info)[n]['status']=='unavailable' for n in ('dual_chain','private_sr')))
        for key,name in (('end.dual.timingConfig','dual_chain'),('end.privateSR.timingConfig','private_sr')):
            info=metadata();info[key]='41'
            self.assertEqual(perf.summarize(info)[name]['status'],'unavailable')
        info=metadata();info['end.privateSR.gpuTimingAvailable']='0'
        self.assertEqual(perf.summarize(info)['private_sr']['status'],'unavailable')

    def test_measured_zero_requires_all_receipts(self):
        info=metadata();info['end.dual.gpuMs.Chain']='last:0,median:0,p95:0'
        info['end.privateSR.gpuMedianMs']=info['end.privateSR.gpuP95Ms']='0'
        result=perf.summarize(info)
        self.assertEqual(result['dual_chain']['median_ms'],0)
        self.assertEqual(result['private_sr']['median_ms'],0)
        info['end.privateSR.gpuTimingAvailable']='0'
        self.assertNotIn('median_ms',perf.summarize(info)['private_sr'])

    def test_render_warns_about_bypass_and_missing_scope(self):
        text='\n'.join(perf.render({}))
        for fragment in ('未取得','不是 0 ms','不能相加','F8','旁路后的高 FPS 不是优化收益','首次开启'):
            self.assertIn(fragment,text)


class PerformanceReportIntegration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        text_fixtures.CoreTextFeedbackTests.setUpClass()
        cls.addClassCleanup(text_fixtures.CoreTextFeedbackTests.doClassCleanups)

    def setUp(self):
        self.z=zip_fixtures.CoreFeedbackTests();self.z.setUp()
        self.addCleanup(self.z.doCleanups)
        self.t=text_fixtures.CoreTextFeedbackTests()

    def txt(self, fields=None, logs='fixture\n', **kwargs):
        fields=metadata() if fields is None else fields
        return self.t.fixture(extra=''.join(f'{k}={v}\n' for k,v in fields.items() if k!='snapshot.valid'),logs=logs,**kwargs)

    def test_actual_txt_and_zip_report_same_selected_timing(self):
        path=self.txt()
        txt=report.prepare(path,symptom='performance')
        archive=self.z.rewrite(self.z.bundle(),lambda m,i,p:i.update(metadata()))
        zipped=report.prepare(archive,symptom='performance')
        for output in (txt,zipped):
            self.assertIn('中位 3 ms，P95 4 ms',output)
            self.assertIn('中位 1.25 ms，P95 2.5 ms',output)
            self.assertIn('整帧 GPU 耗时 / 游戏 FPS：未取得',output)
        self.assertNotIn('性能记录：',report.prepare(path))

    def test_historical_summary_cannot_supply_missing_or_override_current(self):
        text=report.prepare(self.txt({},logs='\n'.join(f'{k}={v}' for k,v in metadata().items())+'\n'),symptom='performance')
        self.assertNotIn('中位 3 ms',text)
        self.assertIn('不是 0 ms',text)
        text=report.prepare(self.txt(logs='end.dual.gpuMs.Chain=last:0,median:0,p95:0\n'),symptom='performance')
        self.assertIn('中位 3 ms',text)
        self.assertNotIn('中位 0 ms',text)

    def test_partial_feedback_is_retained_without_claiming_completeness(self):
        text=report.prepare(self.txt(complete=False,missing='required'),symptom='performance')
        self.assertIn('部分完成',text)
        self.assertIn('性能记录：仅部分 GPU 阶段',text)

    def test_duplicate_selected_scalar_is_rejected(self):
        path=self.txt({'requested.config':'42\nrequested.config=43'})
        with self.assertRaises(ValueError):report.prepare(path,symptom='performance')

    def test_cli_accepts_performance_without_overwriting_or_uploading(self):
        path=self.txt();target=self.z.root/'report.md'
        cmd=[sys.executable,str(ROOT/'scripts/feedback-report.py'),str(path),'--symptom','performance','--output',str(target)]
        result=subprocess.run(cmd,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        saved=target.read_bytes();again=subprocess.run(cmd,capture_output=True,text=True)
        self.assertEqual(again.returncode,2)
        self.assertEqual(target.read_bytes(),saved)
        self.assertNotIn(str(self.z.root),again.stderr)


if __name__=='__main__':unittest.main(verbosity=2)
