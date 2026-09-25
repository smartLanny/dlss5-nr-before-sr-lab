#!/usr/bin/env python3
"""Compare actual Perf6/Perf7 observation bodies, not duplicated algorithms."""
from __future__ import annotations
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE='8baa048cade9bb10b0108c77ad4e014717b49377'

def extract(text,signature):
 start=text.index(signature);opening=text.index('{',start);depth=1;i=opening+1
 while depth:
  depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[start:i]+'\n'

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',required=True);p.add_argument('--output',required=True,type=Path)
 p.add_argument('--expect-unfixed',action='store_true');p.add_argument('--sanitize',action='store_true');args=p.parse_args();out=args.output.resolve()
 if out.exists():raise ValueError('Use a fresh evidence directory')
 out.mkdir(parents=True)
 result={'baseline':BASE,'cpu_doubles':True,'native_srw':os.name=='nt','game_verified':False,'runs':[],'status':'running'}
 def run(cmd,name,expected=0):
  r=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=180,env={**os.environ,'COMPAT_ROUTE_TEST_CXX':args.compiler})
  (out/(name+'.log')).write_text('$ '+' '.join(map(str,cmd))+'\n'+r.stdout+r.stderr,encoding='utf-8')
  result['runs'].append({'name':name,'exit':r.returncode,'stdout':r.stdout.strip()});print(name,r.returncode,r.stdout.strip(),flush=True)
  if r.returncode!=expected:raise RuntimeError(name+' expected '+str(expected)+'\n'+r.stderr[-6000:])
 old=lambda f:subprocess.check_output(['git','show',BASE+':src/'+f],cwd=ROOT,text=True,encoding='utf-8')
 current=lambda f:(ROOT/'src'/f).read_text(encoding='utf-8-sig')
 def state_block(s):return s[s.index('struct ResourceState {') if 'struct ResourceState {' in s else s.index('#include "nr_source_state_table.inl"'):s.index('struct Producer {')]
 stubs='''unsigned long long state_probes=0,resource_queries=0,recording_queries=0,lock_calls=0;
Recording recording{1,1,1,1};Id resource_generation=1;
struct Lock{Lock(){++lock_calls;}};
Recording RecordingOf(ID3D12GraphicsCommandList*){++recording_queries;return recording;}
Resource ResourceOf(ID3D12Resource* p){++resource_queries;return p?Resource{Id(p),resource_generation,1,0}:Resource{};}
std::atomic<bool> source_receipts_pending{false};
struct {unsigned invalidations=0;void InvalidateDevice(Recording){++invalidations;}}lineage;
'''
 try:
  for required in ['perf7_tracking.cpp','perf7_identity.cpp']:
   if not (ROOT/'tests'/required).is_file():raise ValueError('Missing mandatory fixture: '+required)
  run([args.compiler,'--version'],'compiler')
  olds=old('nr_source_frame_runtime.inl');news=current('nr_source_frame_runtime.inl')
  result['runtime_sha256']=hashlib.sha256((ROOT/'src/nr_source_frame_runtime.inl').read_bytes()).hexdigest()
  flags=['-std=c++20','-O2','-g','-Wall','-Wextra','-Werror','-pthread','-I'+str(ROOT/'src')]
  if os.name=='nt':flags+=['-static']
  if args.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
  for name,s,counted in [('baseline',olds,True),('candidate',news,True),('timing',news,False)]:
   folder=out/name;folder.mkdir();(folder/'tracking_stubs.inl').write_text(stubs,encoding='utf-8')
   (folder/'tracking_old.inl').write_text(state_block(olds),encoding='utf-8')
   body=state_block(s)
   if counted:body=body.replace('entry.recording==recording&&entry.resource==resource','(++state_probes,entry.recording==recording&&entry.resource==resource)')
   (folder/'tracking_current.inl').write_text(body,encoding='utf-8')
   h=current('nr_source_state_table.inl')
   if counted:h=h.replace('// PERF7_STATE_PROBE: temporary test instrumentation only.','++state_probes;')
   (folder/'nr_source_state_table.inl').write_text(h,encoding='utf-8')
   binary=folder/'tracking.exe';run([args.compiler,*flags,'-I'+str(folder),ROOT/'tests/perf7_tracking.cpp','-o',binary],name+'-build')
   if counted:
    run([binary,'--budget'],name+'-budget',3 if name=='baseline' or args.expect_unfixed else 0)
    run([binary],name+'-semantics')
   else:
    for n in range(3):run([binary,'--bench'],'timing-'+str(n))
  if (ROOT/'tests/perf7_identity.cpp').exists():
   for tag in ['baseline','candidate']:
    folder=out/('identity-'+tag);folder.mkdir()
    for ns,loader in [('old',old),('current',old if tag=='baseline' else current)]:
     obs=loader('nr_source_observer.inl').split('bool EnsureSourceObservation(')[0]
     runtext=loader('nr_source_frame_runtime.inl');a=runtext.index('struct CoreRecordingDevice');b=runtext.index('Resource ResourceOf(')
     core=runtext[a:b];snapshot=extract(loader('nr_post_boundary_native.inl'),'nr_post_boundary::List SnapshotPostBoundaryList(')
     if ns=='current':
      obs=obs.replace('entry.native==native','(++observer_probes,entry.native==native)').replace('// PERF7_OBSERVER_PROBE','++observer_probes; //')
      core=core.replace('entry.list==core.address&&entry.lifetime==core.token','(++device_probes,entry.list==core.address&&entry.lifetime==core.token)').replace('// PERF7_CORE_DEVICE_PROBE','++device_probes; //')
      snapshot=snapshot.replace('entry.native==native && entry.token','(++snapshot_probes,entry.native==native && entry.token)')
     (folder/(ns+'_observer.inl')).write_text(obs,encoding='utf-8');(folder/(ns+'_recording.inl')).write_text(core,encoding='utf-8');(folder/(ns+'_snapshot.inl')).write_text(snapshot,encoding='utf-8')
    exe=folder/'identity.exe';run([args.compiler,*flags,'-I'+str(folder),ROOT/'tests/perf7_identity.cpp','-o',exe],tag+'-identity-build')
    run([exe],tag+'-identity-semantics');run([exe,'--budget'],tag+'-identity-budget',3 if tag=='baseline' or args.expect_unfixed else 0)
  run([sys.executable,'-m','unittest','tests.test_compat_route','-q'],'compatibility-route')
  result['status']='passed'
 except Exception as e:result.update(status='failed',error=str(e));raise
 finally:(out/'validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
