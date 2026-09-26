"""Validate owned post residual -> private SR -> compose, with explicit evidence scopes.
Default tests use a labelled API fixture and real WARP pixels. --nvidia adds
hardware pixels. An explicit existing bridge/runtime pair adds real Feature1.
No games, injection, runtime download, installation or global settings changes.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys

PACKAGE=Path(__file__).resolve().parents[1]
ROOT=PACKAGE.parents[1]

def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f,'sha256').hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--nvidia',action='store_true')
    p.add_argument('--nrchain',type=Path)
    p.add_argument('--sr-runtime',type=Path)
    a=p.parse_args()
    if os.name!='nt':p.error('Windows D3D12 required; no CPU-only fallback')
    if bool(a.nrchain)!=bool(a.sr_runtime):p.error('Supply both existing runtime paths')
    out=a.output.resolve();compiler=a.compiler.resolve(strict=True)
    if out.exists():p.error('Use a fresh output; failed evidence is retained')
    out.mkdir(parents=True)
    sources=[PACKAGE/'src/nr_post_private_sr_dx12.cpp',PACKAGE/'src/nr_post_private_sr_dx12.hpp',
             PACKAGE/'src/nr_private_sr_runtime.hpp',PACKAGE/'src/nr_edit_dx12.cpp',PACKAGE/'src/nr_edit_dx12.hpp',
             PACKAGE/'src/nr_post_residual_dx12.cpp',PACKAGE/'src/nr_post_residual_dx12.hpp',
             PACKAGE/'tests/post_private_sr_gpu.cpp',PACKAGE/'tests/windows_shader_validation.cpp',
             ROOT/'src/interop/nr_ngx_parameter_abi.h',Path(__file__).resolve()]
    sources += sorted((PACKAGE/'shaders').glob('*.hlsl'))+sorted((PACKAGE/'shaders').glob('*.hlsli'))
    hashes={str(f.relative_to(ROOT)):sha(f) for f in sources}
    report={'schema':'owned-post-private-sr-validation-v1','status':'running',
            'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
            'source_dirty':bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT).strip()),
            'source_hashes':hashes,'compiler_sha256':sha(compiler),'runs':[],
            'real_feature1_requested':bool(a.nrchain),'actual_core_injection_tested':False,
            'feature18_tested':False,'game_tested':False,'temporal_quality_tested':False}
    def save():
        (out/'validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    def run(name,cmd,expected=0,needle=None,timeout=240):
        try:
            r=subprocess.run(list(map(str,cmd)),cwd=out,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=timeout)
            code,log=r.returncode,r.stdout
        except subprocess.TimeoutExpired as e:
            code,log=124,(e.stdout or b'')+b'\nTIMEOUT is not a pass\n'
        path=out/(name+'.log');path.write_bytes(log)
        passed=code==expected and (needle is None or needle in log.decode('utf-8','replace'))
        report['runs'].append({'name':name,'command':list(map(str,cmd)),'exit':code,'expected':expected,'passed':passed,'log_sha256':sha(path)})
        save();print(name,code,log.decode('utf-8','replace')[-1600:],flush=True)
        if not passed:raise RuntimeError('Failed test: '+name)
    save()
    try:
        shader=out/'compile-edit-shader.exe'
        run('build-shader-compiler',[compiler,'-std=c++20','-O2','-static','-municode',PACKAGE/'tools/compile_edit_shader.cpp','-ld3dcompiler','-o',shader])
        shader_root=out/'shaders'
        run('compile-shaders',[sys.executable,PACKAGE/'tools/compile_edit_artifacts.py','--compiler',shader,'--source',PACKAGE/'shaders','--output',shader_root])
        module=PACKAGE/'src/nr_post_private_sr_dx12.cpp'
        run('strict-owned-module',[compiler,'-std=c++20','-O2','-Wall','-Wextra','-Werror','-c',module,'-o',out/'owned.o'])
        def compile_host(source,binary,name):
            run(name,[compiler,'-std=c++20','-O2','-static','-municode','-I'+str(ROOT),'-I'+str(PACKAGE/'src'),
                     PACKAGE/'tests/post_private_sr_gpu.cpp',PACKAGE/'src/nr_edit_dx12.cpp',
                     PACKAGE/'src/nr_post_residual_dx12.cpp',source,
                     '-ld3d12','-ldxgi','-ldxguid','-ld3dcompiler','-lole32','-luuid','-o',binary])
        exe=out/'post-private-sr.exe';compile_host(module,exe,'build-host')
        run('warp-fixture-api',[exe,shader_root],needle='PASS owned post-private-SR WARP-fixture-API')
        if a.nvidia:run('nvidia-fixture-api',[exe,shader_root,'--hardware'],needle='PASS owned post-private-SR NVIDIA-fixture-API')
        run('discarded-recording',[exe,shader_root,'--negative','discard'],needle='PASS discarded recording remains retained')
        run('wrong-submission-queue',[exe,shader_root,'--negative','submit'],needle='PASS wrong queue poisons without release')
        text=module.read_text(encoding='utf-8')
        negatives=[('missing-evaluate-proof','if(!sr.evaluated)return {Status::WaitingForSr,true,false,sr.result,index};',
                    'if(false)return {Status::WaitingForSr,true,false,sr.result,index};','creation is not successful reconstruction'),
                   ('missing-post-phase-proof','!i.post_sr_phase_verified||','',
                    'invalid input cannot record or consume private history')]
        for name,old,new,error in negatives:
            if text.count(old)!=1:raise RuntimeError('Review changed negative anchor: '+name)
            altered=out/(name+'.cpp');altered.write_text(text.replace(old,new),encoding='utf-8')
            binary=out/(name+'.exe');compile_host(altered,binary,'build-'+name)
            run(name,[binary,shader_root],expected=1,needle='FAIL '+error)
        if a.nrchain:
            bridge=a.nrchain.resolve(strict=True);runtime=a.sr_runtime.resolve(strict=True)
            if sha(bridge)!='1acf3cbe509a031be1763a8231cd81e6019aa3532368cd0b08a6c17bc94b70a2':
                raise RuntimeError('Unreviewed NRChain bootstrap identity')
            runtimes=out/'runtime';runtimes.mkdir()
            for source,name in ((bridge,'nrchain_nvngx.dll'),(runtime,'nvngx_dlss.dll')):
                destination=runtimes/name;shutil.copyfile(source,destination)
                if sha(destination)!=sha(source):raise RuntimeError('Runtime copy mismatch')
                report.setdefault('private_test_runtimes',{})[name]={'bytes':destination.stat().st_size,'sha256':sha(destination)}
            run('actual-feature1',[exe,shader_root,'--real',runtimes/'nrchain_nvngx.dll'],needle='PASS owned post-private-SR actual-Feature1',timeout=240)
            report['actual_feature1_passed']=True
        report['sources_unchanged']=all(sha(ROOT/f)==h for f,h in hashes.items())
        if not report['sources_unchanged']:raise RuntimeError('Source changed during test')
        report['status']='passed'
    except Exception as e:
        report['status']='failed';report['error']=str(e)
    finally:
        save()
    return 0 if report['status']=='passed' else 1

if __name__=='__main__':raise SystemExit(main())
