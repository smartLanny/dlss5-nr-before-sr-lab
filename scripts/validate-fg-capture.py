#!/usr/bin/env python3
"""Exercise actual pre-FG recorder and exact-submission ledger on WARP."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

def main():
    p=argparse.ArgumentParser();p.add_argument('--compiler',required=True);p.add_argument('--output',required=True)
    p.add_argument('--legacy-keys-negative',action='store_true');args=p.parse_args()
    root=Path(__file__).resolve().parents[1];output=Path(args.output).resolve();output.mkdir(parents=True,exist_ok=True)
    helper=(root/'tests/ci_warp_runtime.h').read_bytes()
    (output/'ci_warp_runtime.h').write_bytes(helper)
    fixture=(root/'tests/replay_gpu.cpp').read_text(encoding='utf-8-sig').split('void FormatCases() {',1)[0]
    replay=(root/'src/nr_replay_runtime.inl').read_text(encoding='utf-8-sig');start=replay.index('Input Texture(');end=replay.index('unsigned Begin(',start)
    harness=(root/'tests/nr_fg_capture_warp.cpp.in').read_text(encoding='utf-8')
    harness=harness.replace('// @ACTUAL_REPLAY_FIXTURE@',fixture).replace('// @ACTUAL_TEXTURE_HELPER@',replay[start:end]).replace('../src/','')
    generated=output/'fg_capture_warp.cpp';generated.write_text(harness,encoding='utf-8');exe=output/'fg_capture_warp.exe'
    tested_source=(root/'src/nr_fg_capture.inl').read_text(encoding='utf-8-sig')
    if args.legacy_keys_negative:
        assert tested_source.count('"DLSSNR.Depth"')==1 and tested_source.count('"DLSSNR.MVec"')==1
        tested_source=tested_source.replace('"DLSSNR.Depth"','"Depth"').replace('"DLSSNR.MVec"','"MotionVectors"')
    (output/'nr_fg_capture.inl').write_text(tested_source,encoding='utf-8')
    command=[args.compiler,'-std=c++20','-O2','-static','-Wall','-Wextra','-Werror','-I'+str(root/'src'),str(generated),'-o',str(exe),'-ld3d12','-ldxgi','-ldxguid','-ld3dcompiler']
    build=subprocess.run(command,capture_output=True,text=True);(output/'compile.log').write_text(build.stdout+build.stderr,encoding='utf-8')
    if build.returncode:raise RuntimeError(build.stderr)
    run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=90);(output/'run.log').write_text(run.stdout+run.stderr,encoding='utf-8')
    success=run.returncode==0
    if args.legacy_keys_negative:success=run.returncode==1 and 'all requested actual layers recorded' in run.stderr
    receipt={'success':success,'scope':'Actual pre-FG capture/NRF recorder on WARP with controlled layer-context metadata',
             'warp_verified':run.returncode==0,'negative_control_verified':args.legacy_keys_negative and success,
             'ngx_verified':False,'fg_verified':False,'game_verified':False,'stdout':run.stdout,'stderr':run.stderr,
             'ci_warp_runtime_helper_sha256':hashlib.sha256(helper).hexdigest(),
             'source_sha256':hashlib.sha256((root/'src/nr_fg_capture.inl').read_bytes()).hexdigest(),
             'tested_source_sha256':hashlib.sha256(tested_source.encode('utf-8')).hexdigest()}
    (output/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8');print(run.stdout,end='')
    if not success:raise RuntimeError(run.stderr)
    if args.legacy_keys_negative:print('Negative control passed: old SR keys fail the actual Feature18 capture gate.')
if __name__=='__main__':main()
