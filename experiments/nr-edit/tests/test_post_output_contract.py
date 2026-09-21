"""CPU contract tests; not GPU/model or image-quality acceptance."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import classify_post_output as contract


class OutputContract(unittest.TestCase):
    def test_finite_excursions_are_not_nonfinite_or_fp16_failure(self):
        raw = struct.pack('<4e', -1/1024, 1+1/1024, 1.125, 1)
        result = contract.summarize(raw, 1, 1)
        self.assertEqual(result['classification'], 'finite-fp16-excursion')
        self.assertTrue(result['finite_fp16_contract'])
        self.assertFalse(result['bounded_sdr_rgb_contract'])
        self.assertEqual(result['affected_pixels'], 1)
        self.assertEqual(result['counts']['finite_excursion'], 3)
        self.assertEqual(result['hypothetical_unorm']['finite_channels_requiring_clipping'], 3)
        self.assertEqual(result['excursion_bins']['up_to_1_over_1024'], 2)
        self.assertEqual(result['excursion_bins']['over_1_over_64'], 1)

    def test_nonfinite_is_partitioned_and_not_hidden_by_range_counts(self):
        raw = struct.pack('<4H', 0x7e00, 0x7c00, 0xfc00, 0x3c00)
        result = contract.summarize(raw, 1, 1)
        self.assertEqual(result['classification'], 'nonfinite-output')
        self.assertEqual([result['counts'][k] for k in ['nan', 'positive_infinity', 'negative_infinity']], [1, 1, 1])
        self.assertFalse(result['finite_fp16_contract'])
        self.assertIsNone(result['minimum'])
        self.assertIsNone(result['maximum'])
        self.assertEqual(result['counts']['finite_excursion'], 0)

    def test_alpha_nonfinite_is_not_hidden_by_valid_rgb(self):
        result = contract.summarize(struct.pack('<4e', 0, .5, 1, float('nan')), 1, 1)
        self.assertEqual(result['classification'], 'nonfinite-output')
        self.assertEqual(result['alpha_nonfinite'], 1)
        self.assertFalse(result['finite_fp16_contract'])

    def test_signed_zero_subnormal_and_maximum_finite(self):
        result = contract.summarize(struct.pack('<4e', -0., -2**-24, 65504., 1), 1, 1)
        self.assertTrue(result['finite_fp16_contract'])
        self.assertEqual(result['below_zero'], 1)
        self.assertEqual(result['above_one'], 1)
        self.assertEqual(result['counts']['in_unit_interval'], 1)

    def test_domain_requires_provenance_even_when_every_value_is_bounded(self):
        linear = struct.pack('<4e', .25, .5, .75, 1)
        encoded = contract.prepare.transcode(linear)[0]
        proof = dict(transcode={key: dict(output_sha256=contract.sha(encoded)) for key in ['base', 'candidate']},
                     plane_hashes={key: contract.sha(linear) for key in ['source', 'composite']})
        self.assertEqual(contract.binding(encoded, encoded, proof), 'verified-explicit-srgb-transcode')
        self.assertEqual(contract.binding(linear, linear, proof), 'proven-linear-bytes-in-encoded-contract')
        self.assertEqual(contract.binding(encoded, encoded, proof, 'LinearSdr'), 'declared-domain-mismatch')
        unrelated = struct.pack('<4e', .2, .4, .6, 1)
        self.assertEqual(contract.binding(unrelated, unrelated, proof), 'unverified-payload-domain')

    def test_identity_change_is_rejected_even_when_pixels_remain_finite(self):
        raw = struct.pack('<4e', .2, .4, .6, 1)
        identity = dict(bytes=len(raw), sha256=contract.sha(raw))
        self.assertEqual(contract.verify_blob(raw, identity), raw)
        changed = bytearray(raw)
        changed[0] ^= 1
        with self.assertRaisesRegex(ValueError, '^evidence-identity-mismatch$'):
            contract.verify_blob(changed, identity)

    def test_dimensions_cannot_silently_truncate_pixels(self):
        with self.assertRaisesRegex(ValueError, 'shape mismatch'):
            contract.summarize(struct.pack('<4e', 0, 0, 0, 1), 2, 1)


if __name__ == '__main__':
    unittest.main()
