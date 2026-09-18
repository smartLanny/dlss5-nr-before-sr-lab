#!/usr/bin/env python3
"""Validate prepared descriptor ranges against actual Perf4 production headers."""
from __future__ import annotations
import argparse, hashlib, json, os, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE='ea9e0b896e6d386b719cddb916b66bc99c43768b'
HEADERS=('nr_descriptor_provenance.h','nr_source_frame.hpp')

def instrument(text):
    needle='if(r.type==heap.type&&r.first<=range.end&&range.base<=r.last)'
    if text.count(needle)!=1:raise ValueError('Raw candidate fallback changed')
    text=text.replace(needle,'if((++perf5_predicates,r.type==heap.type&&r.first<=range.end&&range.base<=r.last))')
    text=text.replace('const auto& candidate=cpu_ranges[--upper];','++perf5_predicates;const auto& candidate=cpu_ranges[--upper];')
    text=text.replace('const auto& requested=scope.ranges[mid];','++perf5_predicates;const auto& requested=scope.ranges[mid];')
    return text.replace('namespace nr_descriptor {','namespace nr_descriptor {\ninline std::uint64_t perf5_predicates=0;',1)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--expect-unfixed',action='store_true');p.add_argument('--sanitize',action='store_true')
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    result={'baseline':BASE,'status':'running','game_verified':False,'runs':[]}
    def run(cmd,name,expected=0):
        q=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=180)
        (out/(name+'.log')).write_text('$ '+' '.join(map(str,cmd))+'\n'+q.stdout+q.stderr,encoding='utf-8')
        result['runs'].append({'name':name,'exit':q.returncode,'stdout':q.stdout.strip()})
        print(name,q.returncode,q.stdout.strip(),flush=True)
        if q.returncode!=expected:raise RuntimeError(name+': '+q.stderr[-6000:])
    try:
        old={n:subprocess.check_output(['git','show',BASE+':src/'+n],cwd=ROOT,text=True,encoding='utf-8') for n in HEADERS}
        current={n:(ROOT/'src'/n).read_text(encoding='utf-8-sig') for n in HEADERS}
        result['source_sha256']={n:hashlib.sha256((ROOT/'src'/n).read_bytes()).hexdigest() for n in HEADERS}
        run([a.compiler,'--version'],'compiler')
        flags=['-std=c++20','-O2','-g','-Wall','-Wextra','-Werror','-pthread']
        if os.name=='nt':flags+=['-static']
        if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        for label,headers,count in [('old',old,True),('candidate',current,True),('timing',current,False)]:
            target=out/label;target.mkdir()
            for n,text in headers.items():
                (target/n).write_text(instrument(text) if count and n==HEADERS[0] else text,encoding='utf-8')
            for n,suffix,namespace in [(HEADERS[0],'descriptor','nr_descriptor'),(HEADERS[1],'source','nr_source_frame')]:
                (target/('perf3_reference_'+suffix+'.h')).write_text(old[n].replace('namespace '+namespace+' {','namespace perf3_reference_'+('descriptor' if suffix=='descriptor' else 'source')+' {'),encoding='utf-8')
            exe=target/('ranges.exe' if os.name=='nt' else 'ranges')
            prepared='NormalizeUnknownRanges(' in headers[HEADERS[0]]
            run([a.compiler,*flags,'-DPERF3_COUNT_SCANS=0','-DPERF4_COUNT_WORK=0','-DPERF5_COUNT='+str(int(count)),
                 '-DPERF5_NORMALIZE='+str(int(prepared)),'-I'+str(target),ROOT/'tests/perf5_ranges.cpp','-o',exe],label+'-build')
            if count:
                run([exe,'--fallback-budget'],label+'-budget',3 if label=='old' or a.expect_unfixed else 0)
                run([exe],label+'-semantics')
            elif not a.sanitize:
                for repeat in range(3):run([exe,'--bench'],'timing-'+str(repeat))
        if not a.expect_unfixed:
            runtime=(ROOT/'src/nr_descriptor_provenance_runtime.inl').read_text(encoding='utf-8-sig')
            prepare=runtime.index('NormalizeUnknownRanges(copy.ranges.get(),range_count)')
            publish=runtime.index('BeginUnknownCopy(copy.unknown,copy.ranges.get(),range_count)')
            if prepare>=publish:raise ValueError('Native owned-range preparation must precede publication')
            result['runtime_preparation_before_publication']=True
        result['status']='passed'
    except Exception as e:
        result.update(status='failed',error=str(e));raise
    finally:(out/'validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
