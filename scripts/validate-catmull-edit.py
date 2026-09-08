"""Independent CPU Catmull/FP16 horizontal oracle for the bounded GPU capture."""
import argparse
import json
import math
from pathlib import Path
import struct


def weights(t):
    # Public a=-1/2 cubic interpolation polynomial, double-precision reference.
    w=[-.5*t+t*t-.5*t*t*t,1-2.5*t*t+1.5*t*t*t,
       .5*t+2*t*t-1.5*t*t*t,-.5*t*t+.5*t*t*t]
    return [x/sum(w) for x in w]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--capture',type=Path,required=True)
    a=ap.parse_args(); meta=json.loads((a.capture/'metadata.json').read_text())
    assert meta['kind']=='catmull-edit-fused-reference' and meta['count']==80
    checked=0; negative_control_hits=0; fallback_pixels=0; changed_pixels=0; max_fusion_error=0.
    for case in meta['cases']:
        prefix=str(case['id']);nw,nh=case['N'];rw,rh=meta['R']
        field=list(struct.iter_unpack('<ff',(a.capture/(prefix+'-edit.rg32f')).read_bytes()))
        horizontal=list(struct.iter_unpack('<ee',(a.capture/(prefix+'-horizontal.rg16f')).read_bytes()))
        assert len(field)==nw*nh and len(horizontal)==rw*nh
        outputs={name:list(struct.iter_unpack('<ffff',(a.capture/(prefix+'-'+name+'.rgba32f')).read_bytes()))
                 for name in ('bilinear','fused','reference')}
        assert all(len(data)==(rw+4)*(rh+3) for data in outputs.values())
        for y in range(rh):
            for x in range(rw):
                index=(y+1)*(rw+4)+x+2
                f,b,r=(outputs[key][index] for key in ('fused','bilinear','reference'))
                assert all(math.isfinite(v) for v in f+r+b)
                assert f[3]==b[3]==r[3]
                for fv,rv in zip(f,r):
                    difference=abs(fv-rv);max_fusion_error=max(max_fusion_error,difference)
                    assert difference<=3e-5+3e-4*max(abs(fv),abs(rv))
                base_y=math.floor((y+.5)*nh/rh-.5)
                valid=all(horizontal[max(0,min(nh-1,base_y+i-1))*rw+x][1]==1 for i in range(4))
                if not valid:
                    assert f==b,'Invalid Catmull did not retain valid Bilinear/source fallback'
                    fallback_pixels+=1
                if (nw,nh)==(rw,rh) or case['strength']==0 or case['fixture']==1:
                    assert f==b
                changed_pixels+=f!=b
        for y in range(nh):
            for x in range(rw):
                mapped=(x+.5)*nw/rw-.5;base=math.floor(mapped);w=weights(mapped-base)
                taps=[field[y*nw+max(0,min(nw-1,base+i-1))] for i in range(4)]
                valid=all(v==1 and math.isfinite(e) for e,v in taps)
                expected=sum(weight*tap[0] for weight,tap in zip(w,taps)) if valid else 0.
                expected=struct.unpack('<e',struct.pack('<e',expected))[0]
                value,probability=horizontal[y*rw+x]
                assert probability==int(valid) and math.isfinite(value)
                assert abs(value-expected)<=.001+abs(expected)*.001,(case,x,y,value,expected)
                # Mis-centred negative control must differ on patterned noninteger grids.
                wrong=x*nw/rw;base_bad=math.floor(wrong);wb=weights(wrong-base_bad)
                bad=sum(wb[i]*field[y*nw+max(0,min(nw-1,base_bad+i-1))][0] for i in range(4))
                negative_control_hits+=valid and math.isfinite(bad) and abs(value-bad)>.02
                checked+=1
    assert negative_control_hits>0,'Pixel-centre negative control was not sensitive'
    assert fallback_pixels>0,'Invalid-footprint control never exercised'
    print(f'PASS {checked} actual FP16 horizontal/strict-valid samples; wrong-centre hits={negative_control_hits}; '
          f'fallback pixels={fallback_pixels}; changed pixels={changed_pixels}; max fusion error={max_fusion_error}')


if __name__=='__main__':main()
