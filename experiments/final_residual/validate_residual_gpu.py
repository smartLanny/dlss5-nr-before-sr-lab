"""Freeze/build the original pixel consumer, then test actual 5090D readbacks.

Reuses explicitly supplied official FFX1.1.4 object cache; records each object
hash and its earlier build manifest. No SDK/GPL source is vendored or changed.
"""
import argparse, hashlib, json, os, shutil, subprocess
from pathlib import Path

PIN='c6efa6bf7f2027b3ec94f28578bb5965eabb9e55'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--sdk',required=True,type=Path)
    ap.add_argument('--sdk-cache',required=True,type=Path)
    ap.add_argument('--compiler',required=True,type=Path)
    ap.add_argument('--output',required=True,type=Path)
    ap.add_argument('--build-only',action='store_true')
    ap.add_argument('--finite-only',action='store_true',help='two controlled GPU dispatches; no AMD source loop')
    ap.add_argument('--recorded',action='store_true',help='borrowed command-list production seam and GPU SCD')
    args=ap.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    sdk=args.sdk.resolve();cache=args.sdk_cache.resolve();compiler=args.compiler.resolve();root=Path(__file__).resolve().parent
    os.environ['TEMP']=os.environ['TMP']=str(out/'temp');(out/'temp').mkdir(exist_ok=True)
    assert subprocess.check_output(['git','-C',str(sdk),'rev-parse','HEAD'],text=True).strip()==PIN
    assert not subprocess.check_output(['git','-C',str(sdk),'status','--porcelain'],text=True).strip()
    build=json.loads((cache/'build.json').read_text())
    assert build['sdk_commit']==PIN and build['compiler_sha256']==sha(compiler)
    assert build['license_sha256']==sha(sdk/'LICENSE.txt')
    if build.get('sdk_objects'):
        objects=[Path(item['path']) for item in build['sdk_objects']]
        assert all(sha(Path(item['path']))==item['sha256'] for item in build['sdk_objects'])
    else:
        prior=next(c['command'] for c in build['commands'] if c.get('label')=='link-probe')
        objects=[Path(arg) for arg in prior if arg.endswith('.o')]
    assert build.get('effect_context_alignment_fixed'), 'FFX aligned scratch repair required for memory query'
    assert len(objects)==8 and all(p.is_file() for p in objects)
    frozen=out/'source';frozen.mkdir(exist_ok=True)
    names=['amd_of_producer.cpp','amd_of_producer.hpp','amd_of_shader.hpp','amd_of_gpu_probe.cpp',
           'history_contract.hpp','temporal_controls.hpp','residual_accumulator.cpp','residual_accumulator.hpp',
           'residual_accumulator_shader.hpp','residual_gpu_probe.cpp','residual_finite_probe.inl','validate_residual_gpu.py']
    if args.recorded:
        assert not args.finite_only
        names+=['recorded_temporal.hpp','recorded_temporal.cpp','recorded_temporal_probe.cpp']
    for name in names:shutil.copyfile(root/name,frozen/name)
    sources={n:sha(frozen/n) for n in names}
    record={'passed':False,'sdk_commit':PIN,'cache_build_sha256':sha(cache/'build.json'),
        'object_sha256':{str(p):sha(p) for p in objects},'compiler_sha256':sha(compiler),
        'source_sha256':sources,'license_sha256':sha(sdk/'LICENSE.txt'),'variants':[]}
    command=[str(compiler),'-std=c++17','-O2','-fms-extensions','-DNDEBUG','-DUNICODE','-D_UNICODE',
        '-DFFX_OF','-include','windows.h','-include','cwchar','-Wall','-Wextra','-Werror','-Wno-unused-function',
        '-isystem',str(sdk/'sdk/include')]
    def save(): (out/'validation.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
    def compile_variant(folder,label):
        exe=folder/'residual_gpu_probe.exe'
        cpp=['amd_of_producer.cpp','residual_accumulator.cpp']+(['recorded_temporal.cpp','recorded_temporal_probe.cpp'] if args.recorded else ['residual_gpu_probe.cpp'])
        cmd=command+[str(folder/n) for n in cpp]+list(map(str,objects))+[
            '-static','-ld3d12','-ldxgi','-ld3dcompiler','-ldxguid','-lole32','-o',str(exe)]
        result=subprocess.run(cmd,capture_output=True,text=True)
        (folder/'build.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        entry={'name':label,'command':cmd,'build_exit':result.returncode};record['variants'].append(entry);save()
        if result.returncode:raise RuntimeError(result.stderr[-6000:])
        entry['binary_sha256']=sha(exe);save();return exe,entry
    exe,entry=compile_variant(frozen,'production')
    if args.build_only:print('BUILD ONLY passed; no GPU validation claimed');return
    driver=subprocess.run(['nvidia-smi','--query-gpu=name,driver_version','--format=csv'],capture_output=True,text=True,check=True)
    (out/'driver.txt').write_text(driver.stdout,encoding='utf-8');record['hardware']=driver.stdout.strip()
    def run(exe,entry,work,oracle=None):
        result=subprocess.run([str(exe)]+(['--finite-only'] if args.finite_only else []),cwd=work,capture_output=True,text=True,timeout=120)
        text=result.stdout+result.stderr;(work/'gpu.log').write_text(text,encoding='utf-8');print(text,flush=True)
        entry.update(exit_code=result.returncode,log_sha256=sha(work/'gpu.log'),
            debug_layer_available='debug_layer=1' in text,
            readback_sha256={p.name:sha(p) for p in work.glob('*.bin')})
        entry['passed']=result.returncode==0 if oracle is None else result.returncode==1 and ('FAIL '+oracle) in text
        save()
        if not entry['passed']:raise RuntimeError('unexpected '+entry['name']+' result; failure preserved')
    run(exe,entry,out)
    controls=[
        ('reverse-flow','residual_accumulator_shader.hpp','float2 reference=float2(p)+motion;',
         'float2 reference=float2(p)-motion;','RESIDUAL_TRANSLATION_ORACLE'),
        ('ignore-source-pair','residual_accumulator.cpp','!(inputs.enhanced_source==frame.source)||',
         'false||','RESIDUAL_SOURCE_MISMATCH_ORACLE')]
    if args.finite_only:
        controls=[('ignore-invalid-enhanced','residual_accumulator_shader.hpp',
            'float3 current=current_valid?enhanced-base:0;',
            'float3 current=enhanced-base;','NONFINITE_HISTORY_ORACLE'),
            ('ignore-invalid-flow','residual_accumulator_shader.hpp',
             'bool valid=all(isfinite(motion))&&all(reference>=0)&&all(reference<=float2(Width-1,Height-1));',
             'bool valid=true;','FLOW_RESEED_ORACLE')]
    if args.recorded:
        controls=[('reverse-flow','residual_accumulator_shader.hpp','float2 reference=float2(p)+motion;',
            'float2 reference=float2(p)-motion;','RECORDED_NATIVE_PIXEL_ORACLE'),
            ('discard-first-anchor','residual_accumulator_shader.hpp',
             'float3 base=LaterLayers!=0?Anchor.Load(int3(p,0)).rgb:original.rgb;',
             'float3 base=original.rgb;','LATER_COMPOSED_ANCHOR_ORACLE'),
            ('skip-gpu-warmup','residual_accumulator_shader.hpp','motion_ready&&FlowAge>5&&',
             'motion_ready&&','GPU_WARMUP_SCD_SEED_ORACLE'),
            ('clip-hdr','residual_accumulator_shader.hpp','result.rgb=current_valid&&fits?restored:fallback;',
             'result.rgb=current_valid&&fits?saturate(restored):fallback;','HDR_EXPOSURE_HIGHLIGHT_ORACLE'),
            ('ignore-hdr-exposure','residual_accumulator_shader.hpp','current*=SourceToScene;',
             'current*=1;','HDR_EXPOSURE_HIGHLIGHT_ORACLE')]
    for label,file,before,after,oracle in controls:
        work=out/label;work.mkdir(exist_ok=True)
        for n in names:shutil.copyfile(frozen/n,work/n)
        path=work/file;original=path.read_text();assert original.count(before)==1
        path.write_text(original.replace(before,after),encoding='utf-8')
        exe,entry=compile_variant(work,label);run(exe,entry,work,oracle)
    record['source_unchanged']=all(sha(root/n)==digest for n,digest in sources.items())
    record['passed']=record['source_unchanged'] and all(v['passed'] for v in record['variants'])
    save();assert record['passed']

if __name__=='__main__':main()
