#!/usr/bin/env python3
"""Bounded build job for the product's large existing shader suite.
Never modifies source; resumes exact per-entry receipts in an isolated build.
The caller must read job-result.json and must not infer success from launch.
"""
from pathlib import Path
import argparse,json,subprocess,sys,time
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--generated-root',type=Path,required=True)
a=p.parse_args();root=a.generated_root.resolve(strict=True);build=root/'build';out=build/'codec-job'
out.mkdir(exist_ok=False);start=time.time()
result={'schema':'nr-edit-codec-job-v1','start':start,'status':'running','game_verified':False}
(out/'job-result.json').write_text(json.dumps(result),encoding='utf-8')
try:
 with (out/'build.log').open('wb') as log:
  proc=subprocess.run([sys.executable,str(root/'scripts/prepare-codec-artifacts.py'),'--source',str(root/'src/nr_before_sr.cpp'),
   '--compiler',str(build/'compile-codec-artifact.exe'),'--output',str(build),'--workers','1'],stdout=log,stderr=subprocess.STDOUT,timeout=1200)
 result.update(status='passed' if proc.returncode==0 else 'failed',exit_code=proc.returncode)
except Exception as e:result.update(status='failed',error=str(e))
result['seconds']=time.time()-start
(out/'job-result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
