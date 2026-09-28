"""Validate bounded PSO file handling and actual cached compute output on Windows."""
from __future__ import annotations
import argparse, hashlib, json, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--hardware',action='store_true',help='Also run on a separately available NVIDIA GPU')
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
    report={'schema':'pipeline-cache-validation-v1','status':'running','header_sha256':sha(ROOT/'src/nr_pipeline_cache.h'),
            'fixture_sha256':sha(ROOT/'tests/pipeline_cache_windows.cpp'),'game':False,'ngx':False,'runs':[]}
    def run(name,command,expected=0):
        try:
            r=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,timeout=120)
            code=r.returncode;data=r.stdout+r.stderr
        except subprocess.TimeoutExpired as error:
            code=124;data=(error.stdout or b'')+(error.stderr or b'')+b'\nTIMEOUT; not passed\n'
        log=out/(name+'.log');log.write_bytes(data)
        report['runs'].append({'name':name,'command':list(map(str,command)),'exit':code,'expected_exit':expected,'log_sha256':sha(log)})
        print(name,code,data.decode('utf-8','replace')[-3500:],flush=True)
        if code!=expected:raise RuntimeError(name+' failed')
    try:
        exe=out/'pipeline-cache.exe'
        common=[a.compiler,'-std=c++20','-O2','-Wall','-Wextra','-Werror','-static','-municode']
        libraries=['-ld3d12','-ldxgi','-ldxguid','-ld3dcompiler','-lbcrypt']
        run('compile',[*common,'-I'+str(ROOT/'src'),ROOT/'tests/pipeline_cache_windows.cpp','-o',exe,*libraries])
        run('warp',[exe,out/'warp','warp'])
        if a.hardware:run('hardware',[exe,out/'hardware','hardware'])
        # Corrupt-byte rejection must fail before anything reaches the driver.
        negative=out/'checksum-negative';negative.mkdir()
        source=(ROOT/'src/nr_pipeline_cache.h').read_text(encoding='utf-8')
        if source.count('hash!=header.payload')!=1:raise ValueError('Review checksum mutation anchor')
        (negative/'nr_pipeline_cache.h').write_text(source.replace('hash!=header.payload','false'),encoding='utf-8')
        bad=negative/'negative.exe'
        run('negative-compile',[*common,'-I'+str(negative),ROOT/'tests/pipeline_cache_windows.cpp','-o',bad,*libraries])
        run('checksum-negative',[bad,negative/'run','warp'],3)
        report['status']='passed'
    except Exception as e:report.update(status='failed',error=str(e));raise
    finally:(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':main()
