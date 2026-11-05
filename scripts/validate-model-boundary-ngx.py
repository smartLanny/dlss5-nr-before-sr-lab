"""Real fixed-runtime Core model hot/cold boundary; complete FP16 N comparisons."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,math,os,struct,subprocess
from temporal_test_objects import objects_for
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def audit_aux(build,source):
    """Verify all nine source-keyed production auxiliary blobs and receipts."""
    helper=ROOT/'scripts/prepare-aux-artifacts.py'
    spec=importlib.util.spec_from_file_location('model_boundary_aux',helper)
    aux=importlib.util.module_from_spec(spec);spec.loader.exec_module(aux)
    receipt=build/'aux-artifacts.json';data=json.loads(receipt.read_text(encoding='utf-8-sig'))
    if data.get('schema')!='nr-aux-artifacts-v1' or data.get('count')!=9 or len(data['artifacts'])!=9:
        raise ValueError('Require all nine auxiliary artifacts')
    compiler=build/'compile-aux-artifact.exe'
    if sha(compiler)!=data['compiler_sha256']:raise ValueError('Auxiliary compiler identity mismatch')
    files=[receipt,compiler,helper];expected={}
    for group,cases in aux.GROUPS.items():
        for filename,symbol,label,entry,target,flags in cases:
            expected[(group,entry,target)]={
                'source_sha256':hashlib.sha256(aux.shader_body(source/filename,symbol)).hexdigest(),
                'compiler_sha256':data['compiler_sha256'],'source_label':label,
                'entry':entry,'target':target,'flags':flags,'macros':[]}
    seen=set()
    for row in data['artifacts']:
        identity=row['identity'];key=(row['group'],identity['entry'],identity['target'])
        if key in seen or identity!=expected.get(key):raise ValueError('Auxiliary source/options contract mismatch')
        seen.add(key);binary=(build/row['file']).resolve()
        if not binary.is_relative_to(build.resolve()):raise ValueError('Auxiliary payload outside build')
        individual=binary.with_name('artifact.json');info=json.loads(individual.read_text(encoding='utf-8-sig'))
        if info['identity']!=identity or info['sha256']!=row['sha256'] or info['bytes']!=row['bytes']:
            raise ValueError('Individual auxiliary receipt mismatch')
        if sha(binary)!=row['sha256'] or binary.stat().st_size!=row['bytes'] or binary.read_bytes()[:4]!=b'DXBC':
            raise ValueError('Actual auxiliary bytecode mismatch')
        files.extend((binary,individual))
    if seen!=set(expected):raise ValueError('Missing auxiliary production entry')
    headers={f'nr_aux_{group}_precompiled.h' for group in aux.GROUPS}
    if set(data['headers_sha256'])!=headers:raise ValueError('Require every auxiliary embedded header')
    for name,digest in data['headers_sha256'].items():
        header=build/name
        if sha(header)!=digest:raise ValueError('Auxiliary embedded header mismatch')
        files.append(header)
    return {'enabled':True,'count':9,'receipt_sha256':sha(receipt),'headers_sha256':data['headers_sha256'],
            'files_sha256':{str(f.resolve()):sha(f) for f in files}}

def compare(left,right):
    a=left.read_bytes();b=right.read_bytes()
    if len(a)!=len(b) or len(a)%8:raise ValueError('Full FP16 plane size mismatch')
    total=0.;squared=0.;maximum=0.;changed=0
    for (x,),(y,) in zip(struct.iter_unpack('<e',a),struct.iter_unpack('<e',b)):
        if not math.isfinite(x) or not math.isfinite(y):raise ValueError('Nonfinite actual N')
        delta=abs(x-y);total+=delta;squared+=delta*delta;maximum=max(maximum,delta);changed+=delta!=0
    count=len(a)//2
    return dict(left_sha256=sha(left),right_sha256=sha(right),channels=count,
                mean_absolute=total/count,rms=math.sqrt(squared/count),max_absolute=maximum,
                changed_channels=changed,byte_equal=a==b)

def comparisons(out):
    rows=[]
    inputs={}
    for name in ('input-color.rgba16','input-depth.r32','input-motion.rg16'):
        hashes={run:sha(out/run/name) for run in ('a1','a2','b1','b2','aba1','aba2')}
        if len(set(hashes.values()))!=1:raise ValueError('Actual source/guide bytes differ between runs: '+name)
        inputs[name]=hashes
    for label,left,ls,right,rs in (
        ('cold_A_repeat','a1',0,'a2',0),('cold_B_repeat','b1',0,'b2',0),
        ('hot_A0_repeat','aba1',0,'aba2',0),('hot_B_repeat','aba1',1,'aba2',1),('hot_A2_repeat','aba1',2,'aba2',2),
        ('cold_A_vs_hot_A0','a1',0,'aba1',0),('cold_B_vs_hot_B','b1',0,'aba1',1),('cold_A_vs_hot_A2','a1',0,'aba1',2)):
        def events(run,segment):
            data=[json.loads(s) for s in (out/run/'events.jsonl').read_text().splitlines()]
            # Handles/attempts differ legitimately. Compare the actual bound
            # typed settings, extent/exposure/MV and per-model Reset sequence.
            return [{k:v for k,v in r.items() if k not in ('handle','segment')} for r in data if r['event'] in ('create','evaluate') and r['segment']==segment]
        if events(left,ls)!=events(right,rs):raise ValueError('Cold/hot actual parameter or Reset sequence differs: '+label)
        for frame in range(10):
            rows.append(dict(comparison=label,frame=frame,after_two_frame_warmup=frame>=2,
                             **compare(out/left/f's{ls}-n{frame:02}.rgba16',out/right/f's{rs}-n{frame:02}.rgba16')))
    return {'actual_input_sha256':inputs,'full_plane_comparisons':rows,'interpretation':'Measured drift only; no arbitrary output-equality acceptance threshold. Compare aligned same-frame repeated cold/hot baselines before attributing differences to switching.'}
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('compiler','reshade','source','link-core','output'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--run',action='store_true',help='Run real NVIDIA only after GPU coordination')
    p.set_defaults(real_ngx=True)
    p.add_argument('--nr-runtime',type=Path);p.add_argument('--sr-runtime',type=Path)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    source_files={str(f.resolve()):sha(f) for f in a.source.parent.rglob('*') if f.is_file()}
    signatures={}
    if not a.nr_runtime or not a.sr_runtime:p.error('Explicit signed NR and SR runtime paths required')
    expected_nr='e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e'
    if sha(a.nr_runtime)!=expected_nr:raise ValueError('This bounded test fixes the previously applicable 310.8 NR runtime; no DLL A/B')
    for name,path in (('nr',a.nr_runtime),('sr',a.sr_runtime)):
        env={k:v for k,v in os.environ.items() if k.casefold()!='psmodulepath'};env['NR_BOUNDARY_DLL']=str(path.resolve())
        command=['powershell.exe','-NoProfile','-NonInteractive','-Command','$ErrorActionPreference="Stop"; $s=Get-AuthenticodeSignature -LiteralPath $env:NR_BOUNDARY_DLL; @{status=[string]$s.Status;subject=$s.SignerCertificate.Subject;thumbprint=$s.SignerCertificate.Thumbprint}|ConvertTo-Json -Compress']
        r=subprocess.run(command,env=env,capture_output=True,text=True,timeout=60)
        (out/(name+'-signature.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
        if r.returncode:raise ValueError('Signature query failed')
        signatures[name]=json.loads(r.stdout)
        if signatures[name]['status']!='Valid' or 'NVIDIA' not in signatures[name]['subject'].upper():raise ValueError('Valid NVIDIA signature required')
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
        q=q.replace(old,'''Check(!nr_test_warp::Configured(),"Pinned test WARP excludes real NVIDIA");
        for(unsigned i=0;;++i){IDXGIAdapter1* candidate=nullptr;
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
        '#include "'+(ROOT/('tests/model_boundary_ngx.inl')).as_posix()+'"\n'+
        'int wmain(int argc,wchar_t** argv){std::setvbuf(stdout,nullptr,_IONBF,0);if(argc!=3)return 2;ID3D12Debug* debug=nullptr;if(SUCCEEDED(D3D12GetDebugInterface(IID_ID3D12Debug,(void**)&debug))){ID3D12Debug_EnableDebugLayer(debug);ReleaseCom(debug);std::puts("debug_layer=1");}else std::puts("debug_layer=0");'+
        'model_boundary_ngx::Run(argv[1],argv[2]);'+'ExitProcess(0);}\n',encoding='utf-8')
    core=a.link_core.resolve();build=core/'build'
    manifest=a.source.parent.parent.parent/'source-manifest.json';manifest_evidence={}
    if manifest.is_file():
        data=json.loads(manifest.read_text(encoding='utf-8-sig'))
        for relative,digest in data['files'].items():
            file=(manifest.parent/relative).resolve()
            if not file.is_relative_to(manifest.parent.resolve()) or sha(file)!=digest:raise ValueError('Frozen source manifest mismatch')
        manifest_evidence=dict(source_manifest_sha256=sha(manifest),source_commit=data['source_commit'],source_manifest_verified=True)
    if a.run:
        if not manifest_evidence or a.source.resolve()!=core/'src/nr_before_sr.cpp':raise ValueError('GPU evidence requires one exact generated source and linked Core')
        face=json.loads((build/'face-backend.json').read_text(encoding='utf-8-sig'))
        variant=json.loads((build/'build-variant.json').read_text(encoding='utf-8-sig'))
        if not face.get('enabled') or variant.get('sourcecommit')!=manifest_evidence['source_commit']:raise ValueError('Complete same-source Face1 build required')
        for row in variant['artifacts']:
            file=build/row['name']
            if sha(file)!=row['sha256'] or file.stat().st_size!=row['bytes']:raise ValueError('Final build artifact mismatch')
        codec=json.loads((build/'codec-artifacts.json').read_text(encoding='utf-8-sig'))
        body=a.source.read_text(encoding='utf-8').split('const char kCodecShader[] = R"HLSL(')[1].split(')HLSL";',1)[0]
        if hashlib.sha256(body.encode()).hexdigest()!=codec['source'] or sha(build/'nr_codec_precompiled.h')!=codec['header_sha256']:raise ValueError('Exact generated shader/header mismatch')
        for row in codec['artifacts']:
            if sha(build/row['file'])!=row['sha256']:raise ValueError('Shader payload mismatch')
        manifest_evidence.update(face_receipt_sha256=sha(build/'face-backend.json'),build_variant_sha256=sha(build/'build-variant.json'),codec_receipt_sha256=sha(build/'codec-artifacts.json'),codec_artifacts=len(codec['artifacts']))
    objects=[build/'obj'/n for n in ('buffer.o','hook.o','trampoline.o','hde64.o','nr_dual_spatial_d3d12.o','nr_face_detector.o','nr_face_worker.o')]+objects_for(core/'src/nr_before_sr.cpp')
    aux_object=build/'obj/nr_aux_shader_runtime.o';aux_evidence={'enabled':False};aux_flags=[]
    if aux_object.exists() or (build/'aux-artifacts.json').exists() or any(build.glob('nr_aux_*_precompiled.h')):
        if not aux_object.is_file():raise ValueError('Partial auxiliary build; wait for final timing object')
        aux_evidence=audit_aux(build,a.source.parent);objects.append(aux_object)
        aux_flags=['-DNR_AUX_PRECOMPILED=1','-DNR_AUX_TIMING=1']
    elif a.run and (a.source.parent/'nr_aux_shader_runtime.cpp').is_file():
        raise ValueError('Final Core requires precompiled auxiliary path; no runtime-compile fallback in GPU acceptance')
    fixture_inputs=[queue,post,descriptor,ROOT/'tests/ci_warp_runtime.h',ROOT/'tests/native_observer_device.inl',ROOT/'tests/model_boundary_ngx.inl',Path(__file__)]
    frozen_inputs={str(f.resolve()):sha(f) for f in fixture_inputs+objects+[build/'nr_codec_precompiled.h']}
    frozen_inputs.update(aux_evidence.get('files_sha256',{}))
    command=[str(a.compiler),'-std=c++20','-O1','-DNDEBUG','-DUNICODE','-D_UNICODE','-D_WIN64','-DNR_FACE_BACKEND=1','-DNR_CODEC_PRECOMPILED=1',*aux_flags,
             '-ffunction-sections','-fdata-sections','-static','-municode','-Wl,--gc-sections','-I'+str(build),'-I'+str(ROOT/'tests'),
             '-I'+str(core/'third_party/minhook/include'),'-I'+str(a.reshade/'include'),'-I'+str(a.reshade/'deps/imgui'),str(wrapper),*map(str,objects),
             '-o',str(out/'capture.exe'),'-lkernel32','-luser32','-ld3d12','-ldxgi','-ldxguid','-lole32','-luuid','-ld3dcompiler','-lshell32','-lbcrypt']
    records=[]
    def run(cmd,name):
        try:r=subprocess.run(cmd,cwd=out,text=True,capture_output=True,encoding='utf-8',errors='replace',timeout=240)
        except subprocess.TimeoutExpired as error:
            def text(v):return v.decode('utf-8','replace') if isinstance(v,bytes) else v or ''
            r=subprocess.CompletedProcess(cmd,124,text(error.stdout),text(error.stderr)+'\nBounded host timed out; failure retained.\n')
        (out/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');records.append({'command':cmd,'exit':r.returncode})
        print((r.stdout+r.stderr)[-10000:] if r.returncode else r.stdout[-2000:]);return r.returncode
    result=run(command,'compile')
    if not result and a.real_ngx:
        import shutil
        for source,name in ((a.nr_runtime,'nvngx_dlssnr.dll'),(a.sr_runtime,'nvngx_dlss.dll'),(build/'nrchain_nvngx.dll','nrchain_nvngx.dll')):
            shutil.copyfile(source,out/name)
    comparison=None
    if not result and a.run:
        for scenario in ('a1','a2','b1','b2','aba1','aba2'):
            directory=out/scenario;directory.mkdir()
            result=run([str(out/'capture.exe'),str(directory),scenario.rstrip('12')],'run-'+scenario)
            if result:break
        if not result:
            try:comparison=comparisons(out)
            except Exception as error:result=1;comparison={'error':str(error)}
    receipt={'result':result,'run_requested':a.run,'warp_only':not a.real_ngx,'real_ngx':a.real_ngx,'constructed_model_planes':not a.real_ngx,'commands':records,
             'core_source':str(a.source),'core_sha256':sha(a.source),'linked_objects':{str(o):sha(o) for o in objects},
             'fixture_sha256':sha(ROOT/('tests/model_boundary_ngx.inl')),
             'game_verified':False,'static_artificial_guides':a.real_ngx}
    receipt.update(runtime_signatures=signatures,source_files_sha256=source_files,comparison=comparison,frozen_fixture_and_build_inputs=frozen_inputs,auxiliary_precompiled=aux_evidence,auxiliary_compile_flags=aux_flags,
                   actual_gpu_executed=any(r['command'][0].endswith('capture.exe') for r in records),actual_model_output_compared=a.run and result==0,
                   model_scope='fixed NR runtime, intensity A=1.2 B=1.7, normal Core A-B-A recreation; static artificial planar scene, 320x192, zero MV, fixed exposure; no Preset/runtime callbacks',
                   source_and_link_core_may_differ=a.source.resolve()!=core/'src/nr_before_sr.cpp')
    if any(sha(Path(f))!=digest for f,digest in (source_files|frozen_inputs).items()):
        result=1;receipt.update(result=1,source_changed_during_test=True)
    receipt.update(manifest_evidence)
    if manifest.is_file():
        manifest_data=json.loads(manifest.read_text(encoding='utf-8-sig'))
        receipt['source_manifest_sha256']=sha(manifest);receipt['source_commit']=manifest_data.get('source_commit')
        for relative,digest in manifest_data['files'].items():
            f=(manifest.parent/relative).resolve()
            if not f.is_relative_to(manifest.parent.resolve()) or sha(f)!=digest:raise ValueError('Frozen source manifest mismatch')
        receipt['source_manifest_verified']=True
    receipt['runtime_artifacts']={name:sha(out/name) for name in ('nvngx_dlssnr.dll','nvngx_dlss.dll','nrchain_nvngx.dll') if (out/name).is_file()}
    receipt['binary_sha256']=sha(out/'capture.exe') if (out/'capture.exe').is_file() else None
    receipt['core_modules_sha256']={f.name:sha(f) for f in a.source.parent.glob('nr_native_output_capture*') if f.is_file()}
    receipt['stage_source_sha256']=sha(a.source.parent/'nr_stage_capture.inl')
    (out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8');raise SystemExit(result)
if __name__=='__main__':main()
