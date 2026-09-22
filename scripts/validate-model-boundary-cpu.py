"""Bounded actual Core tuning/recreation checks; no runtime DLL sweep or GPU."""
import argparse,hashlib,importlib.util,json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 spec=importlib.util.spec_from_file_location('release',ROOT/'scripts/validate-context-release-admission.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
 source=ROOT/'src/nr_before_sr.cpp';s=source.read_text(encoding='utf-8');route=ROOT/'src/nr_processing_runtime.inl';r=route.read_text(encoding='utf-8')
 groups={'tuning.inl':[("struct NrTuningSignature",True),("NrTuningSignature CurrentNrTuningSignature(",False),("bool SameNrTuningSignature(",False)],
 'lifecycle.inl':[(n,False) for n in ('bool IsSlotFenceCompleteLocked(','bool IsContextFenceCompleteLocked(','bool CanMutateSlotLocked(','bool CanDestroyContextLocked(')],
 'collector.inl':[(n,False) for n in ('void RetireAuxiliaryDependenciesLocked(','void CollectRetiredContextsLocked(','void DrainPendingContextDestructions(','bool DeferNrCreationForReleaseLocked(')],
 'lookup.inl':[("NrContext *FindOrCreateContext(",False)]}
 for name,items in groups.items():(out/name).write_text(''.join(m.definition(s,n,semi) for n,semi in items),encoding='utf-8')
 (out/'route.inl').write_text(''.join(m.definition(r,n) for n in ('void BlockSource(','bool Recorded(','void Reject(')),encoding='utf-8')
 shared=ROOT/'tests/context_release_admission.cpp';fixture=ROOT/'tests/model_boundary_cpu.inl';text=shared.read_text(encoding='utf-8')
 if text.count('int main(){')!=1:raise ValueError('Shared service fixture boundary changed')
 cpp=out/'model-boundary.cpp';cpp.write_text(text.split('int main(){')[0]+fixture.read_text(encoding='utf-8'),encoding='utf-8')
 exe=out/'model-boundary.exe';cmd=[a.compiler,'-std=c++20','-O1','-static','-Wall','-Wextra','-Werror','-I'+str(ROOT/'src'),'-I'+str(out),str(cpp),'-o',str(exe)]
 record={'passed':False,'cpu_only':True,'opaque_model_executed':False,'output_equivalence_verified':False,'source_sha256':sha(source),'route_sha256':sha(route),'shared_fixture_sha256':sha(shared),'fixture_sha256':sha(fixture),'commands':[]}
 for name,c in [('compile',cmd),('run',[str(exe)])]:
  run=subprocess.run(c,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90);(out/(name+'.txt')).write_text(run.stdout+run.stderr,encoding='utf-8')
  record['commands'].append({'command':c,'exit':run.returncode});(out/'result.json').write_text(json.dumps(record,indent=2),encoding='utf-8');print(run.stdout+run.stderr)
  if run.returncode:raise SystemExit(run.returncode)
 record['passed']=True;record['binary_sha256']=sha(exe);(out/'result.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
if __name__=='__main__':main()
