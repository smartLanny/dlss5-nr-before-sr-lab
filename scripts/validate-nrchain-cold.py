#!/usr/bin/env python3
"""Real NVIDIA cold core/official parameter test; never initializes NGX in the host."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser();p.add_argument('--bridge',type=Path,required=True)
    p.add_argument('--ngx-sdk',type=Path,required=True);p.add_argument('--vcvars',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    bridge=a.bridge.resolve(strict=True);sdk=a.ngx_sdk.resolve(strict=True);vc=a.vcvars.resolve(strict=True)
    source=ROOT/'tests/nrchain_cold_bootstrap.cpp';binary=out/'cold-bootstrap.exe'
    for value in [out,bridge,sdk,vc,source]:
        if any(c in str(value) for c in '\r\n"%!'):raise ValueError('Unsupported command-file path')
    cmd=out/'compile.cmd'
    cmd.write_text(f'@echo off\ncall "{vc}" x64\nif errorlevel 1 exit /b 1\n'
        f'cl /nologo /EHsc /O2 /MT /utf-8 /std:c++20 /W4 /I"{sdk / "include"}" "{source}" /Fe:"{binary}" '
        f'/Fo:"{out / "cold-bootstrap.obj"}" /link d3d12.lib dxgi.lib\nexit /b %errorlevel%\n',encoding='utf-8')
    commands=[]
    for name,args in [('compile',['cmd.exe','/d','/c',str(cmd)]),('run',[str(binary),str(bridge)])]:
        r=subprocess.run(args,cwd=out,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90)
        log=out/(name+'.log');log.write_text(r.stdout+r.stderr,encoding='utf-8')
        print(r.stdout+r.stderr,flush=True);commands.append({'command':args,'exit_code':r.returncode,'log_sha256':sha(log)})
        if r.returncode:break
    result={'schema':'nrchain-real-cold-bootstrap-v1','result':r.returncode,'commands':commands,
        'bridge_sha256':sha(bridge),'source_sha256':sha(source),
        'scope':'NVIDIA device; initially absent _nvngx; production bridge official loader; real core parameters; no Feature1 and no game'}
    (out/'validation.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    return r.returncode
if __name__=='__main__':raise SystemExit(main())
