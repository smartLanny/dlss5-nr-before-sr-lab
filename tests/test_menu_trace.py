"""Portable entry for the same production CPU trace gate used by the canonical builder."""
from __future__ import annotations
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
compiler=os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
if not compiler:
    print('SKIP: menu trace CPU gate needs a C++20 compiler')
    raise SystemExit(0)
raise SystemExit(subprocess.call([sys.executable,str(ROOT/'scripts/validate-menu-trace.py'),'--compiler',compiler],cwd=ROOT))
