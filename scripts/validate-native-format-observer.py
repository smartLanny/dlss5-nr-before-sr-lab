#!/usr/bin/env python3
"""Compile/run the actual native-format observer through isolated host COM seams.

Snapshot every production input and record its hash. No GPU, game or injection;
an unfrozen source tree is allowed only when the files are stable during copying.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess

ROOT=Path(__file__).resolve().parents[1]
INPUTS=('src/nr_native_format.h','src/nr_native_format_runtime.inl',
        'src/nr_native_format_hooks.inl','src/interop/nr_format_access.h')

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
