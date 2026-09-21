"""Portable checks for exact production cache/policy code, NOT D3D12 execution.

Windows follow-up must compile the complete Core and run the recorded GPU
oracles in both process-start modes. This runner does not label preprocessing
or a CPU half conversion as HLSL compilation/GPU parity.
"""
from __future__ import annotations
import argparse
import sys
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

BASE = '5567bb479275bf4eb24be4c8db5bbba9a11d5177'
HEADER = '''#include <array>\n#include <memory>\n#include <mutex>\n#include <utility>\n#include <stdexcept>\n#include <cstdint>\n#include <atomic>\n#include <thread>\n#include <vector>\n#include <iostream>\n'''
HARNESS = r'''
int checks=0;
void require(bool condition,const char* name) {
    ++checks;if(!condition)throw std::runtime_error(name);
}
struct Item { std::shared_ptr<int> device; int variant; };
int main() {
    ImmutableArtifactCache<int,3> cache;int builds=0;
    auto make=[&](){++builds;return std::make_shared<const int>(19);};
    auto a=cache.get(0,make), b=cache.get(0,make);
    require(a==b&&builds==1,"one immutable build");
    auto c=cache.get(1,make);require(c!=a&&builds==2,"variant separation");
    bool threw=false;try{cache.get(3,make);}catch(const std::out_of_range&){threw=true;}
    require(threw&&builds==2,"bounded key");
    threw=false;try{cache.get(2,[]()->std::shared_ptr<const int>{throw std::runtime_error("build failure");});}
    catch(const std::runtime_error&){threw=true;}
    require(threw,"failure propagation");
    threw=false;try{cache.get(2,[](){return std::shared_ptr<const int>{};});}
    catch(const std::runtime_error&){threw=true;}
    require(threw,"no empty artifact cached");
    require(*cache.get(2,make)==19&&builds==3,"failure did not poison cache");
    ImmutableArtifactCache<int,1> concurrent;
    std::atomic<int> calls{0};std::array<std::shared_ptr<const int>,24> results;
    std::vector<std::thread> threads;
    for(unsigned i=0;i<results.size();++i)threads.emplace_back([&,i]{
        results[i]=concurrent.get(0,[&]{++calls;return std::make_shared<const int>(42);});});
    for(auto& t:threads){t.join();}
    require(calls==1,"one concurrent compiler");
    for(const auto& r:results)require(r==results[0],"shared immutable identity");
    LiveArtifactCache<Item,2> live;
    auto device=std::make_shared<int>(1), other=std::make_shared<int>(1);
    int pso_builds=0;
    auto acquire=[&](std::shared_ptr<int> d,int variant){return live.get(
        [&](const Item& x){return x.device==d&&x.variant==variant;},
        [&]{++pso_builds;return std::make_shared<Item>(Item{d,variant});});};
    auto first=acquire(device,0), same=acquire(device,0);
    require(first==same&&pso_builds==1,"share on exact live device");
    auto second=acquire(other,0);require(second!=first&&pso_builds==2,"different device not aliased");
    auto variant=acquire(device,1);require(variant!=first&&pso_builds==3,"variant isolation at capacity");
    require(acquire(device,0)==first,"saturation did not evict live entry");
    std::weak_ptr<Item> old=first;first.reset();same.reset();
    require(old.expired(),"weak cache does not retain GPU bundle");
    auto replacement=acquire(device,0);require(pso_builds==4,"expired artifact rebuilt");
    second.reset();variant.reset();replacement.reset();
    std::weak_ptr<int> released_device=device;device.reset();
    require(released_device.expired(),"cache cannot pin retired device");
    bool failed=false;try{live.get([](const Item&){return false;},[]()->std::shared_ptr<Item>{throw std::runtime_error("PSO failed");});}
    catch(const std::runtime_error&){failed=true;}
    require(failed,"failed PSO not published");
    auto retry=acquire(other,1);require(retry->variant==1,"PSO retry after failure");
    for(int mask=0;mask<8;++mask)require(temporal_inplace_choice(mask&1,mask&2,mask&4)==(mask==7),"strict in-place admission");
    const auto max=UINT64_MAX;
    require(temporal_fits_increment(100,40,60),"exact remaining budget");
    require(!temporal_fits_increment(100,40,61),"peak over budget");
    require(!temporal_fits_increment(100,101,1),"no unsigned underflow");
    require(!temporal_fits_increment(0,0,1),"unknown budget refused");
    require(!temporal_fits_increment(100,1,0),"invalid allocation refused");
    require(temporal_fits_increment(max,max-1,1),"64-bit boundary");
    require(!temporal_fits_increment(max,max-1,2),"64-bit overflow refused");
    std::cout<<"PASS production portable cache/policy checks="<<checks<<"\n";
}
'''

def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def normalize(data: str) -> str:
    return ''.join(data.split())

def main() -> None:
    parser=argparse.ArgumentParser()
    parser.add_argument('--compiler',default=shutil.which('clang++') or shutil.which('g++'))
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--baseline-shader',type=Path)
    parser.add_argument('--sanitize',action='store_true')
    parser.add_argument('--negative-controls',action='store_true')
    args=parser.parse_args()
    if not args.compiler:raise RuntimeError('a native C++17 compiler is required')
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    root=Path(__file__).resolve().parent
    cpp=(root/'recorded_temporal.cpp').read_text(encoding='utf-8')
    shader=(root/'residual_accumulator_shader.hpp').read_text(encoding='utf-8')
    begin='// TEMPORAL_CACHE_PORTABLE_BEGIN';end='// TEMPORAL_CACHE_PORTABLE_END'
    assert cpp.count(begin)==cpp.count(end)==1
    snippet=cpp.split(begin)[1].split(end)[0]
    harness=out/'cache_test.cpp';harness.write_text(HEADER+snippet+HARNESS,encoding='utf-8')
    exe=out/'cache_test'
    command=[args.compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-pthread',str(harness),'-o',str(exe)]
    if args.sanitize:command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    build=subprocess.run(command,capture_output=True,text=True)
    (out/'compile.log').write_text(build.stdout+build.stderr,encoding='utf-8')
    report={'windows_compile_verified':False,'gpu_verified':False,'hlsl_compile_verified':False,
        'source_sha256':{n:digest((root/n).read_bytes()) for n in ['recorded_temporal.cpp','recorded_temporal.hpp','residual_accumulator_shader.hpp']},
        'compiler':args.compiler,'command':command,'compile_exit':build.returncode,'passed':False}
    def save(): (out/'validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    save();assert build.returncode==0,build.stderr
    run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
    (out/'run.log').write_text(run.stdout+run.stderr,encoding='utf-8');report['run_exit']=run.returncode;save()
    assert run.returncode==0,run.stdout+run.stderr
    if args.baseline_shader:
        baseline=args.baseline_shader.read_text(encoding='utf-8')
    else:
        repo=root.parents[1]
        baseline=subprocess.check_output(['git','-C',str(repo),'show',BASE+':experiments/final_residual/residual_accumulator_shader.hpp'],text=True)
    raw=baseline.encode();assert hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()=='800bce9295f22739759fd03ed1cc4ad1d4d44132'
    def preprocess(text,name,defines):
        hlsl=text.split('R"HLSL(')[1].split(')HLSL"')[0]
        file=out/(name+'.hlsl');file.write_text(hlsl,encoding='utf-8')
        pre=subprocess.run([args.compiler,'-E','-P','-x','c',*['-D'+d for d in defines],str(file)],capture_output=True,text=True)
        (out/(name+'.preprocess.log')).write_text(pre.stderr,encoding='utf-8');assert pre.returncode==0,pre.stderr
        return normalize(pre.stdout)
    for defines in [[],['RECORDING=1']]:
        suffix=str(len(defines))
        assert preprocess(baseline,'base'+suffix,defines)==preprocess(shader,'new'+suffix,defines)
    fast=preprocess(shader,'inplace',['RECORDING=1','INPLACE_FP16=1'])
    cs=fast.split('voidCS(uint3id:SV_DispatchThreadID)')[1]
    assert 'Output.Load(p+uint2(TargetX,TargetY)).rgb' in cs and 'Enhanced.Load' not in cs
    assert 'Output[p+uint2(TargetX,TargetY)]=result;' in cs
    # Structural obligations complement, never replace, the Windows GPU tests.
    assert 'srv(inplace?nullptr:in.candidate,in.candidate_format);' in cpp
    assert 'srv(inplace?nullptr:snapshot.Get(),fp);' in cpp
    assert 'if(in.candidate_state==ua)temporal_uav_barrier(l,in.candidate);' in cpp
    assert 'if(!inplace)state(l,p.snapshot.Get(),sr,cs);' in cpp
    assert cpp.index('if(!inplace&&!p.ensure_snapshot())')<cpp.index('p.plan=p.history.prepare(next_frame);')<cpp.index('p.amd->record(next_frame')
    assert 'l->CopyResource(p.originals[next].Get(),in.original);' in cpp
    assert 'if(!inplace_enabled)snapshot=texture();' in cpp
    assert 'const Id own=one*(scope==Scope::LaterLayers?6:5);' in cpp
    assert cpp.index('need(own<=budget')<cpp.index('pipelines=temporal_pipelines(d,')
    if args.negative_controls:
        report['negative_controls']=[]
        controls=[('duplicate-compile','if(!entries_[key]) {','if(true) {','one immutable build'),
                  ('wrong-device','if(match(*value))return value;','if(true){(void)match;return value;}','different device not aliased'),
                  ('missing-format-gate','return enabled&&exact_fp16&&typed_load;',
                   '(void)exact_fp16;(void)typed_load;return enabled;','strict in-place admission'),
                  ('unsigned-underflow','return budget&&extra&&used<=budget&&extra<=budget-used;',
                   'return extra<=budget-used;','no unsigned underflow')]
        baseline_file=out/'baseline_shader.hpp';baseline_file.write_text(baseline,encoding='utf-8')
        for name,before,after,oracle in controls:
            assert cpp.count(before)==1
            stage=out/('negative-'+name);stage.mkdir()
            for filename in ['recorded_temporal.cpp','recorded_temporal.hpp','residual_accumulator_shader.hpp','validate_memory_opt.py']:
                shutil.copyfile(root/filename,stage/filename)
            (stage/'recorded_temporal.cpp').write_text(cpp.replace(before,after),encoding='utf-8')
            child=subprocess.run([sys.executable,str(stage/'validate_memory_opt.py'),
                '--compiler',args.compiler,'--output',str(stage/'evidence'),
                '--baseline-shader',str(baseline_file)],capture_output=True,text=True,timeout=60)
            (stage/'runner.log').write_text(child.stdout+child.stderr,encoding='utf-8')
            evidence=json.loads((stage/'evidence/validation.json').read_text(encoding='utf-8'))
            log=(stage/'evidence/run.log').read_text(encoding='utf-8')
            detected=child.returncode!=0 and evidence['compile_exit']==0 and evidence['run_exit']!=0 and oracle in log
            report['negative_controls'].append({'name':name,'detected':detected,'oracle':oracle})
            save();assert detected,'negative control did not fail at expected oracle: '+name
    report.update(passed=True,portable_cache_run=run.stdout.strip(),legacy_shader_preprocessing_identical=True,
        source_wiring_checks=True,note='No Windows/FXC/WARP/NVIDIA execution. Fast path remains opt-in.')
    save();print(run.stdout.strip());print('PASS legacy shader preprocessing and source wiring; not HLSL compilation')

if __name__=='__main__':main()
