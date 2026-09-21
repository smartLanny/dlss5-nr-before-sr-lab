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
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--compile-only',action='store_true')
    p.add_argument('--temporal',action='store_true',help='Run the actual Core temporal seam instead of color-view cases.')
    a=p.parse_args();out=a.output.resolve();source=a.source.resolve(strict=True)
    if out.exists():raise SystemExit('Preserve existing color-view evidence; use a fresh output directory.')
    out.mkdir(parents=True)
    snapshot=out/'core/src';shutil.copytree(source.parent,snapshot)
    original_runtime=sha(snapshot/'nr_native_format_runtime.inl')
    if a.runtime:shutil.copyfile(a.runtime,snapshot/'nr_native_format_runtime.inl')
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
    fixture=ROOT/('tests/temporal_core_harness.inl' if a.temporal else 'tests/native_color_view_harness.inl')
    (out/'fixture.inl').write_text(q+s+fixture.read_text(encoding='utf-8'),encoding='utf-8')
    wrapper=out/'native-color.cpp'
    wrapper.write_text('#include "'+(snapshot/'nr_before_sr.cpp').as_posix()+'"\n#include "fixture.inl"\n'
        'int main(){std::setvbuf(stdout,nullptr,_IONBF,0);'+('temporal_core_test' if a.temporal else 'native_color_test')+'::Run();return 0;}\n',encoding='utf-8')
    compiler=a.compiler.resolve(strict=True)
    c_compiler=compiler.with_name(compiler.name.replace('clang++','clang'))
    common=['-O1','-DNDEBUG','-DUNICODE','-D_UNICODE','-D_WIN64','-ffunction-sections','-fdata-sections',
            '-I'+str(ROOT/'third_party/minhook/include'),'-I'+str(a.reshade.resolve()/'include'),
            '-I'+str(a.reshade.resolve()/'deps/imgui')]
    def run(command,name,timeout=180):
        r=subprocess.run(command,cwd=out,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=timeout)
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
    r=run([str(compiler),*common,'-std=c++20','-static','-Wl,--gc-sections',str(wrapper),*objects,'-o',str(exe),
           '-lkernel32','-luser32','-ld3d12','-ldxgi','-ldxguid','-lole32','-luuid','-ld3dcompiler','-lshell32'],'compile-core')
    if r.returncode:raise SystemExit(r.returncode)
    record={'schema':1,'source_inputs_sha256':hashes,'original_runtime_sha256':original_runtime,
            'explicit_runtime_overlay':bool(a.runtime),'fixture_sha256':sha(fixture),
            'queue_helpers_sha256':sha(queue),'post_helpers_sha256':sha(post),'binary_sha256':sha(exe),
            'production_translation_unit':True,'temporal_seam':a.temporal,'warp_only':True,'nvidia_nr':False,'game_verified':False,'run_exit':None,
            'temporal_objects_sha256':{str(p):sha(p) for p in objects_for(source)}}
    if not a.compile_only:
        r=run([str(exe)],'run',120);record['run_exit']=r.returncode;print(r.stdout)
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    raise SystemExit(r.returncode)
if __name__=='__main__':main()
