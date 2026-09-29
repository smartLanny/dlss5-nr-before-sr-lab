"""Reject malformed manifest paths, mismatched model pins and overlapping outputs."""
import argparse
import importlib.util
import json
import shutil
from pathlib import Path

here=Path(__file__).resolve().parent
ap=argparse.ArgumentParser()
ap.add_argument('--source',type=Path,required=True)
ap.add_argument('--test-root',type=Path,required=True)
a=ap.parse_args();work=a.test_root.resolve()
assert work.is_dir()
spec=importlib.util.spec_from_file_location('restore_face',here/'restore-face-inputs.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
original=json.loads((a.source/'PRIVATE_DEVINPUTS_MANIFEST.json').read_text(encoding='utf-8'))
temp=work/'face-negative-manifest'
assert not temp.exists();temp.mkdir()
def safe_remove(path):
    assert path.parent.resolve()==work and path.resolve()==work/path.name and not path.is_symlink()
    shutil.rmtree(path)
def rejected(manifest,reason):
    (temp/'PRIVATE_DEVINPUTS_MANIFEST.json').write_text(json.dumps(manifest),encoding='utf-8')
    runtime=work/'face-not-created-runtime';models=work/'face-not-created-models'
    assert not runtime.exists() and not models.exists()
    try: module.restore(temp,runtime,models)
    except ValueError as exc: assert reason in str(exc),str(exc)
    else: raise AssertionError('unsafe face manifest accepted')
    assert not runtime.exists() and not models.exists()
try:
    for bad in ('face/runtime/../escape','face/runtime/C:/escape',
                'face/runtime/..\\escape','face/runtime//escape','face/runtime/./escape'):
        payload=json.loads(json.dumps(original))
        payload['files']={bad:{'sha256':'0'*64,'bytes':1},**payload['files']}
        rejected(payload,'unsafe manifest path')
    payload=json.loads(json.dumps(original))
    payload['files']['face/models/yunet-dynamic.onnx']['sha256']='0'*64
    rejected(payload,'pinned face hash missing')
    (temp/'PRIVATE_DEVINPUTS_MANIFEST.json').write_text(json.dumps(original),encoding='utf-8')
    try: module.restore(temp,work/'overlap',work/'overlap'/'models')
    except ValueError as exc: assert 'overlap' in str(exc)
    else: raise AssertionError('overlapping output accepted')
finally:
    safe_remove(temp)
print(json.dumps({'status':'passed','cases':['traversal','drive-colon','backslash',
    'empty-component','dot-component','model-pin','overlapping-output']}))
