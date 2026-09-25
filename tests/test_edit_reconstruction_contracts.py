"""Properties of independent research examples, not production/GPU regression."""
from __future__ import annotations

import importlib.util
import json
import math
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/probe-edit-reconstruction-contracts.py"
SPEC = importlib.util.spec_from_file_location("edit_contract_probe", SCRIPT)
PROBE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = PROBE
SPEC.loader.exec_module(PROBE)


class ChromaConfidenceTests(unittest.TestCase):
    def test_requires_both_risks(self):
        self.assertEqual(PROBE.chroma_confidence(0.0001, 1), 1)
        self.assertEqual(PROBE.chroma_confidence(0.0001, 2), 1)
        self.assertEqual(PROBE.chroma_confidence(0.02, 8), 1)
        self.assertEqual(PROBE.chroma_confidence(0.0001, 8), 0)
        self.assertAlmostEqual(PROBE.chroma_confidence(0.0001, 4), 0.5)

    def test_disabled_is_identity(self):
        for gain in (0.25, 1, 8, 1024):
            self.assertEqual(PROBE.chroma_confidence(0, gain, enabled=False), 1)

    def test_random_bounds_and_monotonicity(self):
        rng = random.Random(20260925)
        for _ in range(2000):
            p0 = rng.uniform(0, 0.03)
            p1 = p0 + rng.uniform(0, 0.03)
            g0 = 2 ** rng.uniform(-4, 6)
            g1 = g0 * 2 ** rng.uniform(0, 3)
            a = PROBE.chroma_confidence(p0, g0)
            self.assertTrue(0 <= a <= 1)
            self.assertGreaterEqual(PROBE.chroma_confidence(p1, g0), a)
            self.assertLessEqual(PROBE.chroma_confidence(p0, g1), a)

    def test_envelope_is_configurable_not_codec_units(self):
        envelope = PROBE.ExampleEnvelope(0.1, 2.0, 2, 8)
        for peak in (0, 0.004, 0.008, 0.02):
            self.assertAlmostEqual(PROBE.chroma_confidence(peak * 100, 4, envelope),
                                   PROBE.chroma_confidence(peak, 4))

    def test_boundaries_are_continuous(self):
        for peak in (0.001, 0.02):
            self.assertAlmostEqual(PROBE.chroma_confidence(peak - 1e-9, 8),
                                   PROBE.chroma_confidence(peak + 1e-9, 8), places=10)
        for gain in (2, 8):
            self.assertAlmostEqual(PROBE.chroma_confidence(0, gain - 1e-8),
                                   PROBE.chroma_confidence(0, gain + 1e-8), places=10)

    def test_rejects_invalid_inputs_and_envelopes(self):
        for peak, gain in ((-1, 1), (0, 0), (math.nan, 1), (0, math.inf)):
            with self.assertRaises(ValueError):
                PROBE.chroma_confidence(peak, gain)
        for params in ((0.1, 0.1, 2, 8), (-1, 1, 2, 8), (0, 1, 8, 2),
                       (0, math.inf, 2, 8), (0, 1, 0, 8)):
            with self.assertRaises(ValueError):
                PROBE.ExampleEnvelope(*params)

    def test_dark_only_negative_control_breaks_low_gain_contract(self):
        dark_only = PROBE.smooth((0.0001 - 0.001) / (0.02 - 0.001))
        self.assertEqual(dark_only, 0)
        self.assertNotEqual(dark_only, PROBE.chroma_confidence(0.0001, 1))


class ColorPreservationTests(unittest.TestCase):
    def test_new_light_is_preserved_at_all_confidences(self):
        source, candidate = (0.4, 0.3, 0.2), (0.2, 0.1, 0.3)
        for target in (0, 0.05, 0.15, 1.5):
            for confidence in (0, 0.25, 0.5, 0.75, 1):
                rgb = PROBE.blend_at_target(source, candidate, target, confidence)
                self.assertAlmostEqual(PROBE.luminance(rgb), target, places=12)

    def test_endpoints_and_new_color_are_not_globally_removed(self):
        source, candidate = (0.4, 0, 0.2), (0.1, 0.3, 0.2)
        target = PROBE.luminance(candidate)
        self.assertEqual(PROBE.blend_at_target(source, candidate, target, 1), candidate)
        out = PROBE.blend_at_target(source, candidate, target, 0)
        self.assertEqual(out[1], 0)
        self.assertAlmostEqual(out[0] / out[2], 2)
        self.assertAlmostEqual(PROBE.luminance(out), target)

    def test_old_light_negative_control_is_wrong(self):
        source = (0.4, 0.3, 0.2)
        self.assertNotAlmostEqual(PROBE.luminance(source), 0.15)
        out = PROBE.blend_at_target(source, (0.1, 0.2, 0.3), 0.15, 0)
        self.assertAlmostEqual(PROBE.luminance(out), 0.15)

    def test_unsupported_values_are_rejected(self):
        for rgb in ((0, 0, 0), (-1, 0, 0), (math.inf, 0, 0), (1, 2)):
            with self.assertRaises(ValueError):
                PROBE.blend_at_target(rgb, (0.1, 0.2, 0.3), 0.15, 0.5)
        with self.assertRaises(ValueError):
            PROBE.blend_at_target((1, 1, 1), (1, 1, 1), 1, 1.1)


class GuidedEditTests(unittest.TestCase):
    def setUp(self):
        self.edits = [1.0 if x <= 0 else -1.0
                      for y in (-1, 0, 1, 2) for x in (-1, 0, 1, 2)]
        self.guides = [0.0 if x <= 0 else 2.0
                       for y in (-1, 0, 1, 2) for x in (-1, 0, 1, 2)]

    def test_luma_edge_can_separate_edits(self):
        value, fallback = PROBE.guided_edit(self.edits, self.guides, 0)
        self.assertGreater(value, 0.99)
        self.assertFalse(fallback)

    def test_equal_luma_cannot_invent_surface_separation(self):
        value, fallback = PROBE.guided_edit(self.edits, [0.0] * 16, 0)
        self.assertAlmostEqual(value, 0, places=14)
        self.assertFalse(fallback)

    def test_bad_guide_support_has_spatial_fallback(self):
        value, fallback = PROBE.guided_edit(self.edits, [100.0] * 16, 0)
        self.assertAlmostEqual(value, 0, places=14)
        self.assertTrue(fallback)

    def test_zero_and_constant_edits_are_preserved(self):
        for constant in (-2, 0, 0.3, 2):
            for guides in (self.guides, [100.0] * 16):
                value, _ = PROBE.guided_edit([constant] * 16, guides, 0)
                self.assertAlmostEqual(value, constant, places=13)

    def test_rejects_wrong_shape_and_invalid_samples(self):
        for edits in ([0.0] * 15, [0.0] * 15 + [math.nan]):
            with self.assertRaises(ValueError):
                PROBE.guided_edit(edits, self.guides, 0)
        with self.assertRaises(ValueError):
            PROBE.guided_edit(self.edits, self.guides, 0, px=2)


class EvidenceBoundaryTests(unittest.TestCase):
    def test_intermediate_ratio_is_not_final_output_bound(self):
        result = PROBE.floored_ratio_counterexample()
        self.assertEqual(result["intermediate_bounded_ratio"], 2)
        self.assertAlmostEqual(result["final_color_endpoint_gain"], 34.35294117647059)
        self.assertEqual(result["final_luma_endpoint_gain"], 2)
        self.assertIn("not target or production", result["source"])

    def test_delta_alone_does_not_localize_the_noise_source(self):
        proxy, model = [1.0] * 3, [1.0, 0.5, 1.0]
        delta = [n - p for p, n in zip(proxy, model)]
        self.assertEqual(delta, [0, -0.5, 0])
        # A negative spot exists before ANY reconstruction operation took place.
        self.assertEqual(model[1], proxy[1] + delta[1])

    def test_report_does_not_claim_runtime_or_gpu_validation(self):
        result = PROBE.report()
        for key in ("hlsl_compiled", "gpu_executed", "ngx_executed", "game_verified",
                    "measured_quality_or_fps_gain", "production_runtime_modified",
                    "parameters_are_production_defaults", "parameters_are_exact_target_immediates"):
            self.assertFalse(result[key])
        json.dumps(result, allow_nan=False)

    def test_cli_preserves_existing_receipt(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary) / "receipt.json"
            args = [sys.executable, str(SCRIPT), "--output", str(out)]
            first = subprocess.run(args, capture_output=True, timeout=30)
            self.assertEqual(first.returncode, 0, first.stderr)
            original = out.read_bytes()
            second = subprocess.run(args, capture_output=True, timeout=30)
            self.assertNotEqual(second.returncode, 0)
            self.assertEqual(out.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
