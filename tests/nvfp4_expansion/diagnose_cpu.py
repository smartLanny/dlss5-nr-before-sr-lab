"""Diagnose saved GPU differences using CPU arithmetic only; never loads CUDA.

Uses actual saved upstream pack operands and scales, recomputes selected dot
products, then distinguishes direct FP32->FP4 from an extra FP16 rounding.
The RN reciprocal here is a CPU diagnostic, not a claim to emulate MUFU.RCP.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import numpy as np

FP4=np.array([0,.5,1,1.5,2,3,4,6,0,-.5,-1,-1.5,-2,-3,-4,-6],np.float64)
POSITIVE=FP4[:8]
MIDPOINTS=(POSITIVE[:-1]+POSITIVE[1:])/2
def fp8_table():
    code=np.arange(256,dtype=np.uint16);exp=(code>>3)&15;mantissa=code&7
    value=np.where(exp==0,mantissa/512.,(1+mantissa/8.)*np.exp2(exp.astype(np.int32)-7))*np.where(code&128,-1.,1.)
    value[(exp==15)&(mantissa==7)]=np.nan
    return value
SF=fp8_table()
def scale_indices(row,groups):
    group=np.arange(groups,dtype=np.uint32)
    return ((row//128)*(groups//4)+group//4)*512+(row%32)*16+((row%128)//32)*4+group%4
def decode_operand(packed,scales,row,k=1024):
    raw=np.frombuffer(packed,np.uint8,count=k//2,offset=row*(k//2))
    codes=np.empty(k,np.uint8);codes[0::2]=raw&15;codes[1::2]=raw>>4
    scale=SF[np.frombuffer(scales,np.uint8)[scale_indices(row,k//16)]]
    return FP4[codes]*np.repeat(scale,16)
def gate(value):
    x=np.float16(value);t=np.float16(np.clip(x,-4,4));coefficient=np.array([0xab28],np.uint16).view(np.float16)[0]
    q=np.float16(np.float32(abs(t))*np.float32(coefficient)+np.float32(.447265625))
    q=np.float16(np.float32(t)*np.float32(q)+np.float32(.89453125))
    return np.float16(x*q)
def fp4_round(value):
    x=float(value)
    if math.isnan(x):return 7
    v=min(abs(x),6.);high=min(int(np.searchsorted(POSITIVE,v)),7);low=max(high-1,0)
    dl=v-POSITIVE[low];dh=POSITIVE[high]-v
    code=low if dl<dh or (dl==dh and low%2==0) else high
    return code | (8 if math.copysign(1.,x)<0 else 0)
def analyze(directory,variant):
    metadata=json.loads((directory/'metadata.json').read_text())
    candidate_metadata=json.loads((directory/f'candidate-v{variant}.json').read_text())
    for name,item in metadata['buffers'].items():
        data=(directory/name).read_bytes();assert len(data)==item['bytes'] and hashlib.sha256(data).hexdigest()==item['sha256']
    original=(directory/'reference.expand.fp4.bin').read_bytes();candidate=(directory/f'candidate-v{variant}.expand.fp4.bin').read_bytes()
    assert hashlib.sha256(candidate).hexdigest()==candidate_metadata['candidate_fp4_sha256']
    x=(directory/'reference.pack.fp4.bin').read_bytes();xs=(directory/'reference.pack.sf.bin').read_bytes()
    w=(directory/'weights.fp4.bin').read_bytes();ws=(directory/'weights.sf.bin').read_bytes()
    output_sf=(directory/'reference.expand.sf.bin').read_bytes();candidate_sf=(directory/f'candidate-v{variant}.expand.sf.bin').read_bytes()
    a=np.frombuffer(original,np.uint8);b=np.frombuffer(candidate,np.uint8)
    xcache={};wcache={};records=[]
    for byte_offset in np.flatnonzero(a!=b):
      for nibble in (0,1):
        old=(int(a[byte_offset])>>(4*nibble))&15;new=(int(b[byte_offset])>>(4*nibble))&15
        if old==new:continue
        row=int(byte_offset)//2048;col=(int(byte_offset)%2048)*2+nibble
        if row not in xcache:xcache[row]=decode_operand(x,xs,row)
        if col not in wcache:wcache[col]=decode_operand(w,ws,col)
        product=xcache[row]*wcache[col]
        dot64=float(np.sum(product,dtype=np.float64));dot32=float(np.sum(product.astype(np.float32),dtype=np.float32))
        rounded=gate(dot64);sf_index=int(scale_indices(row,256)[col//16]);sf=SF[output_sf[sf_index]]
        inverse=np.float32(np.finfo(np.float32).max if sf==0 else np.float32(1.)/np.float32(sf))
        normalized=np.float32(np.float32(rounded)*inverse);half_normalized=np.float16(normalized)
        midpoint=float(MIDPOINTS[np.argmin(np.abs(MIDPOINTS-abs(float(normalized))))])
        records.append({'row':row,'column':col,'reference_code':old,'r1_code':new,'gemm_float64':dot64,'gemm_float32_reduction':dot32,
            'gemm_rounding_agrees':bool(np.float16(dot64)==np.float16(dot32)),
            'gemm_half':float(np.float16(dot64)),'gate_half':float(rounded),'scale_code':output_sf[sf_index],'scale':float(sf),
            'cpu_rn_reciprocal':float(inverse),'normalized_float32':float(normalized),'normalized_half':float(half_normalized),
            'nearest_fp4_midpoint':midpoint,'float32_margin_from_midpoint':abs(float(normalized))-midpoint,
            'direct_fp32_prediction':fp4_round(normalized),'extra_half_prediction':fp4_round(half_normalized),
            'reference_matches_direct':fp4_round(normalized)==old,'r1_matches_extra_half':fp4_round(half_normalized)==new})
    def hist(data):
        values,counts=np.unique(np.frombuffer(data,np.uint8),return_counts=True)
        return {f'{int(v):02x}':int(n) for v,n in zip(values,counts)}
    return {'capture':str(directory),'case':metadata['case'],'rows':metadata['rows'],'block':metadata['block'],'variant':variant,
        'different_nibbles':len(records),'reference_scale_histogram':hist(output_sf),'candidate_scale_histogram':hist(candidate_sf),
        'reference_matches_direct_count':sum(r['reference_matches_direct'] for r in records),
        'r1_matches_extra_half_count':sum(r['r1_matches_extra_half'] for r in records),
        'records':records}
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture-dir',type=Path,required=True);p.add_argument('--variant',type=int,default=0);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    cases=[analyze(path,a.variant) for path in sorted(a.capture_dir.glob('m*')) if (path/'metadata.json').exists()]
    report={'scope':'CPU analysis of saved upstream operands; does not execute GPU or emulate approximate reciprocal hardware',
        'cases':cases,'total_different_nibbles':sum(x['different_nibbles'] for x in cases)}
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps([{k:v for k,v in case.items() if k!='records'} for case in cases],indent=2))
if __name__=='__main__':main()
