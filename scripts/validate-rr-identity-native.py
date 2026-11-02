#!/usr/bin/env python3
"""Independent original Feature13 A/A2/B histories with the complete-Core identity adapter."""
from pathlib import Path
import argparse, difflib, hashlib, json, re, shutil, subprocess, time
from temporal_test_objects import objects_for
ROOT=Path(__file__).resolve().parents[1]
PINNED={
    'nrchain_nvngx.dll':'1acf3cbe509a031be1763a8231cd81e6019aa3532368cd0b08a6c17bc94b70a2',
    'nvngx_dlss.dll':'22c4660249da5b352cde0ce4d95178c3761223dd6ac096d0b88e5c03d7a5cdd1',
    'nvngx_dlssd.dll':'50eb21e654a1fd12403e000f5ac9c2488bf9149930c028e73196f3f1d47d10a9'}
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def ps_quote(value):return "'"+str(value).replace("'","''")+"'"
def save(out,report):(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
def run(out,report,name,args,timeout,cwd=None):
    start=time.monotonic();command=list(map(str,args))
    try:r=subprocess.run(command,cwd=cwd or out,capture_output=True,timeout=timeout)
    except subprocess.TimeoutExpired as e:
        (out/(name+'.log')).write_bytes((e.stdout or b'')+(e.stderr or b''));report['commands'].append({'name':name,'args':command,'exit':None,'timeout':timeout});save(out,report);raise
    (out/(name+'.log')).write_bytes(r.stdout+r.stderr);report['commands'].append({'name':name,'args':command,'exit':r.returncode,'seconds':time.monotonic()-start})
    save(out,report);print((r.stdout+r.stderr).decode('utf-8','replace')[-10000:],flush=True);return r
def compare(out,first,second):
    import numpy as np
    rows=[];rgb_squares=rgb_abs=0.;rgb_count=changed=0;max_rgb=0.;max_alpha=0.
    for index in range(16,32):
        a=np.fromfile(out/first/f'output-{index}.fp16',dtype='<f2').reshape(1080,1920,4)
        b=np.fromfile(out/second/f'output-{index}.fp16',dtype='<f2').reshape(a.shape)
        if not np.isfinite(a).all() or not np.isfinite(b).all():raise ValueError('Nonfinite actual native output')
        d=np.abs(a.astype(np.float32)-b.astype(np.float32));rgb=d[:,:,:3];alpha=d[:,:,3]
        count=rgb.size;squares=float(np.sum(rgb.astype(np.float64)**2));absolute=float(np.sum(rgb,dtype=np.float64))
        bit_changed=int(np.count_nonzero(a.view('<u2')!=b.view('<u2')))
        row={'frame':index,'max_abs_rgb':float(rgb.max()),'mean_abs_rgb':absolute/count,'rms_rgb':(squares/count)**.5,
             'max_abs_alpha':float(alpha.max()),'changed_rgba_half_components':bit_changed,'all_rgba_bits_equal':bit_changed==0}
        rows.append(row);rgb_squares+=squares;rgb_abs+=absolute;rgb_count+=count;changed+=bit_changed;max_rgb=max(max_rgb,row['max_abs_rgb']);max_alpha=max(max_alpha,row['max_abs_alpha'])
    return {'pair':[first,second],'observed_frames':16,'max_abs_rgb':max_rgb,'mean_abs_rgb':rgb_abs/rgb_count,
        'rms_rgb':(rgb_squares/rgb_count)**.5,'max_abs_alpha':max_alpha,'changed_rgba_half_components':changed,
        'all_observed_rgba_bits_equal':changed==0,'frames':rows}
def execute(out):
    report=json.loads((out/'validation.json').read_text())
    if report['result']!='compiled-not-run':raise ValueError('Prepared receipt is not an unstarted compiled host')
    for p,digest in report['immutable_sha256'].items():
        if sha(p)!=digest:raise ValueError('Prepared input/object/binary/runtime changed: '+p)
    report['result']='running-native';save(out,report)
    try:
        for mode in ('A','A2','B'):
            case=out/mode;case.mkdir(exist_ok=False)
            for name in PINNED:shutil.copy2(out/name,case/name)
            shutil.copy2(out/'rr-identity-native.exe',case/'rr-identity-native.exe')
            r=run(out,report,'native-'+mode,[case/'rr-identity-native.exe',mode,case/'nrchain_nvngx.dll'],300,cwd=case)
            log=(r.stdout+r.stderr).decode('utf-8','replace');record={'mode':mode,'host_exit':r.returncode,'input_sha256':{},'contract_sha256':{},'output_sha256':{},'guide_sha256':{},'frames':[]}
            for kind,target in [('INPUT','input_sha256'),('CONTRACT','contract_sha256'),('OUTPUT','output_sha256')]:
                record[target]={int(i):digest for i,digest in re.findall(kind+r'_SHA256 index=(\d+) value=([0-9a-f]{64})',log)}
            record['guide_sha256']=dict(re.findall(r'GUIDE_SHA256 role=(\S+) value=([0-9a-f]{64})',log))
            record['native_calls']=[{'index':int(i),'result_hex':value} for i,value in re.findall(r'NATIVE_CALL index=(\d+) feature=13 result=([0-9a-f]{8})',log)]
            for values in re.findall(r'FRAME index=(\d+) observe=(\d+) reason=(\d+) result=([0-9a-f]{8}) copied=(\d+) substituted=(\d+) restored=(\d+) calls=(\d+)',log):
                names=('index','observe','reason','result_hex','copied','substituted','restored','calls');record['frames'].append({k:v if k=='result_hex' else int(v) for k,v in zip(names,values)})
            record['create_results_hex']=re.findall(r'NATIVE_CREATE calls=1 feature=13 result=([0-9a-f]{8})',log)
            record['bootstrap_results_hex']=re.findall(r'BOOTSTRAP calls=1 result=([0-9a-f]{8})',log)
            record['identities']=re.findall(r'IDENTITY .*',log)
            record['actual_module_path']=next(iter(re.findall(r'NATIVE_MODULE (.*)',log)),None)
            record['captures_sha256']={p.name:sha(p) for p in sorted(case.glob('output-*.fp16'))}
            record['passed']=r.returncode==0 and f'PASS native identity {mode}:' in log
            report['cases'].append(record);report['nvidia_ngx']=True;save(out,report)
            if not record['passed']:raise RuntimeError('Actual native cohort failed: '+mode+'; retained log, no native retry or Feature1 fallback')
            if any(len(record[key])!=32 for key in ('input_sha256','contract_sha256','output_sha256','frames','native_calls','identities')):raise ValueError('Missing complete per-frame native receipt: '+mode)
            if record['create_results_hex']!=['00000001'] or record['bootstrap_results_hex']!=['00000001']:raise ValueError('Not exactly one successful original init/Create')
            if [v['index'] for v in record['native_calls']]!=list(range(1,33)) or any(v['result_hex']!='00000001' for v in record['native_calls']):raise ValueError('Native call sequence mismatch')
            if len(record['captures_sha256'])!=16:raise ValueError('Missing complete 16 observed native outputs')
        first=report['cases'][0]
        for other in report['cases'][1:]:
            for key in ('input_sha256','contract_sha256','guide_sha256'):
                if other[key]!=first[key]:raise ValueError('A/A2/B actual generated input/guide/scalar/reset/source contract mismatch: '+key)
        report['same_inputs_and_contracts']=True
        report['comparisons']={'native_repeat_A_A2':compare(out,'A','A2'),'identity_A_B':compare(out,'A','B')}
        for p,digest in report['immutable_sha256'].items():
            if sha(p)!=digest:raise ValueError('Input changed during native run: '+p)
        report['result']='passed-controlled-native';save(out,report);return 0
    except Exception as e:report['result']='failed';report['error']=str(e);save(out,report);raise
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--execute-prepared',type=Path)
    for name in ('core','compiler','reshade','runtime-root','b1','output'):p.add_argument('--'+name,type=Path)
    p.add_argument('--compile-only',action='store_true');a=p.parse_args()
    if a.execute_prepared:return execute(a.execute_prepared.resolve(strict=True))
    if any(getattr(a,n.replace('-','_')) is None for n in ('core','compiler','reshade','runtime-root','b1','output')):p.error('All build input paths are required')
    core=a.core.resolve(strict=True);build=core/'build';source=core/'src/nr_before_sr.cpp';compiler=a.compiler.resolve(strict=True)
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);manifest_path=core.parent/'source-manifest.json';manifest=json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    for name,digest in manifest['files'].items():
        path=(manifest_path.parent/name).resolve(strict=True)
        if not path.is_relative_to(manifest_path.parent.resolve()) or sha(path)!=digest:raise ValueError('Source manifest mismatch: '+name)
    rsp=build/'obj/source-defines.rsp';rsp.resolve(strict=True)
    queue=ROOT/'tests/queue_retirement_harness.cpp';descriptor=ROOT/'tests/descriptor_provenance_warp.inl';base_fixture=ROOT/'tests/rr_identity_core_host.cpp.in';fixture=ROOT/'tests/rr_identity_native_host.cpp.in'
    module=ROOT/'experiments/nr-edit/src/nr_rr_identity_dx12.cpp'
    qt=queue.read_text(encoding='utf-8-sig').split('void OneShot() {',1)[0]+'\n}\n'
    dt=descriptor.read_text(encoding='utf-8-sig').split('template<class Function> NgxResult Native(',1)[0]+'\n}\n'
    original_helpers=qt+dt
    warp='        Hr(IDXGIFactory4_EnumWarpAdapter(factory, IID_IDXGIAdapter, reinterpret_cast<void **>(&adapter)), "WARP");'
    hardware='''        for(unsigned index=0;!adapter;++index){IDXGIAdapter1* value=nullptr;
            if(IDXGIFactory4_EnumAdapters1(factory,index,&value)==DXGI_ERROR_NOT_FOUND)break;
            DXGI_ADAPTER_DESC1 description{};Hr(IDXGIAdapter1_GetDesc1(value,&description),"actual adapter description");
            if(description.VendorId==0x10de&&!(description.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){
                adapter=reinterpret_cast<IDXGIAdapter*>(value);
                std::printf("ADAPTER vendor=%04x device=%04x luid=%08lx:%08lx index=%u\\n",description.VendorId,description.DeviceId,
                    (unsigned long)description.AdapterLuid.HighPart,(unsigned long)description.AdapterLuid.LowPart,index);
            }else ReleaseCom(value);
        }Check(adapter!=nullptr,"actual NVIDIA hardware adapter required");'''
    if original_helpers.count(warp)!=1:raise ValueError('Exact existing WARP selection anchor changed')
    helpers=original_helpers.replace(warp,hardware).replace('D3D_FEATURE_LEVEL_11_0','D3D_FEATURE_LEVEL_12_0').replace('WaitForSingleObject(event, 5000)','WaitForSingleObject(event, 20000)')
    (out/'helpers.inl').write_text(helpers,encoding='utf-8');(out/'helpers-adaptation.diff').write_text(''.join(difflib.unified_diff(original_helpers.splitlines(True),helpers.splitlines(True),fromfile='existing-warp-helpers',tofile='isolated-native-helpers')),encoding='utf-8')
    b1=json.loads(a.b1.read_text(encoding='utf-8'));resource_keys=sorted({k['value'] for r in b1['roles'] if r['kind']=='resource' for k in r['ngx_keys']});subrect_keys=sorted({k['value'] for r in b1['roles'] for k in r.get('subrect_base_keys',[])})
    (out/'rr-native-keys.inl').write_text('static const char* rr_native_resource_keys[]={'+','.join(json.dumps(k) for k in resource_keys)+'};\nstatic const char* rr_native_subrect_keys[]={'+','.join(json.dumps(k) for k in subrect_keys)+'};\n',encoding='utf-8')
    staged={}
    for name in ('nr_edit_dx12.hpp','nr_private_sr_runtime.hpp'):
        path=core/'src/private_sr'/name
        if sha(path)!=sha(ROOT/'experiments/nr-edit/src'/name):raise ValueError('Core/experiment ABI header mismatch: '+name)
        staged[name]=path
    api_text=module.with_suffix('.hpp').read_text(encoding='utf-8-sig')
    if api_text.count('#include "nr_edit_dx12.hpp"')!=1:raise ValueError('Public header include anchor changed')
    (out/'nr_rr_identity_dx12.hpp').write_text(api_text.replace('#include "nr_edit_dx12.hpp"',f'#include "{staged["nr_edit_dx12.hpp"].as_posix()}"'),encoding='utf-8')
    wrapper=out/'rr-identity-native.cpp';wrapper.write_text(f'#include "{source.as_posix()}"\n#include "helpers.inl"\n#include "{base_fixture.as_posix()}"\n#include "{fixture.as_posix()}"\n'
        'int wmain(int argc,wchar_t** argv){std::setvbuf(stdout,nullptr,_IONBF,0);if(argc!=3)return 2;'
        'std::wstring value=argv[1];rr_identity_native_test::Run(std::string(value.begin(),value.end()),argv[2]);return 0;}\n',encoding='utf-8')
    objects=[build/'obj'/name for name in ('buffer.o','hook.o','trampoline.o','hde64.o','nr_dual_spatial_d3d12.o','nr_face_detector.o','nr_face_worker.o','nr_aux_shader_runtime.o')]+objects_for(source)
    inputs=[queue,descriptor,base_fixture,fixture,module,module.with_suffix('.hpp'),ROOT/'tests/native_observer_device.inl',Path(__file__).resolve(),a.b1.resolve(),manifest_path,rsp,compiler,*staged.values(),*objects]
    runtime_receipts={}
    for name,digest in PINNED.items():
        path=(a.runtime_root/name).resolve(strict=True)
        if sha(path)!=digest:raise ValueError('Pinned runtime hash mismatch: '+name)
        shutil.copy2(path,out/name);inputs.extend([path,out/name])
        result=subprocess.run(['pwsh','-NoProfile','-Command',f'$p={ps_quote(path)}; $s=Get-AuthenticodeSignature -LiteralPath $p; $v=(Get-Item -LiteralPath $p).VersionInfo; [pscustomobject]@{{Status=$s.Status.ToString();Signer=$s.SignerCertificate.Subject;FileVersion=$v.FileVersion;ProductVersion=$v.ProductVersion}} | ConvertTo-Json -Compress'],capture_output=True,check=True)
        receipt=json.loads(result.stdout.decode('utf-8-sig'));runtime_receipts[name]={'path':str(path),'sha256':digest,**receipt}
        (out/'runtime-preflight.json').write_text(json.dumps(runtime_receipts,indent=2)+'\n',encoding='utf-8')
        if name in ('nvngx_dlss.dll','nvngx_dlssd.dll') and (receipt['Status']!='Valid' or 'NVIDIA' not in receipt['Signer']):raise ValueError('Pinned NVIDIA signature not valid: '+name+' '+json.dumps(receipt))
    report={'schema':'rr-identity-controlled-native-v1','result':'preparing','source_commit':manifest['source_commit'],'manifest_sha256':sha(manifest_path),
        'module_source_sha256':sha(module),'fixture_sha256':sha(fixture),'base_adapter_sha256':sha(base_fixture),'runner_sha256':sha(Path(__file__)),
        'actual_source_defines':rsp.read_text(),'runtime':runtime_receipts,'commands':[],'cases':[],'immutable_sha256':{},
        'scope':'Independent generated scene, one original Feature13 Create and 32 Evaluate calls per fresh process/history; 16 warmup then 16 observe. A/A2 no private copy; B exact Color copy with actual full-Core NativeCommandStateScope/NrSlot/RecordNrUse/queue-fence/Reset. No Feature1/18, NR, FG, private SceneRR, product entry or game acceptance.',
        'contract':{'R':[1280,720],'N':[1280,720],'O':[1920,1080],'single_layer':True,'nr_layers':0,'FG':False,'feature':13,'create_flags':75,'perf_quality':0,'denoise_mode':1,'roughness_mode':0,'hw_depth':True,
            'color':'RGBA16F scene-linear HDR = radiance * .75 / 2; deterministic noisy static plane','pre_exposure':.75,'exposure_scale':2.,'auto_exposure':True,
            'depth':'R32F reversed infinite hardware depth .05 at view z=2, near=.1; row-major row-vector projection','matrices':{'world_to_view':[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],'view_to_clip':[1,0,0,0,0,1,0,0,0,0,0,1,0,0,.1,0]},
            'motion':'Primary and reflection RG32F zero input-pixel displacement, MV.Scale=(1,1), static view/geometry','jitter_pixels':[0,0],
            'material':'Linear diffuse RGBA16F(.5,approx .4,approx .3,1), specular RGBA16F approx .04; normal RGBA16F world/view unit +Z; unpacked roughness R32F .5',
            'transparency':'Opaque alpha1; every optional resource key explicitly null, with real getter checks; no before/after dependent Color',
            'view':'One immutable generated camera view ID1; all resource views native typed format, mip0,array0,sample1; active=physical extent and base0',
            'lifetime':'Host owns all generated Color/guides/output/upload planes through final exact queue completion, real Reset and original Feature13 Release; copied Color additionally held by production immutable binding lease',
            'resource_keys':resource_keys,'subrect_keys':subrect_keys,'reset':'First frame1, next31 zero; 60Hz fixed frame delta'},
        'claims':{'game':False,'FPS':False,'quality_benefit':False,'complete_game_contract':False,'product_route_changed':False,'pixel_identity':'pending actual A-A2 and A-B numeric comparison'},'nvidia_ngx':False}
    save(out,report)
    flags=['-std=c++20','-O1','-DNDEBUG','-DUNICODE','-D_UNICODE','-D_WIN64','-DNR_FACE_BACKEND=1','-DNR_CODEC_PRECOMPILED=1','-DNR_AUX_PRECOMPILED=1','-DNR_AUX_TIMING=1','-ffunction-sections','-fdata-sections',
        '-I'+str(build),'-I'+str(source.parent),'-I'+str(out),'-I'+str(ROOT/'experiments/nr-edit/src'),'-I'+str(ROOT/'tests'),'-I'+str(core/'third_party/minhook/include'),'-I'+str(a.reshade.resolve()/'include'),'-I'+str(a.reshade.resolve()/'deps/imgui'),'@'+str(rsp)]
    try:
        experiment=out/'nr_rr_identity_dx12.o';result=run(out,report,'compile-identity-module',[compiler,*flags,'-c',module,'-o',experiment],120)
        if result.returncode:raise RuntimeError('Identity module compile failed')
        binary=out/'rr-identity-native.exe';result=run(out,report,'compile-core-native',[compiler,*flags,'-static','-municode','-Wl,--gc-sections',wrapper,experiment,*objects,'-o',binary,
            '-lkernel32','-luser32','-ld3d12','-ldxgi','-ldxguid','-lole32','-luuid','-ld3dcompiler','-lshell32','-lbcrypt'],240)
        if result.returncode:raise RuntimeError('Complete Core native fixture compile failed')
        inputs.extend([source,wrapper,out/'helpers.inl',out/'rr-native-keys.inl',out/'nr_rr_identity_dx12.hpp',experiment,binary]);report['immutable_sha256']={str(path):sha(path) for path in inputs};report['result']='compiled-not-run';save(out,report)
        return 0 if a.compile_only else execute(out)
    except Exception as e:report['result']='failed';report['error']=str(e);save(out,report);raise
if __name__=='__main__':raise SystemExit(main())
