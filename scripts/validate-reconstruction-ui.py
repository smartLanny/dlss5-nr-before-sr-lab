#!/usr/bin/env python3
"""Compile and exercise actual bilingual controls with deterministic host inputs.
This verifies UI state/labels/layout requests, not a rendered ReShade screenshot.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,time
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',required=True);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 source=ROOT/'src/nr_reconstruction_ui.inl';text=source.read_text(encoding='utf-8')
 for forbidden in ['A##reconstruction','B##reconstruction','A+B','NrCombo(', 'nr_reconstruction::ReasonName(', 'nr_reconstruction::ModeName(']:
  if forbidden in text:raise ValueError('Developer-only terminology returned to public UI: '+forbidden)
 result={'ui_sha256':sha(source),'policy_sha256':sha(ROOT/'src/nr_reconstruction_policy.h'),'harness_sha256':sha(ROOT/'tests/reconstruction_ui.cpp'),'rendered':False,'game_verified':False,'runs':[]}
 def run(name,cmd):
  start=time.monotonic();r=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
  (out/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');result['runs'].append({'name':name,'exit':r.returncode,'seconds':time.monotonic()-start,'stdout':r.stdout.strip()});print(name,r.returncode,r.stdout.strip(),flush=True)
  if r.returncode:raise RuntimeError(name+' failed: '+r.stderr)
 try:
  exe=out/'reconstruction_ui.exe'
  run('compile',[a.compiler,'-std=c++17','-O2','-static','-Wall','-Wextra','-Werror','-I'+str(ROOT/'src'),ROOT/'tests/reconstruction_ui.cpp','-o',exe])
  run('actual-controls',[exe]);result['status']='passed'
 except Exception as e:result.update(status='failed',error=str(e));raise
 finally:(out/'validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
