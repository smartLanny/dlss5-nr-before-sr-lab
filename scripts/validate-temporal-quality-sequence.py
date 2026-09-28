"""Freeze and run deterministic temporal quality sequences.

The C++ fixture calls the production RecordedTemporal shader and records actual
GPU readback. Hardware AMD optical flow is opt-in and never part of the WARP gate.
"""
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
NAMES=('amd_of_producer.cpp','amd_of_producer.hpp','amd_of_shader.hpp',
       'amd_of_gpu_probe.cpp','amd_of_budget.hpp','history_contract.hpp','temporal_controls.hpp',
       'residual_accumulator.cpp','residual_accumulator.hpp',
       'residual_accumulator_shader.hpp','residual_gpu_probe.cpp',
       'residual_finite_probe.inl','recorded_temporal.hpp','recorded_temporal.cpp',
       'recorded_temporal_probe.cpp','recorded_temporal_quality_probe.inl')
def digest(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def run(command,folder,name,receipt):
    result=subprocess.run([str(x) for x in command],cwd=folder,capture_output=True,text=True,timeout=900 if receipt['hardware_gpu'] else 240)
    (folder/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
    receipt['commands'].append({'name':name,'argv':[str(x) for x in command],'exit':result.returncode})
    (folder/'result.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8')
    if result.returncode: raise RuntimeError(name+' failed: '+(result.stdout+result.stderr)[-3000:])
    return result.stdout
def frame_values(path):
    data=path.read_bytes()
    assert len(data)==192*128*8
    return [item[0] for item in struct.iter_unpack('<e',data)][::4]

def analyze(out,receipt):
    checks=[]
    for scene in ('static','pan','occlusion','cut','object','hud_reflection','dark'):
      for mode in ('adaptive','low'):
       for scope in ('whole','later'):
        for backend in (('native','amd') if receipt['hardware_gpu'] else ('native',)):
         for seq in range(1,13 if receipt['hardware_gpu'] else 7):
          name=f'{scene}-{mode}-{scope}-{backend}-{seq}'
          base=frame_values(out/(name+'-B.f16'))
          candidate=frame_values(out/(name+'-Z.f16'))
          actual=frame_values(out/(name+'-out.f16'))
          clean=[v+(.006 if scene=='dark' else .06) for v in base]
          area=[y*192+x for y in range(8,120) for x in range(8,184)]
          mae=lambda values:sum(abs(values[i]-clean[i]) for i in area)/len(area)
          input_mae,output_mae=mae(candidate),mae(actual)
          shift=(seq-1)*2 if scene in ('pan','hud_reflection') else 0
          line=96+shift
          contrast=lambda values:sum(values[y*192+line]-(values[y*192+line-1]+values[y*192+line+1])/2
                                       for y in range(8,120))/112
          clean_edge=contrast(clean)
          off_edge=contrast(candidate)
          output_edge=contrast(actual)
          edge_ratio=output_edge/clean_edge if clean_edge else None
          tail_input=tail_output=None
          if scene in ('occlusion','object') and seq>1:
           left=40+seq*3
           indices=[y*192+x for y in range(36,92) for x in range(left-3,left)]
           tail_input=sum(abs(candidate[i]-clean[i]) for i in indices)/len(indices)
           tail_output=sum(abs(actual[i]-clean[i]) for i in indices)/len(indices)
          hud_input=hud_output=reflection_input=reflection_output=None
          if scene=='hud_reflection':
           hud=[y*192+x for y in range(20,108) for x in range(19,22)]
           reflection=[y*192+x for y in range(72,112) for x in range(132,176)]
           roi=lambda values,indices:sum(abs(values[i]-clean[i]) for i in indices)/len(indices)
           hud_input,hud_output=roi(candidate,hud),roi(actual,hud)
           reflection_input,reflection_output=roi(candidate,reflection),roi(actual,reflection)
          dark_roi=None
          if scene=='dark':
           flat=[y*192+x for y in range(24,104) for x in range(24,72)]
           edited=[y*192+x for y in range(40,88) for x in range(112,152)]
           line_ys=range(24,104)
           dark_contrast=lambda values:sum(values[y*192+96]-(values[y*192+95]+values[y*192+97])/2
                                           for y in line_ys)/80
           roi=lambda values,indices:sum(abs(values[i]-clean[i]) for i in indices)/len(indices)
           dark_roi={'flat_off_mae':roi(candidate,flat),'flat_output_mae':roi(actual,flat),
                     'edited_off_mae':roi(candidate,edited),'edited_output_mae':roi(actual,edited),
                     'clean_line_contrast':dark_contrast(clean),
                     'off_line_contrast':dark_contrast(candidate),
                     'output_line_contrast':dark_contrast(actual),
                     'off_line_error':abs(dark_contrast(candidate)-dark_contrast(clean)),
                     'output_line_error':abs(dark_contrast(actual)-dark_contrast(clean)),
                     'clean_min':min(clean),'clean_max':max(clean)}
          checks.append({'scene':scene,'mode':mode,'scope':scope,'backend':backend,'frame':seq,
                         'off_mae':input_mae,'output_mae':output_mae,
                         'clean_line_contrast':clean_edge,'off_line_contrast':off_edge,
                         'output_line_contrast':output_edge,'line_retention':edge_ratio,
                         'off_line_error':abs(off_edge-clean_edge),
                         'output_line_error':abs(output_edge-clean_edge),
                         'revealed_input_mae':tail_input,'revealed_output_mae':tail_output,
                         'hud_input_mae':hud_input,'hud_output_mae':hud_output,
                         'reflection_input_mae':reflection_input,
                         'reflection_output_mae':reflection_output,
                         'dark_roi':dark_roi,
                         'cut_reseed_exact':actual==candidate if scene=='cut' and seq==4 else None})
    (out/'analysis.json').write_text(json.dumps(checks,indent=2),encoding='utf-8')
    # Functional execution and synthetic quality observations are separate.
    final_frame=12 if receipt['hardware_gpu'] else 6
    finals=[x for x in checks if x['frame']==final_frame and x['scene'] in ('static','pan')]
    dark_finals=[x['dark_roi'] for x in checks if x['frame']==final_frame and x['scene']=='dark']
    receipt['synthetic_quality']={
      'static_pan_noise_improved':sum(x['output_mae']<x['off_mae'] for x in finals),
      'static_pan_cases':len(finals),
      'line_error_improved':sum(x['output_line_error']<x['off_line_error'] for x in finals),
      'line_cases':len(finals),
      'dark_flat_noise_improved':sum(x['flat_output_mae']<x['flat_off_mae'] for x in dark_finals),
      'dark_edited_noise_improved':sum(x['edited_output_mae']<x['edited_off_mae'] for x in dark_finals),
      'dark_line_error_improved':sum(x['output_line_error']<x['off_line_error'] for x in dark_finals),
      'dark_cases':len(dark_finals),
      'cut_reseed_exact':sum(x['cut_reseed_exact'] is True for x in checks),
      'cut_cases':8 if receipt['hardware_gpu'] else 4,
      'full_quality_pass':False,
      'coverage':['static','pan','occlusion','cut','object','hud_reflection','dark'],
      'uncovered':([] if receipt['hardware_gpu'] else ['hardware_AMDOF'])+['game','native_RR_inference']
    }
    receipt['analysis_sha256']=digest(out/'analysis.json')

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--compiler',type=Path,required=True)
    p.add_argument('--sdk',type=Path,required=True)
    p.add_argument('--sdk-cache',type=Path,required=True)
    p.add_argument('--build-only',action='store_true')
    p.add_argument('--core',type=Path,help='Unified generated Core root; verifies temporal module and shader receipt')
    p.add_argument('--hardware',action='store_true',help='Use real 5090D and run NativeMotion plus AMD optical flow')
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    frozen=out/'source';frozen.mkdir()
    src=ROOT/'experiments/final_residual'
    production=(a.core.resolve()/'src/temporal') if a.core else src
    module=None
    if a.core:
        module_path=a.core.resolve()/'build/obj/temporal/module.json'
        module=json.loads(module_path.read_text(encoding='utf-8'))
        assert module['passed']
        for name,expected in module['source_sha256'].items():
            assert digest(production/name)==expected,('generated temporal source mismatch',name)
        for name,expected in module['object_sha256'].items():
            assert digest(name)==expected,('generated temporal object mismatch',name)
        artifact_dir=module_path.parent/'bytecode'
        artifact_receipt=artifact_dir/'temporal-artifacts.json'
        shader_header=artifact_dir/'nr_temporal_precompiled.hpp'
        assert digest(artifact_receipt)==module['temporal_artifacts_sha256']
        assert digest(shader_header)==module['temporal_header_sha256']
        artifact=json.loads(artifact_receipt.read_text(encoding='utf-8'))
        assert artifact['passed'] and artifact['header_sha256']==digest(shader_header)
        for item in artifact['artifacts']:
            assert digest(artifact_dir/item['file'])==item['sha256']
        local_bytecode=frozen/'bytecode';local_bytecode.mkdir()
        shutil.copyfile(shader_header,local_bytecode/shader_header.name)
    local_names={'amd_of_gpu_probe.cpp','residual_gpu_probe.cpp','residual_finite_probe.inl',
                 'recorded_temporal_probe.cpp','recorded_temporal_quality_probe.inl'}
    paths={n:(src/n if n in local_names else production/n) for n in NAMES}
    for n,path in paths.items():shutil.copyfile(path,frozen/n)
    fixture=ROOT/'tests/temporal_quality_sequence.inl';shutil.copyfile(fixture,frozen/fixture.name)
    source_hash={n:digest(path) for n,path in paths.items()};source_hash[fixture.name]=digest(fixture)
    host=frozen/'amd_of_gpu_probe.cpp';text=host.read_text(encoding='utf-8')
    if not a.hardware:
        begin=text.index('        for(UINT i=0;;++i){ComPtr<IDXGIAdapter1> adapter;')
        end=text.index('        if(enabled)device.As(&debug);',begin)
        warp='''        ComPtr<IDXGIAdapter> software;
            hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&software)),"WARP adapter");
            hr(D3D12CreateDevice(software.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)),"WARP device");
            std::printf("adapter=WARP software-only\\n");
    '''
        host.write_text(text[:begin]+warp+text[end:],encoding='utf-8')
    probe=frozen/'recorded_temporal_probe.cpp';text=probe.read_text(encoding='utf-8')
    main_at=text.index('int main(){try{')
    replacement='''#include "temporal_quality_sequence.inl"
int main(){try{
 std::setvbuf(stdout,nullptr,_IONBF,0);Host host;
 for(const char* scene:{"static","pan","occlusion","cut","object","hud_reflection","dark"})
  for(auto mode:{Mode::Adaptive,Mode::LowFrequency})
   for(auto scope:{Scope::WholeChain,Scope::LaterLayers})
   for(auto backend:{Backend::NativeMotion,Backend::AmdOpticalFlow})
    if(backend==Backend::NativeMotion || QUALITY_HARDWARE)
     quality_scene(host,scene,mode,scope,backend,QUALITY_HARDWARE?12u:6u);
 host.validate_debug();std::printf("PASS production temporal quality frames=%u\\n",QUALITY_HARDWARE?672u:168u);
 return 0;
}catch(const std::exception& e){std::printf("FAIL %s checks=%u\\n",e.what(),checks);return 1;}}
'''
    probe.write_text('#define QUALITY_HARDWARE '+('1' if a.hardware else '0')+'\n'+text[:main_at]+replacement,encoding='utf-8')
    cache=json.loads((a.sdk_cache/'build.json').read_text(encoding='utf-8'))
    objects=[Path(item['path']) for item in cache['sdk_objects']]
    for item in cache['sdk_objects']:
        assert digest(item['path'])==item['sha256']
    receipt={'passed':False,'generated_core':str(a.core.resolve()) if a.core else None,
             'module_sha256':digest(module_path) if module else None,
             'shader_receipt_sha256':digest(artifact_receipt) if module else None,
             'shader_header_sha256':digest(shader_header) if module else None,
             'shader_artifact_sha256':{item['file']:item['sha256'] for item in artifact['artifacts']} if module else None,
             'execution':'build-only' if a.build_only else ('hardware' if a.hardware else 'WARP'),
             'production_temporal':True,'hardware_gpu':a.hardware,
             'off_baseline':'saved same-frame Z before temporal processing','game':False,'ngx_inference':False,
             'source_sha256':source_hash,'sdk_cache_sha256':digest(a.sdk_cache/'build.json'),
             'sdk_object_sha256':{str(x):digest(x) for x in objects},'commands':[]}
    exe=out/'quality.exe'
    cmd=[a.compiler,'-std=c++17','-O2','-fms-extensions','-DNDEBUG','-DUNICODE','-D_UNICODE',
         '-DFFX_OF',*(['-DNR_TEMPORAL_PRECOMPILED=1','-I',frozen/'bytecode'] if module else []),'-include','windows.h','-include','cwchar','-Wno-unused-function',
         '-isystem',a.sdk/'sdk/include',frozen/'amd_of_producer.cpp',
         frozen/'residual_accumulator.cpp',frozen/'recorded_temporal.cpp',
         probe,*objects,'-static','-ld3d12','-ldxgi','-ld3dcompiler','-ldxguid',
         '-lole32','-o',exe]
    run(cmd,out,'compile',receipt);receipt['binary_sha256']=digest(exe)
    if not a.build_only:
        log=run([exe],out,'warp',receipt)
        rows=[line for line in log.splitlines() if line.startswith('QUALITY ')]
        expected=672 if a.hardware else 168
        assert len(rows)==expected and f'PASS production temporal quality frames={expected}' in log
        receipt['frames']=len(rows)
        receipt['raw_frame_sha256']={p.name:digest(p) for p in out.glob('*.f16')}
        receipt['metrics']=rows
        analyze(out,receipt)
    receipt['source_unchanged']=all(digest(path)==source_hash[n] for n,path in paths.items()) and digest(fixture)==source_hash[fixture.name]
    receipt['passed']=receipt['source_unchanged']
    (out/'result.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8')
    print('PASS build-only' if a.build_only else f'PASS {receipt["execution"]} {receipt["frames"]} production temporal frames')
if __name__=='__main__':main()
