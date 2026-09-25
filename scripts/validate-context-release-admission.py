"""Production release-admission interaction against a frozen old-Core negative control."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import importlib.util
import os

ROOT=Path(__file__).resolve().parents[1]
BASELINE_COMMIT='cace3e645b4ca76b42d8fe86e58266b3dd95b3c0'
# Already contains release-before-create for ready contexts, but admits a new
# allocation while same-carrier retired contexts remain blocked by their fences.
INFLIGHT_BASELINE_COMMIT='3e94329b18d7ab1da16c46fd62011ccdc04e8ddc'
spec=importlib.util.spec_from_file_location('retirement',ROOT/'scripts/validate-aux-parent-retirement.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
definition=module.definition


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--compiler',required=True)
    p.add_argument('--baseline-core',type=Path,
        help='Optional frozen Core directory; otherwise read the fixed old commit from Git')
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--sanitize',action='store_true',help='Address/undefined behavior checks in the CPU harness')
    a=p.parse_args();out=a.output.resolve()
    if out.exists():p.error('Preserve receipts; use a new output directory')
    out.mkdir(parents=True)
    route_bytes=(ROOT/'src/nr_processing_runtime.inl').read_bytes()
    route=route_bytes.decode('utf-8-sig')
    report=dict(scope='Real production functions; CPU OS/NGX services, not a GPU or game result',runs=[],
        route_source_sha256=hashlib.sha256(route_bytes).hexdigest(),
        fixture_sha256=hashlib.sha256((ROOT/'tests/context_release_admission.cpp').read_bytes()).hexdigest())
    try:
        if a.baseline_core:
            baseline_path=a.baseline_core.resolve(strict=True)/'src/nr_before_sr.cpp'
            baseline_bytes=baseline_path.read_bytes()
            baseline_origin=dict(path=str(baseline_path),kind='frozen-core')
        else:
            baseline_bytes=subprocess.check_output(
                ['git','show',BASELINE_COMMIT+':src/nr_before_sr.cpp'],cwd=ROOT)
            baseline_origin=dict(commit=BASELINE_COMMIT,path='src/nr_before_sr.cpp',kind='git-object')
        candidate_path=ROOT/'src/nr_before_sr.cpp'
        inflight_bytes=subprocess.check_output(
            ['git','show',INFLIGHT_BASELINE_COMMIT+':src/nr_before_sr.cpp'],cwd=ROOT)
        sources=[('baseline',baseline_bytes,baseline_origin),
            ('inflight-baseline',inflight_bytes,dict(commit=INFLIGHT_BASELINE_COMMIT,path='src/nr_before_sr.cpp',kind='git-object')),
            ('candidate',candidate_path.read_bytes(),dict(path=str(candidate_path),kind='working-tree'))]
        for name,source_bytes,origin in sources:
            source=source_bytes.decode('utf-8-sig');folder=out/name;folder.mkdir()
            report[name]=dict(**origin,sha256=hashlib.sha256(source_bytes).hexdigest())
            tuning=''.join(definition(source,m,semi) for m,semi in [('struct NrTuningSignature',True),('NrTuningSignature CurrentNrTuningSignature(',False),('bool SameNrTuningSignature(',False)])
            (folder/'tuning.inl').write_text(tuning,encoding='utf-8')
            predicates=''.join(definition(source,m) for m in ['bool IsSlotFenceCompleteLocked(','bool IsContextFenceCompleteLocked(','bool CanMutateSlotLocked(','bool CanDestroyContextLocked('])
            (folder/'lifecycle.inl').write_text(predicates,encoding='utf-8')
            collector=''.join(definition(source,m) for m in ['void RetireAuxiliaryDependenciesLocked(','void CollectRetiredContextsLocked(','void DrainPendingContextDestructions('])
            if name!='baseline':collector+=definition(source,'bool DeferNrCreationForReleaseLocked(')
            (folder/'collector.inl').write_text(collector,encoding='utf-8')
            (folder/'lookup.inl').write_text(definition(source,'NrContext *FindOrCreateContext('),encoding='utf-8')
            (folder/'route.inl').write_text(''.join(definition(route,m) for m in ['void BlockSource(','bool Recorded(','void Reject(']),encoding='utf-8')
            exe=folder/'interaction.exe'
            flags=['-std=c++20','-O1','-Wall','-Wextra','-Werror']
            if os.name=='nt' and not a.sanitize:flags+=['-static']
            if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
            commands=[([a.compiler,*flags,'-I'+str(ROOT/'src'),'-I'+str(folder),str(ROOT/'tests/context_release_admission.cpp'),'-o',str(exe)],0),([str(exe)],0 if name=='candidate' else 3)]
            for index,(cmd,expected) in enumerate(commands):
                r=subprocess.run(cmd,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90)
                (out/f'{name}-{index}.log').write_text(r.stdout+r.stderr,encoding='utf-8')
                report['runs'].append(dict(name=name,index=index,command=cmd,exit_code=r.returncode,expected=expected))
                print(name,index,r.returncode,r.stdout,flush=True)
                if r.returncode!=expected:raise RuntimeError(r.stdout+r.stderr)
                if name=='baseline' and index==1 and 'REPRODUCED: ready 3840' not in r.stdout:raise RuntimeError('Wrong ready-retirement negative-control failure')
                if name=='inflight-baseline' and index==1 and 'REPRODUCED: in-flight retired model' not in r.stdout:raise RuntimeError('Wrong in-flight negative-control failure')
        report['status']='passed'
    except Exception as e:report.update(status='failed',error=str(e));raise
    finally:(out/'validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')


if __name__=='__main__':main()
