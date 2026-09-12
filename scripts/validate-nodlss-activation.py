"""Real ReShade DXGI proxy + complete Core, with no native SR/RR/FG calls.

Only callbacks registered with the real ReShade runtime activate observation.
"""
import argparse,hashlib,json,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def main():
    sys.stdout.reconfigure(encoding='utf-8',errors='replace');p=argparse.ArgumentParser()
    p.add_argument('--baseline',type=Path,required=True);p.add_argument('--core',type=Path,required=True);p.add_argument('--reshade-dll',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--compile-only',action='store_true');p.add_argument('--case',choices=['single','all'],default='single');a=p.parse_args()
    baseline_path=a.baseline.resolve(strict=True);baseline=json.loads(baseline_path.read_text(encoding='utf-8'));old_core=Path(baseline['core']);core=a.core.resolve(strict=True)
    source_manifest=(core.parent/'source-manifest.json').resolve(strict=True);source_manifest_sha256=sha(source_manifest)
    def production_sources():return {f.relative_to(core).as_posix():sha(f) for f in (core/'src').rglob('*') if f.is_file()}
    production_start=production_sources()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);binary=out/'no-dlss-activation.exe';commands=[]
    host=ROOT/'tests/compat_nodlss_ngx_host.cpp';host_hash=sha(host)
    host_sources={str(f.relative_to(ROOT)):sha(f) for f in [host,ROOT/'tests/compat_present_ngx_host.cpp']}
    def run(command,name,cwd=out):
        try:r=subprocess.run(list(map(str,command)),cwd=cwd,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=240,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        except subprocess.TimeoutExpired as e:r=subprocess.CompletedProcess(command,124,(e.stdout or b'').decode('utf-8','replace') if isinstance(e.stdout,bytes) else e.stdout or '',str(e))
        log=out/(name+'.log');log.write_text(r.stdout+r.stderr,encoding='utf-8');commands.append({'name':name,'exit':r.returncode,'command':list(map(str,command)),'log':str(log),'sha256':sha(log)});print((r.stdout+r.stderr)[-4000:],flush=True);return r.returncode
    command=next(c['command'] for c in baseline['commands'] if c['name']=='compile-host')[:]
    command=[x for x in command if x not in ['-DNR_HOST_NATIVE_FG=1','-DNR_HOST_FG_CAPTURE=1']]
    command=[x.replace(str(old_core),str(core)).replace(old_core.as_posix(),core.as_posix()) for x in command]
    old_host=next(x for x in command if x.endswith('fg_consumer_ngx_host.cpp'));command[command.index(old_host)]=str(host);command[command.index('-o')+1]=str(binary)
    command.append('-ldbghelp')
    code=run(command,'compile-no-dlss-host')
    if not code:
        shutil.copy2(a.reshade_dll.resolve(strict=True),out/'dxgi.dll')
        for filename in ['nvngx_dlssnr.dll']:shutil.copy2(baseline_path.parent/filename,out/filename)
        shutil.copy2(core/'build/nrchain_nvngx.dll',out/'nrchain_nvngx.dll')
        (out/'ReShade.ini').write_text('[GENERAL]\nNoEffectCache=1\nNoDebugInfo=1\n[OVERLAY]\nTutorialProgress=4\nShowClock=0\nShowFPS=0\nShowFrameTime=0\n',encoding='utf-8')
        if not a.compile_only:
            for passes in ([1,4] if a.case=='all' else [1]):
                code=run([binary,out,passes],f'run-no-dlss-{passes}')
                if code:break
    production_end=production_sources();unchanged=production_start==production_end and sha(source_manifest)==source_manifest_sha256
    host_unchanged=all(sha(ROOT/name)==digest for name,digest in host_sources.items())
    if not unchanged or not host_unchanged:code=1
    report={'schema':'real-reshade-no-native-dlss-activation-v1','result':code,'compile_only':a.compile_only,'case':a.case,'core':str(core),'core_source_sha256':sha(core/'src/nr_before_sr.cpp'),'source_manifest_sha256':source_manifest_sha256,'production_sources_start':production_start,'production_sources_end':production_end,'production_sources_unchanged':unchanged,'host_source_sha256':host_hash,'host_unchanged':sha(host)==host_hash,'reshade_sha256':sha(a.reshade_dll),'commands':commands,'scope':'real ReShade DXGI callbacks, real D3D12 output clear/submit/Present and real NGX NR; no native SR/RR/FG feature, no manual source or hook admission, no game claim'}
    report['production_bridge_sha256']=sha(out/'nrchain_nvngx.dll') if (out/'nrchain_nvngx.dll').is_file() else None
    report['host_sources_at_compile']=host_sources;report['host_sources_unchanged']=host_unchanged
    report['outputs']={f.name:{'sha256':sha(f),'bytes':f.stat().st_size} for f in out.glob('no-dlss-*.rgba')}
    if report['outputs']:
        from PIL import Image,PngImagePlugin
        for f in out.glob('no-dlss-*.rgba'):
            pixels=f.read_bytes()
            if len(pixels)!=640*360*4:code=1;continue
            metadata=PngImagePlugin.PngInfo();metadata.add_text('Description','Actual NVIDIA GPU readback: cold no-DLSS controlled Clear output, real ReShade callbacks and production NR. No content editing.')
            Image.frombytes('RGBA',(640,360),pixels).save(f.with_suffix('.png'),pnginfo=metadata)
    report['result']=code
    (out/'validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8');return code
if __name__=='__main__':raise SystemExit(main())
