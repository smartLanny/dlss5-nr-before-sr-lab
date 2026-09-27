"""Compile/run the experimental RR edit recorder's scalar admission contract only."""
import argparse
import json
import pathlib
import subprocess
import sys

ROOT=pathlib.Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--compiler',required=True,type=pathlib.Path)
    p.add_argument('--output',required=True,type=pathlib.Path)
    a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=True)
    exe=a.output/'rr-admission-contract.exe'
    source=ROOT/'tests/rr_admission_contract.cpp'
    includes=ROOT/'src'
    commands=[
        [str(a.compiler),'-std=c++20','-Wall','-Wextra','-Werror','-I',str(includes),str(source),'-o',str(exe)],
        [str(exe)],
    ]
    results=[]
    for cmd in commands:
        run=subprocess.run(cmd,cwd=a.output,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        results.append({'command':cmd,'exit_code':run.returncode,'output':run.stdout})
        if run.returncode: break
    report={'scope':'CPU scalar admission actually called by experimental nr_rr_dx12::Valid; no Feature13 or material guide producer',
            'passed':len(results)==len(commands) and all(x['exit_code']==0 for x in results),
            'results':results}
    (a.output/'validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({'passed':report['passed'],'receipt':str(a.output/'validation.json')}))
    return 0 if report['passed'] else 1

if __name__=='__main__': sys.exit(main())
