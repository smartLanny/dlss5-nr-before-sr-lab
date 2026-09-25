#!/usr/bin/env python3
"""Compile the production recipe and panel against explicit host substitutes."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--compiler')
a=p.parse_args()
cc=a.compiler or os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
if not cc:raise SystemExit('A C++20 host compiler is required')
with tempfile.TemporaryDirectory(prefix='quality-ui-host-') as td:
    exe=Path(td)/('quality.exe' if os.name=='nt' else 'quality')
    command=[cc,'-std=c++20','-O2','-Wall','-Wextra','-Werror',str(ROOT/'tests/quality_recipe_test.cpp'),'-o',str(exe)]
    if os.name=='nt':command.append('-static')
    subprocess.run(command,check=True,timeout=90)
    subprocess.run([str(exe)],check=True,timeout=30)
