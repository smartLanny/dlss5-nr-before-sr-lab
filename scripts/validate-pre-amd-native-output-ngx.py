"""One fixed-runtime real NR -> pre-SR AMD whole-chain -> native SR case."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,subprocess
from temporal_test_objects import objects_for
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def verify_build(source,core,nr,sr,expected_source_commit):
    if source.resolve()!=core/'src/nr_before_sr.cpp':raise ValueError('One exact production source and linked Core required')
    if not nr or not sr:raise ValueError('Explicit fixed signed runtime paths required')
    if sha(nr)!='e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e':
        raise ValueError('Only fixed 310.8 NR runtime is admitted; no DLL comparison')
    manifest=core.parent/'source-manifest.json';data=json.loads(manifest.read_text(encoding='utf-8-sig'))
    if len(expected_source_commit)!=40 or data['source_commit']!=expected_source_commit:raise ValueError('Requested frozen source commit mismatch')
    pinned={}
    for name,digest in data['files'].items():
        path=(manifest.parent/name).resolve()
        if not path.is_relative_to(manifest.parent.resolve()) or sha(path)!=digest:raise ValueError('Frozen source manifest mismatch: '+name)
        pinned[str(path)]=digest
    build=core/'build';face=json.loads((build/'face-backend.json').read_text(encoding='utf-8-sig'))
    variant=json.loads((build/'build-variant.json').read_text(encoding='utf-8-sig'))
    if not face['enabled'] or variant['sourcecommit']!=data['source_commit']:raise ValueError('Same-source complete Face1 build required')
    for row in variant['artifacts']:
        path=build/row['name']
        if sha(path)!=row['sha256'] or path.stat().st_size!=row['bytes']:raise ValueError('Final binary artifact mismatch')
        pinned[str(path)]=sha(path)
    codec=json.loads((build/'codec-artifacts.json').read_text(encoding='utf-8-sig'))
    body=source.read_text(encoding='utf-8').split('const char kCodecShader[] = R"HLSL(')[1].split(')HLSL";',1)[0]
    if hashlib.sha256(body.encode()).hexdigest()!=codec['source'] or sha(build/'nr_codec_precompiled.h')!=codec['header_sha256']:
        raise ValueError('Exact generated codec/header mismatch')
    for row in codec['artifacts']:
        if sha(build/row['file'])!=row['sha256']:raise ValueError('Codec bytecode mismatch')
    aux=json.loads((build/'aux-artifacts.json').read_text(encoding='utf-8-sig'))
    if aux['schema']!='nr-aux-artifacts-v1' or aux['count']!=9 or len(aux['artifacts'])!=9:
        raise ValueError('All nine production auxiliary shaders required')
    for name,digest in aux['headers_sha256'].items():
        path=build/name
        if sha(path)!=digest:raise ValueError('Auxiliary header mismatch')
        pinned[str(path)]=digest
    for row in aux['artifacts']:
        path=build/row['file']
        if sha(path)!=row['sha256'] or path.stat().st_size!=row['bytes']:raise ValueError('Auxiliary shader artifact mismatch')
        pinned[str(path)]=sha(path)
    aux_obj=build/'obj/nr_aux_shader_runtime.o'
    if not aux_obj.is_file():raise ValueError('Production auxiliary timing object required')
    for path in (aux_obj,manifest,build/'face-backend.json',build/'build-variant.json',build/'codec-artifacts.json',build/'aux-artifacts.json',
                 ROOT/'tests/pre_amd_native_output_ngx.inl',ROOT/'tests/pre_amd_native_output_observe.inl',Path(__file__)):
        pinned[str(path.resolve())]=sha(path)
    return {'source_commit':data['source_commit'],'source_manifest_sha256':sha(manifest),
            'aux_shader_count':9,'aux_precompiled':True,'aux_timing':True,'pinned_files':pinned}
def verify_capture(out,expected_source_commit):
    import math,struct
    reader_spec=importlib.util.spec_from_file_location('native_o_reader',ROOT/'scripts/read-native-output-capture.py')
    reader=importlib.util.module_from_spec(reader_spec);reader_spec.loader.exec_module(reader)
    case=out/'0';directories=list((case/'nr-stage-captures').glob('*'))
    if len(directories)!=1:raise ValueError('One complete deep capture required')
    native=reader.read_capture(directories[0]);amd=json.loads((case/'pre-amd-observation.json').read_text())
    if not native['present'] or not native['same_frame_verified']:raise ValueError('Actual same-source native O missing')
    embedded_commit=reader.HEADER.unpack_from((directories[0]/'native-output.nro').read_bytes())[35].split(b'\0',1)[0].decode('ascii')
    if embedded_commit!=expected_source_commit or native.get('source_commit')!=expected_source_commit:
        raise ValueError('Embedded native O source commit does not match the verified frozen Core')
    if (native['source']!=amd['source'] or native['frame']!=amd['parent_nr_frame'] or
            native['request']!=amd['capture_request'] or amd['recorded']!=amd['submitted'] or not amd['continuation_observations']):
        raise ValueError('Temporal input/O identity, submission or continuing history mismatch')
    raw=(case/'post-temporal-input.rgba16').read_bytes()
    if len(raw)!=320*192*8:raise ValueError('Incomplete post-temporal actual SR input')
    values=[x for p in struct.iter_unpack('<4e',raw) for x in p[:3]]
    if not all(math.isfinite(x) for x in values):raise ValueError('Nonfinite actual post-temporal SR input')
    # E remains the pre-temporal compositor sample. Compare stored numeric bits
    # only, after identical FP16 quantization, without a quality/domain claim.
    stages=(directories[0]/'stages.rgba32f').read_bytes();plane=320*192*16
    if len(stages)!=plane*4:raise ValueError('Expected full source grid at this bounded extent')
    before=[]
    for p in struct.iter_unpack('<4f',stages[3*plane:]):before.extend(struct.unpack('<3e',struct.pack('<3e',*p[:3])))
    changed=sum(a!=b for a,b in zip(before,values))
    return {'native':native,'temporal':amd,'actual_sr_input_sha256':sha(case/'post-temporal-input.rgba16'),
            'input_rgb_min':min(values),'input_rgb_max':max(values),'pre_post_temporal_changed_rgb':changed,
            'comparison_scope':'raw same-grid FP16 numeric storage; no visual quality, motion or game claim'}
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('compiler','reshade','source','link-core','output'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--run',action='store_true',help='Run the real NVIDIA NR/AMD/SR chain only after GPU coordination')
    p.add_argument('--expected-source-commit',required=True,help='Exact frozen source commit; checked against manifest and completed build')
    p.set_defaults(real_ngx=True)
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
        '#include "'+(ROOT/('tests/pre_amd_native_output_ngx.inl' if a.real_ngx else 'tests/native_output_capture_harness.inl')).as_posix()+'"\n'+
        'int wmain(int argc,wchar_t** argv){std::setvbuf(stdout,nullptr,_IONBF,0);if(argc!=3)return 2;ID3D12Debug* debug=nullptr;if(SUCCEEDED(D3D12GetDebugInterface(IID_ID3D12Debug,(void**)&debug))){ID3D12Debug_EnableDebugLayer(debug);ReleaseCom(debug);std::puts("debug_layer=1");}else std::puts("debug_layer=0");'+
        ('pre_amd_native_output_ngx::Run(argv[1]);' if a.real_ngx else 'native_capture_test::Run(argv[1],unsigned(std::wcstoul(argv[2],nullptr,10)));')+'ExitProcess(0);}\n',encoding='utf-8')
    core=a.link_core.resolve();build=core/'build'
    proof=verify_build(a.source,core,a.nr_runtime,a.sr_runtime,a.expected_source_commit)
    objects=[build/'obj'/n for n in ('buffer.o','hook.o','trampoline.o','hde64.o','nr_dual_spatial_d3d12.o','nr_face_detector.o','nr_face_worker.o')]+objects_for(core/'src/nr_before_sr.cpp')
    if (build/'obj/nr_aux_shader_runtime.o').is_file():objects.append(build/'obj/nr_aux_shader_runtime.o')
    proof['pinned_files'].update({str(path.resolve()):sha(path) for path in objects})
    command=[str(a.compiler),'-std=c++20','-O1','-DNDEBUG','-DUNICODE','-D_UNICODE','-D_WIN64','-DNR_FACE_BACKEND=1','-DNR_CODEC_PRECOMPILED=1','-DNR_AUX_PRECOMPILED=1','-DNR_AUX_TIMING=1',
             '-DNR_SOURCE_COMMIT="'+proof['source_commit']+'"',
             '-ffunction-sections','-fdata-sections','-static','-municode','-Wl,--gc-sections','-I'+str(build),'-I'+str(ROOT/'tests'),
             '-I'+str(core/'third_party/minhook/include'),'-I'+str(a.reshade/'include'),'-I'+str(a.reshade/'deps/imgui'),str(wrapper),*map(str,objects),
             '-o',str(out/'capture.exe'),'-lkernel32','-luser32','-ld3d12','-ldxgi','-ldxguid','-lole32','-luuid','-ld3dcompiler','-lshell32','-lbcrypt']
    records=[]
    def run(cmd,name):
        try:
            r=subprocess.run(cmd,cwd=out,text=True,capture_output=True,encoding='utf-8',errors='replace',timeout=240)
            output=r.stdout+r.stderr;code=r.returncode
        except subprocess.TimeoutExpired as error:
            def decoded(value):return value.decode('utf-8','replace') if isinstance(value,bytes) else value or ''
            output=decoded(error.stdout)+decoded(error.stderr)+'\nBounded host timeout after 240 seconds.\n';code=124
        (out/(name+'.log')).write_text(output,encoding='utf-8');records.append({'command':cmd,'exit':code})
        print(output[-10000:] if code else output[-2000:]);return code
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
    capture=None
    if not result and a.run:
        try:capture=verify_capture(out,proof['source_commit'])
        except Exception as error:result=1;capture={'error':str(error)}
    receipt={'result':result,'run_requested':a.run,'warp_only':not a.real_ngx,'real_ngx':a.real_ngx,'constructed_model_planes':not a.real_ngx,'commands':records,
             'core_source':str(a.source),'core_sha256':sha(a.source),'linked_objects':{str(o):sha(o) for o in objects},
             'fixture_sha256':sha(ROOT/('tests/pre_amd_native_output_ngx.inl' if a.real_ngx else 'tests/native_output_capture_harness.inl')),
             'game_verified':False,'static_artificial_guides':a.real_ngx}
    receipt['frozen_build']=proof
    receipt['capture_verification']=capture
    receipt['source_unchanged']=all(sha(Path(p))==digest for p,digest in proof['pinned_files'].items())
    if not receipt['source_unchanged']:result=1;receipt['result']=1
    receipt['actual_gpu_executed']=a.run and any(x['command'][0].endswith('capture.exe') for x in records)
    receipt['scenario']='one AMD pre-SR whole-chain, fixed 310.8, static artificial scene; no DLL A/B'
    receipt['runtime_artifacts']={name:sha(out/name) for name in ('nvngx_dlssnr.dll','nvngx_dlss.dll','nrchain_nvngx.dll') if (out/name).is_file()}
    receipt['binary_sha256']=sha(out/'capture.exe') if (out/'capture.exe').is_file() else None
    receipt['core_modules_sha256']={f.name:sha(f) for f in a.source.parent.glob('nr_native_output_capture*') if f.is_file()}
    receipt['stage_source_sha256']=sha(a.source.parent/'nr_stage_capture.inl')
    (out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8');raise SystemExit(result)
if __name__=='__main__':main()
