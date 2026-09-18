#!/usr/bin/env python3
"""Exact Perf7 vs current reset/token bodies. Does not run a game or NGX."""
from __future__ import annotations
import argparse, hashlib, json, os, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE='a3ea8f9cbf3e06662e622852dc11581973798d18'
def extract(s,needle):
    begin=s.index(needle);i=s.index('{',begin);depth=1;j=i+1
    while depth:
        depth+=(s[j]=='{')-(s[j]=='}');j+=1
    return s[begin:j]+'\n'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',required=True);p.add_argument('--output',required=True,type=Path)
    a=p.parse_args();out=a.output.resolve()
    if out.exists():raise ValueError('Choose a new output directory; retain previous evidence')
    out.mkdir(parents=True)
    old=subprocess.check_output(['git','show',BASE+':src/nr_command_state.inl'],cwd=ROOT,text=True,encoding='utf-8')
    new=(ROOT/'src/nr_command_state.inl').read_text(encoding='utf-8-sig')
    types=lambda s:s[s.index('enum class CommandRootKind'):s.index('template <typename T> void CommandStateAssign')]
    def model(s):
        text=types(s)
        for name in ['template <typename T> void CommandStateAssign(','void ReleaseNativeCommandState(','void ResetNativeCommandState(','void InvalidateCommandRootArgument(','void CommandStateSetValue(','void CommandStateSetConstants(']:text+=extract(s,name)
        return text
    results={'baseline':BASE,'source_sha256':hashlib.sha256(new.encode()).hexdigest(),'native_srw':os.name=='nt','game_verified':False,'runs':[]}
    def run(cmd,name,expected=0):
        result=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=180)
        (out/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        results['runs'].append({'name':name,'exit':result.returncode,'stdout':result.stdout.strip()})
        print(name,result.returncode,result.stdout.strip(),flush=True)
        if result.returncode!=expected:raise RuntimeError(name+' unexpected exit '+str(result.returncode)+'\n'+result.stderr[-6000:])
    try:
        run([a.compiler,'--version'],'compiler')
        for label,source in [('baseline',old),('candidate',new)]:
            d=out/label;d.mkdir()
            (d/'reset_reference.inl').write_text(model(old),encoding='utf-8')
            (d/'reset_current.inl').write_text(model(source),encoding='utf-8')
            for tag,body in [('reference',old),('current',source)]:
                token=extract(body,'CommandStateRecord *FindCommandStateTokenLocked(')
                scan='for (auto &record : g_command_states) if (record.token == token)'
                if token.count(scan)!=1:raise ValueError('Token scan counter anchor changed')
                token=token.replace(scan,'for (auto &record : g_command_states) if ((++token_probes, record.token == token))')
                (d/('token_'+tag+'.inl')).write_text(token,encoding='utf-8')
            exe=d/'reset.exe'
            run([a.compiler,'-std=c++20','-O2','-g','-Wall','-Wextra','-Werror','-static','-pthread','-I'+str(d),ROOT/'tests/perf8r_reset.cpp','-o',exe],label+'-build')
            run([exe],label+'-semantics')
            run([exe,'--reset-budget'],label+'-reset-budget',3 if label=='baseline' else 0)
            run([exe,'--token-budget'],label+'-token-budget',3 if label=='baseline' else 0)
        results['status']='passed'
    except Exception as e:results.update(status='failed',error=str(e));raise
    finally:(out/'validation.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
