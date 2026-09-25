#!/usr/bin/env python3
"""Frozen Perf9 vs actual current Reset, binding and submission contracts.

Windows COM/SRW CPU doubles, not NVIDIA or a game. WARP gates are separate.
"""
from __future__ import annotations
import argparse,hashlib,json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE='a93f6d00a6820c2013644ea5aaa415baafc91883'
def extract(text,signature):
 start=text.index(signature);i=text.index('{',start)+1;depth=1
 while depth:depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[start:i]+'\n'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',required=True);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
 out=a.output.resolve()
 if out.exists():raise ValueError('Choose a fresh output, preserve prior evidence')
 out.mkdir(parents=True);result={'baseline':BASE,'game_verified':False,'runs':[],'status':'running'}
 def load(name,old):return subprocess.check_output(['git','show',BASE+':src/'+name],cwd=ROOT,text=True,encoding='utf-8') if old else (ROOT/'src'/name).read_text(encoding='utf-8-sig')
 def run(cmd,name,expected=0):
  r=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=180)
  (out/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');print(name,r.returncode,r.stdout.strip(),flush=True)
  result['runs'].append({'name':name,'exit':r.returncode,'stdout':r.stdout.strip()})
  if r.returncode!=expected:raise RuntimeError(name+' expected '+str(expected)+'\n'+r.stderr[-6000:])
 try:
  run([a.compiler,'--version'],'compiler')
  for tag,old,counted in [('baseline',True,True),('candidate',False,True),('timing',False,False)]:
   folder=out/tag;folder.mkdir()
   for name,use_old in [('reference',True),('current',old)]:
    state=load('nr_command_state.inl',use_old)
    types=state[state.index('enum class CommandRootKind'):state.index('template <typename T> void CommandStateAssign')]
    model=types
    for needle in ['template <typename T> void CommandStateAssign(','void ReleaseNativeCommandState(','void ResetNativeCommandState(','void InvalidateCommandRootArgument(','void CommandStateSetValue(','void CommandStateSetConstants(','void CommandStateInvalidDynamic(','void CommandStateVertices(']:
     body=extract(state,needle)
     if not counted and needle=='void ResetNativeCommandState(':body='__attribute__((noinline)) '+body
     if not counted and needle=='void CommandStateVertices(':body='__attribute__((noinline)) '+body
     model+=body
    (folder/('reset_'+name+'.inl')).write_text(model,encoding='utf-8')
    token=extract(state,'CommandStateRecord *FindCommandStateTokenLocked(')
    scan='for (auto &record : g_command_states) if (record.token == token)'
    assert token.count(scan)==1
    token=token.replace(scan,'for (auto &record : g_command_states) if ((++token_probes,record.token == token))')
    (folder/('token_'+name+'.inl')).write_text(token,encoding='utf-8')
    submit=extract(load('nr_source_transport.inl',use_old),'void GuideCopiesSubmitted(')+extract(load('nr_source_frame_runtime.inl',use_old),'void OnExecute(')
    if counted and name=='current':
     for needle in ['for(const auto& copy:guide_copies)if(copy.recorded','for(auto& copy:guide_copies)if(copy.recorded']:
      assert submit.count(needle)==1
      submit=submit.replace(needle,needle.replace('if(copy.recorded','if((++guide_visits,copy.recorded)'))
    (folder/('submit_'+('old' if name=='reference' else 'current')+'.inl')).write_text(submit,encoding='utf-8')
   for kind in ['state','submit']:
    exe=folder/(kind+'.exe')
    run([a.compiler,'-std=c++20','-O2','-g','-Wall','-Wextra','-Werror','-static','-pthread','-I'+str(ROOT/'src'),'-I'+str(folder),ROOT/('tests/perf10_'+kind+'.cpp'),'-o',exe],tag+'-'+kind+'-build')
    if counted:
     run([exe],tag+'-'+kind+'-semantics');run([exe,'--reset-budget' if kind=='state' else '--budget'],tag+'-'+kind+'-budget',3 if old else 0)
    elif kind=='state':run([exe,'--bench'],'state-timing')
  # Publication/lifetime proof for the false guide summary. Use real source,
  # including the exact Capture and helper body, not a replacement mock.
  transport=load('nr_source_transport.inl',False)
  assert 'c.reserved=false;c.recorded=true;*selected=c;' in transport
  assert 'guides_awaiting_submission.store(true,std::memory_order_release)' in transport
  assert 'pending|=c.recorded&&!c.submitted&&!c.poisoned' in transport
  assert transport.count('guides_awaiting_submission.store(')==2
  result['source_hashes']={p:hashlib.sha256((ROOT/'src'/p).read_bytes()).hexdigest() for p in ['nr_command_state.inl','nr_source_frame_runtime.inl','nr_source_transport.inl']}
  result['status']='passed'
 except Exception as e:result.update(status='failed',error=str(e));raise
 finally:(out/'validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
