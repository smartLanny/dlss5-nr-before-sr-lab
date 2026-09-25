#!/usr/bin/env python3
"""Frozen Perf8/current complete lineage and exact snapshot regressions.

CPU value doubles + native SRW, not NVIDIA inference or a game. Instrumentation
is inserted only into temporary copies; timed copies carry no counters.
"""
from __future__ import annotations
import argparse,hashlib,json,os,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE='43081138a6f53c487fe1204bfddb89f0ff1933a9'
def extract(text,signature):
 start=text.index(signature);opening=text.index('{',start);depth=1;i=opening+1
 while depth:depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[start:i]+'\n'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',required=True);p.add_argument('--output',required=True,type=Path);p.add_argument('--sanitize',action='store_true');a=p.parse_args()
 out=a.output.resolve()
 if out.exists():raise ValueError('Preserve existing receipts; choose a new output directory')
 out.mkdir(parents=True)
 result={'baseline':BASE,'native_srw':os.name=='nt','game_verified':False,'nvidia_verified':False,'runs':[],'status':'running'}
 def load(path,old=False):
  return subprocess.check_output(['git','show',BASE+':src/'+path],cwd=ROOT,text=True,encoding='utf-8') if old else (ROOT/'src'/path).read_text(encoding='utf-8-sig')
 def run(cmd,name,expected=0):
  r=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=180)
  (out/(name+'.log')).write_text('$ '+' '.join(map(str,cmd))+'\n'+r.stdout+r.stderr,encoding='utf-8')
  result['runs'].append({'name':name,'exit':r.returncode,'stdout':r.stdout.strip()});print(name,r.returncode,r.stdout.strip(),flush=True)
  if r.returncode!=expected:raise RuntimeError(name+' expected '+str(expected)+'\n'+r.stderr[-6000:])
 try:
  flags=['-std=c++20','-O2','-g','-Wall','-Wextra','-Werror','-pthread']
  if os.name=='nt':flags+=['-static']
  if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
  run([a.compiler,'--version'],'compiler')
  for tag,old,counted in [('baseline',True,True),('candidate',False,True),('timing',False,False)]:
   folder=out/tag;folder.mkdir()
   for name,use_old in [('old',True),('current',old)]:
    source=load('nr_source_frame.hpp',use_old)
    needle='    bool AnyPending() const noexcept'
    if source.count(needle)!=1:raise ValueError('Lineage extraction boundary changed')
    source=source.replace(needle,'    Id Perf9BatchStrideForTest() const noexcept {return sizeof(Batch);}\n'+needle)
    source=source.replace('namespace nr_source_frame {','namespace nr_source_frame {\ninline std::uint64_t perf9_pending_probes=0;',1)
    if counted and name=='current':
     needle='if(batch.recording&&!batch.submitted&&batch.count)'
     if use_old and source.count(needle)!=1:raise ValueError('Frozen pending work counter boundary changed')
     source=source.replace(needle,'if((++perf9_pending_probes,batch.recording&&!batch.submitted&&batch.count))')
    if name=='old':source=source.replace('namespace nr_source_frame {','namespace perf9_reference {')
    (folder/('lineage_'+name+'.hpp')).write_text(source,encoding='utf-8')
    snapshot=extract(load('nr_post_boundary_native.inl',use_old),'nr_post_boundary::List SnapshotPostBoundaryList(')
    if counted and name=='current':
     needle='entry.native==native && entry.token'
     if snapshot.count(needle)!=1:raise ValueError('Actual snapshot scan boundary changed')
     snapshot=snapshot.replace(needle,'(++snapshot_probes,entry.native==native && entry.token)')
    (folder/('snapshot_'+name+'.inl')).write_text(snapshot,encoding='utf-8')
   for test in ['lineage','snapshot']:
    exe=folder/(test+'.exe')
    run([a.compiler,*flags,'-I'+str(folder),ROOT/('tests/perf9_'+test+'.cpp'),'-o',exe],tag+'-'+test+'-build')
    if counted:
     run([exe],tag+'-'+test+'-semantics');run([exe,'--budget'],tag+'-'+test+'-budget',3 if old else 0)
    elif not a.sanitize:
     for n in range(2):run([exe,'--bench'],test+'-timing-'+str(n))
  result['source_sha256']={p:hashlib.sha256((ROOT/'src'/p).read_bytes()).hexdigest() for p in ['nr_source_frame.hpp','nr_post_boundary_native.inl']}
  result['status']='passed'
 except Exception as e:result.update(status='failed',error=str(e));raise
 finally:(out/'validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
