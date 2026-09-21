#!/usr/bin/env python3
"""Run actual Core external ownership/completion/training CPU consumers."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--compiler', required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
runtime = (ROOT/'src/interop/nr_external_provider_consumer.inl').read_text(encoding='utf-8')
core = (ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8')
blocks = {
    'completion': ('// @PENDING_COMPLETION@', runtime[runtime.index('struct PendingCompletion'):runtime.index('nr_external_provider::ConfigSnapshot Snapshot()')]),
    'training': ('// @EXACT_TRAINING@', core[core.index('SRWLOCK g_external_training_lock'):core.index('void ProcessSubmittedCommandLists(')]),
}
commands = []
def run(command, name):
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True)
    (a.output/(name+'.log')).write_text(result.stdout+result.stderr, encoding='utf-8')
    commands.append({'name':name,'exit_code':result.returncode})
    if result.returncode:
        raise RuntimeError(name+' failed; preserve '+str(a.output))
    print(result.stdout, end='')

passed = False
try:
    for name in ('policy', 'completion', 'training'):
        source = ROOT/'tests/external_provider_policy_test.cpp'
        if name in blocks:
            marker, block = blocks[name]
            template = (ROOT/f'tests/external_provider_{name}.cpp.in').read_text(encoding='utf-8')
            assert template.count(marker) == 1
            source = a.output/(name+'.cpp')
            source.write_text(template.replace(marker, block), encoding='utf-8')
        binary = a.output/(name+'.exe')
        run([a.compiler,'-std=c++20','-O2','-static','-I'+str(ROOT/'src'),source,'-o',binary],name+'-compile')
        run([binary],name+'-run')
    for name in ('source-frame', 'present-color-source'):
        run([sys.executable,ROOT/f'scripts/validate-{name}.py','--compiler',a.compiler,
             '--output',a.output/name],name)
    passed = True
finally:
    inputs = [ROOT/'src/nr_before_sr.cpp',ROOT/'src/nr_source_frame_runtime.inl',ROOT/'src/nr_source_transport.inl']
    inputs += list((ROOT/'src/interop').glob('nr_external_*'))
    (a.output/'validation.json').write_text(json.dumps({
        'passed':passed,'scope':'actual Core CPU admission/ownership/completion/training; graphics objects are mocks',
        'gpu_verified':False,'ngx_verified':False,'component_binary_verified':False,'game_verified':False,
        'commands':commands,'source_sha256':{str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs},
    },indent=2)+'\n',encoding='utf-8')
