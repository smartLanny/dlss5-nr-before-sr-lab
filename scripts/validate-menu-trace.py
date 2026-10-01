#!/usr/bin/env python3
"""Compile and execute the production menu trace's bounded CPU controls."""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import runpy

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
    extract_function=runpy.run_path(str(ROOT/'scripts/validate-feedback-capture.py'))['extract_function']
    feedback=(ROOT/'src/nr_feedback_capture.inl').read_text(encoding='utf-8-sig')
    core=(ROOT/'src/nr_feedback_core.inl').read_text(encoding='utf-8-sig')
    generated=(ROOT/'tests/menu_trace_formatter_test.cpp.in').read_text(encoding='utf-8')
    generated=generated.replace('// @PRODUCTION_FEEDBACK_APPEND@',extract_function(feedback,'void FeedbackAppend('))
    generated=generated.replace('// @PRODUCTION_MENU_FORMATTERS@','\n\n'.join(
        extract_function(core,name) for name in (
            'void AppendMenuTraceWindow(', 'void AppendMenuTraceBounded(', 'void AppendMenuTrace(')))
    cpp=out.with_name('menu-trace-formatter.cpp')
    cpp.write_text(generated,encoding='utf-8')
    formatter=out.with_name('menu-trace-formatter.exe')
    formatter_cmd=[compiler,'-std=c++20','-O0','-Wall','-Wextra','-Werror','-pthread',
                   '-I'+str(ROOT/'src'),str(cpp),'-o',str(formatter)]
    if os.name=='nt':formatter_cmd.insert(5,'-static')
    subprocess.run(formatter_cmd,cwd=ROOT,check=True)
    subprocess.run([str(formatter)],cwd=ROOT,check=True)
    print('PASS: bounded Esc trace, rotation coalescing, production formatter capacity and tail')
    return 0
if __name__=='__main__':raise SystemExit(main())
