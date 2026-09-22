"""Windows NVIDIA A/B runner for the opt-in memory path; never installs a game.

Reuses the existing recorded-temporal production runner and its negative
controls. A generated test-only harness additionally proves actual path use,
allocation accounting and bit-identical native-motion images across modes.
This file has not been executed on Windows by the authoring cloud session.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def replace_once(text: str,old: str,new: str) -> str:
    if text.count(old)!=1:raise RuntimeError('probe instrumentation anchor changed: '+old[:80])
    return text.replace(old,new)


def instrument(text: str) -> str:
    text='#include <fstream>\n#include <string>\n'+text
    text=replace_once(text,'void native_pixels(Host& host,Scope scope,bool hdr=false){',r'''
template<class Bytes> void memory_opt_save(const Bytes& pixels,unsigned scope,unsigned seq,bool hdr) {
    const auto name="memoryopt-native-"+std::to_string(scope)+"-"+std::to_string(seq)+"-hdr"+std::to_string(unsigned(hdr))+".bin";
    std::ofstream stream(name,std::ios::binary);check(bool(stream),"memory opt output open");
    stream.write(reinterpret_cast<const char*>(pixels.data()),std::streamsize(pixels.size()*sizeof(pixels[0])));
    check(bool(stream),"memory opt output write");
}
void native_pixels(Host& host,Scope scope,bool hdr=false){''')
    text=replace_once(text,'    std::vector<float> previous(w*h*3);Id timestamp=1000;',r'''
    wchar_t env[2]{};
    const bool expected_inplace=GetEnvironmentVariableW(L"DLSS5_TEMPORAL_INPLACE_FP16",env,2)==1&&env[0]==L'1';
    check(owner.stats().inplace_available==expected_inplace,"MEMORY_OPT_ACTUAL_CAPABILITY_ORACLE");
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=w;desc.Height=h;desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const auto plane=host.device->GetResourceAllocationInfo(0,1,&desc).SizeInBytes;
    check(owner.stats().snapshot_bytes==(expected_inplace?0:plane),"MEMORY_OPT_SNAPSHOT_ALLOCATION_ORACLE");
    check(owner.stats().bytes==plane*(scope==Scope::LaterLayers?(expected_inplace?5:6):(expected_inplace?4:5)),
        "MEMORY_OPT_TOTAL_ALLOCATION_ORACLE");
    std::vector<float> previous(w*h*3);Id timestamp=1000;''')
    text=replace_once(text,'        const auto pixels=host.readback(z.Get(),8);const bool use=seq>1&&seq!=5&&seq!=6;',r'''
        const auto pixels=host.readback(z.Get(),8);
        memory_opt_save(pixels,unsigned(scope),seq,hdr);
        check(owner.stats().inplace_recorded==(expected_inplace?seq:0),"MEMORY_OPT_FAST_PATH_USED_ORACLE");
        check(owner.stats().snapshot_recorded==(expected_inplace?0:seq),"MEMORY_OPT_SNAPSHOT_DISPATCH_ORACLE");
        const bool use=seq>1&&seq!=5&&seq!=6;''')
    return text


def main() -> None:
    parser=argparse.ArgumentParser()
    for name in ['sdk','sdk-cache','compiler','output']:parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    if os.name!='nt':raise SystemExit('This gate requires Windows/D3D12 and NVIDIA; no GPU result was produced.')
    root=Path(__file__).resolve().parent
    out=args.output.resolve()
    if out==root or root in out.parents:raise RuntimeError('evidence must be outside the source directory')
    out.mkdir(parents=True,exist_ok=False)
    inputs=[p for p in root.iterdir() if p.is_file() and p.suffix in {'.py','.hpp','.cpp','.inl'}]
    manifest={p.name:sha(p) for p in inputs}
    builder=root.parents[1]/'scripts/prepare-temporal-artifacts.py'
    if not builder.is_file():raise RuntimeError('production temporal artifact builder missing')
    frozen_builder=out/'prepare-temporal-artifacts.py';shutil.copyfile(builder,frozen_builder)
    report={'passed':False,'source_sha256':manifest,'modes':[],'game_verified':False,
        'artifact_builder_sha256':sha(builder)}
    def save():(out/'comparison.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    save()
    for name,cache,inplace in [('reference','0','0'),('cache','1','0'),('inplace','1','1'),('precompiled-snapshot','1','0'),('precompiled','1','1')]:
        stage=out/name/'source';stage.mkdir(parents=True)
        for p in inputs:shutil.copyfile(p,stage/p.name)
        probe=stage/'recorded_temporal_probe.cpp'
        probe.write_text(instrument(probe.read_text(encoding='utf-8')),encoding='utf-8')
        env=os.environ.copy();env['DLSS5_TEMPORAL_PIPELINE_CACHE']=cache;env['DLSS5_TEMPORAL_INPLACE_FP16']=inplace
        result_dir=out/name/'result'
        command=[sys.executable,str(stage/'validate_residual_gpu.py'),'--recorded',
            '--sdk',str(args.sdk.resolve()),'--sdk-cache',str(args.sdk_cache.resolve()),
            '--compiler',str(args.compiler.resolve()),'--output',str(result_dir)]
        if name.startswith('precompiled'):command+=['--temporal-artifact-builder',str(frozen_builder)]
        run=subprocess.run(command,env=env,capture_output=True,text=True,timeout=900)
        (out/name/'runner.log').write_text(run.stdout+run.stderr,encoding='utf-8')
        row={'name':name,'exit':run.returncode,'instrumented_probe_sha256':sha(probe),'command':command}
        report['modes'].append(row);save()
        if run.returncode:raise RuntimeError(name+' failed; evidence retained')
        evidence=json.loads((result_dir/'validation.json').read_text(encoding='utf-8'))
        if not evidence.get('passed'):raise RuntimeError(name+' did not complete its GPU negative controls')
        row['native_images']={p.name:sha(p) for p in result_dir.glob('memoryopt-native-*.bin')}
        if len(row['native_images'])!=28:raise RuntimeError('expected seven native frames for both scopes and SDR/HDR')
        row['gpu_validation_sha256']=sha(result_dir/'validation.json');save()
    baseline=report['modes'][0]['native_images']
    report['native_images_bit_identical']=all(row['native_images']==baseline for row in report['modes'][1:])
    report['source_unchanged']=all(sha(root/name)==value for name,value in manifest.items()) and sha(builder)==sha(frozen_builder)
    report['passed']=report['native_images_bit_identical'] and report['source_unchanged']
    save()
    if not report['passed']:raise RuntimeError('A/B parity or source immutability failed')
    print('PASS same-GPU native-motion A/B images and recorded production/negative-control gates; not game performance')

if __name__=='__main__':main()
