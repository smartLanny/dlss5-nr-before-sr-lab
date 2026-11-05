"""Bounded CPU and WARP allocation-query gate; no GPU dispatch or game process."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for k in ('compiler','source','sdk','output'):p.add_argument('--'+k,type=Path,required=True)
 p.add_argument('--status-source',type=Path,required=True)
 a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
 header=a.source.resolve()/'amd_of_budget.hpp';sdk=a.sdk.resolve()
 sdkfile=sdk/'sdk/src/components/opticalflow/ffx_opticalflow.cpp'
 backend=sdk/'sdk/src/backends/dx12/ffx_dx12.cpp'
 fixture=ROOT/'tests/temporal_budget_cpu.cpp'
 status_header=a.status_source.resolve()/'nr_temporal_status.h'
 status_fixture=ROOT/'tests/temporal_status_log_cpu.cpp'
 report={'passed':False,'scope':'CPU boundary tests and WARP GetResourceAllocationInfo queries; no resource creation, GPU dispatch, or game',
  'source':str(header),'status_source':str(status_header),'sdk':str(sdk),'checks':[]}
 def save():(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
 for path in (a.compiler,header,status_header,sdkfile,backend,fixture,status_fixture):
  if not path.is_file():
   report['error']=f'Required temporal budget input missing: {path}';save();print(report['error'],file=sys.stderr);return 1
 try:
  source=sdkfile.read_text();parts=source.split('const FfxInternalResourceDescription internalSurfaceDesc[]',1)
  if len(parts)!=2:raise ValueError('FFX opticalflow internalSurfaceDesc marker changed')
  block=parts[1].split('memset(context->resources',1)[0]
  # Fail rather than reuse the estimate after an SDK resource layout change.
  if not (block.count('FFX_SURFACE_FORMAT_R8_UINT')==14 and
   block.count('FFX_SURFACE_FORMAT_R16G16_SINT')==14 and
   block.count('FFX_SURFACE_FORMAT_R32_UINT')==2 and block.count('FFX_SURFACE_FORMAT_R32_FLOAT')==1 and
   block.count('FFX_RESOURCE_FLAGS_NONE')==31 and 'FFX_RESOURCE_FLAGS_ALIASABLE' not in block and
   'HistogramBins = 256' in source and 'HistogramsPerDim = 3' in source and
   'effectContext.vramUsage.totalUsageInBytes += resourceSize' in backend.read_text()):
   raise ValueError('Pinned FFX resource layout or actual VRAM accounting changed')
 except (OSError,ValueError) as exc:
  report['error']=str(exc);save();print(report['error'],file=sys.stderr);return 1
 report.update(source_sha256=sha(header),status_sha256=sha(status_header),fixture_sha256=sha(fixture),status_fixture_sha256=sha(status_fixture),sdk_sources={str(sdkfile):sha(sdkfile),str(backend):sha(backend)},
  warp_runtime_helper_sha256=sha(ROOT/'tests/ci_warp_runtime.h'),
  aliasing='All 31 pinned SDK textures are non-aliasable. Count every allocation; no reuse discount. Runtime actual FFX totalUsageInBytes check remains.')
 for mode in ('cpu','warp','status'):
  exe=out/(mode+'.exe')
  if mode=='status':
   cmd=[str(a.compiler),'-std=c++17','-UNDEBUG','-static',f'-DNR_TEMPORAL_STATUS_HEADER="{status_header.as_posix()}"',str(status_fixture),'-o',str(exe)]
  else:
   cmd=[str(a.compiler),'-std=c++17','-UNDEBUG',f'-DNR_AMD_BUDGET_HEADER="{header.as_posix()}"',str(fixture),'-o',str(exe)]
  if mode=='warp':cmd+=['-DNR_BUDGET_WARP=1','-static','-ld3d12','-ldxgi','-ldxguid']
  for step,args in [('compile',cmd),('run',[str(exe)])]:
   r=subprocess.run(args,capture_output=True);(out/(mode+'-'+step+'.log')).write_bytes(r.stdout+r.stderr)
   report['checks'].append({'name':mode+'-'+step,'command':args,'exit_code':r.returncode,'log':str(out/(mode+'-'+step+'.log'))})
   if r.returncode:
    report['error']=f'{mode} {step} failed; see {out/(mode+"-"+step+".log")}'
    save();print(report['error'],file=sys.stderr);return r.returncode
   save()
 report['passed']=True;save();print(f'PASS temporal budget CPU, WARP allocation queries, and admission log limiter CPU: {out/"validation.json"}');return 0
if __name__=='__main__':raise SystemExit(main())
