#!/usr/bin/env python3
"""Real Win32/GetProcAddress/MinHook test using isolated fake NGX DLLs, not NVIDIA."""
from pathlib import Path
import argparse,json,os,subprocess
ROOT=Path(__file__).resolve().parents[1]
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--toolchain',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--minhook',type=Path,default=ROOT/'third_party/minhook')
    a=ap.parse_args();out=a.output.resolve()
    if os.name!='nt':raise SystemExit('This gate requires Windows; a host-only substitute is not a pass.')
    if out.exists():raise SystemExit('Output exists; choose a new private directory.')
    cc=a.toolchain/'bin/x86_64-w64-mingw32-clang.exe';cxx=a.toolchain/'bin/x86_64-w64-mingw32-clang++.exe'
    sources=ROOT/'tests/core_entry_windows'
    for f in (cc,cxx,a.minhook/'include/MinHook.h',sources/'main.cpp'):
        if not f.is_file():raise SystemExit('Missing required input: '+str(f))
    out.mkdir(parents=True);results=[]
    def run(cmd,name,expected=0):
        r=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,timeout=120)
        (out/(name+'.txt')).write_text('$ '+' '.join(map(str,cmd))+'\n'+r.stdout+r.stderr,encoding='utf-8')
        results.append({'name':name,'exit':r.returncode})
        if r.returncode!=expected:raise RuntimeError(name+' failed: '+r.stdout+r.stderr)
    status='failed'
    try:
        objs=[]
        for rel in ('src/buffer.c','src/hook.c','src/trampoline.c','src/hde/hde64.c'):
            obj=out/(Path(rel).stem+'.o');run([cc,'-O2','-I'+str(a.minhook/'include'),'-c',a.minhook/rel,'-o',obj],Path(rel).stem);objs.append(obj)
        flags=['-std=c++20','-O2','-static','-I'+str(a.minhook/'include')]
        run([cxx,*flags,'-shared',sources/'native_core.cpp','-o',out/'_nvngx.dll'],'core-dll')
        run([cxx,*flags,'-shared',sources/'client.cpp','-o',out/'client.dll'],'client-dll')
        run([cxx,*flags,'-municode',sources/'main.cpp',*objs,'-o',out/'boundary.exe'],'harness')
        run([out/'boundary.exe',out/'_nvngx.dll',out/'client.dll','--baseline'],'baseline-red',11)
        run([out/'boundary.exe',out/'_nvngx.dll',out/'client.dll'],'candidate-green')
        status='passed'
    finally:
        (out/'validation.json').write_text(json.dumps({'status':status,'real_win32':True,'real_minhook':True,
            'native_ngx':'fake DLL','NVIDIA':False,'game':False,'runs':results},indent=2)+'\n')
    print('Real Win32 entry gate passed; NVIDIA/game validation remains separate.')
if __name__=='__main__':main()
