"""Pure CPU ABI/negative checks. The expansion helper does not import CUDA."""
from __future__ import annotations
import argparse
import ctypes as ct
import hashlib
import json
from pathlib import Path

u32,u64=ct.c_uint32,ct.c_uint64
class Input(ct.Structure):
    _fields_=[('struct_bytes',u32),('abi_version',u32)]+[(name,u64) for name in (
        'original_fp8','weights','weight_scales','packed_output','output_scales',
        'original_capacity','weights_capacity','weight_scales_capacity','packed_output_capacity','output_scales_capacity')]+[
        (name,u32) for name in ('rows','columns','reduction','sm_count','compute_major','compute_minor','variant','max_shared_bytes')]+[
        ('inverse_tensor_scale',ct.c_float),('flags',u32)]
class Params(ct.Structure):
    _fields_=[(name,u64) for name in ('original_fp8','weights','weight_scales','packed_output','output_scales')]+[
        ('rows',u32),('inverse_tensor_scale',ct.c_float),('flags',u32),('reserved',u32)]
class Info(ct.Structure):
    _fields_=[('struct_bytes',u32),('abi_version',u32)]+[(name,u64) for name in (
        'params_bytes','params_alignment','original_bytes','weights_bytes','weight_scales_bytes','packed_output_bytes','output_scales_bytes','linear_input_offset','linear_input_bytes')]+[
        ('grid',u32*3),('block',u32*3),('shared_bytes',u32),('token_tile',u32),('sm_count',u32),('reserved',u32),('kernel_id',ct.c_char*64)]
assert (ct.sizeof(Input),ct.sizeof(Params),ct.sizeof(Info))==(128,56,184)

def load(build:Path):
    receipt=json.loads((build/'expansion-build.json').read_text(encoding='utf-8-sig'))
    for artifact in receipt['artifacts']:
        assert hashlib.sha256((build/artifact['file']).read_bytes()).hexdigest()==artifact['sha256']
    helper=ct.CDLL(str((build/'nr_nvfp4_expansion_params.dll').resolve()))
    helper.nr_nvfp4_expansion_build_id_v1.restype=ct.c_char_p
    assert helper.nr_nvfp4_expansion_build_id_v1().decode()==receipt['abi_fingerprint']
    fn=helper.nr_nvfp4_build_pack_expansion
    fn.argtypes=[ct.POINTER(Input),ct.c_void_p,u64,ct.POINTER(Info),ct.c_void_p,u32];fn.restype=ct.c_int
    return helper,fn,receipt

def valid(rows=640,variant=0):
    p=Input();p.struct_bytes=ct.sizeof(p);p.abi_version=1
    p.rows=rows;p.columns=4096;p.reduction=1024;p.sm_count=82;p.compute_major=12;p.variant=variant;p.max_shared_bytes=101376;p.inverse_tensor_scale=1.
    return p

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--build',type=Path,required=True);parser.add_argument('--output',type=Path)
    args=parser.parse_args();helper,fn,receipt=load(args.build);error=ct.create_string_buffer(192);checks=0
    def invoke(p,blob=None,capacity=0):
        info=Info();info.struct_bytes=ct.sizeof(info);info.abi_version=1
        rc=fn(ct.byref(p),ct.addressof(blob) if blob is not None else None,capacity,ct.byref(info),error,len(error))
        return rc,info
    for rows in (1,7,8,15,16,31,32,63,64,127,128,129,640,641,960,961,2160,3840):
      for variant,tile in enumerate((64,32,128)):
        p=valid(rows,variant);rc,info=invoke(p);assert rc==0,error.value
        assert info.params_bytes==56 and info.params_alignment==8 and info.linear_input_offset==Params.original_fp8.offset and info.linear_input_bytes==8
        assert list(info.grid)==[32,((rows+127)//128*128)//tile,1] and list(info.block)==[256,1,1]
        assert info.token_tile==tile and info.shared_bytes==tile*(528+68) and info.sm_count==82
        # Exact dynamic shared-memory capacity is admitted; one byte less is
        # rejected before any descriptor or GPU-buffer mutation.
        exact=Input.from_buffer_copy(p);exact.max_shared_bytes=info.shared_bytes
        assert invoke(exact)[0]==0
        short=Input.from_buffer_copy(exact);short.max_shared_bytes-=1
        assert invoke(short)[0]==2
        checks+=2
        assert info.original_bytes==((rows+15)//16*16)*1024 and info.packed_output_bytes==((rows+127)//128*128)*2048
        assert info.output_scales_bytes==((rows+127)//128*128)*256
        for index,name in enumerate(('original_fp8','weights','weight_scales','packed_output','output_scales')):setattr(p,name,0x10000000+index*0x10000000)
        for name,field in zip(('original_capacity','weights_capacity','weight_scales_capacity','packed_output_capacity','output_scales_capacity'),('original_bytes','weights_bytes','weight_scales_bytes','packed_output_bytes','output_scales_bytes')):setattr(p,name,getattr(info,field))
        blob=ct.create_string_buffer(bytes([0xa5])*72,72);rc,again=invoke(p,blob,56);assert rc==0,error.value
        actual=Params.from_buffer_copy(blob.raw[:56]);assert actual.original_fp8==p.original_fp8 and actual.rows==rows and actual.inverse_tensor_scale==1
        assert blob.raw[56:]==bytes([0xa5])*16 and bytes(again)==bytes(info)
        for name in ('original_capacity','weights_capacity','weight_scales_capacity','packed_output_capacity','output_scales_capacity'):
            broken=Input.from_buffer_copy(p);setattr(broken,name,getattr(p,name)-1);assert invoke(broken,blob,56)[0]==4;checks+=1
        for output in ('packed_output','output_scales'):
            broken=Input.from_buffer_copy(p);setattr(broken,output,p.original_fp8);assert invoke(broken,blob,56)[0]==4;checks+=1
        broken=Input.from_buffer_copy(p);broken.packed_output=(1<<64)-16;assert invoke(broken,blob,56)[0]==4;checks+=1
        broken=Input.from_buffer_copy(p);broken.weights+=1;assert invoke(broken,blob,56)[0]==4;checks+=1
        checks+=1
    for field,value in [('struct_bytes',127),('abi_version',2),('rows',0),('rows',16385),('columns',1024),('reduction',4096),('sm_count',0),('compute_major',11),('compute_minor',1),('variant',3),('max_shared_bytes',1),('inverse_tensor_scale',.5),('inverse_tensor_scale',0),('inverse_tensor_scale',float('nan')),('flags',1)]:
        p=valid();setattr(p,field,value);assert invoke(p)[0]!=0,(field,value);checks+=1
    summary={'scope':'CPU helper, layouts and source identity; no CUDA driver loaded or GPU kernel executed','checks':checks,'abi_fingerprint':receipt['abi_fingerprint'],'passed':True}
    print(json.dumps(summary,indent=2))
    if args.output:args.output.write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
