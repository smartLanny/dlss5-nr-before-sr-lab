"""Independent scalar oracles for area filtering and matched-delta reconstruction.
These are numerical tests, NOT execution of production HLSL or an NGX model.
"""
import math
import unittest
from fractions import Fraction

def area(image,nw,nh):
    h,w=len(image),len(image[0]);result=[]
    for y in range(nh):
        row=[]
        for x in range(nw):
            l,r=x*w/nw,(x+1)*w/nw;t,b=y*h/nh,(y+1)*h/nh
            total=weight=0.0
            for sy in range(math.floor(t),math.ceil(b)):
                for sx in range(math.floor(l),math.ceil(r)):
                    v=max(0,min(r,sx+1)-max(l,sx))*max(0,min(b,sy+1)-max(t,sy))
                    total+=image[min(sy,h-1)][min(sx,w-1)]*v;weight+=v
            row.append(total/weight)
        result.append(row)
    return result

def exact_area(image,nw,nh):
    h,w=len(image),len(image[0]);out=[]
    # Independent interval-overlap integral: iterate all SOURCE cells, rational coordinates.
    for y in range(nh):
        row=[]
        for x in range(nw):
            l,r=Fraction(x*w,nw),Fraction((x+1)*w,nw)
            t,b=Fraction(y*h,nh),Fraction((y+1)*h,nh)
            total=Fraction()
            for sy in range(h):
                for sx in range(w):
                    dx=max(Fraction(),min(r,sx+1)-max(l,sx))
                    dy=max(Fraction(),min(b,sy+1)-max(t,sy))
                    total+=Fraction(image[sy][sx])*dx*dy
            row.append(float(total/((r-l)*(b-t))))
        out.append(row)
    return out

def sample(image,x,y,w,h):
    mh,mw=len(image),len(image[0]);px=(x+0.5)*mw/w-0.5;py=(y+0.5)*mh/h-0.5
    ix,iy=math.floor(px),math.floor(py);fx,fy=px-ix,py-iy
    def at(a,b):return image[min(max(b,0),mh-1)][min(max(a,0),mw-1)]
    return ((at(ix,iy)*(1-fx)+at(ix+1,iy)*fx)*(1-fy)+
            (at(ix,iy+1)*(1-fx)+at(ix+1,iy+1)*fx)*fy)

class PostMath(unittest.TestCase):
    def test_area_matches_independent_rational_integral(self):
        for w,h in ((3,3),(7,5),(8,6),(11,7)):
            image=[[((x*17+y*13)%31)/16 for x in range(w)] for y in range(h)]
            for nw,nh in ((w,h),(max(1,w//2),max(1,h//2)),(w-1,h-1)):
                actual,expected=area(image,nw,nh),exact_area(image,nw,nh)
                for a,e in zip(sum(actual,[]),sum(expected,[])):self.assertAlmostEqual(a,e,places=12)
    def test_constant_preserved_and_no_unweighted_resize(self):
        for n in (3,4,5,6,7):
            out=area([[0.25]*9 for _ in range(7)],n,n)
            self.assertTrue(all(v==0.25 for row in out for v in row))
        image=[[0,0,1,0,0,0,0]]
        self.assertNotEqual(area(image,4,1)[0],[0,1,0,0])
    def test_matched_noop_leaves_high_frequency_original_exact(self):
        original=[[(x+y)%2 for x in range(11)] for y in range(7)]
        proxy=area(original,6,4);neural=[r[:] for r in proxy]
        restored=[[original[y][x]+sample(neural,x,y,11,7)-sample(proxy,x,y,11,7)
                   for x in range(11)] for y in range(7)]
        # Production has an explicit near-zero model-delta identity branch.
        for y in range(7):
            for x in range(11):
                d=sample(neural,x,y,11,7)-sample(proxy,x,y,11,7)
                self.assertEqual(d,0)
                self.assertEqual(original[y][x] if d==0 else restored[y][x],original[y][x])
    def test_nonzero_output_base_border_and_alpha(self):
        native=[[(x+y)/16 for x in range(13)] for y in range(9)]
        output=[r[:] for r in native];alpha=[[((x*3+y)%7)/8 for x in range(13)] for y in range(9)]
        old_alpha=[r[:] for r in alpha]
        for y in range(2,7):
            for x in range(3,10):output[y][x]+=0.125
        for y in range(9):
            for x in range(13):
                self.assertEqual(output[y][x],native[y][x]+(0.125 if 2<=y<7 and 3<=x<10 else 0))
        self.assertEqual(alpha,old_alpha)
    def test_pixel_center_sampling_not_corner_sampling(self):
        image=[[0.0,1.0]]
        self.assertEqual(sample(image,0,0,4,1),0)
        self.assertEqual(sample(image,1,0,4,1),0.25)
        self.assertEqual(sample(image,2,0,4,1),0.75)
        self.assertEqual(sample(image,3,0,4,1),1)
if __name__=='__main__':unittest.main()
