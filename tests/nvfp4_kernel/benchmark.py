"""Isolated SM120 driver-API contraction oracle and upstream comparison.

GPU buffers belong to this process; no game, renderer, registry, or model file
is modified. JSON results distinguish tensor validation from image acceptance.
"""
from __future__ import annotations
import argparse
import ctypes as ct
import hashlib
import json
from pathlib import Path
import shlex
import struct
import time
import numpy as np

u32, u64, ptr = ct.c_uint32, ct.c_uint64, ct.c_void_p

class Input(ct.Structure):
    _fields_ = [("struct_bytes",u32),("abi_version",u32),("adapter_luid",u64)] + [
        (name,u32) for name in ("m","n","k","sm_count","variant","flags")] + [
        (name,u64) for name in ("weights","weight_scales","activations","activation_scales","output","workspace","workspace_capacity")]

class Info(ct.Structure):
    _fields_ = [("struct_bytes",u32),("abi_version",u32)] + [(name,u64) for name in (
        "params_bytes","params_alignment","workspace_bytes","workspace_zero_bytes","output_bytes","weight_scale_bytes","activation_scale_bytes")] + [
        ("grid",u32*3),("block",u32*3),("shared_bytes",u32),("cuda_device_ordinal",u32),("sm_count",u32),("reserved",u32),("kernel_id",ct.c_char*96)]

assert ct.sizeof(Input)==96 and ct.sizeof(Info)==200

class Cuda:
    def __init__(self):
        self.lib=ct.WinDLL("nvcuda.dll")
        self.allocations=[]
        self.modules=[]
        self.functions={}
        self.bind("cuInit",[u32]);self.call("cuInit",0)
        self.bind("cuDeviceGet",[ct.POINTER(ct.c_int),ct.c_int])
        self.bind("cuDeviceGetLuid",[ptr,ct.POINTER(u32),ct.c_int])
        self.bind("cuDeviceGetAttribute",[ct.POINTER(ct.c_int),ct.c_int,ct.c_int])
        self.bind("cuDevicePrimaryCtxRetain",[ct.POINTER(ptr),ct.c_int])
        self.bind("cuCtxPushCurrent_v2",[ptr])
        self.bind("cuCtxSynchronize",[])
        self.bind("cuMemAlloc_v2",[ct.POINTER(u64),ct.c_size_t])
        self.bind("cuMemFree_v2",[u64])
        self.bind("cuMemcpyHtoD_v2",[u64,ptr,ct.c_size_t])
        self.bind("cuMemcpyDtoH_v2",[ptr,u64,ct.c_size_t])
        self.bind("cuMemsetD8_v2",[u64,ct.c_ubyte,ct.c_size_t])
        self.bind("cuModuleLoadData",[ct.POINTER(ptr),ptr])
        self.bind("cuModuleGetFunction",[ct.POINTER(ptr),ptr,ct.c_char_p])
        self.bind("cuFuncSetAttribute",[ptr,ct.c_int,ct.c_int])
        self.bind("cuLaunchKernel",[ptr,u32,u32,u32,u32,u32,u32,u32,ptr,ptr,ptr])
        self.bind("cuEventCreate",[ct.POINTER(ptr),u32])
        self.bind("cuEventRecord",[ptr,ptr])
        self.bind("cuEventSynchronize",[ptr])
        self.bind("cuEventElapsedTime",[ct.POINTER(ct.c_float),ptr,ptr])
        self.bind("cuEventDestroy_v2",[ptr])
        dev=ct.c_int();self.call("cuDeviceGet",ct.byref(dev),0);self.device=dev.value
        self.luid=u64();node=u32();self.call("cuDeviceGetLuid",ct.byref(self.luid),ct.byref(node),self.device)
        self.sms=self.attr(16);self.cc=(self.attr(75),self.attr(76));self.shared=self.attr(97)
        if self.cc!=(12,0):raise RuntimeError(f"SM120 required, got {self.cc}")
        self.context=ptr();self.call("cuDevicePrimaryCtxRetain",ct.byref(self.context),self.device)
        self.call("cuCtxPushCurrent_v2",self.context)

    def bind(self,name,types):
        f=getattr(self.lib,name);f.argtypes=types;f.restype=ct.c_int;self.functions[name]=f
    def call(self,name,*args):
        rc=self.functions[name](*args)
        if rc:raise RuntimeError(f"{name}: CUDA status {rc}")
    def attr(self,attribute):
        v=ct.c_int();self.call("cuDeviceGetAttribute",ct.byref(v),attribute,self.device);return v.value
    def alloc(self,size,data=None):
        v=u64();self.call("cuMemAlloc_v2",ct.byref(v),size);self.allocations.append(v.value)
        if data is None:self.call("cuMemsetD8_v2",v,0,size)
        else:
            raw=ct.create_string_buffer(bytes(data));self.call("cuMemcpyHtoD_v2",v,raw,len(data))
        return v.value
    def read(self,address,size):
        b=ct.create_string_buffer(size);self.call("cuMemcpyDtoH_v2",b,address,size);return b.raw
    def load(self,path,name,shared):
        b=ct.create_string_buffer(path.read_bytes());m=ptr();self.call("cuModuleLoadData",ct.byref(m),b)
        self.modules.append(m);f=ptr();self.call("cuModuleGetFunction",ct.byref(f),m,name.encode())
        self.call("cuFuncSetAttribute",f,8,shared);return f
    def launch(self,function,params,grid,block,shared):
        size=ct.c_size_t(ct.sizeof(params));extra=(ptr*5)(1,ct.addressof(params),2,ct.addressof(size),0)
        self.call("cuLaunchKernel",function,*grid,*block,shared,None,None,extra)
    def timed(self,function,params,grid,block,shared,count):
        for _ in range(3):self.launch(function,params,grid,block,shared)
        start,end=ptr(),ptr();self.call("cuEventCreate",ct.byref(start),0);self.call("cuEventCreate",ct.byref(end),0)
        self.call("cuEventRecord",start,None)
        for _ in range(count):self.launch(function,params,grid,block,shared)
        self.call("cuEventRecord",end,None);self.call("cuEventSynchronize",end)
        ms=ct.c_float();self.call("cuEventElapsedTime",ct.byref(ms),start,end)
        self.call("cuEventDestroy_v2",start);self.call("cuEventDestroy_v2",end)
        return ms.value/count
    def clear(self):
        self.call("cuCtxSynchronize")
        for address in self.allocations:self.call("cuMemFree_v2",address)
        self.allocations.clear()

def packed(x):
    return (x[:,::2] | (x[:,1::2]<<4)).astype(np.uint8).tobytes()

FP4=np.asarray([0,.5,1,1.5,2,3,4,6,0,-.5,-1,-1.5,-2,-3,-4,-6],np.float32)

def scales(rows,k,rng):
    # Independent implementation of the documented 128x4 scale swizzle.
    groups=k//16;logical=rng.integers(0,3,size=(rows,groups),dtype=np.uint8)
    values=np.asarray([.5,1.,2.],np.float32)[logical]
    encoded=np.asarray([0x30,0x38,0x40],np.uint8)[logical]
    out=np.zeros(((rows+127)//128)*128*groups,np.uint8)
    r=np.arange(rows)[:,None];g=np.arange(groups)[None,:]
    indices=((r//128)*(groups//4)+(g//4))*512+(r%32)*16+((r%128)//32)*4+(g%4)
    out[indices]=encoded
    return out.tobytes(),values

def fixture(m,kind):
    n,k=1024,4096;rng=np.random.default_rng(2409+m)
    w=np.zeros((n,k),np.uint8);x=np.zeros((m,k),np.uint8)
    if kind=="signed":w[:,:32]=rng.integers(0,16,(n,32),dtype=np.uint8);x[:,:32]=rng.integers(0,16,(m,32),dtype=np.uint8)
    elif kind=="dense":w[:]=rng.integers(0,16,(n,k),dtype=np.uint8);x[:]=rng.integers(0,16,(m,k),dtype=np.uint8)
    ws,wsv=scales(n,k,rng);xs,xsv=scales(m,k,rng)
    active=k if kind=="dense" else 32
    wf=FP4[w[:,:active]]*np.repeat(wsv[:,:active//16],16,axis=1)
    xf=FP4[x[:,:active]]*np.repeat(xsv[:,:active//16],16,axis=1)
    expected=(xf@wf.T).astype(np.float16)
    return packed(w),ws,packed(x),xs,expected

def upstream_params(root,m,bindings):
    folder=root/({960:"contract",2160:"contract_m2160",3840:"contract_m3840"}[m])
    manifest={};patches=[]
    for line in (folder/"manifest.txt").read_text().splitlines():
        parts=shlex.split(line)
        if not parts:continue
        if parts[0]=="patch":patches.append((int(parts[1]),parts[2],int(parts[3]) if len(parts)>3 else 0))
        else:manifest[parts[0]]=parts[1:]
    blob=bytearray((folder/manifest["params"][0]).read_bytes())
    for offset,name,add in patches:struct.pack_into("<Q",blob,offset,bindings[name]+add)
    return manifest,ct.create_string_buffer(bytes(blob),len(blob)),folder/manifest["cubin"][0]

def main():
    p=argparse.ArgumentParser();p.add_argument("--build",type=Path,required=True);p.add_argument("--upstream",type=Path)
    p.add_argument("--m",default="640,960,2160,3840,641,961,2173");p.add_argument("--cases",default="zero,signed")
    p.add_argument("--variants",default="0,1,2,3");p.add_argument("--iterations",type=int,default=20);p.add_argument("--output",type=Path,required=True)
    args=p.parse_args();cuda=Cuda();helper=ct.CDLL(str((args.build/"nr_nvfp4_params.dll").resolve()))
    build=helper.nr_nvfp4_build_contract;build.argtypes=[ct.POINTER(Input),ptr,u64,ct.POINTER(Info),ptr,u32];build.restype=ct.c_int
    helper.nr_nvfp4_build_id_v1.restype=ct.c_char_p
    abi_id=helper.nr_nvfp4_build_id_v1().decode()
    results=[]
    for m in map(int,args.m.split(",")):
      for case in args.cases.split(","):
        for variant in map(int,args.variants.split(",")):
          w,ws,x,xs,expected=fixture(m,case)
          wa=cuda.alloc(len(w),w);wsa=cuda.alloc(len(ws),ws);xa=cuda.alloc(len(x),x);xsa=cuda.alloc(len(xs),xs)
          output_bytes=m*1024*2;out=cuda.alloc(output_bytes+4096,bytes([0xA5])*(output_bytes+4096))
          inp=Input(ct.sizeof(Input),1,cuda.luid.value,m,1024,4096,cuda.sms,variant,0,wa,wsa,xa,xsa,out,0,0)
          info=Info();info.struct_bytes=ct.sizeof(Info);info.abi_version=1;err=ct.create_string_buffer(1024)
          rc=build(ct.byref(inp),None,0,ct.byref(info),err,len(err))
          if rc:raise RuntimeError(f"builder query {rc}: {err.value!r}")
          raw=ct.create_string_buffer(info.params_bytes+info.params_alignment)
          address=(ct.addressof(raw)+info.params_alignment-1)&~(info.params_alignment-1)
          blob=(ct.c_char*info.params_bytes).from_address(address)
          rc=build(ct.byref(inp),address,info.params_bytes,ct.byref(info),err,len(err))
          if rc:raise RuntimeError(f"builder encode {rc}: {err.value!r}")
          function=cuda.load(args.build/"nr_nvfp4_contract_sm120.cubin",info.kernel_id.decode(),info.shared_bytes)
          ms=cuda.timed(function,blob,tuple(info.grid),tuple(info.block),info.shared_bytes,args.iterations)
          actual=np.frombuffer(cuda.read(out,output_bytes),np.float16).reshape(m,1024)
          diff=np.abs(actual.astype(np.float32)-expected.astype(np.float32))
          canary=cuda.read(out+output_bytes,4096)==bytes([0xA5])*4096
          passed=bool(np.isfinite(actual).all() and np.array_equal(actual,expected) and canary)
          row={"m":m,"case":case,"variant":variant,"params_bytes":info.params_bytes,"params_alignment":info.params_alignment,
               "grid":list(info.grid),"block":list(info.block),"shared_bytes":info.shared_bytes,"candidate_ms":ms,
               "max_abs_error":float(diff.max()),"different_elements":int(np.count_nonzero(diff)),"output_guard_intact":canary,"passed":passed}
          if args.upstream and m in (960,2160,3840):
              reference=cuda.alloc(output_bytes+4096);work=cuda.alloc(256)
              manifest,oldblob,cubin=upstream_params(args.upstream,m,{"A_packed":wa,"SF_A":wsa,"B_packed":xa,"SF_B":xsa,"D":reference,"workspace":work})
              f=cuda.load(cubin,manifest["kernel"][0],int(manifest["shared"][0]))
              row["upstream_ms"]=cuda.timed(f,oldblob,tuple(map(int,manifest["grid"])),tuple(map(int,manifest["block"])),int(manifest["shared"][0]),args.iterations)
              old=np.frombuffer(cuda.read(reference,output_bytes),np.float16).reshape(m,1024)
              row["upstream_max_abs_error"]=float(np.abs(old.astype(np.float32)-expected.astype(np.float32)).max())
              row["upstream_candidate_equal"]=bool(np.array_equal(old,actual))
              row["passed"] &= row["upstream_candidate_equal"]
          results.append(row);print(json.dumps(row),flush=True);cuda.clear()
    report={"schema":1,"route":"isolated CUDA driver; no renderer", "cc":cuda.cc,"sm_count":cuda.sms,"shared_optin":cuda.shared,
            "passed":all(r["passed"] for r in results),"abi_fingerprint":abi_id,"results":results,
            "image_or_publish_protocol_acceptance":False,
            "timing_valid_for_variant_selection":False,
            "timing_note":"Diagnostic only; exclusive GPU ownership is not established by this test",
            "artifacts":{n:hashlib.sha256((args.build/n).read_bytes()).hexdigest() for n in ("nr_nvfp4_contract_sm120.cubin","nr_nvfp4_params.dll")}}
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2))
    if not report["passed"]:raise SystemExit(1)

if __name__=="__main__":main()
