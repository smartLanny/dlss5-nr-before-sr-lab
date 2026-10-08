"""Independent CPU mathematical oracle and post-run byte checks; never calls D3D/NGX."""
from pathlib import Path
import hashlib,json,math
import numpy as np
ROOT=Path(__file__).resolve().parent
def compose(raw,clean,edited,shift):
 """FP32 ratio before positive bilinear gather; FP16 input/output, raw bit fallback."""
 raw=np.asarray(raw,dtype='<f2');clean=np.asarray(clean,dtype='<f2');edited=np.asarray(edited,dtype='<f2')
 assert raw.shape==clean.shape==edited.shape and raw.ndim==3 and raw.shape[2]==4
 h,w,_=raw.shape;out=raw.copy();accepted=np.zeros((h,w),bool);identity=np.ones((h,w),bool);valid=np.ones((h,w),bool);ratio=np.zeros((h,w,3),np.float32)
 if not all(math.isfinite(v) for v in shift):return out,{'accepted':0,'identity':0,'refused':h*w},accepted
 yy,xx=np.mgrid[:h,:w];px=xx.astype(np.float32)+np.float32(shift[0]);py=yy.astype(np.float32)+np.float32(shift[1]);bx=np.floor(px).astype(np.int32);by=np.floor(py).astype(np.int32);fx=px-bx;fy=py-by
 with np.errstate(divide='ignore',invalid='ignore',over='ignore'):
  for y in range(2):
   for x in range(2):
    weight=((fx if x else 1-fx)*(fy if y else 1-fy)).astype(np.float32);positive=weight>0;qx=bx+x;qy=by+y
    inside=(qx>=0)&(qx<w)&(qy>=0)&(qy<h);valid&=(~positive)|inside
    c=clean[np.clip(qy,0,h-1),np.clip(qx,0,w-1),:3].astype(np.float32);a=edited[np.clip(qy,0,h-1),np.clip(qx,0,w-1),:3].astype(np.float32)
    finite=np.all(np.isfinite(c)&np.isfinite(a),axis=2);same=np.all(c==a,axis=2);identity&=(~positive)|same
    r=np.where(same[:,:,None],np.float32(1),a/c).astype(np.float32)
    domain=finite&(same|(np.all(c>0,axis=2)&np.all(a>=0,axis=2)&np.all(np.isfinite(r),axis=2)))
    valid&=(~positive)|domain
    # Ignore irrelevant zero-weight taps even when their loaded ratio is NaN.
    ratio=(ratio+np.where(positive[:,:,None],weight[:,:,None]*r,np.float32(0))).astype(np.float32)
  rgb=raw[:,:,:3].astype(np.float32);value=(rgb*ratio).astype(np.float32)
  accepted=valid&(~identity)&(raw[:,:,3]==np.float16(1))&np.all(np.isfinite(rgb)&(rgb>=0)&np.isfinite(value)&(value>=0)&(value<=65504),axis=2)
  out[accepted,:3]=value[accepted].astype('<f2')
 return out,{'accepted':int(accepted.sum()),'identity':int((valid&identity).sum()),'refused':int((~accepted&~(valid&identity)).sum())},accepted
def cpu_cases():
 cases=[]
 def check(name,condition,**facts):
  assert condition,name;cases.append({'name':name,'pass':True,**facts})
 raw=np.ones((2,2,4),dtype='<f2');raw[:,:,3]=1;clean=raw.copy();edited=raw.copy()
 # Numerical equality includes signed zero; copy fallback must preserve raw NaN payload/signed zero.
 bits=raw.view('<u2');bits[0,0]=[0x7e35,0x8000,0xbc00,0x7e77]
 out,counts,_=compose(raw,clean,edited,(0,0));check('identity-copy-preserves-NaN-payload-negative-signedzero-alpha',out.tobytes()==raw.tobytes(),counts=counts)
 raw=np.ones((1,2,4),dtype='<f2');raw[:,:,3]=1;clean=raw.copy();edited=raw.copy();clean[0,1,:3]=3;edited[0,0,:3]=2;edited[0,1,:3]=3
 out,counts,_=compose(raw,clean,edited,(.5,0));check('ratio-before-shift-does-not-commute',float(out[0,0,0])==1.5 and (2+3)/(1+3)==1.25,ratio_before=1.5,ratio_after=1.25)
 check('positive-weight-outside-grid-refuses-exact-copy',out[0,1].tobytes()==raw[0,1].tobytes(),counts=counts)
 out,_,_=compose(raw,clean,edited,(0,0));check('zero-weight-outside-tap-ignored',float(out[0,0,0])==2 and float(out[0,1,0])==1)
 edited[0,0,:3]=1;clean[0,0,:3]=0;out,_,_=compose(raw,clean,edited,(0,0));check('nonidentity-zero-denominator-refuses',out[0,0].tobytes()==raw[0,0].tobytes())
 clean[0,0,:3]=-1;out,_,_=compose(raw,clean,edited,(0,0));check('negative-denominator-refuses',out[0,0].tobytes()==raw[0,0].tobytes())
 clean[0,0,:3]=1;edited[0,0,:3]=-1;out,_,_=compose(raw,clean,edited,(0,0));check('negative-edit-refuses',out[0,0].tobytes()==raw[0,0].tobytes())
 edited[0,0,:3]=np.inf;out,_,_=compose(raw,clean,edited,(0,0));check('nonfinite-edit-refuses',out[0,0].tobytes()==raw[0,0].tobytes())
 edited[0,0,:3]=65504;raw[0,0,:3]=65504;out,_,_=compose(raw,clean,edited,(0,0));check('FP16-overflow-refuses',out[0,0].tobytes()==raw[0,0].tobytes())
 out,_,_=compose(raw,clean,edited,(math.nan,0));check('nonfinite-phase-refuses-whole-plane',out.tobytes()==raw.tobytes())
 raw=np.ones((1,1,4),dtype='<f2');raw[:,:,3]=1;clean=raw.copy();edited=raw*2
 raw.view('<u2')[0,0,3]=0x7e77;out,_,_=compose(raw,clean,edited,(0,0));check('nonidentity-unknown-alpha-refuses-all-raw-bits',out.tobytes()==raw.tobytes())
 raw[:]=1;raw[:,:,3]=1;raw.view('<u2')[0,0,0]=0x7e35;out,_,_=compose(raw,clean,edited,(0,0));check('nonidentity-raw-NaN-refuses-payload-exactly',out.tobytes()==raw.tobytes())
 raw[:]=1;raw[:,:,3]=1;raw[0,0,0]=-1;out,_,_=compose(raw,clean,edited,(0,0));check('nonidentity-negative-raw-refuses',out.tobytes()==raw.tobytes())
 # Frozen raster material displacement s and NGX jitter j satisfy s=-j.
 # For clean affine field E(x)=1+x/4, raw sample at x+s must read E(x-j).
 raw=np.ones((1,4,4),dtype='<f2');raw[:,:,3]=1;clean=raw.copy();edited=raw.copy();edited[0,:,:3]=np.arange(4)[:,None]/4+1
 right,_,_=compose(raw,clean,edited,(.25,0));wrong,_,_=compose(raw,clean,edited,(-.25,0))
 check('raster-s=-j-pullback-uses-minus-j',float(right[0,1,0])==1.3125 and float(wrong[0,1,0])==1.1875,s=.25,j=-.25,right=1.3125,wrong=1.1875)
 report={'CPU':True,'GPU':False,'NGX':False,'tests':cases,'scope':'algorithmic counterexamples and FP16 copy oracle; no GPU shader equivalence or RR output acceptance'}
 (ROOT/'CPU_RATIO_CASES.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':print(json.dumps(cpu_cases(),indent=2))
