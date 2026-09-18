#!/usr/bin/env python3
"""Perf7/current exact range unions; timed complete normalize, including folding."""
from __future__ import annotations
import argparse,importlib.util,json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE='a3ea8f9cbf3e06662e622852dc11581973798d18'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',required=True);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    out=a.output.resolve()
    if out.exists():raise ValueError('Choose fresh output')
    out.mkdir(parents=True)
    spec=importlib.util.spec_from_file_location('extract_reset',ROOT/'scripts/validate-perf8r-reset.py');helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
    old=subprocess.check_output(['git','show',BASE+':src/nr_descriptor_provenance.h'],cwd=ROOT,text=True,encoding='utf-8')
    new=(ROOT/'src/nr_descriptor_provenance.h').read_text(encoding='utf-8-sig')
    def body(s,counted):
        text=helper.extract(s,'struct UnknownRange {').rstrip()+';\n'+helper.extract(s,'inline bool NormalizeUnknownRanges(')
        if counted:
            if text.count('if(a.type!=b.type)')!=1:raise ValueError('Comparator counter anchor changed')
            text=text.replace('if(a.type!=b.type)','++comparisons; if(a.type!=b.type)')
        return text
    results={'baseline':BASE,'game_verified':False,'runs':[]}
    def run(cmd,name,expected=0):
        r=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=180)
        (out/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');print(name,r.returncode,r.stdout.strip(),flush=True)
        results['runs'].append({'name':name,'exit':r.returncode,'stdout':r.stdout.strip()})
        if r.returncode!=expected:raise RuntimeError(name+' failed\n'+r.stderr[-4000:])
    try:
        for label,s in [('baseline',old),('candidate',new),('timing',new)]:
            folder=out/label;folder.mkdir()
            (folder/'ranges_reference.inl').write_text(body(old,label!='timing'),encoding='utf-8')
            (folder/'ranges_current.inl').write_text(body(s,label!='timing'),encoding='utf-8')
            exe=folder/'ranges.exe'
            run([a.compiler,'-std=c++20','-O2','-static','-I'+str(ROOT/'src'),'-I'+str(folder),ROOT/'tests/perf8r_ranges.cpp','-o',exe],label+'-build')
            if label=='timing':run([exe,'--bench'],label+'-bench')
            else:
                run([exe],label+'-semantics');run([exe,'--budget'],label+'-budget',3 if label=='baseline' else 0)
        results['status']='passed'
    except Exception as e:results.update(status='failed',error=str(e));raise
    finally:(out/'validation.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
