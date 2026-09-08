import unittest
from cutlass_sweep import parse_output
class Tests(unittest.TestCase):
    def test_pass(self): self.assertEqual(parse_output(0,'Disposition: Passed\nAvg runtime: 0.25 ms'), .25)
    def test_skip(self):
        with self.assertRaises(ValueError): parse_output(0,'This example requires a GPU of NVIDIA Blackwell architecture')
    def test_no_reference(self):
        with self.assertRaises(ValueError): parse_output(0,'Avg runtime: 0.25 ms')
    def test_failed(self):
        with self.assertRaises(ValueError): parse_output(1,'Disposition: Passed\nAvg runtime: 0.25 ms')
    def test_zero(self):
        with self.assertRaises(ValueError): parse_output(0,'Disposition: Passed\nAvg runtime: 0 ms')
if __name__ == '__main__': unittest.main()
