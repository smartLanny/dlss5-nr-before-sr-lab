"""Frozen complete-Core native O capture fixture. GPU runs are explicit."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,subprocess
from temporal_test_objects import objects_for
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('compiler','reshade','source','link-core','output'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--run',action='store_true',help='Run real WARP only after GPU coordination')
    p.add_argument('--real-ngx',action='store_true',help='Actual signed Feature18 and Feature1 on NVIDIA; separate process')
    p.add_argument('--nr-runtime',type=Path);p.add_argument('--sr-runtime',type=Path)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    queue=ROOT/'tests/queue_retirement_harness.cpp';post=ROOT/'tests/post_output_harness.inl';descriptor=ROOT/'tests/descriptor_provenance_warp.inl'
    q=queue.read_text(encoding='utf-8').split('void OneShot() {',1)[0]+'\n}\n'
    q=q.replace('Check(WaitForSingleObject(event, 5000) == WAIT_OBJECT_0, "exact fence completed");', '''const DWORD waited=WaitForSingleObject(event,30000);
    if(waited!=WAIT_OBJECT_0){ID3D12Device* owner=nullptr;ID3D12Fence_GetDevice(fence,IID_ID3D12Device,(void**)&owner);
        std::printf("fence timeout waited=%lu actual=%llu requested=%llu removed=%08lx\\n",waited,ID3D12Fence_GetCompletedValue(fence),value,owner?ID3D12Device_GetDeviceRemovedReason(owner):0);
        ID3D12InfoQueue* info=nullptr;if(owner&&SUCCEEDED(ID3D12Device_QueryInterface(owner,IID_ID3D12InfoQueue,(void**)&info))){
            for(UINT64 i=0;i<ID3D12InfoQueue_GetNumStoredMessages(info);++i){SIZE_T bytes=0;ID3D12InfoQueue_GetMessage(info,i,nullptr,&bytes);std::vector<unsigned char> data(bytes);
                auto* message=(D3D12_MESSAGE*)data.data();if(SUCCEEDED(ID3D12InfoQueue_GetMessage(info,i,message,&bytes)))std::printf("D3D12 %u %s\\n",unsigned(message->Severity),message->pDescription);}ReleaseCom(info);}ReleaseCom(owner);}
    Check(waited==WAIT_OBJECT_0,"exact fence completed");''')
    if a.real_ngx:
        if not a.nr_runtime or not a.sr_runtime:p.error('real NGX requires explicit signed NR and SR DLL paths')
        old='Hr(IDXGIFactory4_EnumWarpAdapter(factory, IID_IDXGIAdapter, reinterpret_cast<void **>(&adapter)), "WARP");'
        if q.count(old)!=1:raise ValueError('GPU fixture anchor changed')
        q=q.replace(old,'''for(unsigned i=0;;++i){IDXGIAdapter1* candidate=nullptr;
            if(IDXGIFactory4_EnumAdapters1(factory,i,&candidate)==DXGI_ERROR_NOT_FOUND)break;
            DXGI_ADAPTER_DESC1 description{};IDXGIAdapter1_GetDesc1(candidate,&description);
            if(description.VendorId==0x10de&&!(description.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){adapter=(IDXGIAdapter*)candidate;break;}
            ReleaseCom(candidate);}Check(adapter!=nullptr,"real NVIDIA required");''')
        # NVIDIA does not provide WARP's known-null baseline. Supply a real
        # already-created PSO to the host-owned command-list constructor.
        q=q.replace('allocator, nullptr, IID_ID3D12GraphicsCommandList',
                    'allocator, g_codec.encode, IID_ID3D12GraphicsCommandList')
    s=post.read_text(encoding='utf-8').split('std::vector<unsigned char> Case(queue_test::Fixture',1)[0]+'\n}\n'
    # Existing real native state setup, no injected state-admission bypass.
    d=descriptor.read_text(encoding='utf-8').split('struct Pipeline {',1)[0]+'\n}\n'
    wrapper=out/'capture.cpp';wrapper.write_text('#include "'+a.source.resolve().as_posix()+'"\n#include <d3d12sdklayers.h>\n#include <vector>\n'+q+s+d+
        '#include "'+(ROOT/('tests/native_output_capture_ngx.inl' if a.real_ngx else 'tests/native_output_capture_harness.inl')).as_posix()+'"\n'+
        'int wmain(int argc,wchar_t** argv){std::setvbuf(stdout,nullptr,_IONBF,0);if(argc!=3)return 2;ID3D12Debug* debug=nullptr;if(SUCCEEDED(D3D12GetDebugInterface(IID_ID3D12Debug,(void**)&debug))){ID3D12Debug_EnableDebugLayer(debug);ReleaseCom(debug);std::puts("debug_layer=1");}else std::puts("debug_layer=0");'+
        ('native_capture_ngx_test::Run(argv[1]);' if a.real_ngx else 'native_capture_test::Run(argv[1],unsigned(std::wcstoul(argv[2],nullptr,10)));')+'ExitProcess(0);}\n',encoding='utf-8')
    core=a.link_core.resolve();build=core/'build'
    if a.run:
        if a.source.resolve()!=core/'src/nr_before_sr.cpp':raise ValueError('GPU run requires the final generated Core and its own objects')
        manifest=core.parent/'source-manifest.json'
        source_manifest=json.loads(manifest.read_text(encoding='utf-8-sig'))
        for relative,expected in source_manifest['files'].items():
            file=(manifest.parent/relative).resolve()
            if not file.is_relative_to(manifest.parent.resolve()) or sha(file)!=expected:raise ValueError('Final source manifest mismatch')
    objects=[build/'obj'/n for n in ('buffer.o','hook.o','trampoline.o','hde64.o','nr_dual_spatial_d3d12.o','nr_face_detector.o','nr_face_worker.o')]+objects_for(core/'src/nr_before_sr.cpp')
    aux_flags=[];aux_evidence={'enabled':False}
    if (build/'obj/nr_aux_shader_runtime.o').is_file():
        helper=importlib.util.spec_from_file_location('native_capture_aux_audit',ROOT/'scripts/validate-model-boundary-ngx.py')
        audit=importlib.util.module_from_spec(helper);helper.loader.exec_module(audit)
        aux_evidence=audit.audit_aux(build,a.source.parent)
        objects.append(build/'obj/nr_aux_shader_runtime.o')
        aux_flags=['-DNR_AUX_PRECOMPILED=1','-DNR_AUX_TIMING=1']
    command=[str(a.compiler),'-std=c++20','-O1','-DNDEBUG','-DUNICODE','-D_UNICODE','-D_WIN64','-DNR_FACE_BACKEND=1','-DNR_CODEC_PRECOMPILED=1',
             *aux_flags,
             '-ffunction-sections','-fdata-sections','-static','-municode','-Wl,--gc-sections','-I'+str(build),'-I'+str(ROOT/'tests'),
             '-I'+str(core/'third_party/minhook/include'),'-I'+str(a.reshade/'include'),'-I'+str(a.reshade/'deps/imgui'),str(wrapper),*map(str,objects),
             '-o',str(out/'capture.exe'),'-lkernel32','-luser32','-ld3d12','-ldxgi','-ldxguid','-lole32','-luuid','-ld3dcompiler','-lshell32','-lbcrypt']
    records=[]
    def run(cmd,name):
        r=subprocess.run(cmd,cwd=out,text=True,capture_output=True,encoding='utf-8',errors='replace',timeout=240)
        (out/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');records.append({'command':cmd,'exit':r.returncode})
        print((r.stdout+r.stderr)[-10000:] if r.returncode else r.stdout[-2000:]);return r.returncode
    result=run(command,'compile')
    if not result and a.real_ngx:
        import shutil
        for source,name in ((a.nr_runtime,'nvngx_dlssnr.dll'),(a.sr_runtime,'nvngx_dlss.dll'),(build/'nrchain_nvngx.dll','nrchain_nvngx.dll')):
            shutil.copyfile(source,out/name)
    if not result and a.run:
        for scenario in range(1 if a.real_ngx else 5):
            directory=out/str(scenario);directory.mkdir()
            result=run([str(out/'capture.exe'),str(directory),str(scenario)],'run-'+str(scenario))
            if result:break
    receipt={'result':result,'run_requested':a.run,'warp_only':not a.real_ngx,'real_ngx':a.real_ngx,'constructed_model_planes':not a.real_ngx,'commands':records,
             'core_source':str(a.source),'core_sha256':sha(a.source),'linked_objects':{str(o):sha(o) for o in objects},
             'fixture_sha256':sha(ROOT/('tests/native_output_capture_ngx.inl' if a.real_ngx else 'tests/native_output_capture_harness.inl')),
             'game_verified':False,'static_artificial_guides':a.real_ngx}
    receipt['runtime_artifacts']={name:sha(out/name) for name in ('nvngx_dlssnr.dll','nvngx_dlss.dll','nrchain_nvngx.dll') if (out/name).is_file()}
    receipt['binary_sha256']=sha(out/'capture.exe') if (out/'capture.exe').is_file() else None
    receipt['core_modules_sha256']={f.name:sha(f) for f in a.source.parent.glob('nr_native_output_capture*') if f.is_file()}
    receipt['stage_source_sha256']=sha(a.source.parent/'nr_stage_capture.inl')
    receipt['auxiliary']=aux_evidence
    (out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8');raise SystemExit(result)
if __name__=='__main__':main()
