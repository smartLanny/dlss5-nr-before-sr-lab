"""Opt-in real NVIDIA/NGX host, using an explicit frozen complete Core tree.

No game installation or source patching. The official SDK bootstrap, real
signed Feature18 and production Present Build/Bootstrap/Frame remain real.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
from temporal_test_objects import objects_for

ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def main():
    sys.stdout.reconfigure(encoding='utf-8',errors='replace')
    p=argparse.ArgumentParser()
    p.add_argument('--core',type=Path,required=True)
    p.add_argument('--compiler',type=Path,required=True)
    p.add_argument('--reshade',type=Path,required=True)
    p.add_argument('--sdk',type=Path)
    p.add_argument('--vcvars',type=Path)
    p.add_argument('--nr-runtime',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--case',choices=['single','multi','all'],default='single')
    p.add_argument('--compile-only',action='store_true')
    p.add_argument('--cold-bootstrap',action='store_true',help='Require the production bridge to bootstrap without any host NGX Init or bootstrap DLL.')
    p.add_argument('--consumer',action='store_true',help='Exercise the production FG-input NR consumer; native FG is explicitly a placeholder.')
    p.add_argument('--native-fg',action='store_true',help='Use the official native DLSSG Create/Evaluate helper and read back both outputs.')
    p.add_argument('--dlssg-sdk',type=Path)
    p.add_argument('--fg-runtime',type=Path)
    p.add_argument('--quadrants',action='store_true',help='Also run fixed 24-frame NR/FG enablement controls with native FG.')
    p.add_argument('--hudless',action='store_true',help='Also run actual HUDless/full recomposition with no alpha and explicit alpha 0/0.5/1.')
    p.add_argument('--failure-controls',action='store_true',help='Inject one NR opaque API failure and one private allocation refusal, explicitly test-only.')
    p.add_argument('--capture',action='store_true',help='Require actual completed per-layer pre-FG NRF and metadata export.')
    a=p.parse_args();core=a.core.resolve(strict=True);out=a.output.resolve()
    if out.exists() and any(out.iterdir()):p.error('--output must be empty to preserve earlier evidence')
    out.mkdir(parents=True,exist_ok=True)
    source_manifest=(core.parent/'source-manifest.json').resolve(strict=True);source_manifest_sha256=sha(source_manifest)
    def production_sources():return {f.relative_to(core).as_posix():sha(f) for f in (core/'src').rglob('*') if f.is_file()}
    production_start=production_sources()
    if a.cold_bootstrap and (a.consumer or a.native_fg):p.error('--cold-bootstrap currently covers the Present host only')
    if not a.cold_bootstrap and (not a.vcvars or (not a.sdk and not a.native_fg)):
        p.error('non-cold validation requires --vcvars and --sdk (or --dlssg-sdk for native FG)')
    compiler=a.compiler.resolve(strict=True);sdk=None;reshade=a.reshade.resolve(strict=True)
    face_receipt=core/'build/face-backend.json'
    face_build=json.loads(face_receipt.read_text(encoding='utf-8-sig'))
    if face_build.get('schema')!='nr-face-build-v1' or type(face_build.get('enabled')) is not bool:
        p.error('the built Core must have a valid face-backend.json receipt')
    face_backend=int(face_build['enabled'])
    commands=[]
    host_sources={str(f.relative_to(ROOT)):sha(f) for f in [ROOT/'tests/compat_present_ngx_host.cpp',ROOT/'tests/compat_present_ngx_bootstrap.cpp',ROOT/'tests/fg_consumer_ngx_host.cpp',ROOT/'tests/fg_consumer_hud_fixture.inl',ROOT/'tests/fg_consumer_capture_fixture.inl',ROOT/'tests/compat_native_fg_bootstrap.cpp'] if f.is_file()}
    def run(cmd,name,timeout=180):
        try:
            proc=subprocess.run([str(x)for x in cmd],cwd=out,capture_output=True,text=True,encoding='utf-8',errors='replace',
                timeout=timeout,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        except subprocess.TimeoutExpired as error:
            def decode(value):return value.decode('utf-8','replace') if isinstance(value,bytes) else value or ''
            proc=subprocess.CompletedProcess(cmd,124,decode(error.stdout),decode(error.stderr)+'\nBounded validation timed out; not a pass.\n')
        log=out/(name+'.log');log.write_text(proc.stdout+proc.stderr,encoding='utf-8')
        commands.append({'name':name,'exit_code':proc.returncode,'command':[str(x)for x in cmd], 'log':str(log),'sha256':sha(log)})
        print(proc.stdout[-1500:],end='',flush=True)
        if proc.returncode:print(proc.stderr[-10000:],flush=True)
        return proc.returncode
    if a.native_fg:
        if not a.dlssg_sdk or not a.fg_runtime:p.error('--native-fg requires --dlssg-sdk and --fg-runtime')
        a.consumer=True;sdk=a.dlssg_sdk.resolve(strict=True)/'include'
    bootstrap=ROOT/('tests/compat_native_fg_bootstrap.cpp' if a.native_fg else 'tests/compat_present_ngx_bootstrap.cpp')
    code=0
    if not a.cold_bootstrap:
        if not a.native_fg:sdk=a.sdk.resolve(strict=True)
        sdk_lib=(a.dlssg_sdk.resolve(strict=True)/'lib/Windows_x86_64/x64/nvsdk_ngx_d.lib') if a.native_fg else sdk/'libs/nvsdk_ngx_d.lib'
        cmd=out/'build-bootstrap.cmd'
        cmd.write_text('@echo off\ncall "'+str(a.vcvars.resolve(strict=True))+'" x64\nif errorlevel 1 exit /b 1\n'+
            'cl /nologo /LD /EHsc /O2 /MD /utf-8 /std:c++20 /I"'+str(sdk)+'" "'+str(bootstrap)+'" /link /OUT:"'+
            str(out/'ngx-bootstrap.dll')+'" "'+str(sdk_lib)+'" kernel32.lib user32.lib advapi32.lib ole32.lib\n'+
            'exit /b %errorlevel%\n',encoding='utf-8')
        code=run(['cmd.exe','/d','/c',cmd],'compile-bootstrap')
    host_source=ROOT/('tests/fg_consumer_ngx_host.cpp' if a.consumer else 'tests/compat_present_ngx_host.cpp')
    binary=out/('fg-consumer-ngx.exe' if a.consumer else 'compat-present-ngx.exe')
    if not code:
        common=['-std=c++20','-O1','-DNDEBUG','-DUNICODE','-D_UNICODE','-D_WIN64',f'-DNR_FACE_BACKEND={face_backend}','-DNR_CODEC_PRECOMPILED=1',
            '-DNR_PRESENT_NGX_CORE="'+(core/'src/nr_before_sr.cpp').as_posix()+'"','-ffunction-sections','-fdata-sections',
            '-I'+str(core/'build'),'-I'+str(core/'third_party/minhook/include'),'-I'+str(reshade/'include'),'-I'+str(reshade/'deps/imgui')]
        if a.native_fg:common.append('-DNR_HOST_NATIVE_FG=1')
        if a.cold_bootstrap:common.append('-DNR_HOST_COLD_BOOTSTRAP=1')
        if a.capture:common.append('-DNR_HOST_FG_CAPTURE=1')
        objs=[core/'build/obj'/x for x in ['buffer.o','hook.o','trampoline.o','hde64.o','nr_dual_spatial_d3d12.o','nr_face_detector.o','nr_face_worker.o']]+objects_for(core/'src/nr_before_sr.cpp')
        code=run([compiler,*common,'-static','-municode','-Wl,--gc-sections',host_source,*objs,
            '-o',binary,'-lkernel32','-luser32','-ld3d12','-ldxgi','-ldxguid','-lole32','-luuid','-ld3dcompiler','-lshell32','-lbcrypt'],'compile-host',240)
    if not code:
        for src,name in [(a.nr_runtime.resolve(strict=True),'nvngx_dlssnr.dll'),(core/'build/nrchain_nvngx.dll','nrchain_nvngx.dll')]:
            shutil.copy2(src,out/name)
        if a.native_fg:shutil.copy2(a.fg_runtime.resolve(strict=True),out/'nvngx_dlssg.dll')
        if not a.compile_only:
            cases=[(1,100)] if a.case=='single' else [(2,100)] if a.case=='multi' else [(1,100),(2,100),(3,100),(4,100),(5,100),(2,50),(3,50),(4,75),(5,50)]
            for passes,scale in cases:
                code=run([binary,out,passes,scale],f'run-{passes}-{scale}',240)
                if code:break
            if not code and a.native_fg and a.quadrants:
                for nr,fg in [(0,0),(1,0),(0,1)]:
                    code=run([binary,out,1,100,nr,fg],f'quadrant-nr{nr}-fg{fg}',240)
                    if code:break
            if not code and a.hudless:
                for passes,mode in [(1,1),(1,2),(4,1),(4,2)]:
                    code=run([binary,out,passes,100,1,1,mode],f'hudless-{passes}-{mode}',240)
                    if code:break
            if not code and a.failure_controls:
                for mode in [1,2]:
                    code=run([binary,out,4,100,1,1,0,mode],f'fault-control-{mode}',240)
                    if code:break
    result={'schema':'real-ngx-present-host-v1','core':str(core),'core_source_sha256':sha(core/'src/nr_before_sr.cpp'),
        'host_source_sha256':sha(host_source),'bootstrap_source_sha256':sha(bootstrap),
        'result':code,'compile_only':a.compile_only,'case':a.case,'commands':commands,
        'scope':('real NVIDIA, official NGX bootstrap, signed Feature18 and production FG input consumer; artificial input, native FG call placeholder, no game/FG output claim' if a.consumer else 'real NVIDIA, official NGX bootstrap, signed Feature18 and production Present internals; artificial color input, no game/FG claim')}
    result['artifacts']={name:sha(out/name) for name in [binary.name,'ngx-bootstrap.dll','nvngx_dlssnr.dll','nrchain_nvngx.dll'] if (out/name).is_file()}
    result['cold_bootstrap']=a.cold_bootstrap
    result['face_build']={'receipt':str(face_receipt),'sha256':sha(face_receipt),'enabled':bool(face_backend)}
    result['linked_objects']={str(f):sha(f) for f in objs} if 'objs' in locals() else {}
    if a.cold_bootstrap:
        result['scope']='real NVIDIA; initially absent NGX core; production nrchain official loader and signed Feature18; artificial input; no game, no host Init and no Feature1'
        if (out/'ngx-bootstrap.dll').exists():raise RuntimeError('Cold scenario must not contain a host bootstrap DLL')
    result['present_source']={name:sha(core/'src'/name) for name in ['nr_compat_present_resources.inl','nr_compat_present_models.inl','nr_compat_present_events.inl','nr_compat_present_runtime.inl','nr_present_capture.inl'] if (core/'src'/name).is_file()}
    result['host_sources_at_compile']=host_sources
    result['host_sources_unchanged']=all(sha(ROOT/name)==digest for name,digest in host_sources.items())
    if not result['host_sources_unchanged']:code=1;result['result']=code
    if not a.consumer:
        result['present_nrf_captures']={f.name:{'sha256':sha(f),'bytes':f.stat().st_size} for f in out.glob('capture-p*-layer*.nrf')}
        result['present_nrf_metadata']={f.name:{'sha256':sha(f),'content':json.loads(f.read_text(encoding='utf-8'))} for f in out.glob('capture-p*-layer*.json')}
        result['present_capture_cases_checked']=0
        if not a.compile_only and not code:
            expected=[(1,100)] if a.case=='single' else [(2,100)] if a.case=='multi' else [(1,100),(2,100),(3,100),(4,100),(5,100),(2,50),(3,50),(4,75),(5,50)]
            for passes,scale in expected:
                prefix=f'capture-p{passes}-s{scale}-layer'
                paths=[out/f'{prefix}{layer}.json' for layer in range(1,passes+1)]
                if not all(path.is_file() and path.with_suffix('.nrf').is_file() and path.with_suffix('.nrf').stat().st_size>6080 for path in paths):
                    code=1;break
                layers=[json.loads(path.read_text(encoding='utf-8')) for path in paths]
                valid=all(layer.get('schema')=='present-nr-observed-frame-v1' and layer.get('complete') is True and
                    layer.get('captured_auxiliary_layer')==index+1 and layer.get('request')==layers[0].get('request') and
                    layer.get('parent_instance')==layers[0].get('parent_instance') and layer.get('parent_evaluation')==layers[0].get('parent_evaluation')
                    for index,layer in enumerate(layers))
                if not valid:code=1;break
                result['present_capture_cases_checked']+=1
    if a.consumer:
        result['consumer_source']={f.name:sha(f) for f in (core/'src').glob('nr_fg*')}
        result['nrf_captures']={f.name:{'sha256':sha(f),'bytes':f.stat().st_size} for f in out.glob('fg-capture-*.nrf')}
        result['nrf_metadata']={f.name:{'sha256':sha(f),'content':json.loads(f.read_text(encoding='utf-8'))} for f in out.glob('fg-capture-*.json')}
        result['nrf_requests_checked']=0
        for manifest in out.glob('fg-capture-*-complete.json'):
            complete=json.loads(manifest.read_text(encoding='utf-8'));prefix=manifest.name.removesuffix('-complete.json')
            layers=[json.loads(f.read_text(encoding='utf-8')) for f in out.glob(prefix+'-layer*.json')]
            valid=complete.get('schema')=='pre-fg-nr-observed-capture-v1' and complete.get('complete') is True and len(layers)==complete.get('layers')
            valid=valid and sorted(layer.get('layer') for layer in layers)==list(range(1,len(layers)+1))
            valid=valid and all(layer.get('schema')=='pre-fg-nr-observed-layer-v1' and layer.get('complete') is True and layer.get('request')==complete.get('request') and layer.get('source')==complete.get('source') and layer.get('requested_layers')==complete.get('layers') for layer in layers)
            if not valid:code=1;result['result']=code
            else:result['nrf_requests_checked']+=1
    if a.native_fg:
        result['schema']='real-ngx-native-fg-host-v1'
        result['scope']='real NVIDIA, official SDK Feature11 Create/Evaluate and interpolation eligibility output, real signed Feature18 Core consumer; artificial fixed 24-frame input, no game or physical Present cadence claim'
        result['artifacts']['nvngx_dlssg.dll']=sha(out/'nvngx_dlssg.dll') if (out/'nvngx_dlssg.dll').is_file() else None
        result['captures']={f.name:{'sha256':sha(f),'bytes':f.stat().st_size} for f in out.glob('*.rgba')}
        from PIL import Image,PngImagePlugin
        for f in out.glob('*.rgba'):
            pixels=f.read_bytes()
            if len(pixels)!=640*360*4:continue
            metadata=PngImagePlugin.PngInfo();metadata.add_text('Description','Actual NVIDIA GPU readback; controlled synthetic gradient, no game scene. RGBA8 bytes encoded without color or content editing.')
            Image.frombytes('RGBA',(640,360),pixels).save(f.with_suffix('.png'),pnginfo=metadata)
        result['png_previews']={f.name:sha(f) for f in out.glob('*.png')}
        if a.quadrants and not code:
            def changed(a,b):return sum(a[i:i+3]!=b[i:i+3] for i in range(0,len(a),4))
            comparison={}
            for name in ['ConsumerBackbuffer','OutputReal','OutputInterpolated']:
                off=out/f'{name}-p1-s100-nr0-fg1.rgba';on=out/f'{name}-p1-s100-nr1-fg1.rgba'
                comparison[name+'_NRoff_vs_NRon_pixels']=changed(off.read_bytes(),on.read_bytes())
            comparison['NRoff_consumer_FGoff_vs_FGon_pixels']=changed((out/'ConsumerBackbuffer-p1-s100-nr0-fg0.rgba').read_bytes(),(out/'ConsumerBackbuffer-p1-s100-nr0-fg1.rgba').read_bytes())
            result['four_quadrant_pixel_comparison']=comparison
            if any(comparison[name+'_NRoff_vs_NRon_pixels']==0 for name in ['ConsumerBackbuffer','OutputReal','OutputInterpolated']):code=1
            if comparison['NRoff_consumer_FGoff_vs_FGon_pixels']!=0:code=1
            result['result']=code
    production_end=production_sources()
    result.update(source_manifest_sha256=source_manifest_sha256,production_sources_start=production_start,production_sources_end=production_end,
        production_sources_unchanged=production_start==production_end and sha(source_manifest)==source_manifest_sha256,
        production_bridge_sha256=sha(out/'nrchain_nvngx.dll') if (out/'nrchain_nvngx.dll').is_file() else None)
    if not result['production_sources_unchanged']:code=1
    result['result']=code
    (out/'validation.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    return code
if __name__=='__main__':raise SystemExit(main())
