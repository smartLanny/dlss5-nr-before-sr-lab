"""Build frozen source, run real 5090D dispatch, hash binary/readbacks/logs.

No WARP, fallback backend, ordinary engine MV or CPU optical-flow substitute.
"""
import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--sdk',required=True,type=Path)
    p.add_argument('--compiler',required=True,type=Path)
    p.add_argument('--output',required=True,type=Path)
    a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
    root=Path(__file__).resolve().parent
    subprocess.run([sys.executable,str(root/'build_amd_probe.py'),'--sdk',str(a.sdk),'--compiler',str(a.compiler),'--output',str(a.output)],check=True)
    driver=subprocess.run(['nvidia-smi','--query-gpu=name,driver_version','--format=csv'],text=True,capture_output=True,check=True).stdout
    (a.output/'driver.txt').write_text(driver)
    exe=a.output/'amd_of_gpu_probe.exe'
    r=subprocess.run([str(exe)],cwd=a.output,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
    (a.output/'gpu.log').write_text(r.stdout,encoding='utf-8')
    print(r.stdout)
    record={'passed':False,'production_gpu_passed':r.returncode==0,'exit_code':r.returncode,'hardware':driver.strip(),
            'debug_layer_available':'debug_layer=1' in r.stdout,'executable_sha256':sha(exe),
            'build_manifest_sha256':sha(a.output/'build.json'),'gpu_log_sha256':sha(a.output/'gpu.log'),
            'readback_sha256':{f.name:sha(f) for f in sorted(a.output.glob('*.bin'))},
            'scope':'synthetic 512x384 original SDR source frames; actual AMD FFX1.1.4 on RTX5090D; no temporal pixel shader or Core integration'}
    (a.output/'validation.json').write_text(json.dumps(record,indent=2))
    if r.returncode:raise RuntimeError('real hardware validation failed; evidence retained')
    # Keep shader conversion mistakes observable on real hardware. Each mutant
    # still calls the same actual vendor dispatch; only our conversion changes.
    build=json.loads((a.output/'build.json').read_text())
    link=next(c['command'] for c in build['commands'] if c.get('label')=='link-probe')
    controls=[]
    for name,before,after in [('reverse-direction','Flow[p.xy]=v;','Flow[p.xy]=-v;'),
                              ('forget-input-scale','*float2(UnitX,UnitY);',';')]:
        work=a.output/name;work.mkdir(exist_ok=True)
        for src in (a.output/'source').iterdir():shutil.copyfile(src,work/src.name)
        shader=work/'amd_of_shader.hpp';text=shader.read_text();assert text.count(before)==1
        shader.write_text(text.replace(before,after))
        command=[str(work/Path(arg).name) if arg in [str(a.output/'source'/'amd_of_producer.cpp'),str(a.output/'source'/'amd_of_gpu_probe.cpp')] else arg for arg in link]
        command[-1]=str(work/'probe.exe')
        compiled=subprocess.run(command,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (work/'build.log').write_text(compiled.stdout)
        if compiled.returncode:raise RuntimeError('negative control must compile: '+compiled.stdout)
        run=subprocess.run([command[-1]],cwd=work,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
        (work/'gpu.log').write_text(run.stdout)
        caught=run.returncode==1 and 'FAIL current-to-reference source-pixel direction and scale' in run.stdout
        controls.append(dict(name=name,caught=caught,exit_code=run.returncode,command=command,
            binary_sha256=sha(work/'probe.exe'),log_sha256=sha(work/'gpu.log')))
        print(name+': '+('expected GPU assertion caught' if caught else 'UNEXPECTED outcome'))
        if not caught:raise RuntimeError('negative control not detected')
    record['negative_controls']=controls
    unchanged=all(sha(root/f.name)==sha(f) for f in (a.output/'source').iterdir())
    record['source_unchanged']=unchanged
    record['passed']=all(c['caught'] for c in controls) and unchanged
    (a.output/'validation.json').write_text(json.dumps(record,indent=2))
    if not unchanged:raise RuntimeError('source changed during GPU validation')

if __name__=='__main__':main()
