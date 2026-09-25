#!/usr/bin/env python3
"""Compile/run the actual production HLSL math through the small C++ float3 shim."""
from __future__ import annotations
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', help='Host C++20 compiler; Windows may use the existing LLVM-MinGW compiler')
    args = parser.parse_args()
    compiler = args.compiler or os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise SystemExit('No C++20 compiler found. Supply --compiler; no compile verification was performed.')
    source = (ROOT/'src/nr_colour_restore.hlsli').read_text(encoding='utf-8')
    start, end = '// NR_COLOUR_RESTORE_MATH_BEGIN', '// NR_COLOUR_RESTORE_MATH_END'
    if source.count(start) != 1 or source.count(end) != 1:
        raise SystemExit('Production shader math markers changed')
    math = source.split(start, 1)[1].split(end, 1)[0]
    prepared=(ROOT/'src/nr_colour_prepare.hlsli').read_text(encoding='utf-8')
    pstart,pend='// NR_COLOUR_PREPARE_MATH_BEGIN','// NR_COLOUR_PREPARE_MATH_END'
    if prepared.count(pstart)!=1 or prepared.count(pend)!=1:
        raise SystemExit('Production preparation markers changed')
    math=prepared.split(pstart,1)[1].split(pend,1)[0]+'\n'+math
    # The HLSL unroll attribute is the only syntax adaptation. All arithmetic,
    # bounds, branches, RGB vectors and face controls are the production code.
    math = math.replace('[unroll]', '')
    with tempfile.TemporaryDirectory(prefix='nr-colour-restore-') as temp:
        build = Path(temp)
        (build/'colour_restore_math.generated.hpp').write_text(math, encoding='utf-8')
        executable = build / ('colour_restore_math.exe' if os.name == 'nt' else 'colour_restore_math')
        command = [compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-ffp-contract=off', '-I', str(build),
                   str(ROOT/'tests/colour_restore_math_test.cpp'), '-o', str(executable)]
        if os.name == 'nt': command.append('-static')
        subprocess.run(command, check=True, timeout=120)
        subprocess.run([str(executable)], check=True, timeout=60)
    print('production_prepare_sha256=' + hashlib.sha256(prepared.encode()).hexdigest())
    print('production_shader_sha256=' + hashlib.sha256(source.encode()).hexdigest())


if __name__ == '__main__':
    try:
        main()
    except (OSError, subprocess.SubprocessError) as error:
        raise SystemExit(str(error)) from error
