#!/usr/bin/env python3
"""Test exact production mutation and readers; Windows uses real SRW primitives.
D3D12/COM objects are test doubles. This is not GPU or game validation.
"""
from __future__ import annotations
import argparse,hashlib,json,os,re,shutil,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE='cbfb33919696a2840ed5154c91abaea56228b661'
EXPECTED={'sync': 'cfe01ba6a28d6751c6a62c6c3f039e59d66955d5739e2c150f1f0ba858de59b4', 'mutation': 'bc8b7d75651b05a767fc13e663b706b548153357193ea34e883e6875a43caec4', 'readers': '16bfdcb08fd26d68450c9f064e9160446273b39cea13934916a92a2238799a82'}
BASELINE='template <typename Mutation> void MutateNativeCommandState(ID3D12GraphicsCommandList *native, Mutation mutation) {\n    // No COM/private-data access on every setter. Unrelated lists cache a no-op.\n    thread_local ID3D12GraphicsCommandList *last_native = nullptr;\n    thread_local unsigned long long last_epoch = 0;\n    thread_local CommandStateRecord *last_record = nullptr;\n    const auto epoch = g_command_state_epoch.load(std::memory_order_acquire);\n    if (last_native != native || last_epoch != epoch) {\n        AcquireSRWLockShared(&g_command_state_lock);\n        last_record = nullptr;\n        for (auto &record : g_command_states)\n            if (record.native == native && record.state != nullptr) { last_record = &record; break; }\n        last_native = native;\n        last_epoch = epoch;\n        ReleaseSRWLockShared(&g_command_state_lock);\n    }\n    if (last_record == nullptr) return;\n    AcquireSRWLockExclusive(&g_command_state_lock);\n    if (last_record->native == native && last_record->state != nullptr && last_record->hooks_ready)\n        mutation(*last_record->state);\n    ReleaseSRWLockExclusive(&g_command_state_lock);\n}\n'

def extract(source: str, signature: str) -> str:
    if source.count(signature)!=1:raise ValueError('Changed function boundary: '+signature)
    start=source.index(signature);i=source.index('{',start);depth=0
    while i<len(source):
        if source.startswith('//',i):
            end=source.find('\n',i);i=len(source) if end<0 else end+1;continue
        if source.startswith('/*',i):
            end=source.find('*/',i+2)
            if end<0:raise ValueError('Unterminated comment')
            i=end+2;continue
        if source[i] in "\"'":
            quote=source[i];i+=1
            while i<len(source):
                if source[i]=='\\':i+=2;continue
                if source[i]==quote:i+=1;break
                i+=1
            continue
        if source[i]=='{':depth+=1
        if source[i]=='}':
            depth-=1
            if depth==0:return source[start:i+1]+'\n'
        i+=1
    raise ValueError('Unterminated C++ function')

def source_parts(root: Path):
    a=(root/'src/nr_command_state.inl').read_text(encoding='utf-8-sig')
    b=(root/'src/nr_descriptor_provenance_commands.inl').read_text(encoding='utf-8-sig')
    begin='// BEGIN COMMAND_STATE_MUTATION_SYNCHRONIZATION';end='// END COMMAND_STATE_MUTATION_SYNCHRONIZATION'
    if a.count(begin)!=1 or a.count(end)!=1:raise ValueError('Candidate synchronization block missing/ambiguous')
    parts={'sync':a[a.index(begin):a.index(end)+len(end)]+'\n',
       'mutation':extract(a,'template <typename Mutation> void MutateNativeCommandState('),
       'readers':extract(b,'std::uint64_t NrDescriptorBoundHeap(')+extract(b,'bool NrDescriptorBoundGpuHeapMatches(')}
    for key,value in parts.items():
        if hashlib.sha256(value.encode()).hexdigest()!=EXPECTED[key]:
            raise ValueError('Reviewed production excerpt changed: '+key)
    return parts

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,default=ROOT);p.add_argument('--compiler',default='g++')
    p.add_argument('--sanitize',action='store_true');p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();root=args.root.resolve();out=args.output.resolve()
    if out.exists():raise ValueError('Output already exists')
    parts=source_parts(root)
    compiler=shutil.which(args.compiler) or (str(Path(args.compiler).resolve()) if Path(args.compiler).is_file() else None)
    if not compiler:raise ValueError('Compiler unavailable: '+args.compiler)
    test=root/'tests/command_state_lockperf.cpp'
    if not test.is_file():raise ValueError('Missing checked-in harness')
    out.mkdir(parents=True)
    result={'candidate':'D21-lockperf1','base':BASE,'native_srw':os.name=='nt','objects':'test doubles',
        'whole_core_build':False,'gpu_validation':False,'game_validation':False,
        'source_sha256':{name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in
            ('src/nr_command_state.inl','src/nr_descriptor_provenance_commands.inl','tests/command_state_lockperf.cpp')},'runs':[]}
    def run(cmd,name,expected=0):
        process=subprocess.run([str(x) for x in cmd],capture_output=True,text=True,timeout=120)
        (out/(name+'.txt')).write_text('$ '+' '.join(str(x) for x in cmd)+'\n'+process.stdout+process.stderr,encoding='utf-8')
        result['runs'].append({'name':name,'exit':process.returncode,'stdout':process.stdout.strip()})
        if process.returncode!=expected:raise RuntimeError(name+' failed: '+process.stdout+process.stderr)
    try:
        run([compiler,'--version'],'compiler')
        flags=['-std=c++20','-O1','-g','-pthread']
        if os.name=='nt':flags+=['-static']
        if args.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        with tempfile.TemporaryDirectory(prefix='nr-command-state-lockperf-') as td:
            td=Path(td);suffix='.exe' if os.name=='nt' else ''
            (td/'production_mutation.inl').write_text(BASELINE,encoding='utf-8')
            baseline=td/('baseline'+suffix)
            run([compiler,*flags,'-DBASELINE_MUTATION','-I'+str(td),test,'-o',baseline],'baseline-build')
            run([baseline,'--contention-only'],'baseline-red',3)
            (td/'production_mutation.inl').write_text(parts['sync']+parts['mutation'],encoding='utf-8')
            (td/'production_readers.inl').write_text(parts['readers'],encoding='utf-8')
            candidate=td/('candidate'+suffix)
            run([compiler,*flags,'-I'+str(td),test,'-o',candidate],'candidate-build')
            run([candidate],'candidate-green')
        result['status']='passed';result['scenarios']=27
    except Exception as error:
        result['status']='failed';result['error']=str(error);raise
    finally:
        (out/'validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False,indent=2))
if __name__=='__main__':
    try:main()
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError) as error:raise SystemExit(str(error))
