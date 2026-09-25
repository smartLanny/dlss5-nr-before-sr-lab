#!/usr/bin/env python3
"""Compile immutable, source-keyed SM5 artifacts and embed them into the Core."""
from pathlib import Path
import argparse,concurrent.futures,hashlib,json,re,shutil,struct,subprocess,time
def sha(data):return hashlib.sha256(data).hexdigest()
def compiler_identity(data):
    """Compare complete PE executables except COFF/debug build timestamps."""
    result=bytearray(data)
    pe=struct.unpack_from('<I',data,60)[0]
    if data[:2]!=b'MZ' or data[pe:pe+4]!=b'PE\0\0':raise ValueError('Invalid compiler PE')
    count=struct.unpack_from('<H',data,pe+6)[0]
    optional=pe+24;optional_size=struct.unpack_from('<H',data,pe+20)[0]
    if struct.unpack_from('<H',data,optional)[0]!=0x20b:raise ValueError('Expected PE32+ compiler')
    result[pe+8:pe+12]=b'\0'*4
    debug_rva,debug_size=struct.unpack_from('<II',data,optional+112+6*8)
    if debug_rva:
        if debug_size%28:raise ValueError('Invalid compiler debug directory')
        for i in range(count):
            section=optional+optional_size+i*40
            virtual_size,rva,raw_size,raw=struct.unpack_from('<IIII',data,section+8)
            if rva<=debug_rva and debug_rva+debug_size<=rva+raw_size:
                offset=raw+debug_rva-rva
                if offset+debug_size>len(data):raise ValueError('Truncated compiler debug directory')
                for entry in range(offset,offset+debug_size,28):result[entry+4:entry+8]=b'\0'*4
                break
        else:raise ValueError('Unmapped compiler debug directory')
    return bytes(result)
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source',type=Path,required=True);ap.add_argument('--compiler',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True);ap.add_argument('--workers',type=int,default=2)
    ap.add_argument('--cache-from',type=Path,help='Prior build directory; requires identical rebuilt compiler except PE build timestamps')
    a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    text=a.source.read_text(encoding='utf-8');mark='const char kCodecShader[] = R"HLSL('
    if text.count(mark)!=1:raise ValueError('Production shader must be unique')
    body=text.split(mark)[1].split(')HLSL";',1)[0]
    entries=re.findall(r'\[numthreads\([^)]*\)\]\s*void\s+(\w+)\s*\(',body)
    if len(set(entries))!=len(entries) or not {'CombineCS','CombineV2CS'}.issubset(entries):raise ValueError('Invalid production entry inventory')
    rebuilt_compiler_sha=sha(a.compiler.read_bytes())
    if a.cache_from:
        cached_compiler=a.cache_from/'compile-codec-artifact.exe'
        if compiler_identity(a.compiler.read_bytes())!=compiler_identity(cached_compiler.read_bytes()):
            raise ValueError('Cached compiler differs from the freshly rebuilt compiler')
        # Keep the original compiler hash and shader receipts. No cache receipt
        # is relabelled as output from the newly built executable.
        if cached_compiler.resolve()!=a.compiler.resolve():shutil.copy2(cached_compiler,a.compiler)
    compiler_sha=sha(a.compiler.read_bytes());body_sha=sha(body.encode())
    key=sha((body_sha+compiler_sha+'SM5-O3-macros-v1').encode())
    cache=a.output/'codec'/key
    if a.cache_from:
        previous=a.cache_from/'codec'/key
        if not previous.is_dir():raise ValueError('No cache for this exact shader source/compiler/options')
        if cache.exists():raise ValueError('Cache destination exists; preserve it')
        shutil.copytree(previous,cache)
    cache.mkdir(parents=True,exist_ok=True)
    source=cache/'actual-codec.hlsl';source.write_text(body,encoding='utf-8',newline='\n')
    cases=[(entry,False,-1) for entry in entries]+[(entry,True,-1) for entry in ['CombineCS','CombineV2CS']]
    reduced=['CombineCS','CombineV2CS','BuildEditFieldCS','BuildColorEditFieldCS']
    cases += [(entry,False,domain) for domain in (0,1) for entry in reduced]
    cases += [(entry,True,domain) for domain in (0,1) for entry in ['CombineCS','CombineV2CS']]
    def compile_one(case):
        entry,face,domain=case;tag=f'{entry}-face{int(face)}-domain{domain}'
        binary=cache/(tag+'.cso');receipt=cache/(tag+'.json');log=cache/(tag+'.log')
        expected={'entry':entry,'face':face,'domain':domain,'shader_sha256':body_sha,'compiler_sha256':compiler_sha,'flags':'optimization-level-3','target':'cs_5_0','file':str(binary.relative_to(a.output))}
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
    if a.cache_from:manifest['cache_reuse']={'rebuilt_compiler_sha256':rebuilt_compiler_sha,'comparison':'complete PE bytes except COFF/debug build timestamps','original_receipts_preserved':True}
    (a.output/'codec-artifacts.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print(f'Embedded {len(results)} real SM5 artifacts; no runtime shader compilation',flush=True)
if __name__=='__main__':main()
