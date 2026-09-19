import random,struct,unittest

def f16(x):return struct.unpack('<e',struct.pack('<e',x))[0]
def f32(x):return struct.unpack('<f',struct.pack('<f',x))[0]
def codec(d,b):
 c=max(-b,min(b,d));p=f16(.5+.5*c/b);tail=f32(d-c)
 return (2*p-1)*b+tail
class Precision(unittest.TestCase):
 def test_small_edit(self):
  self.assertEqual(codec(.0001,4),0)
  self.assertLess(abs(codec(.0001,1/64)-.0001),1/64/2048)
 def test_signed_and_zero(self):
  for b in (1/64,.125,1,4,16):
   self.assertEqual(codec(0,b),0)
   for d in (-100,-4,-.5,.5,4,100):self.assertAlmostEqual(codec(d,b),d,places=5)
 def test_quantization(self):
  random.seed(21)
  for b in (1/64,.125,1,4,16):
   for _ in range(10000):
    d=random.uniform(-b,b);self.assertLessEqual(abs(codec(d,b)-d),b/2048+1e-12)
 def test_not_a_denoiser(self):
  seq=[codec((-1)**i*.003,1/64) for i in range(20)]
  self.assertGreater(max(seq)-min(seq),.0058)
if __name__=='__main__':unittest.main()
