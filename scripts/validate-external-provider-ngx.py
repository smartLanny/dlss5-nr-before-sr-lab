"""Actual signed Feature18/D3D12 producer through the frozen Core provider ABI."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from temporal_test_objects import objects_for

ROOT=Path(__file__).resolve().parents[1]
def sha(path):
    with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def read(path):return json.loads(Path(path).read_text(encoding='utf-8-sig'))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('core','compiler','reshade','nr-runtime','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--source-commit',required=True)
    p.add_argument('--compile-only',action='store_true')
    a=p.parse_args();core=a.core.resolve(strict=True);out=a.output.resolve()
    out.mkdir(parents=True,exist_ok=False)
    report={'schema':'real-external-provider-ngx-v1','source_commit':a.source_commit,
        'status':'failed','compile_only':a.compile_only,'gpu_verified':False,'ngx_verified':False,
        'game_verified':False,'packaged_feeder_verified':False,'native_sr_verified':False,'fg_verified':False,
        'scope':'whole production Core; real D3D12 queue/list/fence; synthetic colour/depth/motion; real signed Feature18; no runtime function substitutions',
        'commands':[]}
    def run(command,label,timeout=240,env=None):
        try:r=subprocess.run([str(x) for x in command],cwd=out,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=timeout,env=env)
        except subprocess.TimeoutExpired as error:
            def text(value):return value.decode('utf-8','replace') if isinstance(value,bytes) else value or ''
            r=subprocess.CompletedProcess(command,124,text(error.stdout),text(error.stderr)+'\nBounded test timed out; failure retained.\n')
        log=out/(label+'.log');log.write_text(r.stdout+r.stderr,encoding='utf-8')
        report['commands'].append({'name':label,'command':[str(x) for x in command],'exit_code':r.returncode,'log_sha256':sha(log)})
        print((r.stdout+r.stderr)[-5000:],flush=True)
        if r.returncode:raise RuntimeError(label+' failed; preserve evidence')
        return r.stdout
    try:
        manifest_path=core.parent/'source-manifest.json';manifest=read(manifest_path)
        if manifest['source_commit']!=a.source_commit:raise ValueError('Wrong frozen source commit')
        for name,digest in manifest['files'].items():
            file=(manifest_path.parent/name).resolve(strict=True)
            if not file.is_relative_to(manifest_path.parent.resolve()) or sha(file)!=digest:raise ValueError('Source manifest mismatch: '+name)
        report['source_manifest_sha256']=sha(manifest_path);report['source_files_verified']=len(manifest['files'])
        face_path=core/'build/face-backend.json';face=read(face_path)
        if face.get('enabled') is not True:raise ValueError('Require full FaceBackend1 build')
        report['face_backend']={'sha256':sha(face_path),'receipt':face}
        variant_path=core/'build/build-variant.json';variant=read(variant_path)
        if variant.get('sourcecommit')!=a.source_commit:raise ValueError('Build receipt source differs from manifest')
        for row in variant['artifacts']:
            file=core/'build'/row['name']
            if sha(file)!=row['sha256'] or file.stat().st_size!=row['bytes']:raise ValueError('Built artifact identity mismatch')
        report['build_variant_sha256']=sha(variant_path)
        # Only a real vendor-signed private runtime is admitted. Path is passed
        # as an environment value rather than interpolated PowerShell code.
        os.environ['NR_EXTERNAL_HOST_RUNTIME']=str(a.nr_runtime.resolve(strict=True))
        signature_env=os.environ.copy()
        # Windows PowerShell must use its own built-in modules, not inherited
        # PowerShell 7 module paths from the invoking development shell.
        signature_env={key:value for key,value in signature_env.items() if key.casefold()!='psmodulepath'}
        signature=run(['powershell.exe','-NoProfile','-NonInteractive','-Command',
            '$ErrorActionPreference="Stop"; $s=Get-AuthenticodeSignature -LiteralPath $env:NR_EXTERNAL_HOST_RUNTIME; '
            '@{status=[string]$s.Status;subject=$s.SignerCertificate.Subject;thumbprint=$s.SignerCertificate.Thumbprint}|ConvertTo-Json -Compress'], 'runtime-signature',env=signature_env)
        report['runtime_signature']=json.loads(signature)
        if report['runtime_signature']['status']!='Valid' or 'NVIDIA' not in report['runtime_signature']['subject'].upper():
            raise ValueError('Runtime must have a valid NVIDIA signature')
        inputs=[ROOT/'tests/external_provider_ngx_host.cpp',ROOT/'tests/compat_present_ngx_host.cpp',Path(__file__)]
        objects=[core/'build/obj'/name for name in ('buffer.o','hook.o','trampoline.o','hde64.o','nr_dual_spatial_d3d12.o','nr_face_detector.o','nr_face_worker.o')]+objects_for(core/'src/nr_before_sr.cpp')
        compiled_header=core/'build/nr_codec_precompiled.h'
        frozen={str(file):sha(file) for file in inputs+objects+[compiled_header,face_path,variant_path,manifest_path]}
        report['linked_objects']={str(file):frozen[str(file)] for file in objects}
        report['codec_precompiled_sha256']=sha(compiled_header)
        report['fixture_inputs']={str(file.relative_to(ROOT)):sha(file) for file in inputs}
        binary=out/'external-provider-ngx.exe'
        common=['-std=c++20','-O1','-DNDEBUG','-DUNICODE','-D_UNICODE','-D_WIN64','-DNR_FACE_BACKEND=1','-DNR_CODEC_PRECOMPILED=1','-DNR_HOST_COLD_BOOTSTRAP=1',
            '-DNR_PRESENT_NGX_CORE="'+(core/'src/nr_before_sr.cpp').as_posix()+'"','-ffunction-sections','-fdata-sections',
            '-I'+str(core/'build'),'-I'+str(core/'third_party/minhook/include'),'-I'+str(a.reshade/'include'),'-I'+str(a.reshade/'deps/imgui')]
        run([a.compiler,*common,'-static','-municode','-Wl,--gc-sections',inputs[0],*objects,'-o',binary,
             '-lkernel32','-luser32','-ld3d12','-ldxgi','-ldxguid','-lole32','-luuid','-ld3dcompiler','-lshell32','-lbcrypt'],'compile-host')
        for source,name in ((a.nr_runtime,'nvngx_dlssnr.dll'),(core/'build/nrchain_nvngx.dll','nrchain_nvngx.dll')):
            shutil.copyfile(source,out/name)
            if sha(source)!=sha(out/name):raise ValueError('Runtime copy mismatch')
        if not a.compile_only:
            for layers in (1,2):
                output=run([binary,out,layers],f'run-{layers}-layers')
                if 'PASS REAL_EXTERNAL_PROVIDER' not in output:raise ValueError('Missing actual host success receipt')
        for file,digest in frozen.items():
            if sha(file)!=digest:raise ValueError('Fixture/build input changed during validation')
        for name,digest in manifest['files'].items():
            if sha(manifest_path.parent/name)!=digest:raise ValueError('Production source changed during validation')
        report.update(status='compiled' if a.compile_only else 'passed',source_unchanged=True,
                      gpu_verified=not a.compile_only,ngx_verified=not a.compile_only)
        report['artifacts']={name:sha(out/name) for name in (binary.name,'nvngx_dlssnr.dll','nrchain_nvngx.dll')}
    except Exception as error:
        report['error']=str(error);print(str(error),flush=True)
    finally:(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return 0 if report['status'] in ('passed','compiled') else 1

if __name__=='__main__':raise SystemExit(main())
