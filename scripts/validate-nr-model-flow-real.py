"""Independent real NR model input: zero vectors, measured AMD dense model flow, and external accumulation only."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,time,array,struct
from temporal_test_objects import objects_for
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def compare(p):
 lut=struct.unpack('<65536e',struct.pack('<65536H',*range(65536)))
 report={'scope':'One real Feature18, nonperiodic translation (-4,-2) source pixels, N=256x192 O=512x384. Interior every-fourth-pixel RGB aligned temporal MAE is a stability proxy, not occlusion/game quality.','frames':[]};previous={}
 indices=[(y*512+x)*4+c for y in range(64,320,4) for x in range(64,448,4) for c in range(3)]
 for i in range(2,17):
  frames={m:array.array('H',(p/m/f'frame-{i}.fp16').read_bytes()) for m in ('zero','amd','external')}
  ds=[abs(lut[frames['zero'][k]]-lut[frames['amd'][k]]) for k in indices]
  row={'frame':i,'byte_identical':frames['zero']==frames['amd'],'mean_abs_between_modes':sum(ds)/len(ds),'max_abs_between_modes':max(ds),'mean_abs_external_vs_zero':sum(abs(lut[frames['zero'][k]]-lut[frames['external'][k]]) for k in indices)/len(indices)}
  for mode,v in frames.items():
   if mode in previous:row[mode+'_motion_aligned_mae']=sum(abs(lut[v[k]]-lut[previous[mode][k-(2*512+4)*4]]) for k in indices)/len(indices)
   previous[mode]=v
  report['frames'].append(row)
 report['warm_motion_mean']={m:sum(f[m+'_motion_aligned_mae'] for f in report['frames'] if f['frame']>=8)/9 for m in ('zero','amd','external')}
 report['capture_sha256']={str(f.relative_to(p)):sha(f) for m in ('zero','amd','external') for f in (p/m).glob('*.fp16')}
 (p/'comparison.json').write_text(json.dumps(report,indent=2));return report['warm_motion_mean']
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('core','compiler','reshade','nr-runtime','output'):p.add_argument('--'+n,type=Path,required=True)
 p.add_argument('--compile-only',action='store_true');a=p.parse_args();core=a.core.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 source=core/'src/nr_before_sr.cpp';build=core/'build';manifest=core.parent/'source-manifest.json'
 m=json.loads(manifest.read_text(encoding='utf-8-sig'))
 for rel,digest in m['files'].items():
  q=(manifest.parent/rel).resolve()
  if not q.is_relative_to(manifest.parent.resolve()) or sha(q)!=digest:raise ValueError('source identity: '+rel)
 original=ROOT/'experiments/nr-edit/tests/private_sr_ngx_host.cpp';host=original.read_text().split('void NeutralAndTinyComposition(',1)[0]+'\n}\n'
 fixture=out/'nr_model_flow_real.cpp';shutil.copy2(ROOT/'tests/nr_model_flow_real.cpp',fixture)
 wrapper=out/'host.cpp';wrapper.write_text('#define NR_PRIVATE_SR_CORE "'+source.as_posix()+'"\n#define NR_FLOW_PRODUCER_HEADER "'+(core/'src/temporal/amd_of_producer.hpp').as_posix()+'"\n'+host+'#include "'+fixture.as_posix()+'"\n')
 objects=[build/'obj'/n for n in ('buffer.o','hook.o','trampoline.o','hde64.o','nr_dual_spatial_d3d12.o','nr_face_detector.o','nr_face_worker.o','nr_aux_shader_runtime.o')]+objects_for(source)
 report={'scope':'Synthetic moving scene; real signed Feature18; measured AMD dense flow resampled on CPU after exact producer completion. External accumulation is a separate third group with unchanged model vectors; no private SR/game. No quality benefit inferred from success. This probe intentionally computes reference flow in all groups and cannot establish overhead.','source_commit':m['source_commit'],'core_sha256':sha(source),'fixture_sha256':sha(fixture),'commands':[],'result':'running','runtime_sha256':{'bridge':sha(build/'nrchain_nvngx.dll'),'nr':sha(a.nr_runtime)},'linked_objects':{str(o):sha(o) for o in objects}}
 def save():(out/'validation.json').write_text(json.dumps(report,indent=2))
 def run(name,args,cwd=out):
  t=time.monotonic();r=subprocess.run(list(map(str,args)),cwd=cwd,capture_output=True,timeout=240);(out/(name+'.log')).write_bytes(r.stdout+r.stderr)
  report['commands'].append({'name':name,'args':list(map(str,args)),'exit':r.returncode,'seconds':time.monotonic()-t});save();print((r.stdout+r.stderr).decode('utf-8','replace')[-7000:],flush=True);return r.returncode
 flags=['-std=c++20','-O1','-DNDEBUG','-DUNICODE','-D_UNICODE','-D_WIN64','-DNR_FACE_BACKEND=1','-DNR_CODEC_PRECOMPILED=1','-DNR_AUX_PRECOMPILED=1','-DNR_AUX_TIMING=1','-ffunction-sections','-fdata-sections','-static','-municode','-Wl,--gc-sections','-I'+str(build),'-I'+str(core/'third_party/minhook/include'),'-I'+str(a.reshade/'include'),'-I'+str(a.reshade/'deps/imgui')]
 code=run('compile',[a.compiler,*flags,wrapper,*objects,'-o',out/'model-flow.exe','-lkernel32','-luser32','-ld3d12','-ldxgi','-ldxguid','-lole32','-luuid','-ld3dcompiler','-lshell32','-lbcrypt'])
 if not code and not a.compile_only:
  for mode in ('zero','amd','external'):
   case=out/mode;case.mkdir();shutil.copy2(out/'model-flow.exe',case/'model-flow.exe')
   for src,name in [(build/'nrchain_nvngx.dll','nrchain_nvngx.dll'),(a.nr_runtime,'nvngx_dlssnr.dll')]:shutil.copy2(src,case/name)
   code=run(mode,[case/'model-flow.exe',case/'nrchain_nvngx.dll',mode],case)
   if code:break
 if not code and not a.compile_only:report['aligned_stability']=compare(out)
 report['result']='passed' if not code else 'failed';report['gpu_execution']='not-run' if a.compile_only else report['result'];save();return code
if __name__=='__main__':raise SystemExit(main())
