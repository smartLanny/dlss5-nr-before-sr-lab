#!/usr/bin/env python3
"""Compile and execute actual production HLSL math through the existing CPU adapter.
No second algorithm, model inference, texture sampling or game claim.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1]
def marked(s,tag):
    a,b='// '+tag+'_BEGIN','// '+tag+'_END'
    if s.count(a)!=1 or s.count(b)!=1:raise ValueError('Math markers changed: '+tag)
    return s.split(a,1)[1].split(b,1)[0].replace('[unroll]','')
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',required=True);p.add_argument('--output',required=True,type=Path)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    restore=(ROOT/'src/nr_colour_restore.hlsli').read_text(encoding='utf-8-sig')
    new=(ROOT/'src/nr_reconstruction_math.hlsli').read_text(encoding='utf-8-sig')
    adapter=(ROOT/'tests/colour_restore_math_test.cpp').read_text(encoding='utf-8').split('#include "colour_restore_math.generated.hpp"')[0]
    adapter=adapter.replace('#include "../src/nr_colour_restore_policy.h"','')
    adapter+='\nbool3 operator==(float3 a,float3 b){return {a.x==b.x,a.y==b.y,a.z==b.z};}\n'
    header=adapter+marked(restore,'NR_COLOUR_RESTORE_MATH')+'\n'+marked(new,'NR_RECONSTRUCTION_MATH')
    (out/'reconstruction.generated.hpp').write_text(header,encoding='utf-8')
    results={'production_shader_sha256':hashlib.sha256(new.encode()).hexdigest(),'gpu_executed':False,'game_verified':False,'runs':[]}
    try:
        exe=out/'reconstruction.exe'
        commands=[('compile',[a.compiler,'-std=c++20','-O2','-static','-Wall','-Wextra','-Werror','-ffp-contract=off','-I'+str(out),'-I'+str(ROOT/'src'),str(ROOT/'tests/reconstruction_math.cpp'),'-o',str(exe)]),('run',[str(exe)])]
        for name,cmd in commands:
            r=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
            (out/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');print(name,r.returncode,r.stdout,r.stderr[-3000:],flush=True)
            results['runs'].append({'name':name,'exit':r.returncode,'stdout':r.stdout})
            if r.returncode:raise RuntimeError(name+' failed')
        results['status']='passed'
    except Exception as e:results.update(status='failed',error=str(e));raise
    finally:(out/'validation.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
if __name__=='__main__':main()
