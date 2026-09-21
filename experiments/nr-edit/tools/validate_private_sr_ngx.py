"""Real private Feature1 and signed-carrier pixels using a frozen staged Core.

Copies explicit existing dependencies into a private fresh test directory. No
game changes, downloaded vendor runtime, mock inference or driver modifications.
"""
import argparse,hashlib,json,shutil,subprocess
from pathlib import Path

PACKAGE=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['core','compiler','reshade','nrchain','sr-runtime','output']:
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--expect-neutral-failure',action='store_true',help='Use only with the frozen old shader negative control.')
    a=p.parse_args();core=a.core.resolve(strict=True);out=a.output.resolve()
    if out.exists():p.error('fresh --output required; preserve earlier evidence')
    manifest=json.loads((core/'edit-integration-manifest.json').read_text())
    assert manifest['schema']=='nr-edit-core-integration-v4'
    for name,digest in {**manifest['modified_files'],**manifest['overlay_files']}.items():assert sha(core/name)==digest,name
    out.mkdir(parents=True)
    source=PACKAGE/'tests/private_sr_ngx_host.cpp';frozen=out/source.name;shutil.copy2(source,frozen)
    receipt={'schema':'actual-private-sr-ngx-v1','game':'not-run','core_commit':manifest['upstream_commit'],
        'host_sha256':sha(frozen),'integration_manifest_sha256':sha(core/'edit-integration-manifest.json'),
        'negative_control':a.expect_neutral_failure,'commands':[],'result':'running'}
    def save():
        receipt['files']={f.name:{'bytes':f.stat().st_size,'sha256':sha(f)} for f in out.iterdir() if f.is_file() and f.name!='validation.json'}
        (out/'validation.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8')
    def run(args,name,timeout=240):
        args=list(map(str,args));r=subprocess.run(args,cwd=out,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=timeout)
        (out/(name+'.log')).write_bytes(r.stdout)
        receipt['commands'].append({'name':name,'args':args,'exit_code':r.returncode});save()
        print(r.stdout.decode('utf-8','replace')[-2200:],flush=True);return r
    # NGX's official loader resolves the snippet relative to its bootstrap
    # module. Keep the verified bridge and supplied SR runtime together.
    shutil.copy2(a.nrchain.resolve(strict=True),out/'nrchain_nvngx.dll')
    shutil.copy2(a.sr_runtime.resolve(strict=True),out/'nvngx_dlss.dll')
    objects=[core/'build/obj'/n for n in ['buffer.o','hook.o','trampoline.o','hde64.o','nr_dual_spatial_d3d12.o',
        'nr_face_detector.o','nr_face_worker.o','nr_edit_dx12.o','nr_deferred_dx12.o','nr_rr_dx12.o']]
    flags=['-std=c++20','-O1','-DNDEBUG','-DUNICODE','-D_UNICODE','-D_WIN64','-DNR_FACE_BACKEND=1','-DNR_CODEC_PRECOMPILED=1',
        '-DNR_PRIVATE_SR_CORE="'+(core/'src/nr_before_sr.cpp').as_posix()+'"','-ffunction-sections','-fdata-sections',
        '-I'+str(core/'build'),'-I'+str(core/'third_party/minhook/include'),'-I'+str(a.reshade/'include'),'-I'+str(a.reshade/'deps/imgui'),
        '-static','-municode','-Wl,--gc-sections']
    binary=out/'private-sr-ngx.exe'
    code=run([a.compiler,*flags,frozen,*objects,'-o',binary,'-lkernel32','-luser32','-ld3d12','-ldxgi','-ldxguid',
        '-lole32','-luuid','-ld3dcompiler','-lshell32','-lbcrypt'],'compile').returncode
    if code:receipt['result']='compile-failed';save();return code
    r=run([binary,out/'nrchain_nvngx.dll'],'run',60)
    if a.expect_neutral_failure:
        content=r.stdout.decode('utf-8','replace');capture=out/'composed-neutral.fp16'
        detected=r.returncode!=0 and capture.is_file() and 'FAIL actual SR/composition pixel expectation' in content
        receipt['result']='negative-control-detected' if detected else 'negative-control-failed'
        save();return 0 if detected else 1
    receipt['result']='actual-feature1-and-carrier-pixels-passed' if r.returncode==0 else 'actual-feature1-failed'
    receipt['scope']='real production owner resolution and private SR lifecycle, synthetic neutral/positive/negative carrier and exact zero/tiny composition; no Feature18, moving-scene ghosting or game acceptance'
    save();return r.returncode
if __name__=='__main__':raise SystemExit(main())
