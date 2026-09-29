"""Compile the production policy, optionally the complete generated Core controller."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--compiler',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--source',type=Path);p.add_argument('--reshade',type=Path)
    p.add_argument('--codec-cache',type=Path,help='Exact generated-Core source-keyed codec artifacts.')
    a=p.parse_args()
    if a.output.exists():raise SystemExit('Use a fresh evidence directory.')
    a.output.mkdir(parents=True);exe=a.output/'policy.exe'
    commands=[[str(a.compiler),'-std=c++20','-O0','-Wall','-Wextra','-Werror','-static','-I'+str(ROOT/'src'),str(ROOT/'tests/acceptance_policy_test.cpp'),'-o',str(exe)],[str(exe)]]
    if a.source:
        if not a.reshade:p.error('--source requires --reshade')
        if not a.codec_cache:p.error('--source requires --codec-cache from the same generated Core')
        shared=[sys.executable,str(ROOT/'scripts/validate-native-color-view.py'),'--acceptance','--source',str(a.source),'--compiler',str(a.compiler),'--reshade',str(a.reshade)]
        if a.codec_cache:shared.extend(['--codec-cache',str(a.codec_cache)])
        commands.append([*shared,'--output',str(a.output/'core')])
        commands.append([*shared,'--acceptance-timing-negative','--output',str(a.output/'rr-timing-negative')])
    records=[]
    for i,cmd in enumerate(commands):
        r=subprocess.run(cmd,text=True,capture_output=True,encoding='utf-8',errors='replace');log=r.stdout+r.stderr;(a.output/f'{i}.txt').write_text(log,encoding='utf-8');print(log[-8000:]);records.append({'command':cmd,'exit':r.returncode})
        if r.returncode:break
    result={'passed':r.returncode==0,'commands':records,'complete_core_requested':bool(a.source),'no_game_claim':True,'policy_sha256':hashlib.sha256((ROOT/'src/nr_acceptance_policy.h').read_bytes()).hexdigest()}
    (a.output/'validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');raise SystemExit(r.returncode)
if __name__=='__main__':main()
