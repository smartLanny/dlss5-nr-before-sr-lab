#!/usr/bin/env python3
"""Compile/run the actual native-format observer through isolated host COM seams.

Snapshot every production input and record its hash. No GPU, game or injection;
an unfrozen source tree is allowed only when the files are stable during copying.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import subprocess

ROOT=Path(__file__).resolve().parents[1]
INPUTS=('src/nr_native_clear_request.h','src/nr_native_clear_request_runtime.inl','src/nr_descriptor_provenance.h','src/nr_native_format.h','src/nr_native_format_runtime.inl',
        'src/nr_native_format_hooks.inl','src/interop/nr_format_access.h',
        'src/nr_resource_semantics.h','src/nr_before_sr.cpp',
        'src/interop/nr_format_access_d3d12.inl')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root',type=Path,default=ROOT)
    parser.add_argument('--compiler',default='c++')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--compile-only',action='store_true')
    args=parser.parse_args()
    source=args.source_root.resolve()
    output=args.output.resolve()
    if output.exists(): raise SystemExit('Output exists; preserve previous native-format evidence.')
    content={name:(source/name).read_bytes() for name in INPUTS}
    if any((source/name).read_bytes()!=data for name,data in content.items()):
        raise SystemExit('Production modules changed during snapshot; retry when edits settle.')
    fixture=(ROOT/'tests/native_format_observer.cpp').read_bytes()
    content['tests/native_format_observer.cpp']=fixture
    # The observer now returns the same final color admission as production.
    # Include the actual storage classifiers and exposure predicates as well,
    # rather than replacing that final decision with a permissive host stub.
    spec=importlib.util.spec_from_file_location('format_source_parser',ROOT/'scripts/patch-format-access.py')
    parser_module=importlib.util.module_from_spec(spec);spec.loader.exec_module(parser_module)
    raw=content['src/nr_before_sr.cpp'].decode('utf-8')
    definitions=[]
    for signature in ('struct ColorContract {', 'enum class CodecColorMode :',
                      'enum class PreSrStorageContract {', 'enum class PostSrStorageContract {',
                      'bool IsUnityExposureContract(', 'bool IsPreSrBoundedR8Contract(',
                      'bool IsPqDisplayExposureContract(', 'bool IsR8SceneColorFormat(',
                      'PreSrStorageContract ClassifyPreSrStorage(', 'bool IsUsablePreSrStorage(',
                      'PostSrStorageContract ClassifyPostSrStorage(', 'bool IsUsablePostSrStorage('):
        a,b=parser_module.block(raw,signature)
        definitions.append(raw[a:b]+(';' if signature.startswith(('struct ','enum ')) else ''))
    adapter=content['src/interop/nr_format_access_d3d12.inl'].decode('utf-8')
    a,b=parser_module.block(adapter,'DXGI_FORMAT NrSceneReadFormat(')
    definitions.insert(0,adapter[a:b])
    content['tests/native_format_color_contract.inl']=('\n'.join(definitions)+'\n').encode()
    output.mkdir(parents=True)
    snapshot=output/'source'
    for name,data in content.items():
        target=snapshot/name; target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(data)
    hashes={name:hashlib.sha256(data).hexdigest() for name,data in content.items()}
    exe=output/('native_format_observer.exe' if os.name=='nt' else 'native_format_observer')
    command=[args.compiler,'-std=c++20','-Wall','-Wextra','-Werror','-O1','-pthread',
             '-I'+str(snapshot),str(snapshot/'tests/native_format_observer.cpp'),'-o',str(exe)]
    if os.name=='nt': command.insert(1,'-static')
    built=subprocess.run(command,capture_output=True,text=True)
    (output/'compile-stdout.txt').write_text(built.stdout,encoding='utf-8')
    (output/'compile-stderr.txt').write_text(built.stderr,encoding='utf-8')
    record={'schema':1,'scope':'actual production modules through host-only COM/hook seams',
            'inputs_sha256':hashes,'compile_exit':built.returncode,'run_exit':None,
            'gpu_executed':False,'game_verified':False}
    code=built.returncode
    if code==0 and not args.compile_only:
        run=subprocess.run([str(exe)],capture_output=True,text=True)
        (output/'stdout.txt').write_text(run.stdout,encoding='utf-8')
        (output/'stderr.txt').write_text(run.stderr,encoding='utf-8')
        print(run.stdout,end=''); print(run.stderr,end='')
        record['run_exit']=code=run.returncode
    else:
        print(built.stdout,end=''); print(built.stderr,end='')
    (output/'result.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    raise SystemExit(code)

if __name__=='__main__': main()
