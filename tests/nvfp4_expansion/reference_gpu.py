"""Opt-in GPU comparison: one real pack+expansion kernel versus two upstream kernels.

The authoring/build workflow never runs this script. The GPU owner must pass
--allow-gpu after reserving the device. This compares exact FP4 and scale bytes;
it does not qualify game imagery, NVAPI ordering, or whole-NR performance.
"""
from __future__ import annotations
import argparse
import ctypes as ct
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import numpy as np
from abi_cpu import Input,Info,load,valid

ROOT=Path(__file__).resolve().parents[2]
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result
driver=module('nr_expansion_driver',ROOT/'tests/nvfp4_kernel/benchmark.py')
io=module('nr_expansion_io',ROOT/'tests/nvfp4_io/verify.py')

LOCKED={
    'original_io.cubin':(82712,'9def9af8e5ebf6a17cd8dc2f8ca36d6bd5bdc1eb8c927f9d9a890e38fde0890e'),
    'expansion/fused_expand.cubin':(4960880,'fe7f5700ac5e6d5a2fe6571d0f369c9432fabcb29120fdfa33f1ec1c05727ee1'),
    'expansion/params_builder.dll':(55296,'5b5a8902dd856aae752bafc3ea30db9a69beb348a0f50fb79bf1377f690ff4fb'),
    'weights.bin':(37765120,'735f0a04a156dcf625097ae34222827bb9cbab8c4c69d09418a7e577034a0371'),
    'weights.txt':(636,'dc15da940164cd71eb619ae24d85f9c3ddf3e84ca332445fa9ee6fed00b444ab'),
}
class FusedInput(ct.Structure):
    _fields_=[(name,ct.c_int32) for name in ('m','n','k','sm_count')]+[(name,ct.c_uint64) for name in ('a','b','sfa','sfb','c','d','sfd','norm','workspace')]
class FusedInfo(ct.Structure):
    _fields_=[(name,ct.c_uint64) for name in ('params_bytes','workspace_bytes','shared_bytes','sfa_bytes','sfb_bytes','sfd_bytes')]+[
        ('grid',ct.c_uint32*3),('block',ct.c_uint32*3)]
assert ct.sizeof(FusedInput)==88 and ct.sizeof(FusedInfo)==72

def sha(data):return hashlib.sha256(data).hexdigest()
def verify_upstream(root):
    for name,(size,digest) in LOCKED.items():
        data=(root/name).read_bytes()
        if len(data)!=size or sha(data)!=digest:raise RuntimeError(f'Pinned upstream asset identity mismatch: {name}')
    offsets={}
    for line in (root/'weights.txt').read_text().splitlines():
        words=line.split()
        if not words:continue
        if words[0]=='gate_inverse_tensor_scale':
            if float(words[1])!=1.:raise RuntimeError('Unsupported model tensor scale')
        elif words[0]=='block':offsets[int(words[1])]=(int(words[3]),int(words[4]))
    return (root/'weights.bin').read_bytes(),offsets

def fixture(rows,case,block,model,offsets):
    rng=np.random.default_rng(20092026+rows+1000*block)
    padded=(rows+127)//128*128
    # Random finite FP8 physical storage exercises the real inverse permutation.
    original=io.fp8_encode(rng.uniform(-3,3,padded*1024).astype(np.float32)).tobytes()
    if case=='model':
        ep,es=offsets[block];return original,model[ep:ep+4096*512],model[es:es+4096*64]
    weights=np.zeros((4096,1024),np.uint8)
    if case=='signed':weights[:,:32]=rng.integers(0,16,(4096,32),dtype=np.uint8)
    elif case=='dense':weights[:]=rng.integers(0,16,weights.shape,dtype=np.uint8)
    elif case!='zero':raise ValueError(case)
    scales,_=driver.scales(4096,1024,rng)
    return original,driver.packed(weights),scales

def guarded(cuda,data):return io.guarded(cuda,data)
def intact(cuda,*buffers):return all(io.intact(cuda,b) for b in buffers)
def diff_bytes(a,b):return int(np.count_nonzero(np.frombuffer(a,np.uint8)!=np.frombuffer(b,np.uint8)))
def scale_histogram(raw):
    values,counts=np.unique(np.frombuffer(raw,np.uint8),return_counts=True)
    return {f'{int(value):02x}':int(count) for value,count in zip(values,counts)}
def diff_samples(actual,reference,actual_scales,reference_scales,limit):
    a=np.frombuffer(actual,np.uint8);b=np.frombuffer(reference,np.uint8)
    samples=[];lookup=io.lookup_fp8()
    for offset in np.flatnonzero(a!=b)[:limit]:
        row=int(offset)//2048;column=(int(offset)%2048)*2
        for nibble in (0,1):
            old=(int(b[offset])>>(nibble*4))&15;new=(int(a[offset])>>(nibble*4))&15
            if old==new:continue
            col=column+nibble;group=col//16
            sf_offset=((row//128)*64+group//4)*512+(row%32)*16+((row%128)//32)*4+group%4
            old_sf=reference_scales[sf_offset];new_sf=actual_scales[sf_offset]
            samples.append({'byte_offset':int(offset),'row':row,'column':col,'reference_code':old,'candidate_code':new,
                'reference_fp4':float(driver.FP4[old]),'candidate_fp4':float(driver.FP4[new]),
                'scale_offset':sf_offset,'reference_scale_code':old_sf,'candidate_scale_code':new_sf,
                'reference_scaled_value':float(driver.FP4[old]*lookup[old_sf]),'candidate_scaled_value':float(driver.FP4[new]*lookup[new_sf])})
    return samples
def aligned_blob(size,alignment):
    owner=ct.create_string_buffer(size+alignment)
    address=(ct.addressof(owner)+alignment-1)&~(alignment-1)
    return owner,(ct.c_char*size).from_address(address)

def timed_sequence(cuda,steps,count):
    for _ in range(3):
        for function,params,grid,block,shared in steps:cuda.launch(function,params,grid,block,shared)
    start,end=ct.c_void_p(),ct.c_void_p()
    cuda.call('cuEventCreate',ct.byref(start),0);cuda.call('cuEventCreate',ct.byref(end),0)
    cuda.call('cuEventRecord',start,None)
    for _ in range(count):
        for function,params,grid,block,shared in steps:cuda.launch(function,params,grid,block,shared)
    cuda.call('cuEventRecord',end,None);cuda.call('cuEventSynchronize',end)
    ms=ct.c_float();cuda.call('cuEventElapsedTime',ct.byref(ms),start,end)
    cuda.call('cuEventDestroy_v2',start);cuda.call('cuEventDestroy_v2',end)
    return ms.value/count

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--allow-gpu',action='store_true')
    parser.add_argument('--build',type=Path,required=True);parser.add_argument('--upstream',type=Path,required=True)
    parser.add_argument('--rows',default='640,960,641,1,127,128,129,961')
    parser.add_argument('--variants',default='0,1,2');parser.add_argument('--cases',default='signed,model')
    parser.add_argument('--blocks',default='31');parser.add_argument('--iterations',type=int,default=0)
    parser.add_argument('--capture-dir',type=Path);parser.add_argument('--max-diffs',type=int,default=64)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if not args.allow_gpu:parser.error('This script requires the GPU owner to explicitly pass --allow-gpu.')
    if args.iterations<0:parser.error('--iterations cannot be negative')
    if args.max_diffs<0:parser.error('--max-diffs cannot be negative')
    model,offsets=verify_upstream(args.upstream)
    helper,build,receipt=load(args.build)
    # No CUDA instance exists until all explicit execution and file-identity gates passed.
    cuda=driver.Cuda()
    old_helper=ct.CDLL(str((args.upstream/'expansion/params_builder.dll').resolve()))
    old_build=old_helper.fused_params
    old_build.argtypes=[ct.POINTER(FusedInput),ct.c_void_p,ct.c_uint64,ct.POINTER(FusedInfo)];old_build.restype=ct.c_int
    old_pack=cuda.load(args.upstream/'original_io.cubin','pack_original_fp8',0)
    old_expand=None;functions={};results=[]
    for rows in map(int,args.rows.split(',')):
      for case in args.cases.split(','):
       for block in map(int,args.blocks.split(',')):
        if block not in offsets:raise ValueError(f'Unknown locked FFN block {block}')
        source,weight_bytes,weight_sf=fixture(rows,case,block,model,offsets)
        padded=(rows+127)//128*128
        original=guarded(cuda,source);weights=guarded(cuda,weight_bytes);ws=guarded(cuda,weight_sf)
        xp=guarded(cuda,bytes(padded*512));xs=guarded(cuda,bytes(padded*64))
        old_output=guarded(cuda,bytes(padded*2048));old_scales=guarded(cuda,bytes(padded*256));norm=guarded(cuda,struct.pack('<f',1.)+bytes(252))
        pack_params=io.blob('<QQQII',original[0],xp[0],xs[0],rows,1024)
        fused_input=FusedInput(rows,4096,1024,cuda.sms,xp[0],weights[0],xs[0],ws[0],0,old_output[0],old_scales[0],norm[0],0)
        fused_info=FusedInfo()
        if old_build(ct.byref(fused_input),None,0,ct.byref(fused_info)) or fused_info.params_bytes!=2176 or fused_info.workspace_bytes:
            raise RuntimeError('Pinned upstream expansion Params/workspace ABI changed')
        if fused_info.sfa_bytes>xs[1] or fused_info.sfb_bytes>ws[1] or fused_info.sfd_bytes>old_scales[1]:raise RuntimeError('Reference scale capacity exceeded')
        old_owner,old_params=aligned_blob(fused_info.params_bytes,128)
        if old_build(ct.byref(fused_input),ct.addressof(old_params),len(old_params),ct.byref(fused_info)):raise RuntimeError('Reference builder encode failed')
        if old_expand is None:old_expand=cuda.load(args.upstream/'expansion/fused_expand.cubin','fused_expand',fused_info.shared_bytes)
        cuda.call('cuFuncSetAttribute',old_expand,8,fused_info.shared_bytes)
        reference_steps=[(old_pack,pack_params,((rows*512+255)//256,1,1),(256,1,1),0),
                         (old_expand,old_params,tuple(fused_info.grid),tuple(fused_info.block),fused_info.shared_bytes)]
        for function,params,grid,thread_block,shared in reference_steps:cuda.launch(function,params,grid,thread_block,shared)
        cuda.call('cuCtxSynchronize')
        reference_output=cuda.read(old_output[0],old_output[1]);reference_scales=cuda.read(old_scales[0],old_scales[1])
        capture=None
        if args.capture_dir:
            capture=args.capture_dir/f'm{rows}-{case}-b{block}'
            capture.mkdir(parents=True,exist_ok=False)
            saved={'input.original.fp8.bin':source,'weights.fp4.bin':weight_bytes,'weights.sf.bin':weight_sf,
                   'reference.pack.fp4.bin':cuda.read(xp[0],xp[1]),'reference.pack.sf.bin':cuda.read(xs[0],xs[1]),
                   'reference.expand.fp4.bin':reference_output,'reference.expand.sf.bin':reference_scales,
                   'reference.expand.params.bin':bytes(old_params)}
            for name,data in saved.items():(capture/name).write_bytes(data)
            (capture/'metadata.json').write_text(json.dumps({'rows':rows,'case':case,'block':block,'abi_fingerprint':receipt['abi_fingerprint'],
                'reference_scale_histogram':scale_histogram(reference_scales),'buffers':{name:{'bytes':len(data),'sha256':sha(data)} for name,data in saved.items()}},indent=2)+'\n',encoding='utf-8')
        reference_ms=timed_sequence(cuda,reference_steps,args.iterations) if args.iterations else None
        for variant in map(int,args.variants.split(',')):
            output=guarded(cuda,bytes(padded*2048));scales=guarded(cuda,bytes(padded*256))
            p=valid(rows,variant);p.sm_count=cuda.sms;p.compute_major,p.compute_minor=cuda.cc;p.max_shared_bytes=cuda.shared
            for name,buffer in (('original_fp8',original),('weights',weights),('weight_scales',ws),('packed_output',output),('output_scales',scales)):setattr(p,name,buffer[0])
            for name,buffer in (('original_capacity',original),('weights_capacity',weights),('weight_scales_capacity',ws),('packed_output_capacity',output),('output_scales_capacity',scales)):setattr(p,name,buffer[1])
            info=Info();info.struct_bytes=ct.sizeof(info);info.abi_version=1;error=ct.create_string_buffer(256)
            if build(ct.byref(p),None,0,ct.byref(info),error,len(error)):raise RuntimeError(error.value.decode())
            owner,params=aligned_blob(info.params_bytes,info.params_alignment)
            if build(ct.byref(p),ct.addressof(params),len(params),ct.byref(info),error,len(error)):raise RuntimeError(error.value.decode())
            if variant not in functions:functions[variant]=cuda.load(args.build/'nr_nvfp4_pack_expansion_sm120.cubin',info.kernel_id.decode(),info.shared_bytes)
            step=(functions[variant],params,tuple(info.grid),tuple(info.block),info.shared_bytes)
            cuda.launch(*step);cuda.call('cuCtxSynchronize')
            actual=cuda.read(output[0],output[1]);actual_scales=cuda.read(scales[0],scales[1])
            different=diff_bytes(actual,reference_output);sf_different=diff_bytes(actual_scales,reference_scales)
            guard_ok=intact(cuda,original,weights,ws,xp,xs,old_output,old_scales,norm,output,scales)
            candidate_ms=timed_sequence(cuda,[step],args.iterations) if args.iterations else None
            row={'rows':rows,'case':case,'block':block,'variant':variant,'token_tile':info.token_tile,
                 'sm_count':cuda.sms,'compute_capability':list(cuda.cc),'input_sha256':sha(source),'weights_sha256':sha(weight_bytes),'weight_scales_sha256':sha(weight_sf),
                 'params_bytes':info.params_bytes,'shared_bytes':info.shared_bytes,'candidate_launches':1,'reference_launches':2,
                 'fp4_different_bytes':different,'valid_fp4_different_bytes':diff_bytes(actual[:rows*2048],reference_output[:rows*2048]),
                 'scale_different_bytes':sf_different,'guards_intact':guard_ok,
                 'reference_fp4_sha256':sha(reference_output),'candidate_fp4_sha256':sha(actual),
                 'reference_scale_sha256':sha(reference_scales),'candidate_scale_sha256':sha(actual_scales),
                 'reference_scale_histogram':scale_histogram(reference_scales),'candidate_scale_histogram':scale_histogram(actual_scales),
                 'diff_samples':diff_samples(actual,reference_output,actual_scales,reference_scales,args.max_diffs),
                 'reference_pack_expansion_ms':reference_ms,'candidate_pack_expansion_ms':candidate_ms,
                 'passed':different==0 and sf_different==0 and guard_ok}
            if capture:
                (capture/f'candidate-v{variant}.expand.fp4.bin').write_bytes(actual)
                (capture/f'candidate-v{variant}.expand.sf.bin').write_bytes(actual_scales)
                (capture/f'candidate-v{variant}.params.bin').write_bytes(bytes(params))
                (capture/f'candidate-v{variant}.json').write_text(json.dumps(row,indent=2)+'\n',encoding='utf-8')
            print(json.dumps(row),flush=True);results.append(row)
        cuda.clear()
    report={'scope':'isolated CUDA pack+expansion reference equality; no NVAPI, game, temporal image or whole-NR claim',
            'abi_fingerprint':receipt['abi_fingerprint'],'reference':'ec96f42f8d32a4fc82f7c4558c0ed12a9f90d853',
            'results':results,'passed':all(row['passed'] for row in results)}
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    if not report['passed']:raise SystemExit(1)

if __name__=='__main__':main()
