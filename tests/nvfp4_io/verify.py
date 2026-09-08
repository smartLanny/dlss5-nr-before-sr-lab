"""Byte-exact GPU comparison of recovered IO against immutable upstream cubin."""
import argparse
import ctypes as ct
import importlib.util
import json
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('nvfp4_driver',ROOT/'tests/nvfp4_kernel/benchmark.py')
driver=importlib.util.module_from_spec(spec);spec.loader.exec_module(driver)

def original_offset(row,pair,cols):
    return ((((row*4)&28)|(pair&3)|(((row>>4)*cols+pair*2)&0xfffffe0))*16
            +(pair&8)+((row>>1)&4)+((pair>>1)&2))

def coordinates(i):
    return (((i>>9)&0x3ffff0)|((i>>5)&7)|((i*4)&8),
            ((i*4)&16)|((i>>3)&0x3e0)|((i*8)&8)|((i>>2)&6))

def lookup_fp8():
    b=np.arange(256,dtype=np.uint16);exp=(b>>3)&15;man=b&7
    return (np.where(exp==0,man/512.0,(1+man/8.0)*np.exp2(exp.astype(np.int32)-7))*np.where(b&128,-1,1)).astype(np.float16)

def fp8_encode(x):
    table=lookup_fp8()[:127].astype(np.float32);v=np.minimum(np.abs(x.astype(np.float32)),448)
    hi=np.searchsorted(table,v).clip(0,126);lo=np.maximum(hi-1,0)
    dl=v-table[lo];dh=table[hi]-v
    choice=np.where((dl<dh)|((dl==dh)&((lo&1)==0)),lo,hi).astype(np.uint8)
    return choice|(np.signbit(x).astype(np.uint8)<<7)

def guarded(cuda,data):
    guard=bytes([0xA5])*256
    base=cuda.alloc(len(data)+512,guard+data+guard)
    return base+256,len(data),base

def intact(cuda,g):
    return cuda.read(g[2],256)==bytes([0xA5])*256 and cuda.read(g[0]+g[1],256)==bytes([0xA5])*256

def blob(fmt,*values):
    import struct
    b=struct.pack(fmt,*values);return ct.create_string_buffer(b,len(b))

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--candidate',type=Path,required=True);p.add_argument('--reference',type=Path,required=True)
    p.add_argument('--rows',default='1,127,128,129,960,961,2160,3840');p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();cuda=driver.Cuda();rng=np.random.default_rng(182609)
    funcs={key:cuda.load(path,name,0) for key,path,name in [('pack_old',a.reference,'pack_original_fp8'),('pack_new',a.candidate,'nr_pack_original_fp8'),('ep_old',a.reference,'contract_epilogue_unsplit_f16_pair_publish'),('ep_new',a.candidate,'nr_contract_epilogue_pair')]}
    results=[]
    for rows in map(int,a.rows.split(',')):
        padded=(rows+127)//128*128;cols=1024;count=rows*cols//2;grid=((count+255)//256,1,1)
        raw=rng.integers(0,127,padded*cols,dtype=np.uint8)|(rng.integers(0,2,padded*cols,dtype=np.uint8)<<7)
        source=guarded(cuda,raw.tobytes());packed=[guarded(cuda,bytes(padded*cols//2)) for _ in range(2)];scales=[guarded(cuda,bytes(padded*cols//16)) for _ in range(2)]
        for j,key in enumerate(('pack_old','pack_new')):cuda.launch(funcs[key],blob('<QQQII',source[0],packed[j][0],scales[j][0],rows,cols),grid,(256,1,1),0)
        cuda.call('cuCtxSynchronize')
        packdiff=sum(x!=y for x,y in zip(cuda.read(packed[0][0],packed[0][1]),cuda.read(packed[1][0],packed[1][1])))
        scalediff=sum(x!=y for x,y in zip(cuda.read(scales[0][0],scales[0][1]),cuda.read(scales[1][0],scales[1][1])))
        # CPU inverse proves that the two independent layout directions agree.
        i=np.arange(padded*cols//2,dtype=np.uint32);r,c=coordinates(i)
        inverse=bool(np.array_equal(original_offset(r,c//2,cols)//2,i))
        gemm_cpu=(rng.standard_normal((padded,cols))*3).astype(np.float16);cos_cpu=rng.uniform(-1,1,cols).astype(np.float16)
        gemm=guarded(cuda,gemm_cpu.tobytes());cosine=guarded(cuda,cos_cpu.tobytes());outputs=[guarded(cuda,bytes(rows*cols)) for _ in range(2)]
        tiles=(rows+127)//128;domains=[]
        for j,key in enumerate(('ep_old','ep_new')):
            done=guarded(cuda,bytes([0xBB])*(tiles*8*4));counter=guarded(cuda,bytes([0xBB])*(tiles*8*4));expand=guarded(cuda,bytes([0xBB])*(tiles*32*4));domains.extend((done,counter,expand))
            params=blob('<QQQQIIQQQ',gemm[0],source[0],cosine[0],outputs[j][0],rows,0,done[0],counter[0],expand[0]);cuda.launch(funcs[key],params,grid,(256,1,1),0)
        cuda.call('cuCtxSynchronize')
        reference=np.frombuffer(cuda.read(outputs[0][0],outputs[0][1]),np.uint8);candidate=np.frombuffer(cuda.read(outputs[1][0],outputs[1][1]),np.uint8)
        i=np.arange(count,dtype=np.uint32);r,c=coordinates(i);columns=np.column_stack((c,c+1));row=np.repeat(r[:,None],2,axis=1)
        skip=lookup_fp8()[raw[:rows*cols]].reshape(-1,2)
        with np.errstate(over='ignore'):
            expected=fp8_encode((gemm_cpu[row,columns]+(skip*cos_cpu[columns]).astype(np.float16)).astype(np.float16)).reshape(-1)
        epdiff=int(np.count_nonzero(reference!=candidate));cpudiff=int(np.count_nonzero(reference!=expected))
        domains_ok=all(cuda.read(g[0],g[1])==(np.full(tiles*(32 if n%3==2 else 8),3 if n%3==1 else 0,dtype=np.uint32).tobytes()) for n,g in enumerate(domains))
        guard_ok=all(intact(cuda,g) for g in [source,*packed,*scales,gemm,cosine,*outputs,*domains])
        result={'rows':rows,'pack_different_bytes':packdiff,'scale_different_bytes':scalediff,'epilogue_different_bytes':epdiff,'cpu_epilogue_different_bytes':cpudiff,'layout_inverse':inverse,'domain_values_correct':domains_ok,'guards_intact':guard_ok}
        result['passed']=not(packdiff or scalediff or epdiff or cpudiff) and inverse and domains_ok and guard_ok
        print(json.dumps(result),flush=True);results.append(result);cuda.clear()
    report={'scope':'isolated CUDA reference equivalence, not game or NVAPI ordering proof','results':results,'passed':all(r['passed'] for r in results)}
    a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    if not report['passed']:raise SystemExit(1)

if __name__=='__main__':main()
