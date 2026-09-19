#!/usr/bin/env python3
"""Exercise actual Core context selection with current/frozen inputs.
Only service/COM execution is stubbed; use existing GPU/lifetime gates as well.
"""
from __future__ import annotations
import argparse,hashlib,json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE='0b01a8e5f6d32cdbc57d7c0c4a7d106a9bdc442a'
def definition(s,marker,semicolon=False):
 start=s.index(marker);end=s.index('{',start)+1;depth=1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end+int(semicolon)]+'\n'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--baseline-only',action='store_true');a=p.parse_args()
 out=a.output.resolve()
 if out.exists():raise RuntimeError('Fresh evidence directory required')
 out.mkdir(parents=True)
 old=subprocess.check_output(['git','show',BASE+':src/nr_before_sr.cpp'],cwd=ROOT,text=True,encoding='utf-8')
 new=(ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
 result={'baseline':BASE,'source_sha256':hashlib.sha256(new.encode()).hexdigest(),'game_verified':False,'runs':[]}
 def run(cmd,name,expected=0):
  r=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=180)
  (out/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
  result['runs'].append({'name':name,'exit':r.returncode,'stdout':r.stdout.strip()});print(name,r.returncode,r.stdout.strip(),flush=True)
  if r.returncode!=expected:raise RuntimeError(name+' expected '+str(expected)+'\n'+r.stderr[-5000:])
 try:
  run([a.compiler,'--version'],'compiler')
  for label,s in [('baseline',old)]+([] if a.baseline_only else [('candidate',new)]):
   folder=out/label;folder.mkdir()
   defs='\n'.join(definition(s,m,semi) for m,semi in [('struct NrTuningSignature',True),('NrTuningSignature CurrentNrTuningSignature(',False),('bool SameNrTuningSignature(',False)])
   (folder/'tuning.inl').write_text(defs,encoding='utf-8')
   (folder/'lookup.inl').write_text(definition(s,'NrContext *FindOrCreateContext('),encoding='utf-8')
   exe=folder/'reuse.exe'
   run([a.compiler,'-std=c++20','-O2','-Wall','-Wextra','-Werror','-static','-DUNIFORM_LAYERS='+str(int('nr_layers::Read(g_config,layer_index)' in s)),'-I'+str(ROOT/'src'),'-I'+str(folder),ROOT/'tests/aux_history_reuse.cpp','-o',exe],label+'-build')
   run([exe,'--reproduction'],label+'-history-only-reproduction',3 if label=='baseline' else 0)
   run([exe],label+'-storage-lifetime-contracts')
  if not a.baseline_only:
   dual=(ROOT/'src/nr_dual_runtime.inl').read_text(encoding='utf-8-sig')
   bind=definition(new,'bool BindNrParameters(')
   # These are dependencies, not substitutes for the extracted behavior checks.
   required=['const bool reset_requested = reset || game_reset != 0 || history_changed ||',
    'context.reset_retry_pending = reset_requested;', 'SetInt(params, "DLSSNR.Reset", reset_requested ? 1 : 0);']
   for text in required:
    if text not in bind:raise AssertionError('Review reset-forwarding contract: '+text)
   for text in ['aux.dual_history.NeedsReset(key,primary.evaluations,primary.diagnostic_reset_requested)',
    'aux.width,aux.height,color,reset,slot.motion_proxy)', 'aux.reset_retry_pending=false;',
    'aux.dual_history.CommitSuccess(key,primary.evaluations)',
    'primary.successful_evaluations_since_reset<=g_config.warmup_after_create']:
    if text not in dual:raise AssertionError('Review auxiliary reset/warmup contract: '+text)
   result['reset_forwarding_source_contract']='unchanged; successful Evaluate still consumes reset, warmup retained'
  result['status']='passed'
 except Exception as e:result.update(status='failed',error=str(e));raise
 finally:(out/'validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
