#!/usr/bin/env python3
"""Compile and execute the production menu trace's bounded CPU controls."""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
def main() -> int:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',default=os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++'))
    p.add_argument('--output',type=Path)
    a=p.parse_args()
    if not a.compiler:
        print('SKIP: menu trace CPU gate needs a C++20 compiler')
        return 77
    if a.output:
        a.output.mkdir(parents=True,exist_ok=True)
        out=a.output/'menu-trace.exe'
        return run(a.compiler,out)
    with tempfile.TemporaryDirectory(prefix='nr-menu-trace-') as folder:
        return run(a.compiler,Path(folder)/'menu-trace.exe')
def run(compiler: str,out: Path) -> int:
    cmd=[compiler,'-std=c++20','-O0','-Wall','-Wextra','-Werror','-pthread',
         '-I'+str(ROOT/'src'),str(ROOT/'tests/menu_trace_test.cpp'),'-o',str(out)]
    if os.name=='nt':cmd.insert(5,'-static')
    subprocess.run(cmd,cwd=ROOT,check=True)
    subprocess.run([str(out)],cwd=ROOT,check=True)
    print('PASS: bounded Esc trace, A-B-A, guide contract, pointer rotation, outcomes, expiry, overflow, concurrent snapshot')
    return 0
if __name__=='__main__':raise SystemExit(main())
