"""Controlled real Feature18 GPU cost: off, native MV, AMD OF, AMD LowFrequency.

Eight warmup frames precede 32 steady samples. Includes every temporal-specific
GPU copy/conversion/dispatch; common scene upload and final image readback are
outside the measured interval. This is not a complete game frame or a 3% verdict.
"""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,time
import sys
from temporal_test_objects import objects_for
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def compare(p,layers=(1,3),modes=range(4)):
 import re,statistics,math
 rows=[]
 for layer in layers:
  for mode in modes:
   folder=p/f'layers{layer}-mode{mode}'
   lines=(p/f'layers{layer}-mode{mode}.log').read_text().splitlines()
   values=[{k:float(v) for k,v in re.findall(r'(\w+)=([0-9.]+)',line)} for line in lines if line.startswith('COST ')]
   stable=[v for v in values if v['warmup']==0];assert len(stable)==32
   row={'layers':layer,'mode':mode,'stable_frames':len(stable),'temporal_bytes':max(v['temporal_bytes'] for v in stable)}
   for metric in ('nr_ms','added_ms','total_ms'):
    v=sorted(x[metric] for x in stable);row[metric]={'median':statistics.median(v),'p95':v[math.ceil(.95*len(v))-1]}
   rows.append(row)
 report={'scope':'Synthetic complete NR work and incremental temporal GPU cost, not a full game frame or <=3% acceptance. Off has no reference flow.','rows':rows}
 (p/'cost-comparison.json').write_text(json.dumps(report,indent=2));return report

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('core','compiler','reshade','nr-runtime','output'):p.add_argument('--'+n,type=Path,required=True)
 p.add_argument('--width',type=int,default=512);p.add_argument('--height',type=int,default=384)
 p.add_argument('--layers',type=int,choices=(1,3));p.add_argument('--mode',type=int,choices=range(4))
 p.add_argument('--compile-only',action='store_true');a=p.parse_args();
 if not all(128<=v<=4096 and v%2==0 for v in (a.width,a.height)):p.error('width/height must be even 128..4096')
 layers=(a.layers,) if a.layers else (1,3);modes=(a.mode,) if a.mode is not None else range(4)
 core=a.core.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 source=core/'src/nr_before_sr.cpp';build=core/'build';manifest=core.parent/'source-manifest.json'
 m=json.loads(manifest.read_text(encoding='utf-8-sig'))
 for rel,digest in m['files'].items():
  q=(manifest.parent/rel).resolve()
  if not q.is_relative_to(manifest.parent.resolve()) or sha(q)!=digest:raise ValueError('source identity: '+rel)
 original=ROOT/'experiments/nr-edit/tests/private_sr_ngx_host.cpp';host=original.read_text().split('void NeutralAndTinyComposition(',1)[0]+'\n}\n'
 fixture=out/'temporal_cost_real.cpp';shutil.copy2(ROOT/'tests/temporal_cost_real.cpp',fixture)
 wrapper=out/'host.cpp';wrapper.write_text('#define NR_PRIVATE_SR_CORE "'+source.as_posix()+'"\n#define NR_FLOW_PRODUCER_HEADER "'+(core/'src/temporal/amd_of_producer.hpp').as_posix()+'"\n'+host+'#include "'+fixture.as_posix()+'"\n')
 objects=[build/'obj'/n for n in ('buffer.o','hook.o','trampoline.o','hde64.o','nr_dual_spatial_d3d12.o','nr_face_detector.o','nr_face_worker.o','nr_aux_shader_runtime.o')]+objects_for(source)
 report={'scope':'Controlled real NR cost; off/native/AMD/LowFrequency, no reference flow baseline, not full game frame','output_extent':[a.width,a.height],'work_extent':[a.width//2,a.height//2],'layers_requested':list(layers),'modes_requested':list(modes),'source_commit':m['source_commit'],'core_sha256':sha(source),'fixture_sha256':sha(fixture),'host_helpers_sha256':sha(original),'wrapper_sha256':sha(wrapper),'manifest_sha256':sha(manifest),'compiler_sha256':sha(a.compiler),'validator_sha256':sha(Path(__file__)),'commands':[],'result':'running','runtime_sha256':{'bridge':sha(build/'nrchain_nvngx.dll'),'nr':sha(a.nr_runtime)},'linked_objects':{str(o):sha(o) for o in objects}}
 def save():(out/'validation.json').write_text(json.dumps(report,indent=2))
 def run(name,args,cwd=out):
  t=time.monotonic();r=subprocess.run(list(map(str,args)),cwd=cwd,capture_output=True,timeout=240);(out/(name+'.log')).write_bytes(r.stdout+r.stderr)
  report['commands'].append({'name':name,'args':list(map(str,args)),'exit':r.returncode,'seconds':time.monotonic()-t});save();print((r.stdout+r.stderr).decode('utf-8','replace')[-7000:],flush=True);return r.returncode
 flags=['-std=c++20','-O1','-DNDEBUG','-DUNICODE','-D_UNICODE','-D_WIN64','-DNR_FACE_BACKEND=1','-DNR_CODEC_PRECOMPILED=1','-DNR_AUX_PRECOMPILED=1','-DNR_AUX_TIMING=1','-ffunction-sections','-fdata-sections','-static','-municode','-Wl,--gc-sections','-I'+str(build),'-I'+str(core/'third_party/minhook/include'),'-I'+str(a.reshade/'include'),'-I'+str(a.reshade/'deps/imgui')]
 code=run('compile',[a.compiler,*flags,wrapper,*objects,'-o',out/'temporal-cost.exe','-lkernel32','-luser32','-ld3d12','-ldxgi','-ldxguid','-lole32','-luuid','-ld3dcompiler','-lshell32','-lbcrypt'])
 if not code:report['binary_sha256']=sha(out/'temporal-cost.exe');save()
 if not code and not a.compile_only:
  for layer in layers:
   for mode in modes:
    label=f'layers{layer}-mode{mode}';case=out/label;case.mkdir();shutil.copy2(out/'temporal-cost.exe',case/'temporal-cost.exe')
    for src,name in [(build/'nrchain_nvngx.dll','nrchain_nvngx.dll'),(a.nr_runtime,'nvngx_dlssnr.dll')]:shutil.copy2(src,case/name)
    code=run(label,[case/'temporal-cost.exe',case/'nrchain_nvngx.dll',mode,layer,a.width,a.height],case)
    if code:break
   if code:break
 if not code and not a.compile_only:report['costs']=compare(out,layers,modes)
 report['compile_only']=a.compile_only
 report['result']='passed' if not code else 'failed';report['gpu_execution']='not-run' if a.compile_only else report['result'];save();return code
if __name__=='__main__':raise SystemExit(main())
