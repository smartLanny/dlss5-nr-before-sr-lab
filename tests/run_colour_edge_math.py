#!/usr/bin/env python3
"""Compile and run production paired-weight arithmetic on a host C++ compiler."""
from __future__ import annotations
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]

def main() -> None:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler')
    p.add_argument('--sanitize',action='store_true',help='Host ASan/UBSan; not Windows D3D validation')
    args=p.parse_args()
    compiler=args.compiler or os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
    if not compiler: raise ValueError('No host C++ compiler; no compile verification performed')
    shader=(ROOT/'src/nr_colour_edge.hlsli').read_text(encoding='utf-8')
    a,b='// NR_COLOUR_PAIR_MATH_BEGIN','// NR_COLOUR_PAIR_MATH_END'
    if shader.count(a)!=1 or shader.count(b)!=1: raise ValueError('Production math markers changed')
    math=shader.split(a,1)[1].split(b,1)[0]
    with tempfile.TemporaryDirectory(prefix='colour4-pair-') as temp:
        temp=Path(temp)
        (temp/'colour4_pair.generated.hpp').write_text(math,encoding='utf-8')
        exe=temp/('pair.exe' if os.name=='nt' else 'pair')
        cmd=[compiler,'-std=c++20','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
             '-I',str(temp),str(ROOT/'tests/colour_edge_math.cpp'),'-o',str(exe)]
        if args.sanitize: cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        elif os.name=='nt': cmd+=['-static']
        subprocess.run(cmd,check=True,timeout=120)
        subprocess.run([str(exe)],check=True,timeout=90)
    print('paired_shader_sha256='+hashlib.sha256(shader.encode()).hexdigest())
if __name__=='__main__':
    try: main()
    except (ValueError,OSError,subprocess.SubprocessError) as error:
        raise SystemExit(str(error)) from error
