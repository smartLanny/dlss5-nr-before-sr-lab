#!/usr/bin/env python3
"""Exercise the actual retired-context collector and lifetime gates vs Perf11.
CPU services are controlled doubles; this does not prove NVIDIA or game VRAM.
"""
from __future__ import annotations
import argparse,hashlib,json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE='f7ca0d9644e56b41114719877b136d1a8af2bdf3'
def definition(text,marker,semi=False):
 start=text.index(marker)
 while True:
  opening=text.index('{',start)
  if ';' not in text[start:opening]:break
  start=text.index(marker,start+len(marker))
 i=opening+1;depth=1
 while depth:depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[start:i+int(semi)]+'\n'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',required=True);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
 out=a.output.resolve()
 if out.exists():raise RuntimeError('Preserve old receipts; use a fresh output')
 out.mkdir(parents=True)
 old=subprocess.check_output(['git','show',BASE+':src/nr_before_sr.cpp'],cwd=ROOT,text=True,encoding='utf-8')
 new=(ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
 result={'baseline':BASE,'source_sha256':hashlib.sha256(new.encode()).hexdigest(),'gpu_services':'CPU doubles; full Core WARP verified separately','game_verified':False,'runs':[]}
 def run(args,name,expected=0):
  r=subprocess.run(list(map(str,args)),cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
  (out/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
  result['runs'].append({'name':name,'exit':r.returncode,'stdout':r.stdout.strip()});print(name,r.returncode,r.stdout.strip(),flush=True)
  if r.returncode!=expected:raise RuntimeError(name+' expected '+str(expected)+'\n'+r.stderr[-5000:])
 try:
  run([a.compiler,'--version'],'compiler')
  for name,source in [('baseline',old),('candidate',new)]:
   folder=out/name;folder.mkdir()
   predicates=''.join(definition(source,m) for m in ['bool IsSlotFenceCompleteLocked(','bool IsContextFenceCompleteLocked(','bool CanMutateSlotLocked(','bool CanDestroyContextLocked('])
   # Definitions appear after forward declarations; select the complete section.
   prefix=definition(source,'void RetireAuxiliaryDependenciesLocked(') if name=='candidate' else ''
   (folder/'collector.inl').write_text(prefix+definition(source,'void CollectRetiredContextsLocked('),encoding='utf-8')
   (folder/'lifecycle.inl').write_text(predicates,encoding='utf-8')
   if name=='candidate':(folder/'snapshot.inl').write_text(definition(source,'struct NrContextMemorySnapshot {',True)+definition(source,'NrContextMemorySnapshot SnapshotNrContextMemory('),encoding='utf-8')
   exe=folder/'retirement.exe'
   run([a.compiler,'-std=c++20','-O2','-Wall','-Wextra','-Werror','-static','-DBASELINE='+str(int(name=='baseline')),'-I'+str(folder),ROOT/'tests/aux_parent_retirement.cpp','-o',exe],name+'-compile')
   run([exe,'--reproduction'],name+'-orphan-reproduction',3 if name=='baseline' else 0)
   run([exe],name+'-lifetime-contracts')
  result['status']='passed'
 except Exception as e:result.update(status='failed',error=str(e));raise
 finally:(out/'validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
