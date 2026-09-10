#!/usr/bin/env python3
"""Compile immutable, source-keyed SM5 artifacts and embed them into the Core."""
from pathlib import Path
import argparse,concurrent.futures,hashlib,json,re,subprocess,time
def sha(data):return hashlib.sha256(data).hexdigest()
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source',type=Path,required=True);ap.add_argument('--compiler',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True);ap.add_argument('--workers',type=int,default=2)
    a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    text=a.source.read_text(encoding='utf-8');mark='const char kCodecShader[] = R"HLSL('
    if text.count(mark)!=1:raise ValueError('Production shader must be unique')
    body=text.split(mark)[1].split(')HLSL";',1)[0]
    entries=re.findall(r'\[numthreads\([^)]*\)\]\s*void\s+(\w+)\s*\(',body)
    if len(set(entries))!=len(entries) or not {'CombineCS','CombineV2CS'}.issubset(entries):raise ValueError('Invalid production entry inventory')
    compiler_sha=sha(a.compiler.read_bytes());body_sha=sha(body.encode())
    key=sha((body_sha+compiler_sha+'SM5-O3-macros-v1').encode())
    cache=a.output/'codec'/key;cache.mkdir(parents=True,exist_ok=True)
    source=cache/'actual-codec.hlsl';source.write_text(body,encoding='utf-8',newline='\n')
    cases=[(entry,False,-1) for entry in entries]+[(entry,True,-1) for entry in ['CombineCS','CombineV2CS']]
    reduced=['CombineCS','CombineV2CS','BuildEditFieldCS','BuildColorEditFieldCS']
    cases += [(entry,False,domain) for domain in (0,1) for entry in reduced]
    cases += [(entry,True,domain) for domain in (0,1) for entry in ['CombineCS','CombineV2CS']]
    def compile_one(case):
        entry,face,domain=case;tag=f'{entry}-face{int(face)}-domain{domain}'
        binary=cache/(tag+'.cso');receipt=cache/(tag+'.json');log=cache/(tag+'.log')
        expected={'entry':entry,'face':face,'domain':domain,'shader_sha256':body_sha,'compiler_sha256':compiler_sha,'flags':'optimization-level-3','target':'cs_5_0'}
        if receipt.exists() and binary.exists():
            old=json.loads(receipt.read_text())
            if all(old.get(k)==v for k,v in expected.items()) and old.get('sha256')==sha(binary.read_bytes()):return old
            raise ValueError('Existing shader cache differs; preserve it for inspection')
        start=time.monotonic()
        result=subprocess.run([str(a.compiler.resolve()),str(source.resolve()),entry,str(int(face)),str(domain),str(binary.resolve())],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        log.write_bytes(result.stdout)
        if result.returncode:raise RuntimeError(f'{tag} failed; inspect {log}')
        data=binary.read_bytes()
        if data[:4]!=b'DXBC':raise ValueError('Expected real DXBC output')
        info=expected|{'file':str(binary.relative_to(a.output)),'sha256':sha(data),'bytes':len(data),'seconds':time.monotonic()-start}
        receipt.write_text(json.dumps(info,indent=2)+'\n',encoding='utf-8')
        print(f'Compiled {tag}: {len(data)} bytes, {info["seconds"]:.1f}s',flush=True)
        return info
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1,min(a.workers,3))) as pool:
        results=list(pool.map(compile_one,cases))
    header=['#pragma once','#include <cstddef>','#include <cstring>','namespace nr_codec_precompiled {',
            'struct Blob {const unsigned char* data;std::size_t size;};']
    for i,info in enumerate(results):
        data=(a.output/info['file']).read_bytes()
        header.append(f'inline constexpr unsigned char shader_{i}[]={{')
        for j in range(0,len(data),32):header.append(','.join(str(v) for v in data[j:j+32])+',')
        header.append('};')
    header.append('inline Blob Find(const char* entry,bool face,int domain) {')
    for i,info in enumerate(results):
        header.append(f'if(face=={str(info["face"]).lower()} && domain=={info["domain"]} && std::strcmp(entry,"{info["entry"]}")==0)return {{shader_{i},sizeof(shader_{i})}};')
    header.extend(['return {nullptr,0};','}','}'])
    out=a.output/'nr_codec_precompiled.h';out.write_text('\n'.join(header)+'\n',encoding='utf-8',newline='\n')
    manifest={'schema':'nr-codec-artifacts-v1','source':body_sha,'compiler':compiler_sha,'header_sha256':sha(out.read_bytes()),'artifacts':results,'game_verified':False}
    (a.output/'codec-artifacts.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print(f'Embedded {len(results)} real SM5 artifacts; no runtime shader compilation',flush=True)
if __name__=='__main__':main()
