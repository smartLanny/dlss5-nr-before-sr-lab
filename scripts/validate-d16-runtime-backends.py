#!/usr/bin/env python3
"""Run production host controllers; optionally build real Windows D3D12 backend."""
import argparse,json,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cxx',default='g++');p.add_argument('--mode',choices=['rr','deferred'],required=True)
p.add_argument('--windows',action='store_true',help='Require a native Windows LLVM-MinGW compiler; builds and runs WARP construction smoke')
a=p.parse_args();root=Path(__file__).resolve().parents[1];out=root/'build'/('d16-backends-'+a.mode);out.mkdir(parents=True,exist_ok=True)
def run(cmd):print('+',' '.join(map(str,cmd)),flush=True);subprocess.run(list(map(str,cmd)),check=True)
inc=root/'src/experiments';tests=['d16_edit_dispatch_slots_test']
if a.mode=='deferred':tests+=['d16_private_sr_session_test']
for name in tests:
    exe=out/(name+('.exe' if sys.platform=='win32' else ''))
    run([a.cxx,'-std=c++20','-Wall','-Wextra','-Werror','-I'+str(inc),root/'tests'/(name+'.cpp'),'-o',exe]);run([exe])
status='not-run'
if a.windows:
    if sys.platform!='win32':raise SystemExit('--windows requires Windows; cross-compilation is not execution')
    source=['nr_edit_dx12_dispatch.cpp', 'nr_rr_edit_program.cpp' if a.mode=='rr' else 'nr_deferred_program.cpp']
    if a.mode=='deferred':source+=['nr_private_sr_ngx_dx12.cpp']
    objects=[]
    for name in source:
        obj=out/(name+'.o');run([a.cxx,'-std=c++20','-Wall','-Wextra','-I'+str(inc),'-c',inc/name,'-o',obj]);objects.append(obj)
    exe=out/'d16-dx12-construction-smoke.exe'
    run([a.cxx,'-std=c++20','-municode','-I'+str(inc),root/'tests/d16_edit_dx12_build_smoke.cpp',*objects,'-ld3d12','-ldxgi','-ldxguid','-ld3dcompiler','-o',exe])
    run([exe,a.mode,root/'shaders/experiments'/('nr_rr_edit.hlsl' if a.mode=='rr' else 'nr_deferred_edit.hlsl')]);status='passed'
report={'host_controller_tests':'passed','windows_warp_pipeline_construction':status,'gpu_dispatch':'not-run','real_private_sr':'not-run','game_integration':'not-complete'}
(out/'validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report,indent=2))
