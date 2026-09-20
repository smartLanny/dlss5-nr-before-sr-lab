"""Build only official FFX 1.1.4 OF/DX12 and the standalone experiment.

Dependencies and generated binaries stay outside the repository. No SDK source
or prebuilt library from another ABI/version is substituted.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

PIN = 'c6efa6bf7f2027b3ec94f28578bb5965eabb9e55'

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--sdk', type=Path, required=True)
    ap.add_argument('--compiler', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--sdk-only', action='store_true')
    a = ap.parse_args()
    sdk, out = a.sdk.resolve(), a.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    os.environ['TEMP'] = os.environ['TMP'] = str(out / 'temp')
    (out/'temp').mkdir(exist_ok=True)
    os.environ['PATH'] = str(a.compiler.resolve().parent)+os.pathsep+os.environ['PATH']
    assert subprocess.check_output(['git','-C',str(sdk),'rev-parse','HEAD'],text=True).strip() == PIN
    assert not subprocess.check_output(['git','-C',str(sdk),'status','--porcelain'],text=True).strip(), 'SDK checkout modified'
    records=[]
    def run(args,label):
        args=list(map(str,args))
        r=subprocess.run(args,cwd=out,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (out/(label+'.log')).write_text(r.stdout,encoding='utf-8')
        records.append(dict(label=label,command=args,exit_code=r.returncode))
        (out/'build.json').write_text(json.dumps(dict(sdk_commit=PIN,commands=records),indent=2))
        if r.returncode:
            raise RuntimeError(label+' failed: '+r.stdout[-5000:])
        print(label+' PASS',flush=True)
    gen=out/'shaders'
    gen.mkdir(exist_ok=True)
    for src in sorted((sdk/'sdk/src/backends/dx12/shaders/opticalflow').glob('*.hlsl')):
        for wave64,half in [(False,False),(True,False),(False,True),(True,True)]:
            name=src.stem+('_wave64' if wave64 else '')+('_16bit' if half else '')
            args=[sdk/'sdk/tools/binary_store/FidelityFX_SC.exe','-reflection','-deps=gcc','-DFFX_GPU=1',
                  '-E','CS','-Wno-for-redefinition','-Wno-ambig-lit-shift','-DFFX_HLSL=1',
                  '-DFFX_OPTICALFLOW_OPTION_HDR_COLOR_INPUT={0,1}', '-name='+name,
                  '-DFFX_HALF='+str(int(half)), '-T','cs_6_6' if wave64 else 'cs_6_2',
                  '-DFFX_HLSL_SM='+('66' if wave64 else '62')]
            if wave64: args+=['-DFFX_PREFER_WAVE64=[WaveSize(64)]']
            if half: args+=['-enable-16bit-types']
            args+=['-I',sdk/'sdk/include/FidelityFX/gpu','-I',sdk/'sdk/include/FidelityFX/gpu/opticalflow',
                   '-output='+str(gen),src]
            run(args,name)
    # Only the optional PIX instrumentation is disabled. Preserve the SDK's
    # copyright/license in the external copy; do not alter OF shaders or ABI.
    original=sdk/'sdk/src/backends/dx12/ffx_dx12.cpp'
    s=original.read_text()
    assert s.count('#define ENABLE_PIX_CAPTURES 1') == 1
    patched=out/'ffx_dx12_no_pix.cpp'
    patched.write_text(s.replace('#define ENABLE_PIX_CAPTURES 1','// Optional PIX instrumentation disabled in portable experiment.'))
    sources=[sdk/'sdk/src/components/opticalflow/ffx_opticalflow.cpp',patched,
             sdk/'sdk/src/backends/shared/ffx_shader_blobs.cpp',
             sdk/'sdk/src/backends/shared/blob_accessors/ffx_opticalflow_shaderblobs.cpp']
    sources += [sdk/'sdk/src/shared'/f for f in ['ffx_assert.cpp','ffx_message.cpp','ffx_object_management.cpp','ffx_breadcrumbs_list.cpp']]
    args=[a.compiler.resolve(),'-std=c++17','-O2','-fms-extensions','-DNDEBUG','-DUNICODE','-D_UNICODE','-DFFX_OF','-include','windows.h','-include','cwchar']
    for inc in [sdk/'sdk/include',sdk/'sdk/src/shared',sdk/'sdk/src/backends/shared',sdk/'sdk/src/components',gen]:
        args+=['-isystem' if inc==sdk/'sdk/include' else '-I',inc]
    objects=[]
    for src in sources:
        obj=out/(src.stem+'.o');objects.append(obj)
        run(args+['-c',src,'-o',obj],src.stem)
    if not a.sdk_only:
        here=Path(__file__).resolve().parent
        frozen=out/'source';frozen.mkdir(exist_ok=True)
        inputs=['amd_of_producer.cpp','amd_of_producer.hpp','amd_of_shader.hpp','amd_of_gpu_probe.cpp','history_contract.hpp','build_amd_probe.py','validate_amd_probe.py']
        source_hashes={}
        for name in inputs:
            data=(here/name).read_bytes();(frozen/name).write_bytes(data);source_hashes[name]=sha(frozen/name)
        exe=out/'amd_of_gpu_probe.exe'
        run(args+['-Wall','-Wextra','-Werror',frozen/'amd_of_producer.cpp',frozen/'amd_of_gpu_probe.cpp',*objects,
                  '-static','-ld3d12','-ldxgi','-ld3dcompiler','-ldxguid','-lole32','-o',exe],'link-probe')
        assert all(sha(here/name)==digest for name,digest in source_hashes.items()), 'production source changed during build'
        records.append(dict(executable_sha256=sha(exe),source_sha256=source_hashes))
    (out/'build.json').write_text(json.dumps(dict(sdk_commit=PIN,license_sha256=sha(sdk/'LICENSE.txt'),
        pix_disabled_original_sha256=sha(original),pix_disabled_build_sha256=sha(patched),
        sdk_tool_sha256={name:sha(sdk/'sdk/tools/binary_store'/name) for name in ['FidelityFX_SC.exe','dxcompiler.dll','dxil.dll']},
        compiler_sha256=sha(a.compiler.resolve()),commands=records),indent=2))

if __name__=='__main__': main()
