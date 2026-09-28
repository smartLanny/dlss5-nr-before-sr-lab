"""Actual global compaction and retired-closure gates, with inherited CPU negatives."""
from __future__ import annotations
import argparse, hashlib, json, subprocess
from pathlib import Path
from importlib.util import spec_from_file_location, module_from_spec
ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    spec=spec_from_file_location('retirement',ROOT/'scripts/validate-aux-parent-retirement.py');module=module_from_spec(spec);spec.loader.exec_module(module);definition=module.definition
    source=(ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
    report={'schema':'nr-global-compaction-cpu-v1','status':'running','source_sha256':hashlib.sha256(source.encode()).hexdigest(),'game':False,'gpu':False,'runs':[]}
    def run(name,command,expected=0):
        r=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,timeout=120);data=r.stdout+r.stderr;(out/(name+'.log')).write_bytes(data)
        report['runs'].append({'name':name,'command':list(map(str,command)),'exit':r.returncode,'expected_exit':expected,'log_sha256':hashlib.sha256(data).hexdigest()})
        print(name,r.returncode,data.decode('utf-8','replace')[-3500:],flush=True)
        if r.returncode!=expected:raise RuntimeError(name+' unexpected result')
    try:
        for name in ('candidate','cross-carrier-negative'):
            folder=out/name;folder.mkdir()
            (folder/'lifecycle.inl').write_text(''.join(definition(source,x) for x in ['bool IsSlotFenceCompleteLocked(','bool IsContextFenceCompleteLocked(','bool CanMutateSlotLocked(','bool CanDestroyContextLocked(']),encoding='utf-8')
            (folder/'collector.inl').write_text(''.join(definition(source,x) for x in ['void RetireAuxiliaryDependenciesLocked(','void CollectRetiredContextsLocked(']),encoding='utf-8')
            (folder/'snapshot.inl').write_text(definition(source,'struct NrContextMemorySnapshot {',True)+definition(source,'NrContextMemorySnapshot SnapshotNrContextMemory('),encoding='utf-8')
            compaction=definition(source,'void ReconcileNrResidencyLocked(')+definition(source,'bool DeferNrCreationForReleaseLocked(')
            if name.endswith('negative'):
                if compaction.count('&&!residency_drain')!=1:raise ValueError('Review global barrier mutation')
                compaction=compaction.replace('&&!residency_drain','')
            (folder/'compaction.inl').write_text(compaction,encoding='utf-8')
            wrapper=folder/'drain.cpp'
            wrapper.write_text('#define main OriginalParentMain\n#include "'+(ROOT/'tests/aux_parent_retirement.cpp').as_posix()+'"\n#undef main\n#include "'+(ROOT/'tests/residency_drain_cpu.inl').as_posix()+'"\n',encoding='utf-8')
            exe=folder/'drain.exe'
            run(name+'-compile',[a.compiler,'-std=c++20','-O2','-Wall','-Wextra','-Werror','-static','-DBASELINE=0','-I'+str(folder),'-I'+str(ROOT/'src'),wrapper,'-o',exe])
            run(name+'-run',[exe],1 if name.endswith('negative') else 0)
        report['status']='passed'
    except Exception as e:report.update(status='failed',error=str(e));raise
    finally:(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':main()
