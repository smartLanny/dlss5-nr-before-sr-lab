"""Independent opt-in public NGX MFG count/index validation on real NVIDIA.

Uses the unchanged production-consumer host and explicit baseline compilation
provenance. It retains all individual interpolated outputs, not just settings.
"""
import argparse,hashlib,json,os,re,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def main():
    sys.stdout.reconfigure(encoding='utf-8',errors='replace')
    p=argparse.ArgumentParser();p.add_argument('--baseline',type=Path,required=True);p.add_argument('--sdk',type=Path,required=True)
    p.add_argument('--vcvars',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--compile-only',action='store_true')
    a=p.parse_args();baseline_path=a.baseline.resolve(strict=True);baseline=json.loads(baseline_path.read_text(encoding='utf-8'))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);sdk=a.sdk.resolve(strict=True);core=Path(baseline['core'])
    if sha(core/'src/nr_before_sr.cpp')!=baseline['core_source_sha256']:raise RuntimeError('Baseline production Core changed')
    source_manifest=(core.parent/'source-manifest.json').resolve(strict=True);source_manifest_sha256=sha(source_manifest)
    def production_sources():return {f.relative_to(core).as_posix():sha(f) for f in (core/'src').rglob('*') if f.is_file()}
    production_start=production_sources()
    if baseline.get('source_manifest_sha256')!=source_manifest_sha256 or baseline.get('production_sources_unchanged') is not True or baseline.get('production_sources_end')!=production_start:
        raise RuntimeError('Baseline must verify the complete same frozen production source tree')
    bridge_hash=sha(baseline_path.parent/'nrchain_nvngx.dll')
    if baseline.get('production_bridge_sha256')!=bridge_hash or sha(core/'build/nrchain_nvngx.dll')!=bridge_hash:raise RuntimeError('Baseline production bridge mismatch')
    frozen_source=ROOT/'tests/fg_consumer_ngx_host.cpp';source=frozen_source.read_text(encoding='utf-8')
    entry='int wmain(int argc,wchar_t** argv){using namespace host_ngx;using namespace host_fg;'
    if source.count(entry)!=1:raise RuntimeError('Frozen host entry shape changed; no broad rewrite')
    adapted=out/'frozen-consumer-main.inl';adapted.write_text(source.replace(entry,entry.replace('wmain','frozen_consumer_main')),encoding='utf-8')
    source_hashes={str(f):sha(f) for f in [frozen_source,*[ROOT/'tests'/name for name in ['compat_native_mfg_bootstrap.cpp','fg_multiframe_ngx_host.cpp','compat_native_fg_bootstrap.cpp','compat_present_ngx_host.cpp','fg_consumer_hud_fixture.inl','fg_consumer_capture_fixture.inl']]]}
    commands=[]
    def run(command,name,cwd=out,env=None):
        try:r=subprocess.run([str(x) for x in command],cwd=cwd,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=240,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        except subprocess.TimeoutExpired as e:
            r=subprocess.CompletedProcess(command,124,(e.stdout or b'').decode('utf-8','replace') if isinstance(e.stdout,bytes) else e.stdout or '',str(e))
        log=out/(name+'.log');log.write_text(r.stdout+r.stderr,encoding='utf-8');commands.append({'name':name,'exit':r.returncode,'command':list(map(str,command)),'log':str(log),'sha256':sha(log)})
        print((r.stdout+r.stderr)[-1800:],flush=True);return r
    build=out/'build-mfg-bootstrap.cmd';build.write_text('@echo off\ncall "'+str(a.vcvars.resolve(strict=True))+'" x64\nif errorlevel 1 exit /b 1\ncl /nologo /LD /EHsc /O2 /MD /utf-8 /std:c++20 /I"'+str(sdk/'include')+'" "'+str(ROOT/'tests/compat_native_mfg_bootstrap.cpp')+'" /link /OUT:"'+str(out/'ngx-bootstrap.dll')+'" "'+str(sdk/'lib/Windows_x86_64/x64/nvsdk_ngx_d.lib')+'" kernel32.lib user32.lib advapi32.lib ole32.lib\nexit /b %errorlevel%\n',encoding='utf-8')
    code=run(['cmd.exe','/d','/c',build],'compile-mfg-bootstrap').returncode
    binary=out/'native-mfg.exe'
    if not code:
        command=next(c['command'] for c in baseline['commands'] if c['name']=='compile-host')[:]
        command=[x for x in command if x!='-DNR_HOST_FG_CAPTURE=1']
        old_host=next(x for x in command if x.endswith('fg_consumer_ngx_host.cpp'));command[command.index(old_host)]=str(ROOT/'tests/fg_multiframe_ngx_host.cpp')
        command[command.index('-o')+1]=str(binary)
        command.extend(['-I'+str(ROOT/'tests'),'-DNR_MFG_FROZEN_HOST="'+adapted.as_posix()+'"'])
        code=run(command,'compile-mfg-host').returncode
    report={'schema':'real-ngx-mfg-all-indexed-outputs-v1','result':code,'compile_only':a.compile_only,'baseline_validation':str(baseline_path),'baseline_sha256':sha(baseline_path),'core':str(core),'core_source_sha256':baseline['core_source_sha256'],'source_hashes':source_hashes,'adapted_entry_only_sha256':sha(adapted),'commands':commands,'cases':[],'scope':'actual SDK count/index calls and every GPU output; synthetic stationary input and production NR consumer, no game or physical Present cadence claim'}
    def case(count,passes=1,nr=1):
        folder=out/f'generated-{count}-nr{nr}-passes{passes}';folder.mkdir(parents=True,exist_ok=True)
        for filename in ['nvngx_dlssnr.dll','nrchain_nvngx.dll','nvngx_dlssg.dll']:shutil.copy2(baseline_path.parent/filename,folder/filename)
        shutil.copy2(out/'ngx-bootstrap.dll',folder/'ngx-bootstrap.dll')
        env=os.environ.copy();env['NR_TEST_MFG_COUNT']=str(count)
        result=run([binary,folder,passes,100,nr,1],f'run-generated-{count}-nr{nr}-passes{passes}',folder,env)
        data={'generated':count,'multiplier':count+1,'passes':passes,'nr':nr,'exit':result.returncode,'folder':str(folder)}
        if not result.returncode:
            observations=[json.loads(line) for line in (folder/'mfg/outputs.jsonl').read_text().splitlines()]
            data['outputs']=len(observations);data['eligible_by_index']={str(i):sum(x['index']==i and x['disable_interpolation']==0 for x in observations) for i in range(1,count+1)}
            if len(observations)!=24*count or any(value<4 for value in data['eligible_by_index'].values()):raise RuntimeError('Incomplete actual indexed outputs')
            from PIL import Image,PngImagePlugin
            data['captures']={}
            for row in observations:
                f=folder/'mfg'/f"source-{row['source']:03}-index-{row['index']:02}.rgba";pixels=f.read_bytes()
                if len(pixels)!=640*360*4:raise RuntimeError('Incomplete GPU image payload')
                changed=sum(pixels[i:i+3]!=bytes([0xC1,0x5A,0x7E]) for i in range(0,len(pixels),4))
                if not row['disable_interpolation'] and changed<640*360//2:raise RuntimeError('Indexed output retained prefill sentinel; not a generated image')
                data['captures'][f.name]={'sha256':sha(f),'bytes':len(pixels),'disabled':row['disable_interpolation'],'pixels_overwritten_from_prefill':changed}
                if row['source']==23:
                    metadata=PngImagePlugin.PngInfo();metadata.add_text('Description','Actual NVIDIA MFG GPU readback; controlled synthetic gradient; no content editing.')
                    Image.frombytes('RGBA',(640,360),pixels).save(f.with_suffix('.png'),pnginfo=metadata)
        report['cases'].append(data);return result
    if not code and not a.compile_only:
        first=case(1);code=first.returncode
        match=re.search(r'OFFICIAL_MFG_CAPS.*maxResult=0x00000001 maxGenerated=(\d+)',first.stdout)
        maximum=int(match.group(1)) if match else None;report['runtime_max_generated']=maximum
        if not code and maximum and maximum<=16:
            for count in range(2,maximum+1):
                code=case(count).returncode
                if code:break
            if not code and maximum>1:
                code=case(maximum,4).returncode
                if not code:code=case(maximum,1,0).returncode
        report['all_reported_modes_completed']=bool(maximum and maximum<=16 and not code)
    report['source_unchanged']=all(sha(f)==digest for f,digest in source_hashes.items())
    if not report['source_unchanged']:code=1
    production_end=production_sources()
    report.update(source_manifest_sha256=source_manifest_sha256,production_sources_start=production_start,production_sources_end=production_end,
        production_sources_unchanged=production_start==production_end and sha(source_manifest)==source_manifest_sha256,production_bridge_sha256=bridge_hash)
    if not report['production_sources_unchanged']:code=1
    report['result']=code;(out/'validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8');return code
if __name__=='__main__':raise SystemExit(main())
