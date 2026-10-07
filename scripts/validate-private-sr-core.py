"""Unmodified final Core callbacks on WARP; manually installed Owner with controlled fixture API."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,time
from temporal_test_objects import objects_for
ROOT=Path(__file__).resolve().parents[1]
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('core','compiler','reshade','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--hardware',action='store_true',help='Use the NVIDIA host adapter with the same controlled fixture API; not NGX')
    p.add_argument('--mode',action='append',choices=('neutral','numeric','odd','odd-quarter','quarter','full','guide-only','reset-fail','discard','duplicate','pending','off','order','signal-fail','hdr-jitter','rr-jitter','domain','budget-preflight','spatial-sr','spatial-rr'),help='Run selected modes; omit to run the complete controlled suite')
    a=p.parse_args();core=a.core.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    source=core/'src/nr_before_sr.cpp';manifest_path=core.parent/'source-manifest.json'
    manifest=json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    for rel,digest in manifest['files'].items():
        f=(manifest_path.parent/rel).resolve()
        if not f.is_relative_to(manifest_path.parent.resolve()) or sha(f)!=digest:raise ValueError('Generated source identity mismatch: '+rel)
    original=ROOT/'experiments/nr-edit/tests/private_sr_ngx_host.cpp'
    host=original.read_text(encoding='utf-8').split('void NeutralAndTinyComposition(',1)[0]+'\n}\n'
    begin=host.index('        for(unsigned i=0;!device;++i)')
    end=host.index('        D3D12_COMMAND_QUEUE_DESC q{};',begin)
    warp_host=host[:begin]+"""        IDXGIAdapter* adapter=nullptr;Hr(IDXGIFactory4_EnumWarpAdapter(factory,IID_IDXGIAdapter,(void**)&adapter),"WARP adapter");
        Hr(D3D12CreateDevice((IUnknown*)adapter,D3D_FEATURE_LEVEL_12_0,IID_ID3D12Device,(void**)&device),"WARP device");ReleaseCom(adapter);ReleaseCom(factory);
"""+host[end:]
    if not a.hardware:host=warp_host
    # Preserve the host helpers except adapter selection above, excluding its independent
    # experiment Run/main; the new fixture calls the actual product entry points.
    fixture=out/'private_sr_core_host.cpp';shutil.copy2(ROOT/'tests/private_sr_core_host.cpp',fixture)
    wrapper=out/'host.cpp';wrapper.write_text('#define NR_PRIVATE_SR_CORE "'+source.as_posix()+'"\n'+host+
        '#include "'+fixture.as_posix()+'"\n',encoding='utf-8')
    build=core/'build';objects=[build/'obj'/n for n in ('buffer.o','hook.o','trampoline.o','hde64.o','nr_dual_spatial_d3d12.o','nr_face_detector.o','nr_face_worker.o','nr_aux_shader_runtime.o')]+objects_for(source)
    modes=a.mode or ('neutral','numeric','odd','odd-quarter','quarter','full','guide-only','reset-fail','discard','duplicate','pending','off','order','signal-fail','hdr-jitter','rr-jitter','domain','budget-preflight','spatial-sr','spatial-rr')
    report={'schema':'actual-core-warp-fixture-v1','result':'running','source_commit':manifest['source_commit'],
        'generated_source_sha256':sha(source),'manifest_sha256':sha(manifest_path),'fixture_sha256':sha(ROOT/'tests/private_sr_core_host.cpp'),
        'host_helpers_sha256':sha(original),'wrapper_sha256':sha(wrapper),'compiler_sha256':sha(a.compiler),'commands':[],
        'linked_objects':{str(o):sha(o) for o in objects},'game':False,'feature18':False,
        'scope':'Unchanged Core callbacks with controlled model output and synthetic native parameter/resource inputs; no real NVIDIA NGX inference, actual Feature18 multi-layer scheduler, or games.',
        'mode_scope':{mode:('Actual generated Core post-final spatial Prepare/Finish with synthetic FP16 B/Z on WARP; actual recording token, Execute/Signal, Reset and fence; no NGX or game.' if mode in ('spatial-sr','spatial-rr') else 'Actual Core WARP 4K allocation-info query plus invalid-extent CreateOwner refusal; zero private allocations or NGX calls.' if mode=='budget-preflight' else 'Actual Core Create/Shutdown hooks, original-export owner resolution, exact parent generation/domain epochs, and private TLS forwarding through a controlled _nvngx fixture DLL; no NVIDIA initialization or inference.' if mode=='domain' else 'Actual Capture/Finish and guide shaders with synthetic Flags75 native Feature'+('13' if mode=='rr-jitter' else '1')+' parameters, dynamic jitter and HDR-range FP16 B/Z; manually installed Owner, no production CreateOwner/export resolution or NVIDIA inference.' if mode in ('hdr-jitter','rr-jitter') else 'Actual Capture/Finish and lifecycle callbacks; manually installed Owner with controlled API output, no production CreateOwner/export resolution or real native model.') for mode in modes}}
    generated=source.read_text(encoding='utf-8')
    finish='nr_post_product::Finish(command_list,context,slot,feature,substitution.private_sr_capture,'
    finish_at=generated.index(finish)
    run_at=generated.rindex('substitution.fatal_recording=nr_dual_runtime::Run(',0,finish_at)
    combine_at=generated.index('DispatchCodec(command_list, slot, g_codec.combine',run_at)
    barrier_at=generated.index('UavBarrier(command_list, slot.result);',combine_at)
    finish_at=generated.index(finish)
    report['final_once_source_structure']={'scope':'Static actual Core call order only; fixture constructs aggregate Z, no Feature18 multi-layer inference',
        'finish_call_count':generated.count(finish),'ordered':run_at<combine_at<barrier_at<finish_at,
        'lines':{name:generated.count('\n',0,offset)+1 for name,offset in [('dual_run',run_at),('final_combine',combine_at),('result_uav',barrier_at),('private_finish',finish_at)]}}
    if generated.count(finish)!=1 or not run_at<combine_at<barrier_at<finish_at:raise ValueError('Final-once structure mismatch')
    def save(): (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    def run(name,args,timeout):
        start=time.monotonic();r=subprocess.run(list(map(str,args)),cwd=out,capture_output=True,timeout=timeout)
        (out/(name+'.log')).write_bytes(r.stdout+r.stderr);report['commands'].append({'name':name,'args':list(map(str,args)),'exit':r.returncode,'seconds':time.monotonic()-start})
        save();print((r.stdout+r.stderr).decode('utf-8','replace')[-9000:],flush=True);return r.returncode
    report['model']='controlled fixture API, not NGX';report['adapter']='NVIDIA hardware' if a.hardware else 'WARP';save()
    flags=['-std=c++20','-O1','-DNDEBUG','-DUNICODE','-D_UNICODE','-D_WIN64','-DNR_FACE_BACKEND=1','-DNR_CODEC_PRECOMPILED=1','-DNR_AUX_PRECOMPILED=1','-DNR_AUX_TIMING=1',
        '-ffunction-sections','-fdata-sections','-static','-municode','-Wl,--gc-sections','-I'+str(build),'-I'+str(core/'third_party/minhook/include'),'-I'+str(a.reshade/'include'),'-I'+str(a.reshade/'deps/imgui')]
    try:
        result=run('compile',[a.compiler,*flags,wrapper,*objects,'-o',out/'core-warp.exe','-lkernel32','-luser32','-ld3d12','-ldxgi','-ldxguid','-lole32','-luuid','-ld3dcompiler','-lshell32','-lbcrypt'],240)
        if not result and (a.mode is None or 'domain' in a.mode):
            result=run('compile-domain-fixture',[a.compiler,'-std=c++20','-shared','-static',ROOT/'tests/private_sr_domain_exports.cpp','-o',out/'_nvngx.dll'],60)
            report['controlled_domain_fixture_sha256']=sha(ROOT/'tests/private_sr_domain_exports.cpp')
        if not result:
            for mode in modes:
                result=run('warp-'+mode,[out/'core-warp.exe',mode],120)
                if result:break
        report['result']='passed' if not result else 'failed'
        if (out/'warp-neutral.log').is_file():
            import re
            log=(out/'warp-neutral.log').read_text(encoding='utf-8',errors='replace')
            report['metrics']=[dict((k,float(v) if '.' in v else int(v)) for k,v in re.findall(r'(\w+)=([0-9.]+)',line)) for line in log.splitlines() if line.startswith('METRIC ')]
        report['captures_sha256']={f.name:sha(f) for f in out.glob('*.fp16')}
        report['binary_sha256']=sha(out/'core-warp.exe') if (out/'core-warp.exe').is_file() else None
        save();return result
    except Exception as e:report['result']='failed';report['error']=str(e);save();raise
if __name__=='__main__':raise SystemExit(main())
