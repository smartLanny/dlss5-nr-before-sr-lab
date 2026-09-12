#!/usr/bin/env python3
"""Compile the actual source-ownership policy; this is a CPU contract gate."""
import argparse
import pathlib
import subprocess
import json
import hashlib

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--compiler',required=True)
    parser.add_argument('--output',required=True)
    args=parser.parse_args()
    root=pathlib.Path(__file__).resolve().parents[1]
    output=pathlib.Path(args.output).resolve()
    output.mkdir(parents=True,exist_ok=True)
    exe=output/'source-frame-test.exe'
    runtime=(root/'src/nr_source_frame_runtime.inl').read_text(encoding='utf-8-sig')
    producer_start=runtime.index('struct Producer {')
    producer_end=runtime.index('// Streamline adapter publishes',producer_start)
    generated=output/'source-frame-test.cpp'
    harness=(root/'tests/nr_source_frame_test.cpp').read_text(encoding='utf-8-sig')
    harness=harness.replace('../src/nr_source_frame.hpp','nr_source_frame.hpp')
    generated.write_text(harness.replace('// @SOURCE_PRODUCERS@',runtime[producer_start:producer_end]),encoding='utf-8')
    command=[args.compiler,'-std=c++17','-O2','-static','-Wall','-Wextra','-Werror',
             '-I'+str(root/'src'),str(generated),'-o',str(exe)]
    compiled=subprocess.run(command,capture_output=True,text=True)
    (output/'compile.log').write_text(compiled.stdout+compiled.stderr,encoding='utf-8')
    if compiled.returncode: raise RuntimeError(compiled.stderr)
    run=subprocess.run([str(exe)],capture_output=True,text=True)
    (output/'run.log').write_text(run.stdout+run.stderr,encoding='utf-8')
    receipt={'scope':'CPU source ownership and submitted-copy ordering only','success':run.returncode==0,
             'stdout':run.stdout,'gpu_verified':False,'ngx_verified':False,'game_verified':False,
             'source_sha256':hashlib.sha256((root/'src/nr_source_frame.hpp').read_bytes()).hexdigest(),
             'runtime_sha256':hashlib.sha256((root/'src/nr_source_frame_runtime.inl').read_bytes()).hexdigest()}
    (output/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    print(run.stdout,end='')
    if run.returncode: raise RuntimeError(run.stderr)

if __name__=='__main__':main()
