"""Reuse the frozen successful consumer's binding helpers, without running GPU."""
import pathlib,sys,hashlib,json
root=pathlib.Path(__file__).resolve().parents[2]
out=pathlib.Path(sys.argv[1])
if out.exists():raise FileExistsError(out)
base=(root/'tests/cascade_nr/two_feature_gpu.cpp').read_text(encoding='utf-8')
start=base.index('namespace {');end=base.index('void Experiment(')
helpers=base[start:end].replace('unsigned layer) {','unsigned layer,UINT work_width,UINT work_height) {').replace('kOutputWidth','work_width').replace('kOutputHeight','work_height')
assert 'unsigned layer,UINT work_width,UINT work_height)' in helpers
source=(root/'tests/nr_size/fixed_size_gpu.cpp.in').read_text(encoding='utf-8')
assert source.count('// @FROZEN_HELPERS@')==1
source=source.replace('// @FROZEN_HELPERS@',helpers+'\n}\n')
source=source.replace('// @DIRECT_SR@',(root/'tests/direct_sr_gpu.cpp').as_posix())
manifest={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [root/'tests/cascade_nr/two_feature_gpu.cpp',root/'tests/direct_sr_gpu.cpp',pathlib.Path(__file__),root/'tests/nr_size/fixed_size_gpu.cpp.in']}
out.mkdir(parents=True,exist_ok=False)
with (out/'fixed_size_gpu.cpp').open('x',encoding='utf-8') as f:f.write(source)
with (out/'source-manifest.json').open('x',encoding='utf-8') as f:json.dump(manifest,f,indent=2)
