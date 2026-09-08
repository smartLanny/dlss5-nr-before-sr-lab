import copy
import unittest
from profile_gate import analyze

class Tests(unittest.TestCase):
    def sample(self):
        # Synthetic validation data, not measurements.
        ident = dict(gpu='SYNTHETIC', driver='test', runtime_sha256='test', shape=[960,4096,1024], mode='test', settings={'strength':1})
        return {'samples':[dict(pair=str(i), identity=ident, variant=v, nr_gpu_ms=t, covered_fp8_ms=2, warmup=False)
                           for i in range(5) for v,t in [('fp8',10),('candidate',9)]]}
    def test_normal(self):
        r=analyze(self.sample()); self.assertAlmostEqual(r['paired_fraction_saved_median'], .1)
        self.assertFalse(r['runtime_admission'])
    def test_overlap(self):
        s=self.sample(); s['samples'][0]['covered_fp8_ms']=11
        with self.assertRaises(ValueError): analyze(s)
    def test_warmup(self):
        s=self.sample(); s['samples'][0]['warmup']=True
        with self.assertRaises(ValueError): analyze(s)
    def test_missing_pair(self):
        s=self.sample(); s['samples'].pop()
        with self.assertRaises(ValueError): analyze(s)
    def test_mixed_identity(self):
        s=copy.deepcopy(self.sample()); s['samples'][0]['identity']=dict(s['samples'][0]['identity'], driver='other')
        with self.assertRaises(ValueError): analyze(s)
    def test_nan(self):
        s=self.sample(); s['samples'][0]['nr_gpu_ms']=float('nan')
        with self.assertRaises(ValueError): analyze(s)
    def test_duplicate(self):
        s=self.sample(); s['samples'].append(s['samples'][0])
        with self.assertRaises(ValueError): analyze(s)
    def test_regression(self):
        s=self.sample(); s['samples'][1]['nr_gpu_ms']=12
        self.assertFalse(analyze(s)['timing_only_all_pairs_improve'])

if __name__ == '__main__': unittest.main()
