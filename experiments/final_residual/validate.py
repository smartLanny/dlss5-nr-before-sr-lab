"""Frozen CPU policy and negative controls; does not load an optical-flow SDK."""
import argparse, hashlib, json, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parent
def main():
    p=argparse.ArgumentParser();p.add_argument('--compiler',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=True)
    paths=[ROOT/'history_contract.hpp',ROOT/'history_contract_test.cpp',Path(__file__)]
    captured=[p.read_bytes() for p in paths]
    header,test=[b.decode('utf-8') for b in captured[:2]]
    mutations={
        'production':None,
        'ignore-reference':('!(flow.reference==plan_.reference)||',''),
        'ignore-flow-generation':('flow.history_generation!=plan_.history_generation||',''),
        'ignore-residual-generation':('||residual_history_generation!=plan_.history_generation','||residual_history_generation==0'),
        'allow-pending-cancel':('if(!owns(token)||flow_started_)return false;','if(!owns(token)||flow_ready_)return false;'),
    }
    expected_failure={
        'ignore-reference':'wrong frame/reference/backend/OF epoch cannot accumulate',
        'ignore-flow-generation':'old OF generation cannot satisfy new residual reset',
        'ignore-residual-generation':'residual epoch mismatch cannot commit even after submit',
        'allow-pending-cancel':'pending OF cannot cancel as unrecorded work',
    }
    results=[]
    for name,mutation in mutations.items():
        work=a.output/name;work.mkdir(exist_ok=True)
        text=header
        if mutation:
            before,after=mutation
            if text.count(before)!=1:raise RuntimeError('mutation anchor changed: '+name)
            text=text.replace(before,after)
        (work/'history_contract.hpp').write_text(text,encoding='utf-8')
        cpp=work/'test.cpp';cpp.write_text(test,encoding='utf-8');exe=work/'test.exe'
        command=[a.compiler,'-std=c++17','-O0','-Wall','-Wextra','-Werror','-static',str(cpp),'-o',str(exe)]
        build=subprocess.run(command,capture_output=True,text=True)
        (work/'build.log').write_text(build.stdout+build.stderr,encoding='utf-8')
        if build.returncode:raise RuntimeError(build.stderr)
        run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=15)
        (work/'run.log').write_text(run.stdout+run.stderr,encoding='utf-8')
        passed=run.returncode==0 if not mutation else run.returncode==1 and expected_failure[name] in run.stderr
        results.append({'variant':name,'passed':passed,'exit_code':run.returncode,'command':command,
            'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()})
        print(name+': '+(run.stdout+run.stderr).strip())
        if not passed:raise RuntimeError('unexpected outcome: '+name)
    unchanged=all(p.read_bytes()==b for p,b in zip(paths,captured))
    report={'passed':unchanged,'scope':'project-owned CPU source/OF/residual transaction contract only; no SDK or GPU execution',
        'files':{p.name:hashlib.sha256(b).hexdigest() for p,b in zip(paths,captured)},'unchanged':unchanged,'results':results}
    (a.output/'validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    if not unchanged:raise RuntimeError('source changed during validation')
if __name__=='__main__':main()
