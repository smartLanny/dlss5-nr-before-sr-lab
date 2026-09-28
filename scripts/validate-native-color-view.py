#!/usr/bin/env python3
"""Exercise complete staged Core color-view/slot/shader paths in one WARP host."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
from temporal_test_objects import objects_for

ROOT=Path(__file__).resolve().parents[1]
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',type=Path,required=True)
    p.add_argument('--reshade',type=Path,required=True)
    p.add_argument('--source',type=Path,required=True)
    p.add_argument('--runtime',type=Path,help='Explicit latest runtime overlay for a focused unfrozen-source check.')
    p.add_argument('--codec-cache',type=Path,help='Exact generated-Core shader cache; validates source key, header and payload hashes.')
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--compile-only',action='store_true')
    p.add_argument('--temporal',action='store_true',help='Run the actual Core temporal seam instead of color-view cases.')
    p.add_argument('--acceptance',action='store_true',help='Exercise the actual transient configuration controller; no model or game.')
    p.add_argument('--acceptance-negative',action='store_true',help='Copied-source wrong post-precision restoration negative control.')
    p.add_argument('--acceptance-timing-negative',action='store_true',help='Copied-source missing real RR q1/q2 negative control.')
    p.add_argument('--acceptance-commit-pressure',action='store_true',help='Actual SR/RR entry with a test-only low system-commit reading; no vendor Create.')
    p.add_argument('--temporal-case',choices=('all','continuous','unsubmitted','replay','submit-reset'),default='all',
                   help='Separate processes preserve each real Core quarantine latch.')
    p.add_argument('--temporal-negative',action='store_true',help='Explicit copied-source wrong-P1 binding negative control.')
    a=p.parse_args();out=a.output.resolve();source=a.source.resolve(strict=True)
    if a.acceptance and a.temporal:p.error('Choose one production fixture.')
    if a.acceptance_negative and not a.acceptance:p.error('--acceptance-negative requires --acceptance')
    if a.acceptance_timing_negative and (not a.acceptance or a.acceptance_negative):
        p.error('--acceptance-timing-negative requires standalone --acceptance')
    if a.acceptance_commit_pressure and (not a.acceptance or a.acceptance_negative or a.acceptance_timing_negative):
        p.error('--acceptance-commit-pressure requires standalone --acceptance')
    if a.acceptance and not a.codec_cache:
        p.error('--acceptance requires --codec-cache from the same generated Core')
    if out.exists():raise SystemExit('Preserve existing color-view evidence; use a fresh output directory.')
    out.mkdir(parents=True)
    snapshot=out/'core/src';shutil.copytree(source.parent,snapshot)
    if a.acceptance_commit_pressure and 'DeferNrCreateForCommitPressure' not in (snapshot/'nr_before_sr.cpp').read_text(encoding='utf-8'):
        raise ValueError('Complete generated Core lacks the commit-pressure production admission')
    original_runtime=sha(snapshot/'nr_native_format_runtime.inl')
    if a.runtime:shutil.copyfile(a.runtime,snapshot/'nr_native_format_runtime.inl')
    cache_receipt=None
    if a.codec_cache:
        cache=a.codec_cache.resolve(strict=True)
        receipt=cache/'codec-artifacts.json';data=json.loads(receipt.read_text(encoding='utf-8'))
        body=source.read_text(encoding='utf-8').split('const char kCodecShader[] = R"HLSL(',1)[1].split(')HLSL";',1)[0]
        if hashlib.sha256(body.encode()).hexdigest()!=data['source']:
            raise ValueError('Cache is not this complete generated production shader')
        header=cache/'nr_codec_precompiled.h'
        if sha(header)!=data['header_sha256']:raise ValueError('Embedded shader header hash mismatch')
        for item in data['artifacts']:
            if sha(cache/item['file'])!=item['sha256']:raise ValueError('Cached shader payload mismatch')
        shutil.copyfile(header,snapshot/'nr_codec_precompiled.h')
        cache_receipt={'receipt_sha256':sha(receipt),'source':data['source'],
                       'header_sha256':sha(header),'artifacts':len(data['artifacts'])}
    if a.acceptance_negative:
        target=snapshot/'nr_acceptance_runtime.inl';text=target.read_text(encoding='utf-8')
        old='nr_post_settings::RequestForCapture(s->post_original);'
        if text.count(old)!=1:raise ValueError('Acceptance restoration anchor drift')
        target.write_text(text.replace(old,'nr_post_settings::RequestForCapture({s->post_original.rr,100});'),encoding='utf-8')
    if a.acceptance_timing_negative:
        target=snapshot/'interop/nr_rr_post_runtime.inl';text=target.read_text(encoding='utf-8')
        for query in (1,2):
            old=f'nr_acceptance_gpu::Stamp(before.command_list,*prepared.slot,{query});'
            if text.count(old)!=1:raise ValueError('Real RR timestamp anchor drift: '+old)
            text=text.replace(old,'/* negative control omits RR q'+str(query)+' */')
        target.write_text(text,encoding='utf-8')
    if a.temporal_negative:
        if not a.temporal:p.error('--temporal-negative requires --temporal')
        target=snapshot/'nr_temporal_runtime.inl';text=target.read_text(encoding='utf-8')
        old='views.Srv(2,slot.neural,DXGI_FORMAT_R16G16B16A16_FLOAT);'
        if text.count(old)!=1:raise ValueError('Wrong-P1 negative-control anchor drift')
        target.write_text(text.replace(old,'views.Srv(2,slot.proxy,DXGI_FORMAT_R16G16B16A16_FLOAT);'),encoding='utf-8')
    hashes={str(f.relative_to(snapshot)):sha(f) for f in sorted(snapshot.rglob('*')) if f.is_file()}
    # Borrow only the unchanged real-WARP fixture helpers, not its queue tests
    # or main. The shared files are never edited and their inputs are hashed.
    queue=ROOT/'tests/queue_retirement_harness.cpp';post=ROOT/'tests/post_output_harness.inl'
    q=queue.read_text(encoding='utf-8').split('void OneShot() {',1)[0]+'\n} // namespace queue_test\n'
    post_text=post.read_text(encoding='utf-8')
    post_boundary='std::vector<unsigned char> Case(queue_test::Fixture'
    if post_text.count(post_boundary)!=1:raise SystemExit('Shared post helper boundary changed.')
    s=post_text.split(post_boundary,1)[0]+'\n} // namespace post_output_test\n'
    if 'struct Fixture' not in q or 'ReadTexture(' not in s:raise SystemExit('Shared fixture helper boundary changed.')
    fixture=ROOT/('tests/acceptance_core_harness.inl' if a.acceptance else 'tests/temporal_core_harness.inl' if a.temporal else 'tests/native_color_view_harness.inl')
    (out/'fixture.inl').write_text(q+s+fixture.read_text(encoding='utf-8'),encoding='utf-8')
    wrapper=out/'native-color.cpp'
    wrapper.write_text('#include "'+(snapshot/'nr_before_sr.cpp').as_posix()+'"\n#include "fixture.inl"\n'
        'int main(int argc,char** argv){std::setvbuf(stdout,nullptr,_IONBF,0);'+
        ('if(argc>1&&std::string(argv[1])=="pressure-bounds")acceptance_core_test::RunPressureBounds();else if(argc>1&&std::string(argv[1])=="pressure-rr")acceptance_core_test::RunPressure(false);else if(argc>1&&std::string(argv[1])=="pressure-sr")acceptance_core_test::RunPressure(true);else if(argc>1&&std::string(argv[1])=="sr-entry")acceptance_core_test::RunSr();else acceptance_core_test::Run()' if a.acceptance else 'temporal_core_test::Run(argc>1?argv[1]:"continuous")' if a.temporal else 'native_color_test::Run()')+';return 0;}\n',encoding='utf-8')
    compiler=a.compiler.resolve(strict=True)
    c_compiler=compiler.with_name(compiler.name.replace('clang++','clang'))
    common=['-O1','-DNDEBUG','-DUNICODE','-D_UNICODE','-D_WIN64','-ffunction-sections','-fdata-sections',
            '-I'+str(ROOT/'third_party/minhook/include'),'-I'+str(a.reshade.resolve()/'include'),
            '-I'+str(a.reshade.resolve()/'deps/imgui')]
    if cache_receipt:common.append('-DNR_CODEC_PRECOMPILED=1')
    if a.acceptance_commit_pressure:common.append('-DNR_CREATE_COMMIT_TEST=1')
    def run(command,name,timeout=180):
        try:
            r=subprocess.run(command,cwd=out,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=timeout)
        except subprocess.TimeoutExpired as exc:
            def partial(value):
                return value.decode('utf-8','replace') if isinstance(value,bytes) else value or ''
            stdout,stderr=partial(exc.stdout),partial(exc.stderr)
            (out/(name+'.timeout.json')).write_text(json.dumps({
                'command':list(map(str,command)),'timeout_seconds':timeout,
                'stdout_file':name+'.stdout.partial.txt','stderr_file':name+'.stderr.partial.txt'},indent=2)+'\n',encoding='utf-8')
            (out/(name+'.stdout.partial.txt')).write_text(stdout,encoding='utf-8')
            (out/(name+'.stderr.partial.txt')).write_text(stderr,encoding='utf-8')
            r=subprocess.CompletedProcess(command,124,stdout,stderr)
        (out/(name+'.txt')).write_text(r.stdout+r.stderr,encoding='utf-8')
        if r.returncode:print((r.stdout+r.stderr)[-14000:])
        return r
    objects=list(map(str,objects_for(source)))
    # The Core's dual spatial declarations are public-header only; compile the
    # captured D3D12 implementation as its real sibling translation unit.
    spatial_source=snapshot/'nr_dual_spatial_d3d12.cpp'
    if spatial_source.is_file():
        spatial_object=out/'nr_dual_spatial_d3d12.o'
        r=run([str(compiler),*common,'-std=c++20','-c',str(spatial_source),'-o',str(spatial_object)],
              'compile-nr-dual-spatial')
        if r.returncode:raise SystemExit(r.returncode)
        objects.append(str(spatial_object))
    for rel in ('buffer.c','hook.c','trampoline.c','hde/hde64.c'):
        obj=out/(Path(rel).stem+'.o')
        r=run([str(c_compiler),*common,'-std=c17','-c',str(ROOT/'third_party/minhook/src'/rel),'-o',str(obj)],'compile-'+obj.stem)
        if r.returncode:raise SystemExit(r.returncode)
        objects.append(str(obj))
    exe=out/'native-color-view.exe'
    r=run([str(compiler),*common,'-std=c++20',*(['-Werror=format'] if a.acceptance else []),'-static','-Wl,--gc-sections',str(wrapper),*objects,'-o',str(exe),
           '-lkernel32','-luser32','-ld3d12','-ldxgi','-ldxguid','-lole32','-luuid','-ld3dcompiler','-lshell32','-lbcrypt'],'compile-core')
    if r.returncode:raise SystemExit(r.returncode)
    record={'schema':1,'source_inputs_sha256':hashes,'original_runtime_sha256':original_runtime,
            'codec_cache':cache_receipt,
            'explicit_runtime_overlay':bool(a.runtime),'fixture_sha256':sha(fixture),
            'queue_helpers_sha256':sha(queue),'post_helpers_sha256':sha(post),'binary_sha256':sha(exe),
            'production_translation_unit':True,'acceptance_controller':a.acceptance,'temporal_seam':a.temporal,'wrong_p1_negative_control':a.temporal_negative,'wrong_restoration_negative_control':a.acceptance_negative,'rr_timestamp_negative_control':a.acceptance_timing_negative,'commit_pressure_negative_control':a.acceptance_commit_pressure,'warp_only':True,'nvidia_nr':False,'game_verified':False,'run_exit':None,
            'temporal_objects_sha256':{str(p):sha(p) for p in objects_for(source)}}
    if not a.compile_only:
        cases=(['continuous','unsubmitted','replay','submit-reset'] if a.temporal_case=='all' else [a.temporal_case]) if a.temporal else (['pressure-bounds','pressure-rr','pressure-sr'] if a.acceptance_commit_pressure else [None] if a.acceptance_timing_negative or a.acceptance_negative else [None,'sr-entry']) if a.acceptance else [None]
        if a.temporal_negative:cases=['continuous']
        records=[];logs=[]
        for case in cases:
            name='run-'+case if case else 'run'
            r=run([str(exe)]+([case] if case else []),name,180 if a.temporal else 120)
            records.append({'case':case,'run_exit':r.returncode});logs.append(r.stdout+r.stderr);print(r.stdout)
            if r.returncode:break
        (out/'run.txt').write_text('\n'.join(logs),encoding='utf-8')
        record['run_exit']=r.returncode;record['cases']=records
        if a.temporal_negative:
            record['negative_control_rejected']=r.returncode==41 and 'FAIL P1_IS_EXISTING_SINGLE_LAYER_COMPOSITION_ORACLE' in r.stdout
            r.returncode=0 if record['negative_control_rejected'] else 1
        if a.acceptance_negative:
            record['negative_control_rejected']=r.returncode==41 and 'FAIL actual configuration restored' in r.stdout
            r.returncode=0 if record['negative_control_rejected'] else 1
        if a.acceptance_timing_negative:
            record['negative_control_rejected']=r.returncode==41 and 'FAIL real RR entry emits q0-q3' in r.stdout
            r.returncode=0 if record['negative_control_rejected'] else 1
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    raise SystemExit(r.returncode)
if __name__=='__main__':main()
