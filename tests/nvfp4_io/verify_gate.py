"""Check recovered half-precision polynomial gate and FP4 output quantization."""
import argparse
import json
from pathlib import Path
import numpy as np
from verify import driver,guarded,intact,blob,fp8_encode,lookup_fp8

def gate(x):
    x=x.astype(np.float16);t=np.clip(x,-4,4).astype(np.float16)
    coefficient=np.array([0xab28],np.uint16).view(np.float16)[0]
    q=(np.abs(t).astype(np.float32)*np.float32(coefficient)+np.float32(.447265625)).astype(np.float16)
    q=(t.astype(np.float32)*q.astype(np.float32)+np.float32(.89453125)).astype(np.float16)
    return (x*q).astype(np.float16)

def oracle(x,inverse):
    rows,cols=x.shape;padded=(rows+127)//128*128;y=gate(x).reshape(rows,cols//16,16).astype(np.float32)
    maximum=np.max(np.abs(y),axis=2)
    raw=np.maximum(np.minimum(maximum*np.float32(1/6)*np.float32(inverse),np.float32(448)),np.float32(1/512))
    scale=fp8_encode(raw);multiplier=np.float32(inverse)/lookup_fp8()[scale].astype(np.float32)
    values=(y*multiplier[:,:,None]).astype(np.float16)
    table=np.array([0,.5,1,1.5,2,3,4,6],np.float32);v=np.minimum(np.abs(values).astype(np.float32),6)
    hi=np.searchsorted(table,v).clip(0,7);lo=np.maximum(hi-1,0);dl=v-table[lo];dh=table[hi]-v
    code=np.where((dl<dh)|((dl==dh)&((lo&1)==0)),lo,hi).astype(np.uint8)|(np.signbit(values).astype(np.uint8)<<3)
    code=code.reshape(rows,cols);packed=(code[:,::2]|(code[:,1::2]<<4)).astype(np.uint8)
    sf=np.ones(padded*cols//16,np.uint8);r=np.arange(rows)[:,None];g=np.arange(cols//16)[None,:]
    ix=((r//128)*((cols//16)//4)+g//4)*512+(r%32)*16+((r%128)//32)*4+g%4;sf[ix]=scale
    return packed.tobytes(),sf.tobytes()

def main():
    p=argparse.ArgumentParser();p.add_argument('--candidate',type=Path,required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    cuda=driver.Cuda();old=cuda.load(a.reference,'gate_to_nvfp4',0);new=cuda.load(a.candidate,'nr_gate_to_nvfp4',0);rng=np.random.default_rng(18640);results=[]
    for rows in (1,127,128,129,640,960):
      for inverse in (.5,1.,2.):
        cols=4096;padded=(rows+127)//128*128;x=rng.uniform(-12,12,(rows,cols)).astype(np.float16);x.flat[:12]=[0,-0.,-4,4,-.01,.01,-1,1,-8,8,-.5,.5]
        source=guarded(cuda,x.tobytes());out=[guarded(cuda,bytes(padded*cols//2)) for _ in range(2)];sf=[guarded(cuda,bytes(padded*cols//16)) for _ in range(2)]
        for j,f in enumerate((old,new)):cuda.launch(f,blob('<QQQIIf',source[0],out[j][0],sf[j][0],rows,cols,inverse),(padded*cols//4//256,1,1),(256,1,1),0)
        cuda.call('cuCtxSynchronize');ab=[cuda.read(o[0],o[1]) for o in out];sb=[cuda.read(s[0],s[1]) for s in sf];expected,expected_sf=oracle(x,inverse)
        diff=sum(x!=y for x,y in zip(*ab));sdiff=sum(x!=y for x,y in zip(*sb));cpu=sum(x!=y for x,y in zip(ab[0][:rows*cols//2],expected));cpusf=sum(x!=y for x,y in zip(sb[0],expected_sf))
        ok=all(intact(cuda,g) for g in [source,*out,*sf]);r={'rows':rows,'inverse_tensor_scale':inverse,'packed_differences':diff,'scale_differences':sdiff,'cpu_packed_differences':cpu,'cpu_scale_differences':cpusf,'guards_intact':ok,'passed':not(diff or sdiff or cpu or cpusf) and ok};print(json.dumps(r),flush=True);results.append(r);cuda.clear()
    report={'scope':'isolated CUDA arithmetic/packing; no fused-GEMM or game equivalence claim','results':results,'passed':all(r['passed'] for r in results)};a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    if not report['passed']:raise SystemExit(1)

if __name__=='__main__':main()
