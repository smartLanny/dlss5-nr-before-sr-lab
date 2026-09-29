"""Narrow relocation checks using the actual eight-object source cache."""
import importlib.util
import argparse
import json
import shutil
from pathlib import Path

here=Path(__file__).resolve().parent
ap=argparse.ArgumentParser()
for name in ('source-cache','sdk','compiler','restored','test-root'):
    ap.add_argument('--'+name,type=Path,required=True)
args=ap.parse_args()
spec=importlib.util.spec_from_file_location('restore_ffx',here/'restore-ffx-cache.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
source=args.source_cache
sdk=args.sdk
compiler=args.compiler
valid=args.restored
work=args.test_root.resolve()
assert work.is_dir() and work!=here.resolve()
def safe_remove(path):
    # All test directories are fixed direct children of the caller's test root.
    assert path.parent.resolve()==work and path.resolve()==work/path.name and not path.is_symlink()
    shutil.rmtree(path)
assert valid.is_dir()
original=json.loads((source/'build.json').read_text())
derived=json.loads((valid/'build.json').read_text())
module.consumer_preflight(derived,valid,sdk,compiler)
assert (valid/'build.original.json').read_bytes()==(source/'build.json').read_bytes()
assert derived['commands']==original['commands']
for old,new in zip(original['sdk_objects'],derived['sdk_objects']):
    assert new['sha256']==old['sha256'] and Path(new['path']).name==Path(old['path']).name
    new['path']=old['path']
assert derived==original

# Existing output is never overwritten, even when the source is valid.
try:
    module.restore(source,valid,sdk,compiler)
except ValueError as exc:
    assert 'overwrite' in str(exc)
else:
    raise AssertionError('existing output accepted')

# Corrupt a single tiny object; no output must be created.
bad=work/'ffx-cache-corrupt-selftest'
target=work/'ffx-cache-rejected-selftest'
assert not bad.exists() and not target.exists()
bad.mkdir()
try:
    shutil.copy2(source/'build.json',bad/'build.json')
    for name in module.EXPECTED: shutil.copy2(source/name,bad/name)
    with (bad/'ffx_assert.o').open('ab') as stream: stream.write(b'wrong')
    try:
        module.restore(bad,target,sdk,compiler)
    except ValueError as exc:
        assert 'hash mismatch' in str(exc)
    else:
        raise AssertionError('corrupted object accepted')
    assert not target.exists()
finally:
    safe_remove(bad)

# Replace one leaf name in the original receipt; the original-receipt hash
# gate must reject it before any output appears.
wrong=work/'ffx-cache-name-selftest'
assert not wrong.exists()
wrong.mkdir()
try:
    edited=json.loads((source/'build.json').read_text())
    edited['sdk_objects'][0]['path']='C:/untrusted/other.o'
    (wrong/'build.json').write_text(json.dumps(edited),encoding='utf-8')
    try:
        module.restore(wrong,target,sdk,compiler)
    except ValueError as exc:
        assert 'original build.json hash mismatch' in str(exc)
    else:
        raise AssertionError('unknown leaf name accepted')
    assert not target.exists()
finally:
    safe_remove(wrong)

receipt={'status':'passed','cases':['actual-consumer-input-preflight','original-receipt-preserved',
    'only-eight-paths-relocated','existing-output-rejected','corrupt-object-rejected',
    'changed-leaf-receipt-rejected']}
(work/'ffx-relocation-selftest.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
print(json.dumps(receipt))
