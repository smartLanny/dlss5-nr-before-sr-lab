"""Negative domain/range and byte-storage gates before any real model call."""
import json
import math
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import prepare_post_capture as capture


class CaptureTests(unittest.TestCase):
    def test_common_transcode_stores_fp16_and_preserves_alpha(self):
        raw = struct.pack('<8e', 0, .003, 1, .25, .125, .5, .75, 1)
        output, stats = capture.transcode(raw)
        values = struct.unpack('<8e', raw)
        expected = [v if i % 4 == 3 else 12.92*v if v <= .0031308 else 1.055*v**(1/2.4)-.055
                    for i, v in enumerate(values)]
        self.assertEqual(output, struct.pack('<8e', *expected))
        self.assertEqual(stats['clipped_count'], 0)
        self.assertEqual(stats['output_sha256'], capture.sha(output))

    def test_no_saturation_or_nonfinite_replacement(self):
        for value in [-2**-24, 1+1/1024, math.inf, -math.inf, math.nan]:
            with self.subTest(value=value):
                output, stats = capture.transcode(struct.pack('<4e', value, .5, .5, 1))
                self.assertIsNone(output)
                self.assertEqual(stats['clipped_count'], 0)
                self.assertEqual(stats['nonfinite_count']+stats['out_of_range_count'], 1)
                json.dumps(stats, allow_nan=False)

    def test_alpha_is_not_exempt_from_declared_input_range(self):
        self.assertIsNone(capture.transcode(struct.pack('<4e', .5, .5, .5, 1.5))[0])

    def test_unknown_producer_cannot_assert_sdr_domain(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            receipt = folder/'validation.json'
            receipt.write_text(json.dumps(dict(result=0, schema='unknown', scene_linear_verified=True)))
            with self.assertRaisesRegex(ValueError, 'unreviewed capture producer'):
                capture.prepare(folder/'absent.nrf', receipt, folder, True)

    def test_incomplete_inference_refused_before_opening_pixels(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            receipt = folder/'validation.json'
            receipt.write_text(json.dumps(dict(result=0, compile_only=True, schema='real-ngx-present-host-v1')))
            with self.assertRaisesRegex(ValueError, 'actual model validation did not pass'):
                capture.prepare(folder/'absent.nrf', receipt, folder, True)


if __name__ == '__main__':
    unittest.main()
