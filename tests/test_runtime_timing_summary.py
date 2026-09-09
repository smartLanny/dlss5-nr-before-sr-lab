import importlib.util
from pathlib import Path
import unittest

path=Path(__file__).resolve().parents[1]/'scripts/summarize-runtime-timing.py'
spec=importlib.util.spec_from_file_location('timing_summary',path)
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

def report(**changes):
    fields={'end.timing.schema':'1','end.timing.available':'1','end.timing.clockAvailable':'1',
            'end.timing.qpcFrequency':'1000000','end.timing.epochStartedTicks':'100',
            'end.timing.capturedTicks':'30000100','end.timing.mixedScopes':'0',
            'end.timing.counterOverflow':'0','end.timing.aggregateScopeAvailable':'1',
            'end.timing.scopedRecords':'4','end.timing.unscopedRecords':'0'}
    for prefix in ('start','end'):
        for key,value in {'epoch':'4','probeMode':'0','probePhase':'1','waitMode':'1','waitPhase':'2'}.items():
            fields[f'{prefix}.timing.{key}']=value
    for stage in module.STAGES:
        fields[f'end.timing.{stage}']='count:4,measured:4,totalTicks:1900,maxTicks:700,p95UpperUs:1000,p99UpperUs:1000,histogram:1,0,1,2,0,0,0,0,0,0,0,0'
    fields.update(changes)
    return '\n'.join(f'{key}={value}' for key,value in fields.items())

class TimingSummary(unittest.TestCase):
    def test_segment_max_is_not_a_start_end_difference(self):
        result=module.summarize(report())
        self.assertTrue(result['meets_segment_data_checks'])
        self.assertEqual(result['stages']['carrier']['segment_max_cpu_ms'],.7)
        self.assertEqual(result['stages']['carrier']['p95_cpu_us'],
                         {'lower_us':500,'upper_us':1000,'kind':'histogram_bucket'})
    def test_unstable_and_mixed_segments_are_not_admitted(self):
        for change in ({'start.timing.epoch':'3'},{'end.timing.mixedScopes':'1'},
                       {'end.timing.counterOverflow':'1'},{'end.timing.capturedTicks':'1000100'}):
            self.assertFalse(module.summarize(report(**change))['meets_segment_data_checks'])
    def test_missing_clock_stays_unknown(self):
        result=module.summarize(report(**{'end.timing.qpcFrequency':'0','end.timing.clockAvailable':'0'}))
        self.assertIsNone(result['segment_seconds'])
        self.assertIsNone(result['stages']['carrier']['mean_cpu_ms'])
    def test_appended_old_log_cannot_replace_current_metadata(self):
        result=module.summarize(report()+'\n===== nr-current / CURRENT PROCESS =====\nend.timing.waitMode=0')
        self.assertEqual(result['wait_mode'],1)
    def test_invalid_histogram_is_not_usable_evidence(self):
        with self.assertRaises(ValueError):
            module.summarize(report(**{'end.timing.carrier':'count:1,measured:1,totalTicks:2,maxTicks:3,histogram:1'}))
    def test_overflow_bucket_is_unbounded(self):
        self.assertEqual(module.quantile([0]*11+[1],99)['upper_us'],None)

if __name__=='__main__':unittest.main()
