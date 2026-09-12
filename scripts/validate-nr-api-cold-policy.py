"""Compile actual production EnsureNrApi with observable OS/SDK boundaries."""
import argparse
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--compiler',required=True);a=p.parse_args()
    core=(ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8')
    production=core[core.index('struct NrApi {'):core.index('\nstruct Codec {')]
    production=production.replace('#include "nr_runtime_load_evidence.inl"',(ROOT/'src/nr_runtime_load_evidence.inl').read_text(encoding='utf-8'))
    fixture=(ROOT/'tests/nr_api_cold_policy.cpp.in').read_text(encoding='utf-8')
    controls=[('positive',production),('missing-cold-bootstrap',production.replace('if (g_nr_api.bootstrap_required && evidence.loaded[0] && evidence.loaded[1])','if (false && g_nr_api.bootstrap_required && evidence.loaded[0] && evidence.loaded[1])')),
        ('permanent-failure',production.replace('g_nr_api.bootstrap_retry_after = GetTickCount64() + 5000;','g_nr_api.failed = true; g_nr_api.bootstrap_retry_after = GetTickCount64() + 5000;'))]
    with tempfile.TemporaryDirectory(prefix='nr-cold-policy-') as temp:
        directory=Path(temp)
        for name,body in controls:
            source=directory/(name+'.cpp');exe=directory/(name+'.exe');source.write_text(fixture.replace('// @PRODUCTION@',body),encoding='utf-8')
            subprocess.run([a.compiler,'-std=c++20','-O2','-static','-Wall','-Wextra',str(source),'-o',str(exe)],check=True)
            r=subprocess.run([str(exe)],capture_output=True,text=True)
            if name=='positive':
                print(r.stdout,end='')
                if r.returncode:raise RuntimeError(r.stderr)
            elif r.returncode!=1 or 'COLD_POLICY_ORACLE' not in r.stderr:raise RuntimeError('Negative control did not reject '+name)
            else:print('PASS compiled negative control: '+name)
if __name__=='__main__':main()
