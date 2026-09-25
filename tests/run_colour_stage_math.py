#!/usr/bin/env python3
"""Execute production early-reference math and bilinear expressions on the CPU.

Uses the existing small float3 adapter; native texture loads are substitutes.
A quarter-pixel phase mutation is a required negative control, not a diagnosis
of the owner's game. No shader/GPU/NGX/Windows build claim is made here.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def load(path):
    spec=importlib.util.spec_from_file_location('colour_stage_math_helpers',path)
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def marked(text, name):
    begin, end='// '+name+'_BEGIN','// '+name+'_END'
    if text.count(begin)!=1 or text.count(end)!=1: raise ValueError('Changed production markers: '+name)
    return text.split(begin,1)[1].split(end,1)[0].replace('[unroll]','')

SHIM = r'''
unsigned Passthrough=0,Width=1,Height=1,ModelWidth=1,ModelHeight=1;
unsigned loadPattern=0,sourceOffset=0;
struct float2 {float x,y; float2(float a,float b):x(a),y(b){} };
float2 operator*(float2 a,float2 b){return {a.x*b.x,a.y*b.y};}
float2 operator/(float2 a,float2 b){return {a.x/b.x,a.y/b.y};}
float2 operator-(float2 a,float b){return {a.x-b,a.y-b};}
float2 floor(float2 a){return {std::floor(a.x),std::floor(a.y)};}
float2 frac(float2 a){return {a.x-std::floor(a.x),a.y-std::floor(a.y)};}
struct int2 {int x,y; int2(int a,int b):x(a),y(b){} explicit int2(float2 a):x(int(a.x)),y(int(a.y)){} };
int2 operator+(int2 a,int2 b){return {a.x+b.x,a.y+b.y};}
float3 LoadProxyLinearV2(int2 p){
    int x=std::clamp(p.x,0,int(ModelWidth)-1),y=std::clamp(p.y,0,int(ModelHeight)-1);
    float a=loadPattern==0?.3f:loadPattern==1?(x<int(ModelWidth)/2?.1f:.8f):
        loadPattern==2?float((x+y+sourceOffset)%2)*.7f+.1f:float((x*43+y*149+sourceOffset)%257)/300.f;
    return {a,.1f+a*.5f,.8f-a*.3f};
}
float3 LoadNeuralLinearV2(int2 p){return LoadProxyLinearV2(p)*.93f+float3(.02f,.01f,.03f);}
'''

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler');args=parser.parse_args()
    cc=args.compiler or os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
    if not cc: raise SystemExit('No host C++20 compiler')
    helpers=load(ROOT/'scripts/patch-colour-restore.py')
    stage=load(ROOT/'scripts/patch-colour-stage.py')
    raw=(ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8')
    shader=(ROOT/'src/nr_colour_stage_reference.hlsli').read_text(encoding='utf-8')
    base=(ROOT/'src/nr_colour_restore.hlsli').read_text(encoding='utf-8')
    prep=(ROOT/'src/nr_colour_prepare.hlsli').read_text(encoding='utf-8')
    adapter=(ROOT/'tests/colour_restore_math_test.cpp').read_text(encoding='utf-8').split('#include "colour_restore_math.generated.hpp"')[0]
    adapter=adapter.replace('#include "../src/nr_colour_restore_policy.h"',
                            '#include "'+str(ROOT/'src/nr_colour_stage_policy.h').replace('\\','/')+'"')
    def body(s,sig):return s[slice(*helpers.definition(s,sig))]
    sampler=stage.reference_sampler(raw,helpers)
    # Same transformation used by the staging overlay, not a hand-written filter.
    shared=adapter+SHIM+marked(prep,'NR_COLOUR_PREPARE_MATH')+marked(base,'NR_COLOUR_RESTORE_MATH')
    shared+=marked(shader,'NR_COLOUR_STAGE_MATH')+body(shader,'bool ColourStageNeutral(')+body(shader,'float3 ColourStageLoadNeural(')
    shared+=body(raw,'float3 SampleProxyLinearV2(')+body(raw,'float3 SampleNeuralLinearV2(')
    tests=(ROOT/'tests/colour_stage_math_test.cpp').read_text(encoding='utf-8')
    with tempfile.TemporaryDirectory(prefix='nr-colour-stage-') as tmp:
        root=Path(tmp)
        for negative in (False,True):
            selected=sampler.replace(' - 0.5;', ' - 0.25;',1) if negative else sampler
            if negative and selected==sampler:raise ValueError('Phase negative-control anchor changed')
            cpp=root/'test.cpp';cpp.write_text(shared+selected+tests,encoding='utf-8')
            exe=root/('test.exe' if os.name=='nt' else 'test')
            command=[cc,'-std=c++20','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',str(cpp),'-o',str(exe)]
            if os.name=='nt':command.append('-static')
            subprocess.run(command,check=True,timeout=120)
            result=subprocess.run([str(exe)],text=True,capture_output=True,timeout=60)
            if negative:
                if result.returncode!=1 or 'neutral pairing uses production sampler coordinates' not in result.stderr:
                    raise ValueError('Quarter-pixel negative control did not fail in the pairing test: '+result.stderr)
                print('Required phase negative control rejected:',result.stderr.strip())
            else:
                if result.returncode:raise ValueError(result.stderr)
                print(result.stdout.strip())
    for name,data in [('reference',shader),('restore',base),('sampling_source',raw)]:
        print(name+'_sha256='+hashlib.sha256(data.encode()).hexdigest())

if __name__=='__main__':
    try:main()
    except (ValueError,OSError,subprocess.SubprocessError) as error:raise SystemExit(str(error)) from error
