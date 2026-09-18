#!/usr/bin/env python3
"""Perf7-to-Perf8 exact-source lifecycle and descriptor-range regressions."""
from __future__ import annotations
import argparse, hashlib, json, os, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE='a3ea8f9cbf3e06662e622852dc11581973798d18'
def extract(text,signature):
 start=text.index(signature);opening=text.index('{',start);depth=1;i=opening+1
 while depth:depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[start:i]+'\n'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--sanitize',action='store_true');args=p.parse_args()
 out=args.output.resolve()
 if out.exists():raise ValueError('Preserve prior evidence; choose a fresh output')
 out.mkdir(parents=True)
 result={'baseline':BASE,'candidate':'Perf8','native_windows':os.name=='nt','game_verified':False,'runs':[],'status':'running'}
 def old(path):return subprocess.check_output(['git','show',BASE+':'+path],cwd=ROOT,text=True,encoding='utf-8')
 def current(path):return (ROOT/path).read_text(encoding='utf-8-sig')
 def run(cmd,name,expected=0):
  r=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=180)
  (out/(name+'.log')).write_text('$ '+' '.join(map(str,cmd))+'\n'+r.stdout+r.stderr,encoding='utf-8')
  result['runs'].append({'name':name,'exit':r.returncode,'stdout':r.stdout.strip()});print(name,r.returncode,r.stdout.strip(),flush=True)
  if r.returncode!=expected:raise RuntimeError(name+' expected '+str(expected)+'\n'+r.stderr[-6000:])
 def count_state(s):
  s=s.replace('Id h=r.list;','++work; Id h=r.list;')
  s=s.replace('for(unsigned i=0;i<capacity;++i)if(entries_[i].recording&&predicate(entries_[i]))',
              'for(unsigned i=0;i<capacity;++i)if((++work,entries_[i].recording&&predicate(entries_[i])))')
  s=s.replace('// PERF8_STATE_SHIFT: temporary test instrumentation only.','++work;')
  s=s.replace('// PERF8_STATE_MEMBER: temporary test instrumentation only.','++work;')
  return s
 try:
  flags=['-std=c++20','-O2','-g','-Wall','-Wextra','-Werror','-pthread','-I'+str(ROOT/'src')]
  if os.name=='nt':flags+=['-static']
  if args.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
  run([args.compiler,'--version'],'compiler')
  for tag,loader,counted in [('baseline',old,True),('candidate',current,True),('timing',current,False)]:
   folder=out/tag;folder.mkdir()
   for name,load in [('old',old),('current',loader)]:
    state=load('src/nr_source_state_table.inl');token=extract(load('src/nr_command_state.inl'),'CommandStateRecord *FindCommandStateTokenLocked(')
    if counted and name=='current':
     state=count_state(state)
     token=token.replace('if (record.token == token)', 'if ((++work,record.token == token))')
     # Only count the old full scan, not the obligatory live-token hint check.
     token=token.replace('if ((++work,record.token == token)) return &record;\n    }', 'if (record.token == token) return &record;\n    }',1) if 'thread_local unsigned slots' in token else token
    (folder/(name+'_state.inl')).write_text(state,encoding='utf-8')
    (folder/(name+'_token.inl')).write_text(token,encoding='utf-8')
   exe=folder/'lifetime.exe';run([args.compiler,*flags,'-I'+str(folder),ROOT/'tests/perf8_lifetime.cpp','-o',exe],tag+'-build')
   if counted:
    run([exe],tag+'-semantics');run([exe,'--budget'],tag+'-budget',3 if tag=='baseline' else 0)
   else:
    for n in range(3):run([exe,'--bench'],'lifetime-timing-'+str(n))
  folder=out/'ranges';folder.mkdir()
  for name,loader in [('old',old),('current',current)]:
   (folder/(name+'_normalize.inl')).write_text(extract(loader('src/nr_descriptor_provenance.h'),'inline bool NormalizeUnknownRanges('),encoding='utf-8')
  exe=folder/'ranges.exe';run([args.compiler,*flags,'-I'+str(folder),ROOT/'tests/perf8_ranges.cpp','-o',exe],'ranges-build')
  run([exe],'ranges-semantics');run([exe,'--bench'],'ranges-timing')
  result['source_hashes']={f:hashlib.sha256((ROOT/'src'/f).read_bytes()).hexdigest() for f in ['nr_source_state_table.inl','nr_command_state.inl','nr_descriptor_provenance.h','nr_descriptor_provenance_runtime.inl']}
  result['status']='passed'
 except Exception as e:result.update(status='failed',error=str(e));raise
 finally:(out/'validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
