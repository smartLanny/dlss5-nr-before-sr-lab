"""Bounded real signed Create/Release ordering probe; never installs into a game."""
import argparse
import hashlib
import json
import os
from pathlib import Path
from temporal_test_objects import objects_for
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    with Path(path).open('rb') as file:
        return hashlib.file_digest(file, 'sha256').hexdigest()


def main():
    p = argparse.ArgumentParser()
    for name in ['core', 'compiler', 'reshade', 'nr-runtime', 'output']:
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--compile-only', action='store_true')
    a = p.parse_args()
    core, out = a.core.resolve(strict=True), a.output.resolve()
    if out.exists() and any(out.iterdir()):
        p.error('Use an empty output to preserve prior evidence')
    out.mkdir(parents=True, exist_ok=True)
    objects = [core / 'build/obj' / x for x in ['buffer.o', 'hook.o', 'trampoline.o', 'hde64.o',
        'nr_dual_spatial_d3d12.o', 'nr_face_detector.o', 'nr_face_worker.o']]+objects_for(core/'src/nr_before_sr.cpp')
    host = ROOT / 'tests/post_scale_create_ngx_host.cpp'
    files = [host, ROOT/'tests/compat_present_ngx_host.cpp', core.parent/'source-manifest.json',
             core/'build/face-backend.json', core/'build/nrchain_nvngx.dll', *objects,
             *[f for f in (core/'src').rglob('*') if f.is_file()]]
    hashes = {str(f): sha(f) for f in files}
    face = json.loads((core/'build/face-backend.json').read_text(encoding='utf-8-sig'))
    binary = out/'post-scale-create.exe'
    cmd = [str(a.compiler), '-std=c++20', '-O1', '-g', '-DNDEBUG', '-DUNICODE', '-D_UNICODE', '-D_WIN64',
           f'-DNR_FACE_BACKEND={int(face["enabled"])}', '-DNR_CODEC_PRECOMPILED=1',
           '-DNR_PRESENT_NGX_CORE="'+(core/'src/nr_before_sr.cpp').as_posix()+'"',
           '-DNR_HOST_COLD_BOOTSTRAP=1', '-ffunction-sections', '-fdata-sections',
           '-I'+str(core/'build'), '-I'+str(core/'third_party/minhook/include'),
           '-I'+str(a.reshade/'include'), '-I'+str(a.reshade/'deps/imgui'),
           '-static', '-municode', '-Wl,--gc-sections', str(host), *map(str, objects), '-o', str(binary),
           '-lkernel32', '-luser32', '-ld3d12', '-ldxgi', '-ldxguid', '-lole32', '-luuid',
           '-ld3dcompiler', '-lshell32', '-lbcrypt', '-lpsapi']
    rows = []
    env = dict(os.environ, TEMP='D:/agent-tools/temp', TMP='D:/agent-tools/temp')

    def run(command, name, cwd):
        try:
            r = subprocess.run(list(map(str, command)), cwd=cwd, env=env, capture_output=True,
                               text=True, encoding='utf-8', errors='replace', timeout=240,
                               creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        except subprocess.TimeoutExpired as e:
            def text(v): return v.decode('utf-8', 'replace') if isinstance(v, bytes) else v or ''
            r = subprocess.CompletedProcess(command, 124, text(e.stdout), text(e.stderr)+'\nTIMEOUT; not a pass\n')
        log = out/(name+'.log')
        log.write_text(r.stdout+r.stderr, encoding='utf-8')
        rows.append(dict(name=name,command=list(map(str,command)),exit_code=r.returncode,log=str(log),sha256=sha(log)))
        print(name, r.returncode, (r.stdout+r.stderr)[-3500:], flush=True)
        return r.returncode

    code = run(cmd, 'compile-host', out)
    if not code and not a.compile_only:
        for order in ['serial', 'overlap']:
            case = out/order
            case.mkdir()
            shutil.copy2(a.nr_runtime, case/'nvngx_dlssnr.dll')
            shutil.copy2(core/'build/nrchain_nvngx.dll', case/'nrchain_nvngx.dll')
            # Production loader resolves the private bridge beside the EXE.
            shutil.copy2(binary, case/binary.name)
            code = max(code, run([case/binary.name, case, order], 'run-'+order, case))
    unchanged = all(sha(f)==digest for f,digest in hashes.items())
    report = dict(schema='real-ngx-post-scale-create-order-v1',result=code,
        compile_only=a.compile_only,commands=rows,source_hashes=hashes,sources_unchanged=unchanged,
        runtime_sha256=sha(a.nr_runtime),core=str(core),
        scope='Frozen production BindNrParameters/CreateSignedNrFeature/DestroyContext, signed Feature18, exact private queue fence. Artificial zero FP16 color/RG16 motion and R32F depth one; no Evaluate, game SR/FG, concurrency, driver OOM injection or game acceptance.')
    (out/'validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return code if unchanged else 1


if __name__ == '__main__':
    raise SystemExit(main())
