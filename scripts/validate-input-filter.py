#!/usr/bin/env python3
"""Independent raw-source/FP16-intermediate/model-input oracle; no Neural output."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]

def unit(x):return min(1,max(0,x))
def srgb(x):
    x=unit(x);return 12.92*x if x<.0031308 else 1.055*x**(1/2.4)-.055
def decode(v,flags):
    if flags in (1,16):return [unit(x) for x in v]
    if flags==32:return [unit(x)/12.92 if unit(x)<.04045 else ((unit(x)+.055)/1.055)**2.4 for x in v]
    if flags==128:
        out=[]
        for q in v:
            x=unit(q)**(1/78.84375);out.append((max(x-.8359375,0)/max(18.8515625-18.6875*x,1e-6))**(1/.1593017578125)*125)
        return out
    return [max(x,0) for x in v]
def encode(v,flags):
    v=[max(0,x) for x in v]
    if flags in (1,16,32):v=[unit(x) for x in v]
    else:
        peak=max(v)
        if peak>.75:
            e=(peak-.75)/.25;mapped=.75+.25*e/math.sqrt(e*e+1)
            v=[x*mapped/peak for x in v]
    return [unit(x) for x in v] if flags==1 else [srgb(x) for x in v]
def f32(x):return struct.unpack('<f',struct.pack('<f',x))[0]
def weights(destination,source_extent,target_extent):
    if source_extent==target_extent:return [(destination,1.)]
    # Support decisions must use the actual FP32 mapping. A double-only centre
    # can put a mathematical endpoint infinitesimally inside the support and
    # invent a nonzero NaN tap that the production float x>=2 branch excludes.
    # Do not threshold away small nonzero weights inside that actual support.
    scale=f32(target_extent/source_extent);center=f32(f32(f32(destination+.5)/scale)-.5)
    radius=f32(2/scale);pi=f32(math.pi)
    result=[]
    for p in range(math.ceil(f32(center-radius)),math.floor(f32(center+radius))+1):
        x=abs(f32(f32(p-center)*scale))
        px=f32(pi*x);half=f32(f32(.5*pi)*x)
        w=0 if x>=2 else 1 if x<1e-6 else f32(f32(f32(math.sin(px))/px)*f32(f32(math.sin(half))/half))
        if w:result.append((max(0,min(source_extent-1,p)),w))
    norm=0.
    for _,w in result:norm=f32(norm+w)
    assert norm>1e-6
    return [(p,w/norm) for p,w in result]

def verify(root,negative=None):
    m=json.loads((root/'input-filter-contract.json').read_text())
    assert m['schema']=='nr-input-filter-fixture-v1' and m['cases_total']==200
    assert m['NR_calls']==m['SR_calls']==0 and not m['old_neural_reused']
    if negative=='coordinate':m['source_rect'][0]+=1
    sx,sy,rw,rh=m['source_rect'];sw,sh=m['source_texture']
    hashes={};total=0;negative_mid=0;model_pixels=0;area_baseline=0;one_to_one=0;mid_values=0
    def read(name,fmt,extent):
        nonlocal total
        raw=(root/name).read_bytes();assert len(raw)==math.prod(extent)*struct.calcsize(fmt),(name,'extent')
        total+=len(raw);hashes[name]=hashlib.sha256(raw).hexdigest();return raw,list(struct.iter_unpack(fmt,raw))
    for c in m['cases']:
        prefix=f"case-{c['id']}-";nw,nh=c['N'];pw,ph=c['proxy_texture'];flags=c['flags'];valid_source=c['pattern']<6
        _,source=read(prefix+'source.rgba32f','<4f',[sw,sh])
        a_raw,area=read(prefix+'area.rgba16f','<4e',[pw,ph]);b_raw,candidate=read(prefix+'candidate.rgba16f','<4e',[pw,ph])
        if 'baseline_source_sha256' in m:
            old_raw,_=read(prefix+'baseline.rgba16f','<4e',[pw,ph])
            if valid_source:assert old_raw==a_raw,(c['id'],'finite_area_baseline');area_baseline+=1
        def sample(x,y):return source[(max(0,min(rh-1,y))+sy)*sw+max(0,min(rw-1,x))+sx][:3]
        def area_reference(x,y):
            if nw==rw and nh==rh and flags in (1,32):return list(sample(x,y))
            x0,x1=x*rw/nw,(x+1)*rw/nw;y0,y1=y*rh/nh,(y+1)*rh/nh;acc=[0.,0.,0.];norm=0.
            for yy in range(math.floor(y0),math.ceil(y1)):
                wy=max(0,min(y1,yy+1)-max(y0,yy))
                for xx in range(math.floor(x0),math.ceil(x1)):
                    w=wy*max(0,min(x1,xx+1)-max(x0,xx));v=decode(sample(xx,yy),flags)
                    for k in range(3):acc[k]+=v[k]*w
                    norm+=w
            return encode([v/norm for v in acc],flags)
        for yy in range(ph):
            for xx in range(pw):
                av,bv=area[yy*pw+xx],candidate[yy*pw+xx]
                if xx>=nw or yy>=nh:
                    assert av==bv==(-123.,)*4,(c['id'],'proxy padding');continue
                assert all(math.isfinite(v) and 0<=v<=1 for v in (*av[:3],*bv[:3])) and av[3]==bv[3]==1,(c['id'],'model bounds')
                model_pixels+=1
                if valid_source:
                    expected=area_reference(xx,yy)
                    assert max(abs(av[k]-expected[k]) for k in range(3))<.0015,(c['id'],'area_oracle',xx,yy,av,expected)
        if not c['horizontal_executed']:
            assert [nw,nh]==[rw,rh] and a_raw==b_raw,(c['id'],'one_to_one');one_to_one+=1;continue
        mw,mh=c['horizontal_texture'];_,mid=read(prefix+'horizontal.rgba16f','<4e',[mw,mh])
        if negative=='normalization' and c['id']==0:mid[0]=tuple(v*1.125 for v in mid[0][:3])+(mid[0][3],)
        scale=500. if flags==128 else 4.
        for yy in range(mh):
            for xx in range(mw):
                got=mid[yy*mw+xx]
                if xx>=nw or yy>=rh:assert got==(-123.,)*4,(c['id'],'intermediate padding');continue
                assert all(math.isfinite(v) for v in got) and got[3] in (0,1),(c['id'],'intermediate finite/valid')
                if got[3]:negative_mid+=sum(v<0 for v in got[:3])
                taps=weights(xx,rw,nw);valid=all(all(math.isfinite(v) and abs(v)<=65504 for v in sample(x,yy)) for x,_ in taps)
                if not valid:assert got==(0.,)*4,(c['id'],'intermediate invalid');continue
                expect=[sum(decode(sample(x,yy),flags)[k]*w/scale for x,w in taps) for k in range(3)]
                assert got[3]==1 and all(abs(got[k]-expect[k])<=.0002+.002*abs(expect[k]) for k in range(3)),(c['id'],'horizontal_oracle',xx,yy,got,expect)
                mid_values+=1
        if valid_source:
            for yy in range(nh):
                taps=weights(yy,rh,nh)
                for xx in range(nw):
                    val=[sum(mid[y*mw+xx][k]*w*scale for y,w in taps) for k in range(3)]
                    expected=encode(val,flags);got=candidate[yy*pw+xx]
                    assert max(abs(got[k]-expected[k]) for k in range(3))<.0015,(c['id'],'vertical_oracle',xx,yy,got,expected)
    assert total==m['plane_bytes']<=m['byte_budget']==8388608
    assert negative_mid>0,'Signed intermediate lobes were not demonstrated'
    if 'baseline_source_sha256' in m:assert area_baseline==m['baseline_equal_finite_cases']==150
    return m,hashes,{'model_pixels':model_pixels,'valid_horizontal_samples':mid_values,'negative_intermediate_components':negative_mid,'one_to_one_cases':one_to_one,'finite_area_baseline_cases':area_baseline}

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--capture',type=Path,required=True);ap.add_argument('--source',type=Path,required=True)
    ap.add_argument('--negative-control',choices=('coordinate','normalization'))
    ap.add_argument('--gpu-fixture-commit',help='Verified source commit at the earlier GPU execution, when rechecking preserved raw data')
    a=ap.parse_args()
    if a.negative_control:
        label='area_oracle' if a.negative_control=='coordinate' else 'horizontal_oracle'
        try:verify(a.capture,a.negative_control)
        except AssertionError as e:
            assert e.args and isinstance(e.args[0],tuple) and e.args[0][1]==label,('wrong negative failure',e.args)
            print('PASS exact input oracle negative control:',a.negative_control);return
        raise AssertionError('Input oracle negative control was not detected')
    m,hashes,counts=verify(a.capture);mf=a.source.parents[2]/'source-manifest.json';manifest=json.loads(mf.read_text())
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();assert sha(a.source)==manifest['files']['core/src/nr_before_sr.cpp']
    validator_commit=subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip()
    gpu_commit=a.gpu_fixture_commit or validator_commit
    assert len(gpu_commit)==40 and all(c in '0123456789abcdef' for c in gpu_commit)
    report={'source_commit':manifest['source_commit'],'source_sha256':sha(a.source),'manifest_sha256':sha(mf),'contract_sha256':sha(a.capture/'input-filter-contract.json'),
        'fixture_commit':gpu_commit,'validator_commit':validator_commit,'validator_sha256':sha(Path(__file__)),
        'validator_dirty':bool(subprocess.check_output(['git','-C',str(ROOT),'status','--porcelain','--untracked-files=no'],text=True).strip()),
        'planes':hashes,'counts':counts,'NR_calls':0,'SR_calls':0,'old_neural_reused':False,'nvidia_verified':False,'game_verified':False}
    (a.capture/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS 200 input-filter pairs:',counts)

if __name__=='__main__':main()
