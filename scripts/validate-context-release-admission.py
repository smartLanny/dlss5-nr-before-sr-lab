"""Production release-admission interaction against a frozen old-Core negative control."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import importlib.util

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('retirement',ROOT/'scripts/validate-aux-parent-retirement.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
definition=module.definition


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--compiler',required=True)
    p.add_argument('--baseline-core',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();out=a.output.resolve()
    if out.exists():p.error('Preserve receipts; use a new output directory')
    out.mkdir(parents=True)
    route_bytes=(ROOT/'src/nr_processing_runtime.inl').read_bytes()
    route=route_bytes.decode('utf-8-sig')
    files=[a.baseline_core/'src/nr_before_sr.cpp',ROOT/'src/nr_before_sr.cpp']
    report=dict(scope='Real production functions; CPU OS/NGX services, not a GPU or game result',runs=[],
        route_source_sha256=hashlib.sha256(route_bytes).hexdigest(),
        fixture_sha256=hashlib.sha256((ROOT/'tests/context_release_admission.cpp').read_bytes()).hexdigest())
    try:
        for name,path in zip(['baseline','candidate'],files):
            source_bytes=path.read_bytes();source=source_bytes.decode('utf-8-sig');folder=out/name;folder.mkdir()
            report[name]=dict(path=str(path),sha256=hashlib.sha256(source_bytes).hexdigest())
            tuning=''.join(definition(source,m,semi) for m,semi in [('struct NrTuningSignature',True),('NrTuningSignature CurrentNrTuningSignature(',False),('bool SameNrTuningSignature(',False)])
            (folder/'tuning.inl').write_text(tuning,encoding='utf-8')
            predicates=''.join(definition(source,m) for m in ['bool IsSlotFenceCompleteLocked(','bool IsContextFenceCompleteLocked(','bool CanMutateSlotLocked(','bool CanDestroyContextLocked('])
            (folder/'lifecycle.inl').write_text(predicates,encoding='utf-8')
            collector=''.join(definition(source,m) for m in ['void RetireAuxiliaryDependenciesLocked(','void CollectRetiredContextsLocked(','void DrainPendingContextDestructions('])
            if name=='candidate':collector+=definition(source,'bool DeferNrCreationForReleaseLocked(')
            (folder/'collector.inl').write_text(collector,encoding='utf-8')
            (folder/'lookup.inl').write_text(definition(source,'NrContext *FindOrCreateContext('),encoding='utf-8')
            (folder/'route.inl').write_text(''.join(definition(route,m) for m in ['void BlockSource(','bool Recorded(','void Reject(']),encoding='utf-8')
            exe=folder/'interaction.exe'
            commands=[([a.compiler,'-std=c++20','-O1','-static','-Wall','-Wextra','-Werror','-I'+str(ROOT/'src'),'-I'+str(folder),str(ROOT/'tests/context_release_admission.cpp'),'-o',str(exe)],0),([str(exe)],3 if name=='baseline' else 0)]
            for index,(cmd,expected) in enumerate(commands):
                r=subprocess.run(cmd,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90)
                (out/f'{name}-{index}.log').write_text(r.stdout+r.stderr,encoding='utf-8')
                report['runs'].append(dict(name=name,index=index,command=cmd,exit_code=r.returncode,expected=expected))
                print(name,index,r.returncode,r.stdout,flush=True)
                if r.returncode!=expected:raise RuntimeError(r.stderr)
                if name=='baseline' and index==1 and 'REPRODUCED: ready 3840' not in r.stdout:raise RuntimeError('Wrong negative-control failure')
        report['status']='passed'
    except Exception as e:report.update(status='failed',error=str(e));raise
    finally:(out/'validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')


if __name__=='__main__':main()
