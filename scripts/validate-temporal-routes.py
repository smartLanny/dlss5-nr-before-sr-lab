"""Actual complete-Core pre/RR compositor entry probes; synthetic opaque models."""
import argparse,hashlib,json,shutil,subprocess
from pathlib import Path
from temporal_test_objects import objects_for
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('compiler','reshade','source','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--compile-only',action='store_true')
    p.add_argument('--codec-cache',type=Path,help='Exact source-keyed published shader cache; every payload hash is verified.')
    p.add_argument('--case',default='all',choices=('all','pre','pre-scale','pre-off','pre-hdr','pre-hdr-r11','pre-native','pre-later','rr','rr-later','rr-off','rr-hdr','rr-native'))
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    source=a.source.resolve();snapshot=out/'core/src';shutil.copytree(source.parent,snapshot)
    queue=ROOT/'tests/queue_retirement_harness.cpp';post=ROOT/'tests/post_output_harness.inl';fixture=ROOT/'tests/temporal_routes_gpu.inl'
    q=queue.read_text(encoding='utf-8').split('void OneShot() {',1)[0]+'\n}\n'
    old='    Check(WaitForSingleObject(event, 5000) == WAIT_OBJECT_0, "exact fence completed");'
    if q.count(old)!=1:raise ValueError('Shared bounded completion diagnostic anchor changed')
    # Published O3 shaders can be expensive on WARP's first execution. This is
    # still a bounded real fence wait, never a CPU signal or completion stub.
    q=q.replace(old,'''    const auto waited=WaitForSingleObject(event,60000);
    if(waited!=WAIT_OBJECT_0){ID3D12Device* d=nullptr;ID3D12Fence_GetDevice(fence,IID_ID3D12Device,(void**)&d);
        std::printf("fence wait=%lu error=%lu done=%llu required=%llu removed=%08lx\\n",waited,GetLastError(),ID3D12Fence_GetCompletedValue(fence),value,d?ID3D12Device_GetDeviceRemovedReason(d):E_FAIL);ReleaseCom(d);}
    Check(waited==WAIT_OBJECT_0,"exact fence completed");''')
    s=post.read_text(encoding='utf-8').split('std::vector<unsigned char> Case(queue_test::Fixture',1)[0]+'\n}\n'
    (out/'fixture.inl').write_text(q+s+fixture.read_text(encoding='utf-8'),encoding='utf-8')
    cpp=out/'host.cpp';cpp.write_text('#include "'+(snapshot/'nr_before_sr.cpp').as_posix()+'"\n#include "fixture.inl"\nint main(int argc,char**argv){std::setvbuf(stdout,nullptr,_IONBF,0);temporal_route_gpu::Run(argc>1?argv[1]:"pre");}\n',encoding='utf-8')
    cxx=a.compiler.resolve();cc=cxx.with_name(cxx.name.replace('clang++','clang'))
    flags=['-O1','-DNDEBUG','-DUNICODE','-D_UNICODE','-D_WIN64','-ffunction-sections','-fdata-sections','-I'+str(ROOT/'third_party/minhook/include'),'-I'+str(a.reshade/'include'),'-I'+str(a.reshade/'deps/imgui')]
    record={'passed':False,'synthetic_model':True,'synthetic_native_frame':True,'actual_core_entry':True,'warp_only':True,'nvidia_feature18':False,'game_verified':False,'source_sha256':{str(f.relative_to(snapshot)):sha(f) for f in snapshot.rglob('*') if f.is_file()},'fixture_sha256':sha(fixture),'commands':[],'cases':[]}
    if a.codec_cache:
        cache=a.codec_cache.resolve();receipt=cache/'codec-artifacts.json';data=json.loads(receipt.read_text(encoding='utf-8'))
        body=source.read_text(encoding='utf-8').split('const char kCodecShader[] = R"HLSL(')[1].split(')HLSL";',1)[0]
        if hashlib.sha256(body.encode()).hexdigest()!=data['source']:raise ValueError('Cache is not this complete generated production shader')
        header=cache/'nr_codec_precompiled.h'
        if sha(header)!=data['header_sha256']:raise ValueError('Embedded shader header hash mismatch')
        for item in data['artifacts']:
            if sha(cache/item['file'])!=item['sha256']:raise ValueError('Cached shader payload mismatch')
        shutil.copyfile(header,snapshot/'nr_codec_precompiled.h');flags.append('-DNR_CODEC_PRECOMPILED=1')
        record['codec_cache']={'receipt_sha256':sha(receipt),'source':data['source'],'header_sha256':sha(header),'artifacts':len(data['artifacts'])}
        record['source_sha256']['nr_codec_precompiled.h']=sha(header)
    def save():(out/'result.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
    def run(cmd,name,timeout=240):
        r=subprocess.run(list(map(str,cmd)),cwd=out,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=timeout)
        (out/(name+'.txt')).write_text(r.stdout+r.stderr,encoding='utf-8');record['commands'].append({'command':list(map(str,cmd)),'exit':r.returncode});save()
        if r.returncode:print((r.stdout+r.stderr)[-8000:]);raise SystemExit(r.returncode)
        return r
    objects=list(objects_for(source));record['temporal_objects_sha256']={str(f):sha(f) for f in objects}
    for rel in ('buffer.c','hook.c','trampoline.c','hde/hde64.c'):
        obj=out/(Path(rel).stem+'.o');run([cc,*flags,'-c',ROOT/'third_party/minhook/src'/rel,'-o',obj],'compile-'+obj.stem);objects.append(obj)
    spatial=snapshot/'nr_dual_spatial_d3d12.cpp';obj=out/'spatial.o';run([cxx,*flags,'-std=c++20','-c',spatial,'-o',obj],'compile-spatial');objects.append(obj)
    exe=out/'temporal-routes.exe';run([cxx,*flags,'-std=c++20','-static','-Wl,--gc-sections',cpp,*objects,'-o',exe,'-lkernel32','-luser32','-ld3d12','-ldxgi','-ldxguid','-lole32','-luuid','-ld3dcompiler','-lshell32'],'compile-core')
    record['binary_sha256']=sha(exe)
    if not a.compile_only:
        cases=('pre','pre-scale','pre-off','pre-hdr','pre-hdr-r11','pre-native','pre-later','rr','rr-later','rr-off','rr-hdr','rr-native') if a.case=='all' else (a.case,)
        for case in cases:
            r=run([exe,case],'run-'+case);print(r.stdout);record['cases'].append({'name':case,'passed':True})
    record['passed']=True;record['compile_only']=a.compile_only;save()
if __name__=='__main__':main()
