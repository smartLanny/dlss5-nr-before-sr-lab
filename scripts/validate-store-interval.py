#!/usr/bin/env python3
"""Compile full frozen Core and run actual CPU readback + shader compilation.

No D3D device, GPU execution, NGX, game, or source-function extraction.
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--reshade',type=Path,required=True)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--negative-control',choices=('ignore-mismatch','accept-incomplete'))
    args=parser.parse_args();out=args.build_dir.resolve();source=args.source.resolve(strict=True)
    if out.exists():raise SystemExit('Preserve prior evidence; choose a fresh build directory.')
    out.mkdir(parents=True);snapshot=out/'source/src';shutil.copytree(source.parent,snapshot)
    inputs={p.relative_to(snapshot).as_posix():sha(p) for p in sorted(snapshot.rglob('*')) if p.is_file()}
    mutation=None
    if args.negative_control=='ignore-mismatch':
        mutation=('result.match=result.known && counts.mismatched==0;','result.match=result.known;',
                  'STORE_INTERVAL_ONE_MISMATCH')
    elif args.negative_control=='accept-incomplete':
        mutation=('result.flags|=CoverageIncomplete;','result.flags|=0;', 'STORE_INTERVAL_MISSING_EXPECTATION')
    if mutation:
        path=snapshot/'nr_store_comparison.h';body=path.read_text(encoding='utf-8')
        if body.count(mutation[0])!=1:raise SystemExit('Negative-control anchor changed.')
        path.write_text(body.replace(mutation[0],mutation[1],1),encoding='utf-8')
    fixture=ROOT/'tests/store_interval_cpu.inl';shutil.copy2(fixture,out/'fixture.inl')
    fixture_shader=ROOT/'tests/store_interval_shader.h';shutil.copy2(fixture_shader,out/fixture_shader.name)
    wrapper=out/'store-interval-core.cpp'
    wrapper.write_text('#include "'+(snapshot/source.name).as_posix()+'"\n#include "fixture.inl"\n',encoding='utf-8')
    compiler=args.compiler.resolve(strict=True);cc=compiler.with_name(compiler.name.replace('clang++','clang'))
    common=['-O1','-DNDEBUG','-DUNICODE','-D_UNICODE','-D_WIN64','-ffunction-sections','-fdata-sections',
            '-I'+str(ROOT/'third_party/minhook/include'),'-I'+str(args.reshade.resolve()/'include'),
            '-I'+str(args.reshade.resolve()/'deps/imgui')]
    def run(command,name,timeout=180):
        result=subprocess.run(command,cwd=out,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=timeout)
        (out/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        if result.returncode:print((result.stdout+result.stderr)[-10000:])
        return result
    objects=[]
    for rel in ('buffer.c','hook.c','trampoline.c','hde/hde64.c'):
        obj=out/(Path(rel).stem+'.o')
        result=run([str(cc),*common,'-std=c17','-c',str(ROOT/'third_party/minhook/src'/rel),'-o',str(obj)],'compile-'+obj.stem)
        if result.returncode:raise SystemExit(result.returncode)
        objects.append(str(obj))
    binary=out/'store-interval.exe'
    result=run([str(compiler),*common,'-std=c++20','-static','-Wl,--gc-sections',str(wrapper),*objects,'-o',str(binary),
        '-lkernel32','-luser32','-ld3d12','-ldxgi','-ldxguid','-lole32','-luuid','-ld3dcompiler','-lshell32'],'compile-core')
    if result.returncode:raise SystemExit(result.returncode)
    result=run([str(binary)],'run',60)
    passed=result.returncode==41 and ('FAIL '+mutation[2]) in result.stdout if mutation else result.returncode==0
    record={'gate':'production UNORM interval collection CPU and shader compile','passed':passed,
        'full_production_translation_unit':True,'source_function_extraction':False,'source':str(source),
        'source_sha256':sha(source),'original_source_inputs_sha256':inputs,
        'compiled_source_inputs_sha256':{p.relative_to(snapshot).as_posix():sha(p) for p in sorted(snapshot.rglob('*')) if p.is_file()},
        'fixture_sha256':sha(fixture),'runner_sha256':sha(Path(__file__)),'binary_sha256':sha(binary),
        'fixture_shader_sha256':sha(fixture_shader),
        'compiler_sha256':sha(compiler),'negative_control':args.negative_control,'run_exit':result.returncode,
        'synthetic_cpu_resource_and_fence':True,'shader_compilation_only':True,'gpu_executed':False,'ngx_executed':False,'game_verified':False}
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    if not mutation:print(result.stdout.strip())
    print('PASS' if passed else 'FAIL',record['gate'],args.negative_control or '')
    if not passed:raise SystemExit(1)
if __name__=='__main__':main()
