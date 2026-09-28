"""Reproduce empty-before-idle pool growth with the actual production selector.

CPU resource services are doubles; this is not a VRAM or NGX performance claim.
The unsafe-lifetime mutation must fail rather than silently bypass coverage.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
BASE='b2990b120337cdd8026f83a1bd369f3708b78fbe'

def definition(text: str, marker: str) -> str:
    start=text.index(marker)
    while True:
        opening=text.index('{',start)
        if ';' not in text[start:opening]:break
        start=text.index(marker,start+len(marker))
    end=opening+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]+'\n'

def main() -> None:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--source',type=Path,default=ROOT/'src/nr_before_sr.cpp')
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    current=a.source.read_text(encoding='utf-8-sig')
    baseline=subprocess.check_output(['git','show',BASE+':src/nr_before_sr.cpp'],cwd=ROOT).decode('utf-8-sig')
    digest=lambda b:hashlib.sha256(b).hexdigest()
    report={'schema':'nr-residency-cpu-v1','baseline_commit':BASE,'source':str(a.source.resolve()),
            'source_sha256':digest(a.source.read_bytes()),'game_verified':False,'gpu_executed':False,'runs':[],'status':'running'}
    def run(name,command,expected=0):
        r=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,timeout=120)
        log=r.stdout+r.stderr;(out/(name+'.log')).write_bytes(log)
        report['runs'].append({'name':name,'command':list(map(str,command)),'exit':r.returncode,'expected_exit':expected,'log_sha256':digest(log)})
        print(name,r.returncode,log.decode('utf-8','replace')[-2200:],flush=True)
        if r.returncode!=expected:raise RuntimeError(name+' unexpected result')
    try:
        reload=definition(current,'void ReloadConfig(')
        apply=definition(current,'bool ApplyConfig(')
        call='ReconcileNrResidencyLocked(nr_layers::Active(g_config.nr_layers), nr_layers::Active(next.nr_layers),'
        if reload.count(call)!=1 or apply.count(call)!=1:
            raise AssertionError('Both INI reload and committed configuration must reconcile cancelled layers')
        if 'if (persist_to_ini)\n        '+call not in apply:
            raise AssertionError('Temporary F8 configuration must not eagerly evict the warm cache')
        for name,text in [('baseline',baseline),('candidate',current),('unsafe-fence-negative',current)]:
            folder=out/name;folder.mkdir()
            lifetime=definition(text,'bool IsSlotFenceCompleteLocked(')+definition(text,'bool CanMutateSlotLocked(')
            if name=='unsafe-fence-negative':
                lifetime=definition(text,'bool IsSlotFenceCompleteLocked(')+'bool CanMutateSlotLocked(const NrSlot& s) {(void)s;return true;}\n'
            (folder/'residency_lifetime.inl').write_text(lifetime,encoding='utf-8')
            (folder/'residency_selection.inl').write_text(definition(text,'NrSlot *FindOrCreateSlot('),encoding='utf-8')
            exe=folder/'residency.exe'
            run(name+'-compile',[a.compiler,'-std=c++20','-O2','-Wall','-Wextra','-Werror','-static',
                '-DBASELINE='+str(int(name=='baseline')),'-I'+str(folder),'-I'+str(ROOT/'src'),ROOT/'tests/residency_selection_cpu.cpp','-o',exe])
            run(name+'-run',[exe],3 if name=='unsafe-fence-negative' else 0)
        report['status']='passed'
    except Exception as e:
        report.update(status='failed',error=str(e));raise
    finally:
        (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':main()
