#!/usr/bin/env python3
"""Verify and restore the pinned face build inputs from extracted private dev inputs."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path, PurePosixPath

EXPECTED = {
    'face/runtime/include/onnxruntime_c_api.h':'820cc6c379d259045d96dcce9d30f2505fdb31ba526e294a283083d015914466',
    'face/runtime/lib/onnxruntime.dll':'dec964ab1ee36cc9b0ae247d13b376627992fc57dec0454354017ab8fd84f1ea',
    'face/models/yunet-original.onnx':'8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4',
    'face/models/yunet-dynamic.onnx':'b1b3061b1ae7a43bcc1497284ade754f701db774ef3a19481edcaf27fbc425b1',
}

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024),b''): h.update(block)
    return h.hexdigest()

def restore(source,runtime_output,models_output):
    source=source.resolve();runtime_output=runtime_output.resolve();models_output=models_output.resolve()
    if (runtime_output==models_output or runtime_output.is_relative_to(models_output)
            or models_output.is_relative_to(runtime_output)):
        raise ValueError('face output roots overlap')
    manifest=json.loads((source/'PRIVATE_DEVINPUTS_MANIFEST.json').read_text(encoding='utf-8'))
    files=manifest['files']
    for key,digest in EXPECTED.items():
        if files.get(key,{}).get('sha256')!=digest: raise ValueError('pinned face hash missing: '+key)
    targets=[]
    for relative,info in files.items():
        if not relative.startswith('face/'): continue
        components=relative.split('/')
        if (not relative.isascii() or '\\' in relative or ':' in relative
                or any(part in ('','.','..') for part in components)
                or relative.startswith('/')):
            raise ValueError('unsafe manifest path')
        p=PurePosixPath(relative)
        if relative.startswith('face/runtime/'):
            target=runtime_output.joinpath(*p.parts[2:])
            root=runtime_output
        elif relative.startswith('face/models/'):
            target=models_output.joinpath(*p.parts[2:])
            root=models_output
        else: raise ValueError('unexpected face path: '+relative)
        original=source.joinpath(*p.parts)
        if not original.resolve().is_relative_to(source) or not target.resolve().is_relative_to(root):
            raise ValueError('manifest path escapes source or target root')
        if not original.is_file() or sha(original)!=info['sha256'] or original.stat().st_size!=info['bytes']:
            raise ValueError('face input hash/size mismatch: '+relative)
        targets.append((original,target))
    if runtime_output.exists() or models_output.exists(): raise ValueError('refuse to overwrite face outputs')
    if not runtime_output.parent.is_dir() or not models_output.parent.is_dir(): raise ValueError('output parent missing')
    runtime_output.mkdir();models_output.mkdir()
    for original,target in targets:
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(original,target)
        if sha(target)!=sha(original): raise ValueError('copy changed: '+str(target))
    return {'status':'verified-face-inputs','files':len(targets),
            'runtime_root':str(runtime_output),'models_root':str(models_output)}

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('source','runtime-output','models-output'):ap.add_argument('--'+name,type=Path,required=True)
    a=ap.parse_args()
    print(json.dumps(restore(a.source,a.runtime_output,a.models_output),ensure_ascii=False))
