#!/usr/bin/env python3
"""Validate real pre-Result storage through complete staged Core on WARP only."""
from pathlib import Path
from temporal_test_objects import objects_for
import argparse
import hashlib
import json
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',type=Path,required=True)
    p.add_argument('--reshade',type=Path,required=True)
    p.add_argument('--source',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--compile-only',action='store_true')
    a=p.parse_args();out=a.output.resolve();source=a.source.resolve(strict=True)
    if out.exists():raise SystemExit('Preserve previous probe evidence; use a fresh output directory.')
    out.mkdir(parents=True)
    snapshot=out/'core/src';shutil.copytree(source.parent,snapshot)
    hashes={str(f.relative_to(snapshot)):sha(f) for f in sorted(snapshot.rglob('*')) if f.is_file()}
    queue=ROOT/'tests/queue_retirement_harness.cpp'
    warp_helper=out/'ci_warp_runtime.h'
    warp_helper.write_bytes((ROOT/'tests/ci_warp_runtime.h').read_bytes())
    post=ROOT/'tests/post_output_harness.inl'
    native=ROOT/'tests/native_color_view_harness.inl'
    fixture=ROOT/'tests/pre_store_probe_harness.inl'
    q=queue.read_text(encoding='utf-8').split('void OneShot() {',1)[0]+'\n} // namespace queue_test\n'
    if 'struct Fixture' not in q:raise SystemExit('Shared WARP fixture boundary changed.')
    parts=[q,post.read_text(encoding='utf-8'),native.read_text(encoding='utf-8'),fixture.read_text(encoding='utf-8')]
    (out/'fixture.inl').write_text('\n'.join(parts),encoding='utf-8')
    wrapper=out/'pre-store.cpp'
    wrapper.write_text('#include "'+(snapshot/'nr_before_sr.cpp').as_posix()+'"\n#include "fixture.inl"\n'
        'int main(){std::setvbuf(stdout,nullptr,_IONBF,0);pre_store_test::Run();return 0;}\n',encoding='utf-8')
    compiler=a.compiler.resolve(strict=True)
    c_compiler=compiler.with_name(compiler.name.replace('clang++','clang'))
    common=['-O1','-DNDEBUG','-DUNICODE','-D_UNICODE','-D_WIN64','-ffunction-sections','-fdata-sections',
            '-I'+str(ROOT/'third_party/minhook/include'),'-I'+str(a.reshade.resolve()/'include'),
            '-I'+str(a.reshade.resolve()/'deps/imgui')]
    def run(command,name,timeout=180):
        r=subprocess.run(command,cwd=out,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=timeout)
        (out/(name+'.txt')).write_text(r.stdout+r.stderr,encoding='utf-8')
        if r.returncode:print((r.stdout+r.stderr)[-12000:])
        return r
    objects=list(map(str,objects_for(source)))
    for rel in ('buffer.c','hook.c','trampoline.c','hde/hde64.c'):
        obj=out/(Path(rel).stem+'.o')
        r=run([str(c_compiler),*common,'-std=c17','-c',str(ROOT/'third_party/minhook/src'/rel),'-o',str(obj)],'compile-'+obj.stem)
        if r.returncode:raise SystemExit(r.returncode)
        objects.append(str(obj))
    # D13+ full Core references its out-of-line spatial implementation even
    # when this fixture does not enable NR2. Compile the same frozen source.
    dual_source = snapshot / 'nr_dual_spatial_d3d12.cpp'
    if dual_source.is_file(): objects.append(str(dual_source))
    exe=out/'pre-store-probe.exe'
    r=run([str(compiler),*common,'-std=c++20','-static','-Wl,--gc-sections',str(wrapper),*objects,'-o',str(exe),
           '-lkernel32','-luser32','-ld3d12','-ldxgi','-ldxguid','-lole32','-luuid','-ld3dcompiler','-lshell32'],'compile-core')
    if r.returncode:raise SystemExit(r.returncode)
    record={'schema':1,'source_inputs_sha256':hashes,'fixture_sha256':sha(fixture),'binary_sha256':sha(exe),
            'borrowed_helpers_sha256':{f.name:sha(f) for f in (queue,post,native,warp_helper)},
            'production_translation_unit':True,'warp_only':True,'nvidia_nr':False,'game_verified':False,'run_exit':None}
    if not a.compile_only:
        r=run([str(exe)],'run',120);record['run_exit']=r.returncode;print(r.stdout)
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    raise SystemExit(r.returncode)
if __name__=='__main__':main()
