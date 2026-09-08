"""Exercise actual MSVC DLL ABI rejection before any kernel launch."""
import argparse
import ctypes as ct
import json
from pathlib import Path
from benchmark import Cuda, Input, Info, ptr, u32, u64

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    cuda=Cuda()
    helper=ct.CDLL(str((args.build/'nr_nvfp4_params.dll').resolve()))
    fn=helper.nr_nvfp4_build_contract
    fn.argtypes=[ct.POINTER(Input),ptr,u64,ct.POINTER(Info),ptr,u32]
    fn.restype=ct.c_int
    helper.nr_nvfp4_build_id_v1.restype=ct.c_char_p
    receipt=json.loads((args.build/'kernel-build.json').read_text(encoding='utf-8-sig'))
    assert helper.nr_nvfp4_build_id_v1().decode()==receipt['abi_fingerprint']
    checks=[]
    for field,value,expected in (
        ('struct_bytes',0,1),('abi_version',99,1),('m',0,2),('m',16385,2),
        ('n',1023,2),('k',4095,2),('sm_count',170,2),('flags',1,2),('variant',99,2),
        ('adapter_luid',0xffffffffffffffff,4),
    ):
        inp=Input(ct.sizeof(Input),1,cuda.luid.value,960,1024,4096,cuda.sms,0,0)
        setattr(inp,field,value)
        info=Info();info.struct_bytes=ct.sizeof(Info);info.abi_version=1
        error=ct.create_string_buffer(12)
        rc=fn(ct.byref(inp),None,0,ct.byref(info),error,len(error))
        assert rc==expected,(field,rc,error.value)
        assert error.raw[-1:]==b'\0'
        checks.append({'field':field,'status':rc,'rejected':True})
    inp=Input(ct.sizeof(Input),1,cuda.luid.value,960,1024,4096,cuda.sms,0,0)
    info=Info();info.struct_bytes=ct.sizeof(Info);info.abi_version=1
    error=ct.create_string_buffer(128)
    assert fn(ct.byref(inp),None,0,ct.byref(info),error,len(error))==0
    backing=ct.create_string_buffer(info.params_bytes+info.params_alignment)
    address=(ct.addressof(backing)+info.params_alignment-1)&~(info.params_alignment-1)
    assert fn(ct.byref(inp),address,info.params_bytes-1,ct.byref(info),error,len(error))==3
    assert fn(ct.byref(inp),address+1,info.params_bytes,ct.byref(info),error,len(error))==3
    assert fn(ct.byref(inp),address,info.params_bytes,ct.byref(info),error,len(error))==2
    checks.extend([{'case':x,'rejected':True} for x in ('undersized_params','unaligned_params','null_gpu_addresses')])
    args.output.parent.mkdir(parents=True,exist_ok=True)
    result={'schema':1,'passed':True,'kernel_launches':0,'checks':checks,'abi_fingerprint':receipt['abi_fingerprint']}
    args.output.write_text(json.dumps(result,indent=2))
    print(json.dumps(result))

if __name__=='__main__':main()
