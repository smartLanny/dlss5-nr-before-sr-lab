#!/usr/bin/env python3
"""Validate a sequence/runtime pair, run isolated NR, and compare captured outputs."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import nr_replay

def hash_file(path):
    digest=hashlib.sha256()
    with path.open('rb') as file:
        while block:=file.read(1<<20):digest.update(block)
    return digest.hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sequence',type=Path,required=True)
    parser.add_argument('--runtime',type=Path,required=True)
    parser.add_argument('--core',type=Path,required=True)
    parser.add_argument('--lab',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--structure',type=float)
    parser.add_argument('--skin',type=float)
    args=parser.parse_args()
    sequence=nr_replay.validate_sequence(args.sequence)
    runtime=sequence['runtime']
    paths={'nr':args.runtime/'nvngx_dlssnr.dll','bridge':args.runtime/'nrchain_nvngx.dll','core':args.core}
    for name,path in paths.items():
        nr_replay.fail(path.is_file() and path.stat().st_size==runtime[name+'_bytes'] and
            hash_file(path)==runtime[name+'_sha256'],f'{name} differs from captured runtime; no comparable replay')
    nr_replay.fail(not args.output.exists(),'output exists; previous evidence retained')
    output=args.output.resolve();output.parent.mkdir(parents=True,exist_ok=True)
    log=output.with_suffix('.log')
    nr_replay.fail(not log.exists(),'replay log exists')
    command=[str(args.lab.resolve()),'--runtime',str(args.runtime.resolve()),'--core',str(args.core.resolve()),
        '--sequence',str(args.sequence.resolve()),'--output',str(output),'--save-all','--trace-runtime']
    for name in ('structure','skin'):
        value=getattr(args,name)
        if value is not None:command+=['--'+name,str(value)]
    with log.open('x',encoding='utf-8') as file:
        process=subprocess.run(command,stdout=file,stderr=subprocess.STDOUT,timeout=180)
    nr_replay.fail(process.returncode==0,f'isolated model run failed; inspect {log.name}')
    import numpy as np
    metrics=[]
    for index in range(16):
        with nr_replay.Frame(args.sequence/f'frame-{index:02d}.nrf') as frame:
            expected=frame.plane('neural').astype(np.float32)
            path=output/f'nr-{index:03d}.rgba16'
            actual=np.fromfile(path,dtype='<f2').reshape(expected.shape).astype(np.float32)
            nr_replay.fail(np.isfinite(expected).all() and np.isfinite(actual).all(),'non-finite model result')
            delta=np.abs(actual[:,:,:3]-expected[:,:,:3])
            metrics.append({'index':index,'output_sha256':hash_file(path),'mean_abs_rgb':float(delta.mean()),
                'max_abs_rgb':float(delta.max()),'exact_rgb':bool((delta==0).all())})
    comparison=all(m['exact_rgb'] for m in metrics)
    report={'schema':'nr-model-replay-comparison-v1','input_sequence':sequence,
        'lab_sha256':hash_file(args.lab),'parameters_changed':args.structure is not None or args.skin is not None,
        'exact_captured_rgb_reproduced':comparison,'frames':metrics,
        'scope':'Exact reproduction is required before treating altered-parameter results as a faithful game-input experiment. A mismatch is not a face-mask failure diagnosis.'}
    (output/'comparison.json').write_text(json.dumps(report,indent=2,allow_nan=False),encoding='utf-8')
    print('Exact captured RGB reproduced:',comparison)
    return 0 if comparison or report['parameters_changed'] else 2

if __name__=='__main__':sys.exit(main())
