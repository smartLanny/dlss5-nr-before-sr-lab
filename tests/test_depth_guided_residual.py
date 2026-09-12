"""Synthetic CPU checks only: no runtime, device, frame history or image quality claim."""

from dataclasses import replace
import math
from pathlib import Path
import random
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from experiments.depth_guided_residual import (  # noqa: E402
    DepthGuide, FrameDomain, ResidualTap, SYNTHETIC_VIEW_Z,
    baseline_average, interpolate_residual,
)


DOMAIN = FrameDomain(41, 7, (16, 8, 3, 3), "post-sr")
POINTS = ((0, 1), (2, 1), (1, 0), (1, 2))


def guide(values=None, **changes):
    depth = DepthGuide(((2.0,) * 3,) * 3 if values is None else values, DOMAIN,
                       "positive-view-z", "fixture-scale-A", SYNTHETIC_VIEW_Z)
    return replace(depth, **changes)


def estimate(depth, residuals=(2.0, 8.0, 2.0, 8.0), weights=(.15,) * 4, center=0.0):
    taps = tuple(ResidualTap(x, y, value, weight)
                 for (x, y), value, weight in zip(POINTS, residuals, weights))
    return interpolate_residual(center=(1, 1), center_residual=center,
                                taps=taps, domain=DOMAIN, depth=depth)


class DepthGuidedResidualTests(unittest.TestCase):
    def test_same_plane_exactly_preserves_baseline_for_varied_residuals(self):
        rng = random.Random(1701)
        for _ in range(64):
            residuals = tuple(rng.uniform(-8, 8) for _ in POINTS)
            weights = tuple(rng.uniform(0, .15) for _ in POINTS)
            center = rng.uniform(-8, 8)
            got = estimate(guide(), residuals, weights, center)
            self.assertTrue(got.depth_used)
            self.assertEqual(got.value.hex(), got.baseline.hex())
            self.assertEqual(got.neighbor_weights, weights)
        # Independent simple numeric oracle, as well as exact identity above.
        self.assertAlmostEqual(estimate(guide()).value, 3.0)

    def test_depth_boundary_blocks_other_surface_but_keeps_same_surface(self):
        depth = guide(((2., 2., 20.), (2., 2., 20.), (20., 20., 20.)))
        before = estimate(depth)
        changed_other_surface = estimate(depth, (2., -8., 2., -8.))
        self.assertEqual(before.neighbor_weights, (.15, 0., .15, 0.))
        self.assertAlmostEqual(before.value, .6 / .7)
        self.assertEqual(before.value, changed_other_surface.value)
        self.assertNotEqual(before.baseline, changed_other_surface.baseline)

    def test_all_rejected_neighbors_return_exact_center(self):
        depth = guide(((20., 20., 20.), (20., 2., 20.), (20., 20., 20.)))
        center = math.nextafter(.3, 1.0)
        got = estimate(depth, center=center)
        self.assertEqual(got.value.hex(), center.hex())
        self.assertEqual(got.neighbor_weights, (0.,) * 4)
        self.assertEqual(got.reason, "center-only")

    def test_uniform_scale_does_not_change_two_plane_result(self):
        original = guide(((2., 2., 20.), (2., 2., 20.), (20., 20., 20.)))
        scaled = replace(original, values=tuple(tuple(z * 10 for z in row)
                                                for row in original.values),
                         scale_id="fixture-scale-B-ten-times-A")
        self.assertEqual(estimate(original), estimate(scaled))

    def test_missing_or_mismatched_guide_exactly_uses_existing_baseline(self):
        cases = [None]
        for field, value in (("frame_id", 42), ("generation", 8),
                             ("viewport", (17, 8, 3, 3)), ("route", "pre-sr")):
            cases.append(guide(domain=replace(DOMAIN, **{field: value})))
        for depth in cases:
            with self.subTest(depth=depth):
                got = estimate(depth)
                self.assertFalse(got.depth_used)
                self.assertEqual(got.value.hex(), got.baseline.hex())
                self.assertEqual(got.neighbor_weights, (.15,) * 4)

    def test_unknown_scale_and_non_view_z_encodings_are_not_guessed(self):
        cases = [guide(scale_id=None), guide(scale_id=" "),
                 guide(provenance=None), guide(provenance="engine-unverified")]
        for encoding in ("hardware", "ndc", "inverse-view-z", "unknown"):
            cases.append(guide(encoding=encoding))
        for depth in cases:
            with self.subTest(depth=depth):
                got = estimate(depth)
                self.assertFalse(got.depth_used)
                self.assertEqual(got.value, got.baseline)

    def test_zero_negative_and_nonfinite_depth_never_become_geometry(self):
        for value in (0., -1., math.nan, math.inf, -math.inf):
            for y, x in ((1, 1), (0, 0)):  # Even an unsampled invalid point rejects the guide.
                rows = [[2.] * 3 for _ in range(3)]
                rows[y][x] = value
                got = estimate(guide(tuple(tuple(row) for row in rows)))
                self.assertEqual(got.reason, "depth-invalid")
                self.assertFalse(got.depth_used)
                self.assertEqual(got.value, got.baseline)

    def test_grid_mismatch_falls_back_even_if_frame_identity_matches(self):
        for values in ((), ((2.,) * 3,) * 2, ((2.,) * 2,) * 3, ((2.,) * 3, (), (2.,) * 3)):
            got = estimate(guide(values))
            self.assertEqual(got.reason, "depth-grid-mismatch")
            self.assertEqual(got.value, got.baseline)

    def test_weights_only_decrease_and_estimate_stays_in_retained_range(self):
        rng = random.Random(223)
        for _ in range(64):
            depths = tuple(tuple(rng.choice((2., 20.)) for _ in range(3)) for _ in range(3))
            residuals = tuple(rng.uniform(-8., 8.) for _ in POINTS)
            weights = tuple(rng.uniform(0., .15) for _ in POINTS)
            center = rng.uniform(-8., 8.)
            got = estimate(guide(depths), residuals, weights, center)
            retained = [center]
            for value, before, after in zip(residuals, weights, got.neighbor_weights):
                self.assertTrue(0. <= after <= before <= .15)
                if after:
                    retained.append(value)
            self.assertTrue(min(retained) - 1e-12 <= got.value <= max(retained) + 1e-12)

    def test_zero_baseline_weights_keep_center_with_or_without_guide(self):
        for depth in (None, guide()):
            self.assertEqual(estimate(depth, weights=(0.,) * 4, center=-.37).value, -.37)

    def test_malformed_residual_kernel_is_rejected_without_reading_depth(self):
        for weight in (-.01, .16, math.inf, math.nan):
            with self.assertRaises(ValueError):
                estimate(None, weights=(weight, .15, .15, .15))
        with self.assertRaises(ValueError):
            baseline_average(math.nan, ())
        with self.assertRaises(ValueError):
            baseline_average(0., (ResidualTap(0, 0, 1.),) * 5)
        with self.assertRaises(ValueError):
            interpolate_residual(center=(3, 1), center_residual=0., taps=(), domain=DOMAIN, depth=None)

    def test_inputs_are_preserved(self):
        depth = guide()
        original = repr(depth)
        estimate(depth)
        self.assertEqual(repr(depth), original)


if __name__ == "__main__":
    unittest.main(verbosity=2)
