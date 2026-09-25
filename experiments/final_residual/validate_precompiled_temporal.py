"""Portable tests of artifact validation and the actual precompiled consumer.

Synthetic DXBC containers validate packaging only, never HLSL or GPU behavior.
Windows four-way pixel validation is validate_memory_opt_gpu.py.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess


def load(path):
    spec=importlib.util.spec_from_file_location('temporal_artifacts',path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

def fixture(value):
    return b'DXBC'+bytes(16)+struct.pack('<III',1,48,1)+struct.pack('<I',36)+b'SHEX'+struct.pack('<II',4,value)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--compiler',required=True);ap.add_argument('--output',required=True,type=Path)
    a=ap.parse_args();root=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    builder=root/'scripts/prepare-temporal-artifacts.py';module=load(builder)
    cpp=root/'experiments/final_residual/recorded_temporal.cpp';text=cpp.read_text(encoding='utf-8')
    helper=(cpp.parent/'compile_temporal_artifacts.cpp').read_text(encoding='utf-8')
    checks=0
    def require(value,reason):
        nonlocal checks
        checks+=1
        if not value:raise AssertionError(reason)
    def rejected(data):
        try:module.validate_dxbc(data)
        except ValueError:return True
        return False
    blobs=[fixture(i+1) for i in range(3)]
    for b in blobs:module.validate_dxbc(b)
    for bad in (b'',b'DXBC',blobs[0][:-1],blobs[0]+b'X',blobs[0].replace(b'SHEX',b'RDEF')):
        require(rejected(bad),'malformed DXBC accepted')
    for offset,value in ((24,1),(28,0),(28,1000),(32,0),(32,1000),(40,1000)):
        b=bytearray(blobs[0]);struct.pack_into('<I',b,offset,value)
        require(rejected(b),'invalid DXBC range accepted')
    for wrong in ([],blobs[:2],blobs+[blobs[0]]):
        try:module.emit_header(wrong)
        except ValueError:checks+=1
        else:raise AssertionError('wrong variant count')
    header=out/'nr_temporal_precompiled.hpp';header.write_text(module.emit_header(blobs),encoding='utf-8')
    begin=text.index('struct TemporalBytecode {');end=text.index('struct TemporalPipelines {',begin)
    actual=text[begin:end]
    harness='''#include <memory>\n#include <stdexcept>\n#include <cstddef>\n#include <cstdio>\n#include "nr_temporal_precompiled.hpp"\nstruct ShaderBlob {};\nstruct D3D12_SHADER_BYTECODE {const void* pShaderBytecode; std::size_t BytecodeLength;};\nvoid need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}\n'''+actual+'''
int main(){
 for(unsigned i=0;i<3;++i){auto a=temporal_bytecode(i),b=temporal_bytecode(i);
  need(!a.owner&&a.code.pShaderBytecode==b.code.pShaderBytecode,"static bytecode should not allocate or compile");
  need(a.code.BytecodeLength==48,"wrong artifact length");
  need(static_cast<const unsigned char*>(a.code.pShaderBytecode)[44]==i+1,"wrong shader variant");}
 bool failed=false;try{(void)temporal_bytecode(3);}catch(const std::runtime_error&){failed=true;}
 need(failed,"invalid shader variant accepted");
 std::puts("PASS actual precompiled consumer: stable data, no runtime compilation/blob ownership, exact variant, bounded lookup");
}
'''
    source=out/'consumer.cpp';source.write_text(harness,encoding='utf-8');exe=out/'consumer'
    cmd=[a.compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-DNR_TEMPORAL_PRECOMPILED=1',str(source),'-o',str(exe)]
    report={'passed':False,'gpu_verified':False,'hlsl_compile_verified':False,'synthetic_packaging_data':True,
            'source_sha256':hashlib.sha256(cpp.read_bytes()).hexdigest(),'command':cmd}
    def save():(out/'validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    r=subprocess.run(cmd,capture_output=True,text=True);report['compile_exit']=r.returncode
    (out/'compile.log').write_text(r.stdout+r.stderr,encoding='utf-8');save()
    if r.returncode:raise RuntimeError(r.stderr)
    r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20);report['run_exit']=r.returncode
    (out/'run.log').write_text(r.stdout+r.stderr,encoding='utf-8');save()
    if r.returncode:raise RuntimeError(r.stdout+r.stderr)
    # Exact compiler source/entry/flags and defines used by reference and baker.
    for anchor in ('const D3D_SHADER_MACRO macros[]=', 'const auto hr=D3DCompile('):
        def statement(s):
            start=s.index(anchor);return ''.join(s[start:s.index(';',start)+1].split())
        require(statement(text)==statement(helper),'runtime/baked compiler contract differs')
    stage=(root/'scripts/patch-temporal-runtime.py').read_text(encoding='utf-8')
    build=(root/'scripts/build-temporal-module.py').read_text(encoding='utf-8')
    require("'compile_temporal_artifacts.cpp'" in stage,'helper absent from generated source inputs')
    require("'scripts/prepare-temporal-artifacts.py'" in stage,'builder absent from generated source inputs')
    require("-DNR_TEMPORAL_PRECOMPILED=1" in build,'production does not require baked artifacts')
    require(build.index('Required temporal bytecode build failed')<build.index("for name in ('amd_of_producer'"),'objects built before artifacts accepted')
    # Break each contract independently; the consumer must not pass via dynamic compilation.
    for mode in ('omit-header','missing-entry'):
        work=out/mode;work.mkdir();(work/'consumer.cpp').write_text(harness,encoding='utf-8')
        if mode=='missing-entry':
            h=header.read_text().replace('case 2:return {shader_2,sizeof(shader_2)};','case 2:return {nullptr,0};')
            (work/header.name).write_text(h,encoding='utf-8')
        c=cmd[:-3]+[str(work/'consumer.cpp'),'-o',str(work/'consumer')]
        run=subprocess.run(c,capture_output=True,text=True)
        if mode=='omit-header':require(run.returncode!=0,'missing production header silently accepted')
        else:
            require(run.returncode==0,'missing entry negative control must compile')
            failed=subprocess.run([str(work/'consumer')],capture_output=True,text=True)
            require(failed.returncode!=0 and 'required temporal build artifact missing' in failed.stderr,'missing entry silently compiled at runtime')
    # Exercise the actual whole-Core link helper with synthetic provenance files.
    # These are not executable objects and are never linked into a GPU binary.
    spec=importlib.util.spec_from_file_location('temporal_test_objects',root/'scripts/temporal_test_objects.py')
    linker=importlib.util.module_from_spec(spec);spec.loader.exec_module(linker)
    core=out/'provenance/core';src=core/'src/temporal';src.mkdir(parents=True)
    receipts=core/'build/obj/temporal';art=receipts/'bytecode';art.mkdir(parents=True)
    obj=receipts/'synthetic.o';obj.write_bytes(b'not-an-object: provenance-test-only')
    payload=src/'sample.cpp';payload.write_bytes(b'// test-only source')
    artifact=art/'temporal-artifacts.json';artifact.write_bytes(b'{"synthetic":true}')
    baked=art/'nr_temporal_precompiled.hpp';baked.write_bytes(b'// test-only header')
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    receipt={'passed':True,'source_sha256':{'sample.cpp':digest(payload)},
             'object_sha256':{str(obj):digest(obj)},'temporal_artifacts_sha256':digest(artifact),
             'temporal_header_sha256':digest(baked)}
    receipt_path=receipts/'module.json';receipt_path.write_text(json.dumps(receipt))
    require(linker.objects_for(core/'src/nr_before_sr.cpp')==[obj],'valid artifact provenance rejected')
    for target in (baked,artifact,obj):
        original=target.read_bytes();target.write_bytes(original+b'changed')
        try:linker.objects_for(core/'src/nr_before_sr.cpp')
        except ValueError:checks+=1
        else:raise AssertionError('changed artifact/object accepted')
        finally:target.write_bytes(original)
    partial=dict(receipt);del partial['temporal_header_sha256']
    receipt_path.write_text(json.dumps(partial))
    try:linker.objects_for(core/'src/nr_before_sr.cpp')
    except ValueError:checks+=1
    else:raise AssertionError('partial baked provenance accepted')
    legacy={k:v for k,v in receipt.items() if not k.startswith('temporal_')}
    receipt_path.write_text(json.dumps(legacy))
    require(linker.objects_for(core/'src/nr_before_sr.cpp')==[obj],'historical dynamic receipt compatibility lost')
    report.update(passed=True,packaging_checks=checks);save();print(r.stdout.strip());print(f'PASS {checks} artifact validation/wiring/negative controls; no HLSL execution')

if __name__=='__main__':main()
