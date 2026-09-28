"""Actual Core private SR with native Feature1/13 and optional real Feature18 chain."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,time
from temporal_test_objects import objects_for
ROOT=Path(__file__).resolve().parents[1]
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('core','compiler','reshade','sr-runtime','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--reshade-runtime',type=Path,help='Optional actual ReShade d3d12.dll; isolated proxy/raw device compatibility test, no Core addon')
    p.add_argument('--nr-runtime',type=Path)
    p.add_argument('--rr-runtime',type=Path)
    p.add_argument('--layers',type=int,choices=range(6),default=0)
    p.add_argument('--percent',type=int,choices=(25,50,100),default=50)
    p.add_argument('--native-rr',action='store_true')
    p.add_argument('--private-sr-off',action='store_true',help='Isolated native/ordinary NR control; performs no private Feature1 work')
    p.add_argument('--odd',action='store_true',help='Add one pixel to each native render/output dimension; N uses floor percent')
    p.add_argument('--compile-only',action='store_true')
    p.add_argument('--frames',type=int,default=6,help='Final Z frames, 2..256; use at least 32 for a stable timing window')
    a=p.parse_args()
    if not 2<=a.frames<=256:p.error('--frames must be 2..256')
    if a.layers and not a.nr_runtime:p.error('--layers requires explicit --nr-runtime')
    if a.native_rr and not a.rr_runtime:p.error('--native-rr requires explicit --rr-runtime')
    core=a.core.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    source=core/'src/nr_before_sr.cpp';manifest_path=core.parent/'source-manifest.json'
    manifest=json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    for rel,digest in manifest['files'].items():
        f=(manifest_path.parent/rel).resolve()
        if not f.is_relative_to(manifest_path.parent.resolve()) or sha(f)!=digest:raise ValueError('Generated source identity mismatch: '+rel)
    original=ROOT/'experiments/nr-edit/tests/private_sr_ngx_host.cpp'
    host=original.read_text(encoding='utf-8').split('void NeutralAndTinyComposition(',1)[0]+'\n}\n'
    # Preserve the existing host helpers verbatim, excluding its independent
    # experiment Run/main; the new fixture calls the actual product entry points.
    fixture=out/'private_sr_product_real.cpp';shutil.copy2(ROOT/'tests/private_sr_product_real.cpp',fixture)
    wrapper=out/'host.cpp';wrapper.write_text('#define NR_PRIVATE_SR_CORE "'+source.as_posix()+'"\n'+host+
        '#include "'+fixture.as_posix()+'"\n',encoding='utf-8')
    build=core/'build';objects=[build/'obj'/n for n in ('buffer.o','hook.o','trampoline.o','hde64.o','nr_dual_spatial_d3d12.o','nr_face_detector.o','nr_face_worker.o','nr_aux_shader_runtime.o')]+objects_for(source)
    report={'schema':'actual-product-private-sr-v2','result':'running','gpu_execution':'not-run','compile_only':a.compile_only,'private_sr_enabled':not a.private_sr_off,'source_commit':manifest['source_commit'],
        'generated_source':str(source),'generated_source_sha256':sha(source),'manifest_path':str(manifest_path),'manifest_sha256':sha(manifest_path),'fixture_sha256':sha(fixture),
        'host_helpers_sha256':sha(original),'wrapper_sha256':sha(wrapper),'compiler_sha256':sha(a.compiler),'commands':[],
        'linked_objects':{str(o):sha(o) for o in objects},'game':False,'feature18':bool(a.layers),'nr_layers':a.layers,'frames':a.frames,'percent':a.percent,'native_feature':13 if a.native_rr else 1,
        'native_render_extent':[x+int(a.odd) for x in ([1280,720] if a.native_rr else [320,192])],
        'native_output_extent':[x+int(a.odd) for x in ([1920,1080] if a.native_rr else [640,384])],
        'timing':'CPU recording includes native SR; GPU timestamps isolate guides, complete NR-chain work (including codec and final Z copy), and private SR; none is a complete game frame. Synthetic native frames plus optional real 1-5 layer Feature18; not a game benchmark.'}
    def save(): (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    def run(name,args,timeout):
        start=time.monotonic();r=subprocess.run(list(map(str,args)),cwd=out,capture_output=True,timeout=timeout)
        (out/(name+'.log')).write_bytes(r.stdout+r.stderr);report['commands'].append({'name':name,'args':list(map(str,args)),'exit':r.returncode,'seconds':time.monotonic()-start})
        save();print((r.stdout+r.stderr).decode('utf-8','replace')[-9000:],flush=True);return r.returncode
    for s,n in ((build/'nrchain_nvngx.dll','nrchain_nvngx.dll'),(a.sr_runtime,'nvngx_dlss.dll')):shutil.copy2(s,out/n)
    for runtime,name in [(a.nr_runtime,'nvngx_dlssnr.dll'),(a.rr_runtime,'nvngx_dlssd.dll')]:
        if runtime:shutil.copy2(runtime,out/name)
    if a.reshade_runtime:
        shutil.copy2(a.reshade_runtime.resolve(strict=True),out/'d3d12.dll')
    report['reshade_proxy_mode']=bool(a.reshade_runtime)
    report['object_domains']=({'init':'proxy device','native_parent':'proxy command list','core_resources':'raw device','private_sr':'actual product live original alias; raw Core recording and resources','feature18':'proxy command list','submission_and_reset':'raw list with exact Core receipts'} if a.reshade_runtime else {'all':'native raw D3D12'})
    report['core_addon_loaded']=False
    report['runtime_sha256']={f.name:sha(f) for f in out.glob('*.dll')};save()
    flags=['-std=c++20','-O1','-DNDEBUG','-DUNICODE','-D_UNICODE','-D_WIN64','-DNR_FACE_BACKEND=1','-DNR_CODEC_PRECOMPILED=1','-DNR_AUX_PRECOMPILED=1','-DNR_AUX_TIMING=1',
        '-ffunction-sections','-fdata-sections','-static','-municode','-Wl,--gc-sections','-I'+str(build),'-I'+str(core/'third_party/minhook/include'),'-I'+str(a.reshade/'include'),'-I'+str(a.reshade/'deps/imgui')]
    if a.reshade_runtime:flags.append('-DNR_TEST_RESHADE_RUNTIME=1')
    try:
        result=run('compile',[a.compiler,*flags,wrapper,*objects,'-o',out/'product-real.exe','-lkernel32','-luser32','-ld3d12','-ldxgi','-ldxguid','-lole32','-luuid','-ld3dcompiler','-lshell32','-lbcrypt'],240)
        if not result and not a.compile_only:
            report['gpu_execution']='running';save()
            result=run('real-feature1',[out/'product-real.exe',out/'nrchain_nvngx.dll',a.layers,a.percent,int(a.native_rr),a.frames,int(a.odd),int(not a.private_sr_off)],240)
            report['gpu_execution']='passed' if not result else 'failed'
        report['result']='passed' if not result else 'failed'
        if (out/'real-feature1.log').is_file():
            import re
            log=(out/'real-feature1.log').read_text(encoding='utf-8',errors='replace')
            report['metrics']=[dict((k,float(v) if '.' in v else int(v)) for k,v in re.findall(r'(\w+)=([0-9.]+)',line)) for line in log.splitlines() if line.startswith('METRIC ')]
        report['captures_sha256']={f.name:sha(f) for pattern in ('*.fp16','*.fp32') for f in out.glob(pattern)}
        capture_manifest=out/'actual-core-same-frame.json'
        if capture_manifest.is_file():
            report['same_frame']=json.loads(capture_manifest.read_text())
            report['same_frame_manifest_sha256']=sha(capture_manifest)
        report['cost_sample_exclusion']='Final diagnostic-capture frame and all creation frames are excluded from stable cost comparisons.'
        report['binary_sha256']=sha(out/'product-real.exe') if (out/'product-real.exe').is_file() else None
        save();return result
    except Exception as e:
        report['result']='failed';report['error']=str(e)
        if report['gpu_execution']=='running':report['gpu_execution']='failed'
        save();raise
if __name__=='__main__':raise SystemExit(main())
