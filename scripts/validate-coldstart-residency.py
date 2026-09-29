"""Profile real first-use API/codec/Create and retained model memory without a game.

Uses an exact generated Core and the existing artificial-input signed-Create
fixture. GPU completion, CPU API wall time and process DXGI usage stay distinct.
This is not an F6/game-latency or image-quality acceptance test.
"""
from __future__ import annotations
import argparse
import os
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]

def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

MAIN = r'''
int wmain(int argc,wchar_t** argv) {
    using namespace scale_host;
    std::setvbuf(stdout,nullptr,_IONBF,0); std::setvbuf(stderr,nullptr,_IONBF,0);
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(Fault);
    try {
        Check(argc==6,"usage: host directory width height layers compact-cycle");
        const unsigned w=std::wcstoul(argv[2],nullptr,10),h=std::wcstoul(argv[3],nullptr,10),layers=std::wcstoul(argv[4],nullptr,10);
        Check(w>=320&&w<=3840&&h>=192&&h<=2160&&layers>=1&&layers<=5,"bounded workload");
        InitializeCriticalSection(&g_chain_cs);g_config=ChainConfig{};g_config.enabled=true;g_config.mode=1;
        g_enabled_fast.store(true);g_config_generation.store(1);
        swprintf_s(g_log_path,L"%ls\\production.log",argv[1]);swprintf_s(g_ini_path,L"%ls\\host.ini",argv[1]);
        IDXGIFactory4* factory=nullptr;IDXGIAdapter1* adapter=nullptr;ID3D12Device* device=nullptr;ID3D12CommandQueue* queue=nullptr;
        Hr(CreateDXGIFactory1(IID_IDXGIFactory4,reinterpret_cast<void**>(&factory)),"factory");
        for(unsigned i=0;IDXGIFactory4_EnumAdapters1(factory,i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){
            DXGI_ADAPTER_DESC1 d{};IDXGIAdapter1_GetDesc1(adapter,&d);
            if(d.VendorId==0x10de&&!(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){std::printf("ADAPTER vendor=%x device=%x dedicated=%llu\n",d.VendorId,d.DeviceId,static_cast<unsigned long long>(d.DedicatedVideoMemory));break;}ReleaseCom(adapter);
        }
        Check(adapter,"real NVIDIA adapter");
        Hr(IDXGIAdapter1_QueryInterface(adapter,IID_IDXGIAdapter3,reinterpret_cast<void**>(&memory_adapter)),"memory adapter");
        Hr(D3D12CreateDevice(reinterpret_cast<IUnknown*>(adapter),D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,reinterpret_cast<void**>(&device)),"device");
        const auto elapsed=[](std::uint64_t before){return 1000.*double(NrTimingCounter()-before)/double(NrTimingFrequency());};
        Memory("before-api");auto tick=NrTimingCounter();Check(EnsureNrApi(device),"production API init");std::printf("PROFILE stage=api-first ms=%.6f\n",elapsed(tick));Memory("after-api");
        tick=NrTimingCounter();Check(EnsureCodec(device),"production codec init");std::printf("PROFILE stage=codec-first ms=%.6f\n",elapsed(tick));Memory("after-codec");
        tick=NrTimingCounter();Check(EnsureNrApi(device)&&EnsureCodec(device),"hot API/codec reuse");std::printf("PROFILE stage=api-codec-hot ms=%.6f\n",elapsed(tick));
        D3D12_COMMAND_QUEUE_DESC q{};Hr(ID3D12Device_CreateCommandQueue(device,&q,IID_ID3D12CommandQueue,reinterpret_cast<void**>(&queue)),"queue");Io io;io.Init(device,queue);
        std::vector<Model*> held;
        for(unsigned i=0;i<layers;++i){
            auto* model=new Model;model->context->layer_index=i;
            tick=NrTimingCounter();Check(Create(io,*model,w,h),"signed Create succeeds");std::printf("PROFILE stage=model-and-resources layer=%u ms=%.6f\n",i,elapsed(tick));
            for(auto* prior:held)Check(prior->context->nr_handle!=model->context->nr_handle,"distinct retained model handles");held.push_back(model);
            std::printf("RETAINED models=%zu\n",held.size());Memory("retained",w,h);
        }
        // No Evaluate or game claims: this deliberately isolates Create/storage.
        while(held.size()>1){auto* model=held.back();held.pop_back();tick=NrTimingCounter();Release(*model);delete model;std::printf("PROFILE stage=partial-release remaining=%zu ms=%.6f\n",held.size(),elapsed(tick));}
        Memory("after-partial-shrink-one");
        tick=NrTimingCounter();Release(*held.front());delete held.front();held.clear();std::printf("PROFILE stage=last-release ms=%.6f\n",elapsed(tick));
        Memory("models-released");
        if(std::wcstoul(argv[5],nullptr,10)){
            Model model;tick=NrTimingCounter();Check(Create(io,model,w,h),"recreate selected single model after full drain");
            std::printf("PROFILE stage=compact-recreate-one ms=%.6f\n",elapsed(tick));Memory("compacted-one");Release(model);Memory("compacted-released");
        }
        io.Destroy();ReleaseCom(queue);ReleaseCom(device);ReleaseCom(memory_adapter);ReleaseCom(adapter);ReleaseCom(factory);
        std::printf("PASS layers=%u; real signed Create/Release, artificial initialized inputs, no Evaluate or game\n",layers);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"HOST_FAIL %s; unresolved dependencies retained until process exit\n",e.what());return 1;}
}
'''

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ('core','compiler','reshade','nr-runtime','output'):
        parser.add_argument('--'+key,type=Path,required=True)
    parser.add_argument('--run',action='store_true',help='Requires separately coordinated NVIDIA GPU availability')
    parser.add_argument('--candidate-codec',type=Path,help='Controlled override of only the production EnsureCodec function; not a complete candidate build')
    parser.add_argument('--cache-root',type=Path,help='Child-only LOCALAPPDATA for an isolated, optionally shared PSO cache')
    parser.add_argument('--compact-cycle',action='store_true')
    parser.add_argument('--layers',type=int,choices=range(1,6),default=1)
    parser.add_argument('--width',type=int,default=2304)
    parser.add_argument('--height',type=int,default=1296)
    args=parser.parse_args()
    if not 320<=args.width<=3840 or not 192<=args.height<=2160:parser.error('bounded width/height required')
    core=args.core.resolve(strict=True);out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    source=core/'src/nr_before_sr.cpp';manifest_path=core.parent/'source-manifest.json'
    manifest=json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    for rel,digest in manifest['files'].items():
        path=(manifest_path.parent/rel).resolve()
        if not path.is_relative_to(manifest_path.parent.resolve()) or sha(path)!=digest:
            raise ValueError('Frozen source manifest mismatch: '+rel)
    original=ROOT/'tests/post_scale_create_ngx_host.cpp'
    prefix=original.read_text(encoding='utf-8').split('\nint wmain(',1)[0]
    prefix=prefix.replace('#include "compat_present_ngx_host.cpp"','#include "'+(ROOT/'tests/compat_present_ngx_host.cpp').as_posix()+'"')
    anchor='const auto result=CreateSignedNrFeature(io.list,c,feature,&handle);'
    if prefix.count(anchor)!=1:raise ValueError('Review Create profiling anchor')
    prefix=prefix.replace(anchor,'const auto create_begin=NrTimingCounter();\n    '+anchor+'\n    std::printf("PROFILE stage=create-wrapper layer=%u ms=%.6f\\n",c.layer_index,1000.*double(NrTimingCounter()-create_begin)/double(NrTimingFrequency()));')
    main_body=MAIN
    codec_source=None
    if args.candidate_codec:
        codec_source=args.candidate_codec.resolve(strict=True)
        text=codec_source.read_text(encoding='utf-8-sig')
        start=text.index('bool EnsureCodec(ID3D12Device *device) {');end=text.index('{',start)+1;depth=1
        while depth:
            depth+=(text[end]=='{')-(text[end]=='}');end+=1
        definition=text[start:end].replace('bool EnsureCodec(', 'bool CandidateEnsureCodec(',1)
        prefix+='\n#include "'+(codec_source.parent/'nr_pipeline_cache.h').as_posix()+'"\n'+definition+'\n'
        main_body=main_body.replace('EnsureCodec(device)','CandidateEnsureCodec(device)')
    wrapper=out/'host.cpp';wrapper.write_text(prefix+main_body,encoding='utf-8')
    child_env=os.environ.copy()
    if args.cache_root:
        args.cache_root=args.cache_root.resolve();args.cache_root.mkdir(parents=True,exist_ok=True)
        child_env['LOCALAPPDATA']=str(args.cache_root)

    build=core/'build'
    # Use the exact frozen generator's link contract. Beta7 has a private-SR
    # module that the older main helper intentionally does not know about.
    # The delivery source allowlist does not ship every test helper. Resolve
    # this test-only helper from its exact recorded Git commit, never main.
    commit=manifest['source_commit']
    import re, runpy
    if not re.fullmatch(r'[0-9a-f]{40}',commit):
        raise ValueError('Invalid frozen source commit')
    helper=out/'frozen_link_contract.py'
    helper.write_bytes(subprocess.check_output(
        ['git','show',commit+':scripts/temporal_test_objects.py'],cwd=ROOT))
    optional=runpy.run_path(str(helper))['objects_for'](source)
    objects=[build/'obj'/name for name in ('buffer.o','hook.o','trampoline.o','hde64.o','nr_dual_spatial_d3d12.o','nr_face_detector.o','nr_face_worker.o','nr_aux_shader_runtime.o')]+optional
    flags=['-std=c++20','-O1','-DNDEBUG','-DUNICODE','-D_UNICODE','-D_WIN64','-DNR_FACE_BACKEND=1','-DNR_CODEC_PRECOMPILED=1','-DNR_AUX_PRECOMPILED=1','-DNR_AUX_TIMING=1','-DNR_HOST_COLD_BOOTSTRAP=1',
           '-DNR_PRESENT_NGX_CORE="'+source.as_posix()+'"','-ffunction-sections','-fdata-sections','-static','-municode','-Wl,--gc-sections','-I'+str(build),'-I'+str(core/'third_party/minhook/include'),'-I'+str(args.reshade/'include'),'-I'+str(args.reshade/'deps/imgui')]
    binary=out/'coldstart-residency.exe'
    report={'schema':'nr-coldstart-residency-v1','source_commit':manifest['source_commit'],'source_sha256':sha(source),'manifest_sha256':sha(manifest_path),'fixture_sha256':sha(original),'wrapper_sha256':sha(wrapper),'link_helper_sha256':sha(helper),'linked_objects':{str(x):sha(x) for x in objects},'runtime_sha256':sha(args.nr_runtime),'commands':[],'game':False,'evaluate':False,'run_requested':args.run,'result':'running','codec_override':str(codec_source) if codec_source else None,'codec_override_sha256':sha(codec_source) if codec_source else None,'cache_root':str(args.cache_root),'compact_cycle':args.compact_cycle}
    def save(): (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    def run(name,cmd,timeout):
        start=time.monotonic()
        try:
            completed=subprocess.run(list(map(str,cmd)),cwd=out,capture_output=True,timeout=timeout,env=child_env)
            code=completed.returncode;data=completed.stdout+completed.stderr
        except subprocess.TimeoutExpired as error:
            code=124;data=(error.stdout or b'')+(error.stderr or b'')+b'\nTIMEOUT; not passed\n'
        log=out/(name+'.log');log.write_bytes(data)
        report['commands'].append({'name':name,'command':list(map(str,cmd)),'exit':code,'seconds':time.monotonic()-start,'log_sha256':sha(log)})
        save();print(name,code,data.decode('utf-8','replace')[-3000:],flush=True);return code
    try:
        code=run('compile',[args.compiler,*flags,wrapper,*objects,'-o',binary,'-lkernel32','-luser32','-ld3d12','-ldxgi','-ldxguid','-lole32','-luuid','-ld3dcompiler','-lshell32','-lbcrypt','-lpsapi'],240)
        if not code and args.run:
            for src,name in ((args.nr_runtime,'nvngx_dlssnr.dll'),(build/'nrchain_nvngx.dll','nrchain_nvngx.dll')):shutil.copy2(src,out/name)
            code=run('real-create',[binary,out,args.width,args.height,args.layers,int(args.compact_cycle)],180)
        report['result']='passed' if not code else 'failed'
        report['hardware_executed']=any(x['name']=='real-create' for x in report['commands'])
        report['binary_sha256']=sha(binary) if binary.exists() else None
        report['interpretation']='CPU wall stages and whole-process DXGI residency; no game latency/FPS, no model quality, no inferred exact opaque model bytes.'
        return code
    except Exception as error:
        report.update(result='failed',error=str(error));raise
    finally:save()

if __name__=='__main__':raise SystemExit(main())
