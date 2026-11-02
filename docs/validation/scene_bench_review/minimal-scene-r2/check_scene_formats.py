"""CPU-only negative controls for the bench exporter; no native executable."""
from pathlib import Path
import importlib.util
import json
import math
import tempfile
import numpy as np

spec=importlib.util.spec_from_file_location('suite',Path(__file__).with_name('scene_suite.py'))
suite=importlib.util.module_from_spec(spec);spec.loader.exec_module(suite)
checks=[]
def rejects(name, fn):
    try: fn()
    except ValueError: checks.append(name);return
    raise AssertionError(name+' was accepted')

good={'warm_frames':1,'observed_frames':2,
      'timing':{'scope':'CPU format fixture only','frames':[
          {'id':i,'milliseconds':float(i+1),'begin':i*10,'end':i*10+5,'fence':i+1,'completed':i+1}
          for i in range(3)]},
      'memory_samples':[{'phase':'observe','usage_bytes':10},
                        {'phase':'observe','usage_bytes':14},
                        {'phase':'after_retire','usage_bytes':9}]}
summary=suite.perf_summary(good)
assert summary['gpu_ms_median']==2.5 and summary['process_vram_stable_median_bytes']==12
checks.append('finite ordered complete fixture')
import copy
duplicate=copy.deepcopy(good);duplicate['timing']['frames'][2]['id']=1
rejects('duplicate timestamp frame rejected',lambda:suite.perf_summary(duplicate))
unretired=copy.deepcopy(good);unretired['timing']['frames'][2]['completed']=0
rejects('uncompleted fence rejected',lambda:suite.perf_summary(unretired))
missing=copy.deepcopy(good);missing['memory_samples']=missing['memory_samples'][:2]
rejects('missing post-retirement sample rejected',lambda:suite.perf_summary(missing))
bad=copy.deepcopy(good);bad['timing']['frames'][2]['milliseconds']=math.nan
rejects('nonfinite timing rejected',lambda:suite.perf_summary(bad))
with tempfile.TemporaryDirectory(prefix='dlss5-cpu-format-') as tmp:
    p=Path(tmp)/'frame.bin'
    np.array([[[1,2,4,1]]],dtype='<f2').tofile(p)
    image=suite.display_frame(p,1,1,1,1)
    assert image.tolist()==[[[188,213,231]]]
    checks.append('fixed common HDR display transform')
    suite.png(Path(tmp)/'image.png',image)
    assert (Path(tmp)/'image.png').read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
    checks.append('standalone PNG output')
    rejects('wrong packed stride rejected',lambda:suite.display_frame(p,2,1,1,1))
    np.array([[[math.nan,2,4,1]]],dtype='<f2').tofile(p)
    rejects('nonfinite image rejected',lambda:suite.display_frame(p,1,1,1,1))
print(json.dumps({'scope':'CPU exporter controls only; GPU/native/model/bench execution=0',
                  'passed':checks},ensure_ascii=False,indent=2))
