#!/usr/bin/env python3
"""Bake the three temporal variants; production never falls back to D3DCompile.

Run only at Windows build time. Outputs are private/generated, not repository
binaries. No existing artifact directory is reused or overwritten.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

VARIANTS = (
    {'variant':0,'entry':'CopyCandidate','macros':[['RECORDING','1']]},
    {'variant':1,'entry':'CS','macros':[['RECORDING','1']]},
    {'variant':2,'entry':'CS','macros':[['RECORDING','1'],['INPLACE_FP16','1']]},
)

def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def validate_dxbc(data: bytes) -> None:
    # Bounded container checks, not shader execution or GPU compatibility.
    if len(data)<32 or len(data)>16*1024*1024 or data[:4]!=b'DXBC':
        raise ValueError('expected bounded DXBC shader container')
    size,count=struct.unpack_from('<II',data,24)
    if size!=len(data) or count==0 or count>(len(data)-32)//4:
        raise ValueError('inconsistent DXBC size/chunk table')
    intervals=[];code=False
    for i in range(count):
        offset=struct.unpack_from('<I',data,32+4*i)[0]
        if offset<32+4*count or offset>len(data)-8:
            raise ValueError('out-of-bounds DXBC chunk')
        length=struct.unpack_from('<I',data,offset+4)[0]
        end=offset+8+length
        if end>len(data) or any(offset<b and a<end for a,b in intervals):
            raise ValueError('overlapping/truncated DXBC chunk')
        intervals.append((offset,end));code|=data[offset:offset+4] in (b'SHDR',b'SHEX')
    if not code:raise ValueError('DXBC missing SM5 code chunk')

def emit_header(blobs: list[bytes]) -> str:
    if len(blobs)!=len(VARIANTS):raise ValueError('exactly three temporal variants required')
    lines=['#pragma once','#include <cstddef>',
           '// Generated at build time. Do not edit; see temporal-artifacts.json.',
           'namespace nr_temporal_precompiled {',
           'struct Blob {const void* data; std::size_t size;};']
    for i,data in enumerate(blobs):
        validate_dxbc(data)
        lines.append(f'alignas(4) inline constexpr unsigned char shader_{i}[]={{')
        lines.extend(','.join(str(x) for x in data[j:j+32])+',' for j in range(0,len(data),32))
        lines.append('};')
    lines.append('inline Blob Find(unsigned variant) {switch(variant){')
    for i in range(len(blobs)):
        lines.append(f'case {i}:return {{shader_{i},sizeof(shader_{i})}};')
    lines.extend(['default:return {nullptr,0};}}','}'])
    return '\n'.join(lines)+'\n'

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('source','compiler','output'):ap.add_argument('--'+name,type=Path,required=True)
    a=ap.parse_args()
    if os.name!='nt':raise RuntimeError('Windows FXC build required; no shader was compiled')
    source,compiler,out=a.source.resolve(),a.compiler.resolve(),a.output.resolve()
    out.mkdir(parents=True,exist_ok=False)
    helper=source/'compile_temporal_artifacts.cpp';shader=source/'residual_accumulator_shader.hpp'
    exe=out/'compile-temporal-artifacts.exe'
    report={'schema':'nr-temporal-artifacts-v1','passed':False,'compiler_sha256':sha(compiler),
            'source_sha256':sha(shader),'helper_sha256':sha(helper),'artifacts':[],
            'gpu_verified':False,'game_verified':False}
    def save():(out/'temporal-artifacts.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    save()
    command=[str(compiler),'-std=c++17','-O2','-municode','-DUNICODE','-D_UNICODE',
             '-Wall','-Wextra','-Werror',str(helper),'-ld3dcompiler','-o',str(exe)]
    run=subprocess.run(command,capture_output=True,text=True)
    (out/'helper-build.log').write_text(run.stdout+run.stderr,encoding='utf-8')
    report['helper_build']={'command':command,'exit':run.returncode};save()
    if run.returncode:raise RuntimeError('temporal compiler helper failed: '+run.stderr)
    report['helper_exe_sha256']=sha(exe);blobs=[]
    for variant in VARIANTS:
        binary=out/f"variant-{variant['variant']}.cso"
        run=subprocess.run([str(exe),str(variant['variant']),str(binary)],capture_output=True,text=True)
        (out/f"variant-{variant['variant']}.log").write_text(run.stdout+run.stderr,encoding='utf-8')
        if run.returncode:raise RuntimeError('temporal FXC variant failed: '+str(variant['variant']))
        data=binary.read_bytes();validate_dxbc(data);blobs.append(data)
        report['artifacts'].append({**variant,'file':binary.name,'sha256':sha(binary),'bytes':len(data),
                                    'source_label':'recorded-temporal','target':'cs_5_0','flags':1<<11})
        save()
    # Publish a header only after ALL exact variants have compiled successfully.
    header=out/'nr_temporal_precompiled.hpp'
    header.write_text(emit_header(blobs),encoding='utf-8',newline='\n')
    report['header_sha256']=sha(header);report['passed']=True;save()
    print('Compiled 3 temporal shader variants; runtime/GPU validation remains separate')

if __name__=='__main__':main()
