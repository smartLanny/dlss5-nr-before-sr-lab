import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('analysis',ROOT/'scripts/analyze-nvfp4-performance.py')
analysis=importlib.util.module_from_spec(spec);spec.loader.exec_module(analysis)

class EvidenceTests(unittest.TestCase):
    def setUp(self):self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
    def tearDown(self):self.tmp.cleanup()
    def rows(self,name,value=1,**fields):
        path=self.root/name;row={'nr_chain_ms':value,'gemm_ms':value,'gpu_complete':True,'replaced_pairs':8,**fields}
        path.write_text('\n'.join(json.dumps(row) for _ in range(600))+'\n');return path
    def test_gemm_speed_does_not_claim_nr_goal(self):
        a=self.rows('a',2);b=self.rows('b',1)
        result=analysis.compare([a]*5,[b]*5,'gemm_ms',8)
        self.assertTrue(result['performance_evidence_admissible'])
        self.assertFalse(result['complete_nr_10_percent_target_met'])
        self.assertFalse(result['quality_verified'])
    def test_insufficient_runs_not_admitted(self):
        a=self.rows('a',2);b=self.rows('b',1)
        self.assertFalse(analysis.compare([a],[b],'nr_chain_ms',8)['performance_evidence_admissible'])
    def test_missing_gpu_completion_rejected(self):
        a=self.rows('a',gpu_complete=None)
        with self.assertRaisesRegex(ValueError,'completion'):analysis.read(a,'nr_chain_ms',0)
    def test_missing_actual_replacement_rejected(self):
        a=self.rows('a',replaced_pairs=0)
        with self.assertRaisesRegex(ValueError,'replacements'):analysis.read(a,'nr_chain_ms',8)
    def test_shared_gpu_rejected(self):
        a=self.rows('a',performance_context='shared_gpu')
        with self.assertRaisesRegex(ValueError,'contention'):analysis.read(a,'nr_chain_ms',0)
    def test_regression_not_admitted(self):
        a=self.rows('a',1);b=self.rows('b',1.1)
        self.assertFalse(analysis.compare([a]*5,[b]*5,'nr_chain_ms',8)['performance_evidence_admissible'])

if __name__=='__main__':unittest.main()
