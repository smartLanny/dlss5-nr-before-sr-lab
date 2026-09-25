import unittest
import numpy as np
from nr_dual_reference_math import decode, encode, downsample, resolve

class MathTests(unittest.TestCase):
    def setUp(self):
        self.rng = np.random.default_rng(113)
        self.primary = self.rng.uniform(.05,.85,(11,17,4))

    def test_identity_is_exact_both_domains_and_awkward_ratios(self):
        for domain in (0,1):
            for width,height in ((5,3),(9,6),(12,8),(17,11)):
                low,valid=downsample(self.primary,width,height,domain)
                result=resolve(self.primary,low,low.copy(),valid,domain)
                self.assertTrue(np.array_equal(result,self.primary))

    def test_constant_field_survives_area_filter(self):
        p=np.full((13,19,4),.4)
        for domain in (0,1):
            low,valid=downsample(p,7,5,domain)
            self.assertTrue(valid.all())
            np.testing.assert_allclose(low[...,:3],.4,atol=1e-14)

    def test_constant_increment_preserves_primary_texture(self):
        low,valid=downsample(self.primary,5,3,0)
        out=low.copy(); out[...,:3]+=.03
        result=resolve(self.primary,low,out,valid,0)
        np.testing.assert_allclose(result[...,:3],self.primary[...,:3]+.03,atol=2e-16)
        self.assertTrue(np.array_equal(result[...,3],self.primary[...,3]))

    def test_negative_delta_not_saturated(self):
        low,valid=downsample(self.primary,5,3,0)
        out=low.copy(); out[...,:3]-=.02
        result=resolve(self.primary,low,out,valid,0)
        np.testing.assert_allclose(result[...,:3],self.primary[...,:3]-.02,atol=2e-16)

    def test_srgb_delta_is_decoded_before_subtraction(self):
        p=np.full((8,8,4),.5)
        low,valid=downsample(p,4,4,1)
        out=low.copy(); out[...,:3]=encode(decode(low[...,:3],1)+.02,1)
        result=resolve(p,low,out,valid,1)
        np.testing.assert_allclose(decode(result[...,:3],1),decode(p[...,:3],1)+.02,atol=2e-15)
        self.assertGreater(np.max(np.abs(result[...,:3]-(p[...,:3]+.02))),.001)

    def test_invalid_low_output_falls_back_locally(self):
        low,valid=downsample(self.primary,5,3,0)
        out=low.copy(); out[...,:3]+=.03; out[1,2,0]=np.nan
        result=resolve(self.primary,low,out,valid,0)
        self.assertTrue(np.isfinite(result).all())
        self.assertTrue(np.array_equal(result[5,8],self.primary[5,8]))
        self.assertGreater(np.max(np.abs(result-self.primary)),.02)

    def test_invalid_downsample_is_marked_not_fabricated_valid(self):
        p=self.primary.copy(); p[0,0,0]=np.nan
        low,valid=downsample(p,5,3,1)
        self.assertFalse(valid[0,0])
        self.assertTrue(np.isfinite(low).all())
        self.assertTrue(np.array_equal(low[0,0,:3],np.zeros(3)))

    def test_invalid_validity_rejects_entire_edit_footprint(self):
        low,valid=downsample(self.primary,5,3,0)
        out=low.copy(); out[...,:3]+=.03
        valid[:]=False
        self.assertTrue(np.array_equal(resolve(self.primary,low,out,valid,0),self.primary))

    def test_wrong_reference_double_counts_first_pass_negative_control(self):
        original=np.full((8,8,4),.2)
        first=original.copy(); first[...,:3]+=.1
        low_first,valid=downsample(first,4,4,0)
        low_original,_=downsample(original,4,4,0)
        second=low_first.copy(); second[...,:3]+=.05
        good=resolve(first,low_first,second,valid,0)
        wrong=resolve(first,low_original,second,valid,0)
        np.testing.assert_allclose(good[...,:3],.35,atol=1e-15)
        np.testing.assert_allclose(wrong[...,:3],.45,atol=1e-15)

    def test_area_downsample_checkerboard(self):
        p=np.zeros((8,8,4)); p[...,3]=1
        p[...,:3]=((np.indices((8,8)).sum(axis=0)%2)[...,None])
        low,valid=downsample(p,4,4,0)
        np.testing.assert_array_equal(low[...,:3],np.full((4,4,3),.5))
        self.assertTrue(valid.all())

    def test_zero_weight_invalid_neighbour_does_not_disable_edit(self):
        p=np.full((1,3,4),.4)
        low=p.copy(); out=low.copy(); out[0,0,:3]=.5; out[0,1,0]=np.nan
        result=resolve(p,low,out,np.ones((1,3),bool),0)
        np.testing.assert_allclose(result[0,0,:3],.5,atol=1e-15)

if __name__=='__main__':
    unittest.main(verbosity=2)
