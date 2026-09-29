"""CPU quality examples for temporal shader review; not GPU/shader parity evidence.

The oracle states required behavior and includes naive counterexamples. Production
shader output must be compared with these fixtures by a separate GPU test.
"""
import math
import unittest


def reconstruct(current, colours, previous, previous_colour, mode=1, weight=.75):
    """Nine samples in row-major order; centre is index 4."""
    assert len(current) == len(colours) == 9 and mode in (0, 1)
    centre = current[4]
    if not all(math.isfinite(v) for v in (*current, *colours, previous, previous_colour)):
        return centre
    colour_delta = abs(colours[4] - previous_colour)
    if colour_delta > .12 or centre*previous < 0 and min(abs(centre), abs(previous)) > .02:
        return centre
    accepted = [(v, 1.0 / (1.0 + 25.0 * abs(c-colours[4])))
                for v, c in zip(current, colours) if abs(c-colours[4]) < .12]
    low = sum(v*w for v, w in accepted) / sum(w for _, w in accepted)
    lo, hi = min(v for v, _ in accepted), max(v for v, _ in accepted)
    previous = min(hi, max(lo, previous))
    confidence = max(0.0, 1.0 - abs(centre-previous) / max(.01, hi-lo+.01))
    blend = weight * confidence
    if mode == 0:
        return centre * (1-blend) + previous * blend
    # Accumulate only the low-frequency edit; current-frame high frequency stays.
    return centre-low + low*(1-blend) + previous*blend


class TemporalQualityOracle(unittest.TestCase):
    def test_uniform_edit_does_not_shift_or_blur(self):
        values = [.32]*9
        self.assertAlmostEqual(reconstruct(values, [.5]*9, .32, .5), .32)

    def test_current_thin_line_survives_low_frequency_reconstruction(self):
        values = [0.,0.,0.,0.,1.,0.,0.,0.,0.]
        got = reconstruct(values, [.5]*9, 0., .5, mode=1)
        naive = .25*values[4] + .75*0.
        self.assertGreater(got, naive)
        self.assertGreater(got, .5)

    def test_old_line_cannot_ghost_into_uniform_current_edit(self):
        current = [0.]*9
        self.assertEqual(reconstruct(current, [.5]*9, 1., .5), 0.)
        self.assertGreater(.25*current[4]+.75*1., 0.)  # naive negative control

    def test_colour_boundary_rejects_previous_surface(self):
        current = [0.]*9
        self.assertEqual(reconstruct(current, [.1]*9, .8, .9), 0.)

    def test_current_edge_not_pulled_by_neighbours_across_colour_boundary(self):
        current = [0.]*9; current[4] = .8
        colours = [.1]*9; colours[4] = .9
        self.assertAlmostEqual(reconstruct(current, colours, .8, .9), .8)

    def test_nonfinite_history_falls_back_to_current(self):
        current = [.2]*9
        for value in (float('nan'), float('inf'), -float('inf')):
            self.assertEqual(reconstruct(current, [.5]*9, value, .5), .2)

    def test_opposite_signed_edit_is_not_temporally_cancelled(self):
        current = [.25]*9
        self.assertEqual(reconstruct(current, [.5]*9, -.25, .5, mode=0), .25)
        self.assertEqual(reconstruct(current, [.5]*9, -.25, .5, mode=1), .25)

    def test_hand_computed_neighbourhood_and_current_high_frequency(self):
        current = [.2]*9; current[4] = .3
        low = (8*.2+.3)/9
        confidence = 1-.1/.11
        expected = .3 + .75*confidence*(.2-low)
        self.assertAlmostEqual(reconstruct(current, [.5]*9, .2, .5), expected)
        self.assertLess(abs(reconstruct(current, [.5]*9, .2, .5)-.3), .002)


if __name__ == '__main__':
    unittest.main()
