#!/usr/bin/env python3
"""Exact Perf5 negative controls and Perf6 descriptor cache/ordered-input tests."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[1]
BASE='cafb42215b02f0043cd93a16e4077f0d7fd26963'

def instrument(text):
    needle='auto &s=values[n-1];if(s.stamp.heap==heap && s.stamp.index==index)'
    if text.count(needle)!=1:raise ValueError('Slot Find observation boundary changed')
    text=text.replace(needle,'auto &s=values[n-1];++perf6_chain_checks;if(s.stamp.heap==heap && s.stamp.index==index)')
    needle='if(a.type!=b.type)return a.type<b.type;'
    if text.count(needle)!=1:raise ValueError('Range compare boundary changed')
    text=text.replace(needle,'++perf6_range_compares;'+needle)
    return text.replace('namespace nr_descriptor {','namespace nr_descriptor {\ninline std::uint64_t perf6_chain_checks=0,perf6_range_compares=0;',1)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--expect-unfixed',action='store_true');p.add_argument('--sanitize',action='store_true');args=p.parse_args()
    out=args.output.resolve()
    if out.exists():raise ValueError('Retain previous evidence; use a fresh output')
    out.mkdir(parents=True)
    result={'baseline':BASE,'game_verified':False,'runs':[],'status':'running'}
    def run(cmd,name,expected=0):
        r=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=180)
        (out/(name+'.log')).write_text('$ '+' '.join(map(str,cmd))+'\n'+r.stdout+r.stderr,encoding='utf-8')
        result['runs'].append({'name':name,'exit':r.returncode,'stdout':r.stdout.strip()});print(name,r.returncode,r.stdout.strip(),flush=True)
        if r.returncode!=expected:raise RuntimeError(name+': '+r.stderr[-5000:])
    try:
        run([args.compiler,'--version'],'compiler')
        source=(ROOT/'src/nr_descriptor_provenance.h').read_text(encoding='utf-8-sig')
        old=subprocess.check_output(['git','show',BASE+':src/nr_descriptor_provenance.h'],cwd=ROOT,text=True,encoding='utf-8')
        lineage=subprocess.check_output(['git','show',BASE+':src/nr_source_frame.hpp'],cwd=ROOT,text=True,encoding='utf-8')
        result['source_sha256']=hashlib.sha256((ROOT/'src/nr_descriptor_provenance.h').read_bytes()).hexdigest()
        flags=['-std=c++20','-O2','-g','-Wall','-Wextra','-Werror','-pthread']
        if os.name=='nt':flags+=['-static']
        if args.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        for name,text,count in [('old',old,True),('candidate',source,True),('timing',source,False)]:
            folder=out/name;folder.mkdir()
            (folder/'nr_descriptor_provenance.h').write_text(instrument(text) if count else text,encoding='utf-8')
            (folder/'nr_source_frame.hpp').write_text(lineage,encoding='utf-8')
            (folder/'perf3_reference_descriptor.h').write_text(old.replace('namespace nr_descriptor {','namespace perf3_reference_descriptor {'),encoding='utf-8')
            (folder/'perf3_reference_source.h').write_text(lineage.replace('namespace nr_source_frame {','namespace perf3_reference_source {'),encoding='utf-8')
            exe=folder/('test.exe' if os.name=='nt' else 'test')
            run([args.compiler,*flags,'-DPERF3_COUNT_SCANS=0','-DPERF6_COUNT_WORK='+str(int(count)),'-DPERF6_HAS_CACHE='+str(int('lookup_cache' in text)), '-I'+str(folder),ROOT/'tests/perf6_descriptor_lookup.cpp','-o',exe],name+'-build')
            if count:
                expected=3 if name=='old' or args.expect_unfixed else 0
                for test in ('lookup','sorted'):run([exe,'--'+test+'-budget'],name+'-'+test,expected)
                run([exe],name+'-semantics')
            elif not args.sanitize:
                for i in range(3):run([exe,'--bench'],'timing-'+str(i))
        result['status']='passed'
    except Exception as e:result.update(status='failed',error=str(e));raise
    finally:(out/'validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
