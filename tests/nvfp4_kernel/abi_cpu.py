"""CPU-only ABI/receipt/packed-layout checks. Does not initialize CUDA."""
import argparse
import ctypes as ct
import hashlib
import json
from pathlib import Path
import numpy as np
from benchmark import Input, Info, ptr, u32, u64

class FusedInfo(ct.Structure):
    _fields_=[('launch',Info),('runtime_bindings_offset',u64),('runtime_bindings_bytes',u64)]
class FusedInput(ct.Structure):
    _fields_=[('contract',Input)]+[(name,u64) for name in ('skip','cosine','original_output','done','counter','expand_done')]
class Bindings(ct.Structure):
    _fields_=[(name,u64) for name in ('skip','cosine','original_output','done','counter','expand_done')]
assert ct.sizeof(FusedInfo)==216 and ct.sizeof(FusedInput)==144 and ct.sizeof(Bindings)==48

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--build',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    receipt=json.loads((args.build/'kernel-build.json').read_text(encoding='utf-8-sig'))
    for artifact in receipt['artifacts']:
        assert hashlib.sha256((args.build/artifact['file']).read_bytes()).hexdigest()==artifact['sha256']
    helper=ct.CDLL(str((args.build/'nr_nvfp4_params.dll').resolve()))
    helper.nr_nvfp4_build_id_v1.restype=ct.c_char_p
    assert helper.nr_nvfp4_build_id_v1().decode()==receipt['abi_fingerprint']
    fn=helper.nr_nvfp4_build_contract
    fn.argtypes=[ct.POINTER(Input),ptr,u64,ct.POINTER(Info),ptr,u32];fn.restype=ct.c_int
    checks=[]
    # Every case is rejected before get_context/cuInit. Device identity is unused.
    for field,value,expected in [('struct_bytes',0,1),('abi_version',9,1),('m',0,2),('m',16385,2),('n',1023,2),('k',4095,2),('sm_count',0,2),('flags',1,2),('variant',99,2)]:
        inp=Input(ct.sizeof(Input),1,0,960,1024,4096,82,0,0)
        setattr(inp,field,value);info=Info();info.struct_bytes=ct.sizeof(Info);info.abi_version=1
        error=ct.create_string_buffer(8)
        status=fn(ct.byref(inp),None,0,ct.byref(info),error,len(error))
        assert status==expected and error.raw[-1:]==b'\0'
        checks.append({'field':field,'status':status})
    describe=helper.nr_nvfp4_describe_fused_layout_v1
    describe.argtypes=[ct.POINTER(FusedInfo)];describe.restype=ct.c_int
    info=FusedInfo();info.launch.struct_bytes=ct.sizeof(Info);info.launch.abi_version=1
    assert describe(ct.byref(info))==0
    offset,size=info.runtime_bindings_offset,info.runtime_bindings_bytes
    assert size==48 and offset+size<=info.launch.params_bytes and offset%8==0
    assert info.launch.params_alignment==128
    assert info.launch.shared_bytes<=101376 # measured device limit; no device query here
    blob=bytearray([0xA5])*info.launch.params_bytes
    bindings=Bindings(*[0x10000000+0x1000*i for i in range(6)])
    blob[offset:offset+size]=bytes(bindings)
    assert blob[:offset]==bytes([0xA5])*offset
    assert blob[offset+size:]==bytes([0xA5])*(len(blob)-offset-size)
    assert bytes(Bindings.from_buffer_copy(blob[offset:offset+size]))==bytes(bindings)
    before=bytes(info);info.launch.struct_bytes=ct.sizeof(FusedInfo)
    rejected=bytes(info);assert describe(ct.byref(info))==1 and bytes(info)==rejected
    layouts=[]
    for m in (1,127,128,129,640,641,960,961,2160,2173,3840):
        row=np.arange(((m+15)//16)*16,dtype=np.uint32)[:,None]
        col=np.arange(1024,dtype=np.uint32)[None,:];pair=col//2
        hi=((row*4)&28)|(pair&3)|(((row>>4)*1024+pair*2)&0xfffffe0)
        lo=(pair&8)|((row>>1)&4)|((pair>>1)&2)
        byte=hi*16+lo+(col&1)
        live=byte[byte<m*1024]
        assert np.array_equal(np.sort(live),np.arange(m*1024,dtype=np.uint32))
        layouts.append({'m':m,'prefix_bytes':int(live.size),'bijection':True})
    report={'schema':1,'passed':True,'cuda_initialized':False,'gpu_launches':0,
            'abi_fingerprint':receipt['abi_fingerprint'],'checks':checks,'layouts':layouts,
            'fused_params_bytes':int(info.launch.params_bytes),'fused_shared_bytes':int(info.launch.shared_bytes),
            'runtime_bindings_offset':int(offset),'runtime_bindings_bytes':int(size),
            'fused_gpu_validation':'pending user-authorized GPU validation'}
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2));print(json.dumps(report))

if __name__=='__main__':main()
