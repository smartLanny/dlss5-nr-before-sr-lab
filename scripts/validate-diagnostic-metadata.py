#!/usr/bin/env python3
"""Compile the production metadata sanitizer's strict menu/ledger privacy checks."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def run(compiler: str,output: Path) -> int:
    command=[compiler,'-std=c++20','-O0','-Wall','-Wextra','-Werror',
             '-I'+str(ROOT/'src'),str(ROOT/'tests/diagnostic_metadata_privacy_test.cpp'),
             '-o',str(output)]
    if os.name=='nt':command.insert(5,'-static')
    subprocess.run(command,cwd=ROOT,check=True)
    subprocess.run([str(output)],cwd=ROOT,check=True)
    return 0

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',default=os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++'))
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    if not args.compiler:
        print('SKIP: diagnostic metadata gate needs a C++20 compiler')
        return 77
    if args.output:
        args.output.mkdir(parents=True,exist_ok=True)
        return run(args.compiler,args.output/'diagnostic-metadata.exe')
    with tempfile.TemporaryDirectory(prefix='nr-diagnostic-metadata-') as folder:
        return run(args.compiler,Path(folder)/'diagnostic-metadata.exe')

if __name__=='__main__':raise SystemExit(main())
