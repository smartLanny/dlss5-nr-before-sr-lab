"""Exercise the actual Core Record wrapper with a bounded allocating module fake.

This is a CPU accounting regression, not GPU allocation or game OOM validation.
The wrapper and status atomics are taken verbatim from production source.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

HARNESS = r'''
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <initializer_list>
#include "nr_temporal_status.h"
using Id=unsigned long long;
struct ID3D12GraphicsCommandList{};
struct NrSlot{unsigned original_state=0,source_state=0,result_state=0;};
struct NrContext{Id temporal_token=0;bool lifecycle_dependencies_quarantined=false;};
enum class RecordResult{Disabled,Unavailable,Duplicate,Busy,Recorded,Faulted};
struct Stats{Id bytes=0;bool snapshot_refused=false;};
struct Inputs{unsigned original_state=0,candidate_state=0;};
struct Runtime{
 Stats values{};Id growth=0;RecordResult result=RecordResult::Recorded;
 Stats stats()const{return values;}
 RecordResult record(ID3D12GraphicsCommandList*,const Inputs&){values.bytes+=growth;growth=0;return result;}
 const char* status()const{return "test-only module result";}
};
struct Prepared{Runtime* runtime=nullptr;Inputs inputs{};Id recording=3;explicit operator bool()const{return runtime!=nullptr;}};
void Status(unsigned s){nr_temporal_status::state.store(s);}
void MarkLifecycleUncertain(const char*){}
void Log(const char*,...){}
void check(bool condition,const char* reason){if(!condition)throw std::runtime_error(reason);}
'''
TAIL = r'''
int main(){
 constexpr Id plane=3840ull*2160*8, initial=plane*4, unrelated=17;
 for(auto result:{RecordResult::Recorded,RecordResult::Unavailable,RecordResult::Faulted}){
  nr_temporal_status::bytes=0;nr_temporal_status::peak=0;
  nr_temporal_status::Allocated(unrelated);nr_temporal_status::Allocated(initial);
  Runtime runtime{{initial,false},plane,result};Prepared p{&runtime};NrSlot slot;NrContext context;
  (void)Record(nullptr,context,slot,p);
  check(nr_temporal_status::bytes==initial+plane+unrelated,"lazy snapshot growth missing from Core bytes");
  check(nr_temporal_status::peak==initial+plane+unrelated,"lazy allocation missing from peak");
  runtime.result=RecordResult::Recorded;
  for(unsigned i=0;i<100;++i)(void)Record(nullptr,context,slot,p);
  check(nr_temporal_status::bytes==initial+plane+unrelated,"same snapshot double counted");
  nr_temporal_status::bytes.fetch_sub(runtime.stats().bytes);
  check(nr_temporal_status::bytes==unrelated,"retirement underflow or another owner lost");
 }
 for(auto result:{RecordResult::Unavailable,RecordResult::Duplicate,RecordResult::Busy}){
  nr_temporal_status::bytes=initial;Runtime runtime{{initial,false},0,result};Prepared p{&runtime};NrSlot slot;NrContext context;
  (void)Record(nullptr,context,slot,p);
  check(nr_temporal_status::bytes==initial,"allocation refusal changed account");
 }
 nr_temporal_status::bytes=initial;Runtime refused{{initial,true},0,RecordResult::Unavailable};
 Prepared refusal{&refused};NrSlot refusal_slot;NrContext refusal_context;
 check(Record(nullptr,refusal_context,refusal_slot,refusal),"allocation refusal lost current candidate");
 check(nr_temporal_status::state==20&&nr_temporal_status::bytes==initial,"allocation refusal missing specific F8 state");
 nr_temporal_status::bytes=0;Prepared none;NrSlot slot;NrContext context;
 check(Record(nullptr,context,slot,none)&&nr_temporal_status::bytes==0,"disabled wrapper allocates");
 std::puts("PASS Core Record growth/peak/retirement; 3 allocating outcomes, 100 repeated frames each, 3 no-allocation outcomes, disabled");
}
'''

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--compiler',required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--sanitize',action='store_true')
    a=ap.parse_args();root=Path(__file__).resolve().parents[2]
    source=root/'src/nr_temporal_runtime.inl'
    text=source.read_text(encoding='utf-8');start=text.index('bool Record(')
    opening=text.index('{',start);depth=1;end=opening+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    body=text[start:end]
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    test=out/'core-accounting.cpp';test.write_text(HARNESS+body+TAIL,encoding='utf-8')
    exe=out/'core-accounting'
    cmd=[a.compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-I',str(root/'src'),str(test),'-o',str(exe)]
    if a.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    report={'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'command':cmd,'passed':False,'gpu_verified':False}
    def save():(out/'validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    build=subprocess.run(cmd,capture_output=True,text=True);report['compile_exit']=build.returncode
    (out/'compile.log').write_text(build.stdout+build.stderr,encoding='utf-8');save()
    if build.returncode:raise RuntimeError(build.stderr)
    run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);report['run_exit']=run.returncode
    (out/'run.log').write_text(run.stdout+run.stderr,encoding='utf-8');report['passed']=run.returncode==0;save()
    if run.returncode:raise RuntimeError(run.stdout+run.stderr)
    print(run.stdout.strip())

if __name__=='__main__':main()
